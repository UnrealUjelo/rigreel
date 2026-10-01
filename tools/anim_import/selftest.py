"""Offline self-test of the import pipeline: synthetic BVH -> Blender export -> retarget onto a synthetic RE4-like rig.
    python tools/anim_import/selftest.py
"""
import json
import math
import subprocess
import sys
import glob
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "studio"))
import retarget  # noqa: E402

OUT = Path(__file__).resolve().parent / "_selftest"
OUT.mkdir(exist_ok=True)

# ---- synthetic BVH (Mixamo-like names, T-pose, cm), 20 frames: the left arm swings down, the character walks +Z
BVH_HIER = """HIERARCHY
ROOT Hips
{
  OFFSET 0 95 0
  CHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation
  JOINT Spine
  {
    OFFSET 0 10 0
    CHANNELS 3 Zrotation Xrotation Yrotation
    JOINT Spine1
    {
      OFFSET 0 12 0
      CHANNELS 3 Zrotation Xrotation Yrotation
      JOINT Spine2
      {
        OFFSET 0 12 0
        CHANNELS 3 Zrotation Xrotation Yrotation
        JOINT Neck
        {
          OFFSET 0 14 0
          CHANNELS 3 Zrotation Xrotation Yrotation
          JOINT Head
          {
            OFFSET 0 10 0
            CHANNELS 3 Zrotation Xrotation Yrotation
            End Site
{
OFFSET 0 15 0
}
          }
        }
        JOINT LeftShoulder
        {
          OFFSET 5 10 0
          CHANNELS 3 Zrotation Xrotation Yrotation
          JOINT LeftArm
          {
            OFFSET 12 0 0
            CHANNELS 3 Zrotation Xrotation Yrotation
            JOINT LeftForeArm
            {
              OFFSET 28 0 0
              CHANNELS 3 Zrotation Xrotation Yrotation
              JOINT LeftHand
              {
                OFFSET 26 0 0
                CHANNELS 3 Zrotation Xrotation Yrotation
                End Site
{
OFFSET 10 0 0
}
              }
            }
          }
        }
        JOINT RightShoulder
        {
          OFFSET -5 10 0
          CHANNELS 3 Zrotation Xrotation Yrotation
          JOINT RightArm
          {
            OFFSET -12 0 0
            CHANNELS 3 Zrotation Xrotation Yrotation
            JOINT RightForeArm
            {
              OFFSET -28 0 0
              CHANNELS 3 Zrotation Xrotation Yrotation
              JOINT RightHand
              {
                OFFSET -26 0 0
                CHANNELS 3 Zrotation Xrotation Yrotation
                End Site
{
OFFSET -10 0 0
}
              }
            }
          }
        }
      }
    }
  }
  JOINT LeftUpLeg
  {
    OFFSET 9 0 0
    CHANNELS 3 Zrotation Xrotation Yrotation
    JOINT LeftLeg
    {
      OFFSET 0 -45 0
      CHANNELS 3 Zrotation Xrotation Yrotation
      JOINT LeftFoot
      {
        OFFSET 0 -42 0
        CHANNELS 3 Zrotation Xrotation Yrotation
        JOINT LeftToeBase
        {
          OFFSET 0 -8 14
          CHANNELS 3 Zrotation Xrotation Yrotation
          End Site
{
OFFSET 0 0 6
}
        }
      }
    }
  }
  JOINT RightUpLeg
  {
    OFFSET -9 0 0
    CHANNELS 3 Zrotation Xrotation Yrotation
    JOINT RightLeg
    {
      OFFSET 0 -45 0
      CHANNELS 3 Zrotation Xrotation Yrotation
      JOINT RightFoot
      {
        OFFSET 0 -42 0
        CHANNELS 3 Zrotation Xrotation Yrotation
        JOINT RightToeBase
        {
          OFFSET 0 -8 14
          CHANNELS 3 Zrotation Xrotation Yrotation
          End Site
{
OFFSET 0 0 6
}
        }
      }
    }
  }
}
"""
NJ = 22  # joints with channels (hips + 21)
frames = []
for i in range(20):
    a = -80.0 * i / 19  # left arm swings down (Z rotation of LeftArm in the BVH's Y-up frame)
    vals = [0.0, 95.0, 4.0 * i, 0, 0, 0]  # hips: walks +Z 4 cm per frame
    per_joint = [[0, 0, 0] for _ in range(NJ - 1)]
    per_joint[6] = [a, 0, 0]  # LeftArm is the 7th joint after Hips (Spine, Spine1, Spine2, Neck, Head, LeftShoulder, LeftArm)
    for pj in per_joint:
        vals += pj
    frames.append(" ".join(f"{v:.4f}" for v in vals))
bvh = BVH_HIER + f"MOTION\nFrames: {len(frames)}\nFrame Time: 0.0333333\n" + "\n".join(frames) + "\n"
src = OUT / "swing.bvh"
src.write_text(bvh)

# ---- Blender export
blender = sorted(glob.glob(r"C:\Program Files\Blender Foundation\Blender*\blender.exe"), reverse=True)[0]
world = OUT / "swing_world.json"
r = subprocess.run([blender, "-b", "--python", str(Path(__file__).resolve().parent / "blender_export_anim.py"), "--", str(src), str(world), "--fps", "30"], capture_output=True, text=True, timeout=300)
print(r.stdout[-600:])
if r.returncode != 0 or not world.is_file():
    print(r.stderr[-1500:])
    sys.exit(1)
anim = json.loads(world.read_text())
print("bones:", list(anim["bones"])[:8], "... frames", anim["frames"])
hips = anim["tracks"]["Hips"]["pos"]
print("hips pos frame0 -> last:", hips[0], hips[-1], "(expect ~+0.76 in +Z, y ~0.95 m)")
larm = anim["tracks"]["LeftArm"]["pos"]
lfore = anim["tracks"]["LeftForeArm"]["pos"]
print("left arm dir frame0:", [round(b - a, 3) for a, b in zip(larm[0], lfore[0])], "last:", [round(b - a, 3) for a, b in zip(larm[-1], lfore[-1])])

# ---- synthetic RE4-like rig (A-pose: arms 45 deg down), metres, facing +Z, world-space rest
def q_axis(axis, deg):
    h = math.radians(deg) / 2
    s = math.sin(h)
    return [math.cos(h), axis[0] * s, axis[1] * s, axis[2] * s]

rig = {"code": "test", "joints": {}}
def add(name, parent, pos, rot=(1, 0, 0, 0)):
    rig["joints"][name] = {"parent": parent, "pos": list(pos), "rot": list(rot)}
add("root", None, (0, 0, 0)); add("Hip", "root", (0, 0.95, 0))
add("Spine_0", "Hip", (0, 1.05, 0)); add("Spine_1", "Spine_0", (0, 1.17, 0)); add("Spine_2", "Spine_1", (0, 1.29, 0))
add("Neck_0", "Spine_2", (0, 1.43, 0)); add("Neck_1", "Neck_0", (0, 1.48, 0)); add("Head", "Neck_1", (0, 1.53, 0))
c45 = math.cos(math.radians(45)) * 0.28
for side, sx in (("L", 1), ("R", -1)):
    add(f"{side}_Shoulder", "Spine_2", (sx * 0.05, 1.39, 0), q_axis((0, 0, 1), -sx * 45))
    add(f"{side}_UpperArm", f"{side}_Shoulder", (sx * 0.17, 1.39, 0), q_axis((0, 0, 1), -sx * 45))
    add(f"{side}_Forearm", f"{side}_UpperArm", (sx * (0.17 + c45), 1.39 - c45, 0), q_axis((0, 0, 1), -sx * 45))
    add(f"{side}_Hand", f"{side}_Forearm", (sx * (0.17 + 2 * c45), 1.39 - 2 * c45, 0), q_axis((0, 0, 1), -sx * 45))
    add(f"{side}_Thigh", "Hip", (sx * 0.09, 0.95, 0)); add(f"{side}_Shin", f"{side}_Thigh", (sx * 0.09, 0.50, 0))
    add(f"{side}_Foot", f"{side}_Shin", (sx * 0.09, 0.08, 0)); add(f"{side}_Toe", f"{side}_Foot", (sx * 0.09, 0.0, 0.14))
rigf = OUT / "rig_test.json"
rigf.write_text(json.dumps(rig))

out = OUT / "swing_retargeted.json"
info = retarget.run(str(world), str(rigf), str(out), out_fps=60)
print("retarget:", info)
res = json.loads(out.read_text())
k0, k1 = res["keys"][0], res["keys"][-1]
print("first key t", k0["t"], "last key t", k1["t"], "joints:", sorted(k0["joints"])[:10])
print("L_UpperArm local frame0:", k0["joints"]["L_UpperArm"], "last:", k1["joints"]["L_UpperArm"])
print("R_UpperArm local frame0:", k0["joints"]["R_UpperArm"], "last:", k1["joints"]["R_UpperArm"], "(should stay ~constant)")
print("root travel last:", res["root"][-1])
bad = [k for k in res["keys"] for v in k["joints"].values() if any(math.isnan(x) for x in v)]
print("NaN keys:", len(bad))
