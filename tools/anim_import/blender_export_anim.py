"""Runs INSIDE Blender (headless). Imports an animation file and dumps the armature animation as JSON, world space, Y-up.

    blender -b --python blender_export_anim.py -- <input.(fbx|bvh|glb|gltf|dae)> <out.json> [--fps 30] [--max-frames 6000]

Output:
{
  "source": "...", "fps": 30, "frames": N, "up": "Y",
  "bones": { name: { "parent": name|null, "rest_pos": [x,y,z], "rest_rot": [w,x,y,z], "head": [x,y,z], "tail": [x,y,z] } },
  "tracks": { name: { "rot": [[w,x,y,z] * N], "pos": [[x,y,z] * N] } }       # world space per frame
}
World space keeps the retargeter independent of how each exporter nests its bones. Blender is Z-up; everything is
converted to Y-up (RE Engine): (x, y, z) -> (x, z, -y), which is a -90° rotation about X.
"""
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Matrix, Quaternion, Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
if len(argv) < 2:
    print("usage: blender -b --python blender_export_anim.py -- input out.json [--fps 30]")
    sys.exit(2)
src, out = Path(argv[0]), Path(argv[1])
fps = 30
max_frames = 6000
if "--fps" in argv:
    fps = int(argv[argv.index("--fps") + 1])
if "--max-frames" in argv:
    max_frames = int(argv[argv.index("--max-frames") + 1])

bpy.ops.wm.read_factory_settings(use_empty=True)
ext = src.suffix.lower()
if ext == ".fbx":
    bpy.ops.import_scene.fbx(filepath=str(src), automatic_bone_orientation=False, ignore_leaf_bones=False, use_anim=True)
elif ext == ".bvh":
    bpy.ops.import_anim.bvh(filepath=str(src), axis_forward="-Z", axis_up="Y", update_scene_fps=True, update_scene_duration=True)
elif ext in (".glb", ".gltf"):
    bpy.ops.import_scene.gltf(filepath=str(src))
elif ext == ".dae":
    bpy.ops.wm.collada_import(filepath=str(src))
else:
    print("unsupported file type", ext)
    sys.exit(3)

arm = next((o for o in bpy.data.objects if o.type == "ARMATURE"), None)
if arm is None:
    print("no armature in", src)
    sys.exit(4)

scene = bpy.context.scene
# frame range from the action (or the scene)
f0, f1 = scene.frame_start, scene.frame_end
act = arm.animation_data.action if arm.animation_data else None
if act is not None:
    r = act.frame_range
    f0, f1 = int(math.floor(r[0])), int(math.ceil(r[1]))
src_fps = scene.render.fps / scene.render.fps_base
step = src_fps / fps if fps > 0 else 1.0
n = int(min(max_frames, math.floor((f1 - f0) / step) + 1))

TO_Y_UP = Matrix.Rotation(-math.pi / 2, 4, "X")  # Blender Z-up -> Y-up
R_Q = TO_Y_UP.to_quaternion()


def conv_v(v: Vector):
    w = TO_Y_UP @ v
    return [round(w.x, 5), round(w.y, 5), round(w.z, 5)]


def conv_q(q: Quaternion):
    w = (R_Q @ q).normalized()
    return [round(w.w, 6), round(w.x, 6), round(w.y, 6), round(w.z, 6)]


unit = 1.0
# FBX from Mixamo is centimetres unless scaled on import; Blender's importer applies the file's unit scale into the
# object scale. Normalise by the armature's world scale so metres come out.
arm_scale = arm.matrix_world.to_scale()
bones = {}
for b in arm.data.bones:
    m = arm.matrix_world @ b.matrix_local  # rest, world
    bones[b.name] = {
        "parent": b.parent.name if b.parent else None,
        "rest_pos": conv_v(m.to_translation()),
        "rest_rot": conv_q(m.to_quaternion()),
        "head": conv_v(arm.matrix_world @ b.head_local),
        "tail": conv_v(arm.matrix_world @ b.tail_local),
    }

tracks = {b.name: {"rot": [], "pos": []} for b in arm.data.bones}
for i in range(n):
    scene.frame_set(f0 + int(round(i * step)))
    mw = arm.matrix_world
    for pb in arm.pose.bones:
        m = mw @ pb.matrix
        tracks[pb.name]["rot"].append(conv_q(m.to_quaternion()))
        tracks[pb.name]["pos"].append(conv_v(m.to_translation()))

data = {"source": str(src), "fps": fps, "src_fps": src_fps, "frames": n, "up": "Y", "armature": arm.name, "scale": [arm_scale.x, arm_scale.y, arm_scale.z],
        "bones": bones, "tracks": tracks}
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(data), encoding="utf-8")
print(f"exported {n} frames @ {fps} fps, {len(bones)} bones -> {out}")
