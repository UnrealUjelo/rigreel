"""Retarget an external skeleton animation (Mixamo, BVH mocap, glTF, ...) onto an RE4 character.

Input  : the world-space JSON written by tools/anim_import/blender_export_anim.py (any rig, Y-up, metres)
Target : a rig capture from the runtime (director/rigs/<code>.json): every joint's rest world position / rotation + parent
Output : director/import/<name>.json for the runtime: pose keys (local joint rotations) + root travel keys

Method (per frame, parents first):
  * limb / spine bones: the target bone is swung so that its direction (joint -> child) matches the source bone's
    world direction; the source's twist about the bone axis is added. Absolute directions make T-pose rigs, A-pose rigs
    and different bone rolls interchangeable, which plain rotation-delta retargeting is not.
  * hips: full rotation delta relative to the rest pose (all rigs stand upright facing +Z).
  * leaves (hands, head, feet): local rotation delta, scaled by `leaf_weight`.
  * root travel: hips world XZ displacement scaled by hip-height ratio -> position keys; yaw from the hips.
"""
from __future__ import annotations

import json
import math
import re
from pathlib import Path

import numpy as np

# canonical name -> RE4 joint
CANON_TO_RE4 = {
    "hips": "Hip", "spine": "Spine_0", "spine1": "Spine_1", "spine2": "Spine_2", "neck": "Neck_0", "head": "Head",
    "leftshoulder": "L_Shoulder", "leftarm": "L_UpperArm", "leftforearm": "L_Forearm", "lefthand": "L_Hand",
    "rightshoulder": "R_Shoulder", "rightarm": "R_UpperArm", "rightforearm": "R_Forearm", "righthand": "R_Hand",
    "leftupleg": "L_Thigh", "leftleg": "L_Shin", "leftfoot": "L_Foot", "lefttoebase": "L_Toe",
    "rightupleg": "R_Thigh", "rightleg": "R_Shin", "rightfoot": "R_Foot", "righttoebase": "R_Toe",
    # fingers (Mixamo names)
    "lefthandthumb1": "L_Thumb1", "lefthandthumb2": "L_Thumb2", "lefthandthumb3": "L_Thumb3",
    "lefthandindex1": "L_IndexF1", "lefthandindex2": "L_IndexF2", "lefthandindex3": "L_IndexF3",
    "lefthandmiddle1": "L_MiddleF1", "lefthandmiddle2": "L_MiddleF2", "lefthandmiddle3": "L_MiddleF3",
    "lefthandring1": "L_RingF1", "lefthandring2": "L_RingF2", "lefthandring3": "L_RingF3",
    "lefthandpinky1": "L_PinkyF1", "lefthandpinky2": "L_PinkyF2", "lefthandpinky3": "L_PinkyF3",
    "righthandthumb1": "R_Thumb1", "righthandthumb2": "R_Thumb2", "righthandthumb3": "R_Thumb3",
    "righthandindex1": "R_IndexF1", "righthandindex2": "R_IndexF2", "righthandindex3": "R_IndexF3",
    "righthandmiddle1": "R_MiddleF1", "righthandmiddle2": "R_MiddleF2", "righthandmiddle3": "R_MiddleF3",
    "righthandring1": "R_RingF1", "righthandring2": "R_RingF2", "righthandring3": "R_RingF3",
    "righthandpinky1": "R_PinkyF1", "righthandpinky2": "R_PinkyF2", "righthandpinky3": "R_PinkyF3",
}

# aliases seen in BVH / mocap / other rigs -> canonical
ALIASES = {
    "hips": ["hips", "hip", "pelvis", "root_hips", "bip01_pelvis", "bip001_pelvis", "cc_base_hip", "cc_base_pelvis"],
    "spine": ["spine", "spine_01", "spine01", "chest", "bip01_spine", "cc_base_waist", "torso", "abdomen", "lowerback"],
    "spine1": ["spine1", "spine_02", "spine02", "bip01_spine1", "cc_base_spine01", "chest1", "spine2_"],
    "spine2": ["spine2", "spine_03", "spine03", "bip01_spine2", "cc_base_spine02", "upperchest", "chest2"],
    "neck": ["neck", "neck_01", "neck1", "bip01_neck", "cc_base_necktwist01"],
    "head": ["head", "bip01_head", "cc_base_head"],
    "leftshoulder": ["leftshoulder", "l_shoulder", "shoulder_l", "leftcollar", "lcollar", "clavicle_l", "bip01_l_clavicle", "cc_base_l_clavicle", "left_shoulder", "lshoulder"],
    "leftarm": ["leftarm", "l_upperarm", "upperarm_l", "leftuparm", "lshldr", "l_arm", "bip01_l_upperarm", "cc_base_l_upperarm", "left_arm", "larm", "leftupperarm"],
    "leftforearm": ["leftforearm", "l_forearm", "lowerarm_l", "leftlowarm", "lforearm", "bip01_l_forearm", "cc_base_l_forearm", "left_forearm", "leftlowerarm"],
    "lefthand": ["lefthand", "l_hand", "hand_l", "lhand", "bip01_l_hand", "cc_base_l_hand", "left_hand", "leftwrist"],
    "rightshoulder": ["rightshoulder", "r_shoulder", "shoulder_r", "rightcollar", "rcollar", "clavicle_r", "bip01_r_clavicle", "cc_base_r_clavicle", "right_shoulder", "rshoulder"],
    "rightarm": ["rightarm", "r_upperarm", "upperarm_r", "rightuparm", "rshldr", "r_arm", "bip01_r_upperarm", "cc_base_r_upperarm", "right_arm", "rarm", "rightupperarm"],
    "rightforearm": ["rightforearm", "r_forearm", "lowerarm_r", "rightlowarm", "rforearm", "bip01_r_forearm", "cc_base_r_forearm", "right_forearm", "rightlowerarm"],
    "righthand": ["righthand", "r_hand", "hand_r", "rhand", "bip01_r_hand", "cc_base_r_hand", "right_hand", "rightwrist"],
    "leftupleg": ["leftupleg", "l_thigh", "thigh_l", "leftthigh", "lthigh", "l_upleg", "bip01_l_thigh", "cc_base_l_thigh", "left_thigh", "lefthip", "leftupperleg"],
    "leftleg": ["leftleg", "l_shin", "calf_l", "leftshin", "lshin", "l_calf", "bip01_l_calf", "cc_base_l_calf", "left_shin", "leftknee", "leftlowerleg", "l_leg"],
    "leftfoot": ["leftfoot", "l_foot", "foot_l", "lfoot", "bip01_l_foot", "cc_base_l_foot", "left_foot", "leftankle"],
    "lefttoebase": ["lefttoebase", "l_toe", "toe_l", "ltoe", "lefttoe", "ball_l", "bip01_l_toe0", "cc_base_l_toebase", "left_toe"],
    "rightupleg": ["rightupleg", "r_thigh", "thigh_r", "rightthigh", "rthigh", "r_upleg", "bip01_r_thigh", "cc_base_r_thigh", "right_thigh", "righthip", "rightupperleg"],
    "rightleg": ["rightleg", "r_shin", "calf_r", "rightshin", "rshin", "r_calf", "bip01_r_calf", "cc_base_r_calf", "right_shin", "rightknee", "rightlowerleg", "r_leg"],
    "rightfoot": ["rightfoot", "r_foot", "foot_r", "rfoot", "bip01_r_foot", "cc_base_r_foot", "right_foot", "rightankle"],
    "righttoebase": ["righttoebase", "r_toe", "toe_r", "rtoe", "righttoe", "ball_r", "bip01_r_toe0", "cc_base_r_toebase", "right_toe"],
}
for k in list(CANON_TO_RE4):
    ALIASES.setdefault(k, [k])

# RE4 chain children used for the direction (joint -> child)
RE4_CHILD = {
    "Hip": "Spine_0", "Spine_0": "Spine_1", "Spine_1": "Spine_2", "Spine_2": "Neck_0", "Neck_0": "Head",
    "L_Shoulder": "L_UpperArm", "L_UpperArm": "L_Forearm", "L_Forearm": "L_Hand",
    "R_Shoulder": "R_UpperArm", "R_UpperArm": "R_Forearm", "R_Forearm": "R_Hand",
    "L_Thigh": "L_Shin", "L_Shin": "L_Foot", "L_Foot": "L_Toe", "R_Thigh": "R_Shin", "R_Shin": "R_Foot", "R_Foot": "R_Toe",
}
ORDER = ["Hip", "Spine_0", "Spine_1", "Spine_2", "Neck_0", "Head",
         "L_Shoulder", "L_UpperArm", "L_Forearm", "L_Hand", "R_Shoulder", "R_UpperArm", "R_Forearm", "R_Hand",
         "L_Thigh", "L_Shin", "L_Foot", "L_Toe", "R_Thigh", "R_Shin", "R_Foot", "R_Toe"]
FINGERS = [v for k, v in CANON_TO_RE4.items() if "hand" in k and k not in ("lefthand", "righthand")]


# ---------------------------------------------------------------- quaternion helpers ([w, x, y, z])
def qmul(a, b):
    w1, x1, y1, z1 = a
    w2, x2, y2, z2 = b
    return np.array([w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2, w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
                     w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2, w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2])


def qinv(q):
    return np.array([q[0], -q[1], -q[2], -q[3]])


def qnorm(q):
    n = np.linalg.norm(q)
    return q / n if n > 1e-12 else np.array([1.0, 0, 0, 0])


def qrot(q, v):
    w, x, y, z = q
    u = np.array([x, y, z])
    return v + 2 * np.cross(u, np.cross(u, v) + w * v)


def from_to(a, b):
    a = a / max(1e-12, np.linalg.norm(a))
    b = b / max(1e-12, np.linalg.norm(b))
    d = float(np.clip(np.dot(a, b), -1, 1))
    if d > 0.999999:
        return np.array([1.0, 0, 0, 0])
    if d < -0.999999:
        axis = np.cross([1.0, 0, 0], a)
        if np.linalg.norm(axis) < 1e-6:
            axis = np.cross([0, 1.0, 0], a)
        axis = axis / np.linalg.norm(axis)
        return np.array([0.0, *axis])
    axis = np.cross(a, b)
    axis = axis / np.linalg.norm(axis)
    ang = math.acos(d)
    return np.array([math.cos(ang / 2), *(axis * math.sin(ang / 2))])


def axis_angle(axis, ang):
    axis = axis / max(1e-12, np.linalg.norm(axis))
    return np.array([math.cos(ang / 2), *(axis * math.sin(ang / 2))])


def twist_about(q, axis):
    """angle of the twist component of q about axis (signed)"""
    axis = axis / max(1e-12, np.linalg.norm(axis))
    v = np.array(q[1:4])
    p = np.dot(v, axis) * axis
    tw = qnorm(np.array([q[0], *p]))
    ang = 2 * math.atan2(np.dot(tw[1:4], axis), tw[0])
    while ang > math.pi:
        ang -= 2 * math.pi
    while ang < -math.pi:
        ang += 2 * math.pi
    return ang


def slerp(a, b, t):
    if np.dot(a, b) < 0:
        b = -b
    d = float(np.clip(np.dot(a, b), -1, 1))
    if d > 0.9995:
        return qnorm(a + (b - a) * t)
    th = math.acos(d)
    return (math.sin((1 - t) * th) * a + math.sin(t * th) * b) / math.sin(th)


# ---------------------------------------------------------------- name matching
def norm_name(n: str) -> str:
    n = n.lower()
    n = re.sub(r"^(mixamorig\d*[:_]|armature[|:]|.*:)", "", n)
    n = n.replace(" ", "").replace("-", "_").replace(".", "_")
    return n


def map_bones(src_names):
    """source bone name -> RE4 joint"""
    by_norm = {norm_name(n): n for n in src_names}
    out = {}
    for canon, aliases in ALIASES.items():
        tgt = CANON_TO_RE4.get(canon)
        if not tgt:
            continue
        for a in aliases:
            if a in by_norm and by_norm[a] not in out:
                out[by_norm[a]] = tgt
                break
    return out


# ---------------------------------------------------------------- rig capture (target)
class Rig:
    def __init__(self, data):
        self.code = data.get("code")
        self.joints = data["joints"]  # name -> {parent, pos, rot}

    def has(self, n):
        return n in self.joints

    def pos(self, n):
        return np.array(self.joints[n]["pos"], dtype=float)

    def rot(self, n):
        return np.array(self.joints[n]["rot"], dtype=float)

    def parent(self, n):
        return self.joints[n].get("parent")


# ---------------------------------------------------------------- main
def retarget(anim: dict, rig: Rig, out_fps: int = 60, leaf_weight: float = 1.0, twist_weight: float = 1.0,
             fingers: bool = True, root_travel: bool = True, key_every: int = 1) -> dict:
    src_bones = anim["bones"]
    tracks = anim["tracks"]
    n = anim["frames"]
    fps = anim["fps"]
    bmap = map_bones(src_bones.keys())
    inv = {v: k for k, v in bmap.items()}          # RE4 joint -> source bone
    if "Hip" not in inv:
        raise ValueError("no hips bone found in the source rig (names: %s)" % ", ".join(list(src_bones)[:12]))
    missing = [j for j in ORDER if j not in inv]

    # the character's heading is carried by the root travel keys, so the pose is retargeted in a frame that follows
    # the hips' yaw (otherwise a turning walk would turn twice: once in the pose, once in the transform)
    def hips_yaw(q):
        fwd = qrot(q, np.array([0, 0, 1.0]))
        return math.atan2(fwd[0], fwd[2])

    hip_src = inv["Hip"]
    yaw_f = [hips_yaw(np.array(tracks[hip_src]["rot"][f], dtype=float)) for f in range(n)]
    unyaw_f = [axis_angle(np.array([0, 1.0, 0]), -y) for y in yaw_f]
    yaw_rest = hips_yaw(np.array(src_bones[hip_src]["rest_rot"], dtype=float))
    unyaw_rest = axis_angle(np.array([0, 1.0, 0]), -yaw_rest)

    def src_rot(j, f):
        return qnorm(qmul(unyaw_f[f], np.array(tracks[inv[j]]["rot"][f], dtype=float)))

    def src_pos(j, f):
        return qrot(unyaw_f[f], np.array(tracks[inv[j]]["pos"][f], dtype=float))

    def src_rest_rot(j):
        return qnorm(qmul(unyaw_rest, np.array(src_bones[inv[j]]["rest_rot"], dtype=float)))

    def src_rest_pos(j):
        return qrot(unyaw_rest, np.array(src_bones[inv[j]]["rest_pos"], dtype=float))

    def src_pos_raw(j, f):
        return np.array(tracks[inv[j]]["pos"][f], dtype=float)

    # rest world of the target, and locals
    def tgt_local_rest(j):
        p = rig.parent(j)
        return qnorm(qmul(qinv(rig.rot(p)), rig.rot(j))) if p and rig.has(p) else rig.rot(j)

    # source local rest / per-frame local (for leaf deltas) — locals are unaffected by the yaw frame
    def src_local(j, f):
        pj = src_bones[inv[j]]["parent"]
        q = np.array(tracks[inv[j]]["rot"][f], dtype=float)
        if pj and pj in tracks:
            return qnorm(qmul(qinv(np.array(tracks[pj]["rot"][f], dtype=float)), q))
        return q

    def src_local_rest(j):
        pj = src_bones[inv[j]]["parent"]
        q = np.array(src_bones[inv[j]]["rest_rot"], dtype=float)
        if pj and pj in src_bones:
            return qnorm(qmul(qinv(np.array(src_bones[pj]["rest_rot"], dtype=float)), q))
        return q

    # scale for root travel: hip height ratio
    src_hip_h = float(np.array(src_bones[hip_src]["rest_pos"])[1])
    tgt_hip_h = float(rig.pos("Hip")[1]) if rig.has("Hip") else src_hip_h
    if abs(src_hip_h) < 1e-3:
        src_hip_h = tgt_hip_h or 1.0
    scale = (tgt_hip_h / src_hip_h) if src_hip_h else 1.0

    keys = []
    root = []
    hip0 = src_pos_raw("Hip", 0)
    step = 1 if key_every < 1 else key_every
    for f in range(0, n, step):
        world = {}     # RE4 joint -> world rotation this frame
        joints = {}    # RE4 joint -> local rotation (output)

        def world_of(j):
            if j in world:
                return world[j]
            p = rig.parent(j)
            # unmapped joints keep their rest local under their (possibly moved) parent
            if p and rig.has(p):
                world[j] = qnorm(qmul(world_of(p), tgt_local_rest(j)))
            else:
                world[j] = rig.rot(j)
            return world[j]

        for j in ORDER:
            if j not in inv or not rig.has(j):
                continue
            p = rig.parent(j)
            pw = world_of(p) if p and rig.has(p) else np.array([1.0, 0, 0, 0])
            child = RE4_CHILD.get(j)
            if j == "Hip":
                delta = qmul(src_rot(j, f), qinv(src_rest_rot(j)))
                w = qnorm(qmul(delta, rig.rot(j)))
            elif child and child in inv and rig.has(child):
                # swing: match the world direction of the bone
                d_src = src_pos(child, f) - src_pos(j, f)
                d_tgt_rest = rig.pos(child) - rig.pos(j)
                if np.linalg.norm(d_src) < 1e-6 or np.linalg.norm(d_tgt_rest) < 1e-6:
                    w = qnorm(qmul(pw, tgt_local_rest(j)))
                else:
                    swing = from_to(d_tgt_rest, d_src)
                    w = qnorm(qmul(swing, rig.rot(j)))
                    if twist_weight > 0:
                        # twist of the source relative to its rest, measured about the source bone axis
                        d_src_rest = src_rest_pos(child) - src_rest_pos(j)
                        delta = qmul(src_rot(j, f), qinv(src_rest_rot(j)))
                        # express the delta about the rest axis: remove the swing part first
                        sw_src = from_to(d_src_rest, d_src)
                        tw_only = qmul(qinv(sw_src), delta)
                        ang = twist_about(tw_only, d_src_rest) * twist_weight
                        w = qnorm(qmul(axis_angle(d_src, ang), w))
            else:
                # leaf: local delta
                delta_local = qmul(qinv(src_local_rest(j)), src_local(j, f))
                if leaf_weight < 1:
                    delta_local = slerp(np.array([1.0, 0, 0, 0]), delta_local, leaf_weight)
                w = qnorm(qmul(pw, qmul(tgt_local_rest(j), delta_local)))
            world[j] = w
            joints[j] = qnorm(qmul(qinv(pw), w))

        if fingers:
            for j in FINGERS:
                if j in inv and rig.has(j):
                    p = rig.parent(j)
                    pw = world_of(p) if p and rig.has(p) else np.array([1.0, 0, 0, 0])
                    delta_local = qmul(qinv(src_local_rest(j)), src_local(j, f))
                    w = qnorm(qmul(pw, qmul(tgt_local_rest(j), delta_local)))
                    world[j] = w
                    joints[j] = qnorm(qmul(qinv(pw), w))

        t = round(f * out_fps / fps)
        keys.append({"t": t, "joints": {k: [round(float(x), 6) for x in v] for k, v in joints.items()}})
        if root_travel:
            hp = src_pos_raw("Hip", f)
            d = (hp - hip0) * scale
            yaw = math.degrees(yaw_f[f])
            root.append({"t": t, "dx": round(float(d[0]), 4), "dz": round(float(d[2]), 4), "dy": round(float(d[1]), 4), "yaw": round(yaw, 2)})

    return {"fps": out_fps, "src_fps": fps, "frames": n, "length": keys[-1]["t"] + 1 if keys else 0, "keys": keys, "root": root,
            "mapped": sorted(set(inv) & set(rig.joints)), "missing": missing, "scale": scale, "source": anim.get("source")}


def run(anim_path: str, rig_path: str, out_path: str, **opts) -> dict:
    anim = json.loads(Path(anim_path).read_text(encoding="utf-8"))
    rig = Rig(json.loads(Path(rig_path).read_text(encoding="utf-8")))
    res = retarget(anim, rig, **opts)
    Path(out_path).parent.mkdir(parents=True, exist_ok=True)
    Path(out_path).write_text(json.dumps(res), encoding="utf-8")
    return {"length": res["length"], "keys": len(res["keys"]), "mapped": len(res["mapped"]), "missing": res["missing"], "scale": res["scale"]}


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("anim")
    ap.add_argument("rig")
    ap.add_argument("out")
    ap.add_argument("--fps", type=int, default=60)
    ap.add_argument("--no-fingers", action="store_true")
    ap.add_argument("--json", action="store_true", help="print the summary as one JSON line (for the Studio)")
    ns = ap.parse_args()
    info = run(ns.anim, ns.rig, ns.out, out_fps=ns.fps, fingers=not ns.no_fingers)
    print(json.dumps(info) if ns.json else info)
