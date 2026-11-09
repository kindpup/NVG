# inspect_rig.py - READ ONLY. Dumps rig structure for original (.bak) vs current FBX.
# Answers: how is each mesh attached to the rig - skinned, bone-parented, object-parented,
# or not attached at all - and which bone actually carries its weights.
import bpy, os, json, traceback

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
OUT = os.path.join(ROOT, "Tools", "rig_inspection.json")
BONE = "w_nvgRotator"
FBXS = ["Assets/Fpano.fbx", "Assets/GPNVG.fbx", "Assets/PVS14.fbx",
        "Assets/PVS31Rigged.fbx", "Assets/PVS7/PVS7 rigged.fbx", "Assets/IVAS/IVAS.fbx"]

def describe():
    out = []
    for o in bpy.context.scene.objects:
        d = {
            "name": o.name, "type": o.type,
            "parent": o.parent.name if o.parent else None,
            "parent_type": o.parent_type if o.parent else None,
            "parent_bone": (o.parent_bone or None) if o.parent else None,
            "loc": [round(v, 4) for v in o.matrix_world.translation],
        }
        if o.type == "MESH":
            d["armature_mods"] = [m.object.name for m in o.modifiers if m.type == "ARMATURE" and m.object]
            # how many verts carry weight in each vertex group
            gnames = {g.index: g.name for g in o.vertex_groups}
            counts = {}
            for v in o.data.vertices:
                for g in v.groups:
                    if g.weight > 0.0001:
                        n = gnames.get(g.group, "?")
                        counts[n] = counts.get(n, 0) + 1
            d["weighted_verts_per_group"] = counts
            d["vert_count"] = len(o.data.vertices)
        if o.type == "ARMATURE":
            d["bones"] = {b.name: {
                "parent": b.parent.name if b.parent else None,
                "head": [round(v, 4) for v in b.head_local],
                "tail": [round(v, 4) for v in b.tail_local],
            } for b in o.data.bones}
        out.append(d)
    return out

rep = {}
for rel in FBXS:
    for label, p in (("ORIGINAL", os.path.join(ROOT, rel) + ".bak"),
                     ("CURRENT",  os.path.join(ROOT, rel))):
        key = f"{os.path.basename(rel)} [{label}]"
        try:
            if not os.path.isfile(p):
                rep[key] = {"status": "missing"}; continue
            bpy.ops.wm.read_homefile(use_empty=True)
            bpy.ops.import_scene.fbx(filepath=p)
            rep[key] = {"status": "ok", "objects": describe()}
        except Exception:
            rep[key] = {"status": "ERROR", "trace": traceback.format_exc()}

with open(OUT, "w") as f:
    json.dump(rep, f, indent=1)
print("WROTE " + OUT)
