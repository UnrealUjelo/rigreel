"""Blender add-on-less importer for Director exports (director/export/<name>.json).

In Blender: Scripting tab -> open this file -> set FILE below (or run from the command line):
    blender --python blender_import_director.py -- "C:/.../reframework/data/director/export/Ada_1_131200.json"

Builds an armature from the exported rig (RE4 joint names, metres, Y-up converted to Blender Z-up), keys the exported
pose keys and root travel, so you can polish the animation in Blender. Export it back as FBX and use
"Import animation…" in RigReel Studio: the retargeter maps it onto the same skeleton 1:1.
"""
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Matrix, Quaternion, Vector

FILE = None  # set to a path when running from the Scripting tab
argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
path = Path(argv[0]) if argv else (Path(FILE) if FILE else None)
if path is None or not path.is_file():
    raise SystemExit("give the exported .json as argument (or set FILE)")

data = json.loads(path.read_text(encoding="utf-8"))
rig = data["rig"]["joints"]
fps = int(data.get("fps") or 60)

TO_Z_UP = Matrix.Rotation(math.pi / 2, 4, "X")  # Y-up (engine) -> Blender Z-up
R_Q = TO_Z_UP.to_quaternion()


def v_up(v):
    return TO_Z_UP @ Vector((v[0], v[1], v[2]))


def q_up(q):  # [w,x,y,z] engine -> Blender quaternion
    return (R_Q @ Quaternion((q[0], q[1], q[2], q[3]))).normalized()


scene = bpy.context.scene
scene.render.fps = fps
name = data.get("name") or path.stem
arm_data = bpy.data.armatures.new(name)
arm = bpy.data.objects.new(name, arm_data)
scene.collection.objects.link(arm)
bpy.context.view_layer.objects.active = arm
bpy.ops.object.mode_set(mode="EDIT")

children = {}
for jn, j in rig.items():
    children.setdefault(j.get("parent"), []).append(jn)

ebones = {}
for jn, j in rig.items():
    eb = arm_data.edit_bones.new(jn)
    head = v_up(j["pos"])
    kids = children.get(jn, [])
    if kids:
        tail = sum((v_up(rig[k]["pos"]) for k in kids), Vector((0, 0, 0))) / len(kids)
        if (tail - head).length < 0.005:
            tail = head + q_up(j["rot"]) @ Vector((0, 0.05, 0))
    else:
        tail = head + q_up(j["rot"]) @ Vector((0, 0.05, 0))
    eb.head, eb.tail = head, tail
    # align the bone's roll with the exported rest rotation as well as a head/tail bone allows
    m = q_up(j["rot"]).to_matrix()
    eb.align_roll(m.col[2])
    ebones[jn] = eb
for jn, j in rig.items():
    p = j.get("parent")
    if p and p in ebones:
        ebones[jn].parent = ebones[p]
        ebones[jn].use_connect = False

# rest world rotations (armature space) as Blender sees them, for converting exported locals
bpy.ops.object.mode_set(mode="POSE")
rest_world = {jn: arm.data.bones[jn].matrix_local.to_quaternion() for jn in rig}
exp_rest = {jn: q_up(j["rot"]) for jn, j in rig.items()}

act = bpy.data.actions.new(name + "_action")
arm.animation_data_create()
arm.animation_data.action = act
for pb in arm.pose.bones:
    pb.rotation_mode = "QUATERNION"

# exported keys are local rotations in the engine's joint frames; the Blender bone frames differ (head->tail
# convention), so convert through world space: world = parent_world * local, then pose = rest_world^-1 * world
def world_from_locals(locals_at, jn, cache):
    if jn in cache:
        return cache[jn]
    j = rig[jn]
    p = j.get("parent")
    local_q = locals_at.get(jn)
    if local_q is None:
        # unkeyed joint: keep its rest local under the (possibly rotated) parent
        if p and p in rig:
            pw = world_from_locals(locals_at, p, cache)
            rest_local = exp_rest[p].inverted() @ exp_rest[jn]
            cache[jn] = (pw @ rest_local).normalized()
        else:
            cache[jn] = exp_rest[jn]
        return cache[jn]
    lq = q_up_local(local_q)
    if p and p in rig:
        pw = world_from_locals(locals_at, p, cache)
        cache[jn] = (pw @ lq).normalized()
    else:
        cache[jn] = (R_Q @ Quaternion((local_q[0], local_q[1], local_q[2], local_q[3]))).normalized()
    return cache[jn]


def q_up_local(q):
    # local rotations are frame-relative; the up-axis change cancels inside the hierarchy, so only re-order components
    return Quaternion((q[0], q[1], q[2], q[3])).normalized()


def pose_world_b(locals_at, jn, cache):
    """Blender-space world rotation of the bone once the engine's posed joint frame is applied to Blender's rest frame."""
    w = world_from_locals(locals_at, jn, cache)
    return (w @ exp_rest[jn].inverted() @ rest_world[jn]).normalized()


for key in data.get("pose", []):
    frame = int(key["t"])
    cache = {}
    for jn in key["joints"]:
        if jn not in rig:
            continue
        pb = arm.pose.bones[jn]
        parent = pb.parent
        pw_b = pose_world_b(key["joints"], jn, cache)
        if parent:
            par_b = pose_world_b(key["joints"], parent.name, cache)
            rest_local_b = rest_world[parent.name].inverted() @ rest_world[jn]
            basis = rest_local_b.inverted() @ par_b.inverted() @ pw_b
        else:
            basis = rest_world[jn].inverted() @ pw_b
        pb.rotation_quaternion = basis.normalized()
        pb.keyframe_insert("rotation_quaternion", frame=frame)

for k in data.get("travel", []):
    frame = int(k["t"])
    arm.location = v_up(k["pos"])
    arm.rotation_mode = "QUATERNION"
    arm.rotation_quaternion = q_up(k["rot"])
    arm.keyframe_insert("location", frame=frame)
    arm.keyframe_insert("rotation_quaternion", frame=frame)

scene.frame_start = 0
scene.frame_end = max([int(k["t"]) for k in data.get("pose", [])] + [int(k["t"]) for k in data.get("travel", [])] + [1])
bpy.ops.object.mode_set(mode="OBJECT")
print(f"imported {len(data.get('pose', []))} pose keys, {len(data.get('travel', []))} travel keys onto {name}")
