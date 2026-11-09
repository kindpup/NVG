# rerig_nvg_bindpose.py
# Re-rig ACM NVG models: make the STOWED position the bind/rest pose.
#
# WHY: Arma Reforger's inventory paperdoll and arsenal previews render worn models
# in their raw bind pose (no proc anim runs there). These models were authored with
# the goggles DEPLOYED at bind, so previews show them across the face. The game
# rotates w_nvgRotator by -145 deg (X axis) to stow; baking that same rotation into
# the rest pose makes previews show stowed, and the runtime math is then flipped in
# ACM_V2_NVG.siga (+145 to deploy) plus a script polarity change.
#
# Run via run_rerig.bat (blender --background --factory-startup --python thisfile).
# Originals are backed up as <name>.fbx.bak next to each file (only the first run -
# an existing .bak is never overwritten). Check renders land in Tools/rerig_renders.

import bpy, os, json, math, shutil, traceback
from mathutils import Vector, Matrix

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
RENDER_DIR = os.path.join(ROOT, "Tools", "rerig_renders")
REPORT = os.path.join(ROOT, "Tools", "rerig_report.json")
BONE = "w_nvgRotator"

# Per-model rotation. axis/angle are in the bone's LOCAL space, matching the game's
# RotationMake X axis * -145. If a render shows a model rotating the wrong way or
# around the wrong axis, override just that model here and rerun.
DEFAULT_ROT = {"axis": "X", "deg": -145.0}
MODELS = [
    {"fbx": "Assets/Fpano.fbx"},
    {"fbx": "Assets/GPNVG.fbx"},
    {"fbx": "Assets/PVS14.fbx"},
    {"fbx": "Assets/PVS31Rigged.fbx"},
    {"fbx": "Assets/pvs5RIGGED.fbx"},
    {"fbx": "Assets/PVS7/PVS7 rigged.fbx"},
    {"fbx": "Assets/IVAS/IVAS.fbx"},
]

def wipe_scene():
    bpy.ops.wm.read_homefile(use_empty=True)

def scene_bbox(objs):
    pts = []
    dg = bpy.context.evaluated_depsgraph_get()
    for o in objs:
        if o.type not in {"MESH", "ARMATURE"}:
            continue
        try:
            ev = o.evaluated_get(dg)
            for c in ev.bound_box:
                pts.append(ev.matrix_world @ Vector(c))
        except Exception:
            pass
    if not pts:
        return Vector((0, 0, 0)), 1.0
    mn = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    mx = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    center = (mn + mx) / 2
    radius = max((mx - mn).length / 2, 0.05)
    return center, radius

def render_check(name):
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "MATCAP"
    scene.render.resolution_x = 640
    scene.render.resolution_y = 640
    center, radius = scene_bbox(scene.objects)
    cam_data = bpy.data.cameras.new("chkcam")
    cam = bpy.data.objects.new("chkcam", cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam
    # side-ish view: from +X (right side), slightly above - flip direction shows best
    direction = Vector((1.0, -0.4, 0.25)).normalized()
    cam.location = center + direction * radius * 3.2
    look = (center - cam.location).normalized()
    cam.rotation_euler = look.to_track_quat("-Z", "Y").to_euler()
    cam_data.clip_start = 0.001
    cam_data.clip_end = radius * 20
    os.makedirs(RENDER_DIR, exist_ok=True)
    scene.render.filepath = os.path.join(RENDER_DIR, name + ".png")
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(cam)
    bpy.data.cameras.remove(cam_data)

def find_armature():
    for o in bpy.context.scene.objects:
        if o.type == "ARMATURE" and BONE in o.data.bones:
            return o
    return None

def process(entry, report):
    rel = entry["fbx"]
    rot = entry.get("rot", DEFAULT_ROT)
    path = os.path.join(ROOT, rel)
    tag = os.path.splitext(os.path.basename(rel))[0].replace(" ", "_")
    info = {"fbx": rel, "status": "?", "notes": []}
    report.append(info)
    if not os.path.isfile(path):
        info["status"] = "MISSING FILE"
        return
    wipe_scene()
    bpy.ops.import_scene.fbx(filepath=path)
    arm = find_armature()
    if not arm:
        info["status"] = "SKIPPED - no armature with bone " + BONE
        info["armatures"] = [f"{o.name}:{list(o.data.bones.keys())}" for o in bpy.context.scene.objects if o.type == "ARMATURE"]
        return
    info["armature"] = arm.name
    info["bones"] = list(arm.data.bones.keys())
    info["meshes"] = [o.name for o in bpy.context.scene.objects if o.type == "MESH"]
    info["materials"] = sorted({m.name for o in bpy.context.scene.objects if o.type == "MESH" for m in o.data.materials if m})

    render_check(tag + "_1_before")

    # pose the rotator in bone-local space
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="POSE")
    pb = arm.pose.bones[BONE]
    pb.rotation_mode = "XYZ"
    ang = math.radians(rot["deg"])
    axis_index = {"X": 0, "Y": 1, "Z": 2}[rot["axis"].upper()]
    eul = [0.0, 0.0, 0.0]
    eul[axis_index] = ang
    pb.rotation_euler = eul
    bpy.ops.object.mode_set(mode="OBJECT")
    bpy.context.view_layer.update()

    render_check(tag + "_2_posed")

    # bake deformation into every mesh skinned to this armature
    baked = []
    for o in list(bpy.context.scene.objects):
        if o.type != "MESH":
            continue
        for m in list(o.modifiers):
            if m.type == "ARMATURE" and m.object == arm:
                with bpy.context.temp_override(object=o, active_object=o, selected_objects=[o]):
                    try:
                        bpy.ops.object.modifier_apply(modifier=m.name)
                        baked.append(o.name)
                    except Exception as e:
                        info["notes"].append(f"modifier_apply failed on {o.name}: {e}")
    info["baked_meshes"] = baked

    # make the current pose the new rest pose
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="POSE")
    bpy.ops.pose.select_all(action="SELECT")
    bpy.ops.pose.armature_apply(selected=False)
    bpy.ops.object.mode_set(mode="OBJECT")

    # re-bind the baked meshes (pose == rest now, so no double deformation)
    for name in baked:
        o = bpy.data.objects[name]
        mod = o.modifiers.new("Armature", "ARMATURE")
        mod.object = arm

    bpy.context.view_layer.update()
    render_check(tag + "_3_final")

    # backup once, then overwrite the fbx
    bak = path + ".bak"
    if not os.path.exists(bak):
        shutil.copy2(path, bak)
        info["backup"] = os.path.basename(bak)
    else:
        info["backup"] = "already existed, kept"

    bpy.ops.export_scene.fbx(
        filepath=path,
        use_selection=False,
        add_leaf_bones=False,
        object_types={"EMPTY", "ARMATURE", "MESH", "OTHER"},
        mesh_smooth_type="OFF",
        use_tspace=False,
        bake_anim=False,
        path_mode="AUTO",
        embed_textures=False,
    )
    info["status"] = "OK"

def main():
    report = []
    for entry in MODELS:
        try:
            process(entry, report)
        except Exception:
            report.append({"fbx": entry["fbx"], "status": "ERROR", "trace": traceback.format_exc()})
    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    with open(REPORT, "w") as f:
        json.dump(report, f, indent=1)
    print(json.dumps(report, indent=1))

main()
