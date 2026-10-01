// Secondary motion: the bones RE Engine moves with physics at run time — hair, cloth, straps, sleeves, collars
// (their names carry "_chain": hair03_chain_01, cloth_chain_05_02, ShoulderBelt_chain_A_00) — are not in the
// animation clips, so they would ride along stiffly. Here each is a particle on a spring towards its animated
// place, with damping and a little gravity, kept at its bone length; the bones then turn to point at where the
// particles went. It steps with film time only (playback, render: the same every time); a jump in time or a
// scrub starts it from the animated pose.
#include "Maths.h"
#include "StageRuntime.h"

#include <cmath>

using namespace film;

namespace {
bool isChain(const QString &n) { return n.contains(QLatin1String("_chain"), Qt::CaseInsensitive); }

// how firmly a bone follows its animated place per 1/60 s: hair swings, cloth sways, straps and collars barely
float stiffnessOf(const QString &name)
{
    const QString n = name.toLower();
    if (n.contains(QLatin1String("hair")) || n.contains(QLatin1String("bang")) || n.contains(QLatin1String("tail"))) return 0.08f;
    if (n.contains(QLatin1String("cloth")) || n.contains(QLatin1String("skirt")) || n.contains(QLatin1String("coat")) || n.contains(QLatin1String("cape"))) return 0.12f;
    return 0.32f;
}
} // namespace

void StageRuntime::simulateChains(double t)
{
    QSet<qint64> seen;
    for (ActorFrame &f : m_frames) {
        const Actor *a = m_doc.actor(f.id);
        const PreparedModel *pm = a ? prepared(f.id) : nullptr;
        if (!a || !pm || !pm->model || !a->physics || f.pose.isEmpty()) continue;
        const dir::Skeleton &sk = pm->model->skeleton;
        const int nb = int(std::min<qsizetype>(sk.bones.size(), f.pose.size()));
        seen.insert(f.id);
        ChainSim &cs = m_chains[f.id];
        if (cs.model != a->model) {
            cs = ChainSim();
            cs.model = a->model;
            QHash<int, int> index;
            for (int b = 0; b < nb; ++b) {
                if (!isChain(sk.bones[b].name)) continue;
                const int p = sk.bones[b].parent;
                index.insert(b, int(cs.bones.size()));
                cs.bones << b;
                cs.parent << (p >= 0 && p < b ? index.value(p, -1) : -1);
                cs.stiff << stiffnessOf(sk.bones[b].name);
            }
            const QVector<QMatrix4x4> g = globalMatrices(sk, bindPose(sk));
            cs.len.resize(cs.bones.size());
            cs.child.fill(-1, cs.bones.size());
            for (int i = 0; i < cs.bones.size(); ++i) {
                const int p = cs.parent[i];
                cs.len[i] = p >= 0 ? (g[cs.bones[i]].column(3).toVector3D() - g[cs.bones[p]].column(3).toVector3D()).length() : 0.f;
                if (p >= 0 && cs.child[p] < 0) cs.child[p] = i;
            }
        }
        const int n = int(cs.bones.size());
        if (!n) continue;

        // where the animation puts every chain bone this frame (world)
        QMatrix4x4 actorM;
        actorM.translate(f.pos);
        actorM.rotate(f.rot);
        actorM.scale(f.scale);
        const QVector<QMatrix4x4> g = globalMatrices(sk, f.pose);
        QVector<QVector3D> anim(n);
        for (int i = 0; i < n; ++i) anim[i] = (actorM * g[cs.bones[i]]).column(3).toVector3D();

        const double dt = t - cs.lastT;
        const bool running = m_playing || m_rendering;
        if (cs.lastT < 0 || dt < -1e-6 || dt > 4 || (!running && std::abs(dt) > 1e-6) || cs.p.size() != n) {
            cs.p = anim;
            cs.prev = anim;
        } else if (dt > 1e-6) {
            const int steps = std::clamp(int(std::ceil(dt)), 1, 4);
            const float h = float(dt / steps);                                  // film frames per step
            const float hs = h / 60.f;                                           // seconds
            const QVector3D sag(0, -9.81f * 0.35f * hs * hs, 0);
            const float keep = std::pow(0.9f, h);                                // damping of the swing
            for (int s = 0; s < steps; ++s) {
                for (int i = 0; i < n; ++i) {
                    if (cs.parent[i] < 0) { cs.p[i] = anim[i]; cs.prev[i] = anim[i]; continue; }   // rides its (animated) parent
                    const QVector3D v = (cs.p[i] - cs.prev[i]) * keep;
                    cs.prev[i] = cs.p[i];
                    cs.p[i] += v + sag;
                    cs.p[i] += (anim[i] - cs.p[i]) * (1.f - std::pow(1.f - cs.stiff[i], h));
                }
                for (int i = 0; i < n; ++i) {                                    // bone lengths, parents first
                    const int p = cs.parent[i];
                    if (p < 0) continue;
                    const QVector3D d = cs.p[i] - cs.p[p];
                    if (d.lengthSquared() > 1e-12f) cs.p[i] = cs.p[p] + d.normalized() * cs.len[i];
                }
            }
        }
        cs.lastT = t;

        // turn each chain bone towards where its child went (model space, parents first)
        QVector<QQuaternion> R(nb);
        QVector<QVector3D> P(nb);
        for (int b = 0; b < nb; ++b) {
            const int p = sk.bones[b].parent;
            R[b] = p >= 0 && p < b ? (R[p] * f.pose[b].r).normalized() : f.pose[b].r;
            P[b] = p >= 0 && p < b ? P[p] + R[p].rotatedVector(f.pose[b].t) : f.pose[b].t;
        }
        const QMatrix4x4 toModel = actorM.inverted();
        for (int i = 0; i < n; ++i) {
            const int b = cs.bones[i], p = sk.bones[b].parent;
            if (p >= 0 && p < b) {                                               // the parent may have turned
                R[b] = (R[p] * f.pose[b].r).normalized();
                P[b] = P[p] + R[p].rotatedVector(f.pose[b].t);
            }
            const int c = cs.child[i];
            if (c < 0) continue;
            const QVector3D cur = R[b].rotatedVector(f.pose[cs.bones[c]].t);
            const QVector3D want = toModel.map(cs.p[c]) - P[b];
            if (cur.lengthSquared() < 1e-12f || want.lengthSquared() < 1e-12f) continue;
            R[b] = (QQuaternion::rotationTo(cur.normalized(), want.normalized()) * R[b]).normalized();
            f.pose[b].r = p >= 0 && p < b ? (R[p].conjugated() * R[b]).normalized() : R[b];
        }
    }
    for (auto it = m_chains.begin(); it != m_chains.end();) it = seen.contains(it.key()) ? std::next(it) : m_chains.erase(it);
}
