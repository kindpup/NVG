# axis_candidates.py - RENDER ONLY. Touches no asset.
# Imports the ORIGINAL (.bak) Fpano model and renders it in the rest pose plus every
# candidate 145-degree rotation of w_nvgRotator, from front and side, colliders hidden.
# Goal: identify which axis/sign actually folds the goggles UP off the face.
import bpy, os, math, json, traceback
from mathutils import Vector

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
OUT  = os.path.join(ROOT, "Tools", "axis_candidates")
SRC  = os.path.join(ROOT, "Assets", "Fpano.fbx.bak")   # untouched original
BONE = "w_nvgRotator"
HIDE = ("UBX", "UTM", "UCX", "USP", "UCS", "UCL", "COM_", "OCC")

CANDIDATES = [("rest", None, 0.0)]
for ax in ("X", "Y", "Z"):
    for d in (145.0, -145.0):
        CANDIDATES.append((f"{ax}{'+' if d>0 else '-'}145", ax, d))

VIEWS = {"side": Vector((1.0, 0.0, 0.05)), "front": Vector((0.0, -1.0, 0.05))}

def is_col(n):
    u = n.upper()
    return any(u.startswith(p) for p in HIDE) or "FIREGEO" in u

def render(png, view_dir):
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "STUDIO"
    sc.display.shading.show_cavity = True
    sc.render.resolution_x = 480; sc.render.resolution_y = 480
    meshes = [o for o in sc.objects if o.type == "MESH" and not is_col(o.name)]
    for o in sc.objects:
        if o.type == "MESH" and is_col(o.name):
            o.hide_render = True
    dg = bpy.context.evaluated_depsgraph_get()
    pts = []
    for o in meshes:
        ev = o.evaluated_get(dg)
        for c in ev.bound_box:
            pts.append(ev.matrix_world @ Vector(c))
    if not pts: return False
    mn = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    mx = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    # frame on the REST bbox so candidates stay comparable, with generous margin
    center = (mn + mx) / 2
    radius = max((mx - mn).length / 2, 0.02) * 1.5
    cd = bpy.data.cameras.new("c"); cam = bpy.data.objects.new("c", cd)
    sc.collection.objects.link(cam); sc.camera = cam
    cam.location = center + view_dir.normalized() * radius * 3.0
    cam.rotation_euler = (center - cam.location).normalized().to_track_quat("-Z","Y").to_euler()
    cd.clip_start = 0.0005; cd.clip_end = radius * 40
    os.makedirs(os.path.dirname(png), exist_ok=True)
    sc.render.filepath = png
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(cam); bpy.data.cameras.remove(cd)
    return True

rep = []
for label, axis, deg in CANDIDATES:
    try:
        bpy.ops.wm.read_homefile(use_empty=True)
        bpy.ops.import_scene.fbx(filepath=SRC)
        arm = next((o for o in bpy.context.scene.objects if o.type=="ARMATURE" and BONE in o.data.bones), None)
        if not arm:
            rep.append({"cand": label, "status": "no armature"}); continue
        if axis:
            bpy.context.view_layer.objects.active = arm
            bpy.ops.object.mode_set(mode="POSE")
            pb = arm.pose.bones[BONE]; pb.rotation_mode = "XYZ"
            e = [0.0,0.0,0.0]; e[{"X":0,"Y":1,"Z":2}[axis]] = math.radians(deg)
            pb.rotation_euler = e
            bpy.ops.object.mode_set(mode="OBJECT")
            bpy.context.view_layer.update()
        for vname, vdir in VIEWS.items():
            render(os.path.join(OUT, f"{label}_{vname}.png"), vdir)
        rep.append({"cand": label, "status": "ok"})
    except Exception:
        rep.append({"cand": label, "status": "ERROR", "trace": traceback.format_exc()})
print(json.dumps(rep, indent=1))
