// Poses and animation sampling. A pose is one local transform per skeleton bone; clips bind to a skeleton by
// bone-name hash once, then sample at any (fractional) frame.
#pragma once

#include <director/Assets.h>
#include <QMatrix4x4>

struct Xform {
    QVector3D t;
    QQuaternion r;
    QVector3D s{1, 1, 1};
    QMatrix4x4 matrix() const
    {
        QMatrix4x4 m;
        m.translate(t);
        m.rotate(r);
        m.scale(s);
        return m;
    }
    static Xform lerp(const Xform &a, const Xform &b, float w)
    {
        return {a.t + (b.t - a.t) * w, QQuaternion::nlerp(a.r, b.r, w), a.s + (b.s - a.s) * w};
    }
};

using Pose = QVector<Xform>;

Pose bindPose(const dir::Skeleton &sk);
// a joint by name, trying the other games' names for the same joint (RE4 L_UpperArm = RE2 l_arm_humerus ...)
int findJoint(const dir::Skeleton &sk, const QString &name);
// model-space matrices of a local pose
QVector<QMatrix4x4> globalMatrices(const dir::Skeleton &sk, const Pose &local);

// How a clip made for one character lands on another. Positions are played as offsets from the source's bind
// pose onto the target's (a face or hips keep their own shape); face bones, whose joint orientations differ from rig
// to rig, get their rotations the same way. Body bones share one convention, so their rotations transfer as they are.
struct Retarget {
    const dir::Skeleton *source = nullptr;    // the skeleton the clip was made for, when known
    bool sameRig = false;                     // made for this very skeleton: play the values as they are
};

class ClipBinding {
public:
    ClipBinding() = default;
    ClipBinding(const dir::AnimationClip *clip, const dir::Skeleton &sk, const Retarget &rt = {});
    bool isValid() const { return m_clip != nullptr; }
    const dir::AnimationClip *clip() const { return m_clip; }
    float frames() const { return m_clip ? m_clip->frames : 0; }
    int boundTracks() const { return int(m_bones.size()); }
    // Writes the sampled channels into `pose` (blended by weight). Bones the clip does not animate keep their value.
    // mask: optional per-bone weight 0..1.
    void sample(float frame, Pose &pose, float weight = 1.f, const QVector<float> *mask = nullptr) const;
    // root motion: the skeleton's top bone as animated by the clip (the game moves the character by it)
    int rootBone() const { return m_cross ? m_rootT : m_rootTrack >= 0 ? m_bones[m_rootTrack] : -1; }
    Xform rootAt(float frame) const;
    bool crossRig() const { return m_cross; }
    int mappedBones() const;                     // cross-rig: target bones that follow the clip

private:
    void setupCross(const dir::Skeleton &sk, const dir::Skeleton &src);
    void sampleCross(float frame, Pose &pose, float weight, const QVector<float> *mask) const;

    const dir::AnimationClip *m_clip = nullptr;
    QVector<int> m_bones;          // per track: skeleton bone or -1
    QVector<QVector3D> m_moveBy;   // per track: added to its positions (target bind - source bind)
    QVector<QQuaternion> m_turnBy; // per track: applied after its rotations (source bind^-1 * target bind)
    int m_rootTrack = -1;
    // cross-rig (another game: other bone names, other joint axes): the clip plays on its own skeleton and each
    // matched bone hands its world rotation change to the target bone, from rest poses brought to one stance
    bool m_cross = false;
    dir::Skeleton m_src;
    Pose m_srcBind;
    QVector<int> m_srcTrack;       // per track: source bone
    QVector<int> m_map;            // per target bone: source bone or -1
    QVector<int> m_tgtParent;
    QVector<QQuaternion> m_corr;   // per target bone: source rest^-1 * stance fix * target rest (model space)
    QVector<QVector3D> m_srcBindPos, m_tgtBindPos;
    int m_pelvisT = -1, m_pelvisS = -1, m_rootT = -1;
    float m_scale = 1;
    Xform m_tgtRootBind;
};
