# verify_rerig.py - RENDER ONLY, changes nothing.
# Renders each re-rigged FBX next to its .bak original, with collider/FireGeo meshes
# (UBX_/UTM_/UCX_/COM_/FireGeo) hidden so the actual goggle geometry is visible.
import bpy, os, json, traceback
from mathutils import Vector

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
OUT = os.path.join(ROOT, "Tools", "verify_renders")
HIDE_PREFIX = ("UBX", "UTM", "UCX", "USP", "UCS", "UCL", "COM_", "OCC")

FBXS = ["Assets/Fpano.fbx", "Assets/GPNVG.fbx", "Assets/PVS14.fbx",
        "Assets/PVS31Rigged.fbx", "Assets/PVS7/PVS7 rigged.fbx", "Assets/IVAS/IVAS.fbx"]

def is_collider(name):
    n = name.upper()
    return any(n.startswith(p) for p in HIDE_PREFIX) or "FIREGEO" in n

def visible_meshes():
    return [o for o in bpy.context.scene.objects if o.type == "MESH" and not is_collider(o.name)]

def render(path_png):
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "STUDIO"
    sc.display.shading.show_cavity = True
    sc.render.resolution_x = 700
    sc.render.resolution_y = 700
    sc.render.film_transparent = False
    meshes = visible_meshes()
    for o in bpy.context.scene.objects:
        if o.type == "MESH" and is_collider(o.name):
            o.hide_render = True
    pts = []
    dg = bpy.context.evaluated_depsgraph_get()
    for o in meshes:
        ev = o.evaluated_get(dg)
        for c in ev.bound_box:
            pts.append(ev.matrix_world @ Vector(c))
    if not pts:
        return False
    mn = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    mx = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    center = (mn + mx) / 2
    radius = max((mx - mn).length / 2, 0.02)
    cd = bpy.data.cameras.new("c"); cam = bpy.data.objects.new("c", cd)
    sc.collection.objects.link(cam); sc.camera = cam
    # look from the model's right side: X across, Z up -> profile view shows flip clearly
    d = Vector((1.0, -0.25, 0.15)).normalized()
    cam.location = center + d * radius * 3.0
    cam.rotation_euler = (center - cam.location).normalized().to_track_quat("-Z", "Y").to_euler()
    cd.clip_start = 0.0005; cd.clip_end = radius * 30
    os.makedirs(os.path.dirname(path_png), exist_ok=True)
    sc.render.filepath = path_png
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(cam); bpy.data.cameras.remove(cd)
    return True

rep = []
for rel in FBXS:
    tag = os.path.splitext(os.path.basename(rel))[0].replace(" ", "_")
    for label, p in (("ORIGINAL", os.path.join(ROOT, rel) + ".bak"), ("RERIGGED", os.path.join(ROOT, rel))):
        try:
            if not os.path.isfile(p):
                rep.append({"file": rel, "which": label, "status": "missing"}); continue
            bpy.ops.wm.read_homefile(use_empty=True)
            bpy.ops.import_scene.fbx(filepath=p)
            ok = render(os.path.join(OUT, f"{tag}_{label}.png"))
            rep.append({"file": rel, "which": label, "status": "ok" if ok else "no visible mesh",
                        "meshes": [o.name for o in visible_meshes()]})
        except Exception:
            rep.append({"file": rel, "which": label, "status": "ERROR", "trace": traceback.format_exc()})
with open(os.path.join(ROOT, "Tools", "verify_report.json"), "w") as f:
    json.dump(rep, f, indent=1)
print(json.dumps(rep, indent=1))
