"""M1.4: assigns each mesh its runner materials and builds /Game/Runner/Maps/Dev/L_AssetGallery.

Reads Import/Gallery.json (written by the Unity tool UnrealGalleryCapture): per model, the FBX's
material names per submesh and the Unity materials the runner renders them with. Unity materials map to
Unreal instances through the manifest, and Unreal slot names are the FBX material names, so slots are
matched by name (index order as fallback). The map shows each model, scaled to a common size, beside a
plate with its Unity capture.
"""
import json
import os

import unreal

from .common import IMPORT_DIR, asset_exists, load, log, read_manifest, run_import_tasks, save, split_object_path
from . import materials

GALLERY_JSON = os.path.join(IMPORT_DIR, "Gallery.json")
MAP_PATH = "/Game/Runner/Maps/Dev/L_AssetGallery"
DEV = "/Game/Runner/Dev/Gallery"
PLATE_MASTER = f"{DEV}/M_GalleryPlate"
CELL = 600.0       # cm between models
DISPLAY = 150.0    # every model is scaled to this bounding radius (cm); the label gives the real size


def _norm(name):
    return "".join(ch for ch in name.lower() if ch.isalnum())


def assign_materials(report, models, to_ue):
    """Sets each imported mesh's slot materials to the runner's. Returns {mesh path: [material paths]}."""
    assigned = {}
    for model in models:
        mesh_path = to_ue.get(model["fbx"])
        mesh = load(mesh_path) if mesh_path and asset_exists(mesh_path) else None
        if mesh is None:
            report.add("FAILED", model["fbx"], "mesh not imported")
            continue
        by_name, ordered = {}, []
        for r in model["renderers"]:
            for src, mat in zip(r["sourceMaterials"], r["materials"]):
                # A material embedded in the FBX carries the FBX's path, which maps to the mesh: skip it.
                ue = to_ue.get(mat) if mat and mat.lower().endswith(".mat") else None
                if ue:
                    by_name.setdefault(_norm(src), ue)
                ordered.append(ue)
        slots = list(mesh.get_editor_property("static_materials"))
        result, missing = [], []
        for i, slot in enumerate(slots):
            name = str(slot.get_editor_property("material_slot_name"))
            ue = by_name.get(_norm(name)) or by_name.get(_norm(name.split("_skinned")[0]))
            if ue is None and i < len(ordered):
                ue = ordered[i]
            mi = load(ue) if ue and asset_exists(ue) else None
            if mi is None:
                missing.append(name)
            else:
                slot.set_editor_property("material_interface", mi)
            result.append(ue)
        mesh.set_editor_property("static_materials", slots)
        save(mesh_path)
        assigned[mesh_path] = result
        report.add("ASSIGNED" if not missing else "PARTIAL", mesh_path,
                   ", ".join(p.rsplit("/", 1)[1] if p else "-" for p in result)
                   + (f"; no runner material for slots {missing}" if missing else ""))
    return assigned


def _plate_master(report):
    mat = materials._fresh(PLATE_MASTER, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    g = materials.Graph(mat)
    tex = g.node(unreal.MaterialExpressionTextureSampleParameter2D, -1, parameter_name="Capture",
                 texture=load("/Engine/EngineResources/WhiteSquareTexture"))
    g.out(tex, unreal.MaterialProperty.MP_EMISSIVE_COLOR, "RGB")
    unreal.MaterialEditingLibrary.recompile_material(mat)
    save(PLATE_MASTER)


def _capture_plate_material(model, report):
    stem = os.path.splitext(os.path.basename(model["fbx"]))[0]
    name = "".join(p[:1].upper() + p[1:] for p in stem.replace("-", " ").split())
    tex_path = f"{DEV}/T_Capture_{name}"
    src = os.path.join(IMPORT_DIR, model["capture"].replace("/", os.sep))
    task = unreal.AssetImportTask()
    for k, v in (("filename", src), ("destination_path", DEV), ("destination_name", f"T_Capture_{name}"),
                 ("replace_existing", True), ("automated", True), ("save", True)):
        task.set_editor_property(k, v)
    run_import_tasks([task])
    tex = load(tex_path)
    if tex is None:
        report.add("FAILED", tex_path, "capture import")
        return None
    tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    save(tex_path)
    mi_path = f"{DEV}/MI_Capture_{name}"
    mi = load(mi_path) if asset_exists(mi_path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        f"MI_Capture_{name}", DEV, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    unreal.MaterialEditingLibrary.set_material_instance_parent(mi, load(PLATE_MASTER))
    unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(mi, "Capture", tex)
    save(mi_path)
    return mi


def _outline_for(material_paths):
    widths = [materials.INSTANCES[p][2].get("outline") for p in material_paths if p in materials.INSTANCES]
    widths = [w for w in widths if w]
    return load(materials.outline_instance_path(max(widths))) if widths else None


def _spawn_mesh(actors, mesh, pos, rot=None):
    """A StaticMeshActor with mesh (spawn_actor_from_object returns None in headless runs)."""
    actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, pos, rot or unreal.Rotator(0, 0, 0))
    actor.static_mesh_component.set_static_mesh(mesh)
    return actor


def build_map(report, models, to_ue, assigned):
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if asset_exists(MAP_PATH):
        # Rebuild in place: the map may be the loaded level, which cannot be deleted.
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        already_open = world is not None and world.get_path_name().split(".")[0] == MAP_PATH
        if not already_open and not levels.load_level(MAP_PATH):
            report.add("FAILED", MAP_PATH, "load_level")
            return
        for a in actors.get_all_level_actors():
            if not isinstance(a, (unreal.WorldSettings, unreal.Brush)):
                actors.destroy_actor(a)
    elif not levels.new_level(MAP_PATH):
        report.add("FAILED", MAP_PATH, "new_level")
        return

    _plate_master(report)
    plane = load("/Engine/BasicShapes/Plane")
    light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1000),
                                          # Unity Euler(40, -30, 0) -> pitch -40, yaw = 90 - (-30) in Unreal axes
                                          unreal.Rotator(roll=0, pitch=-40, yaw=120))
    light.set_actor_label("GalleryLight")
    actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 500)).set_actor_label("SkyLight")
    ppv = actors.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0))
    ppv.set_editor_property("unbound", True)
    settings = ppv.get_editor_property("settings")
    settings.set_editor_property("override_auto_exposure_method", True)
    settings.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
    settings.set_editor_property("override_auto_exposure_bias", True)
    settings.set_editor_property("auto_exposure_bias", 10.0)
    ppv.set_editor_property("settings", settings)

    cols = 6
    for i, model in enumerate(models):
        mesh_path = to_ue.get(model["fbx"])
        mesh = load(mesh_path) if mesh_path else None
        if mesh is None:
            continue
        x, y = (i // cols) * CELL * 1.6, (i % cols) * CELL
        box = mesh.get_bounding_box()
        size = box.max - box.min
        radius = max(size.length() * 0.5, 1.0)
        scale = DISPLAY / radius
        centre = (box.min + box.max) * 0.5
        pos = unreal.Vector(x, y, DISPLAY) - centre * scale
        actor = _spawn_mesh(actors, mesh, pos)
        actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
        actor.set_actor_label(mesh_path.rsplit("/", 1)[1])
        outline = _outline_for(assigned.get(mesh_path, []))
        if outline:
            actor.static_mesh_component.set_editor_property("overlay_material", outline)

        mi = _capture_plate_material(model, report)
        if mi:
            # A 3 m plate standing behind the model (plane is 1 m, facing +Z; stand it up facing -X).
            plate = _spawn_mesh(actors, plane, unreal.Vector(x + 250, y, DISPLAY),
                                unreal.Rotator(roll=90, pitch=0, yaw=-90))
            plate.set_actor_scale3d(unreal.Vector(3, 3, 1))
            plate.static_mesh_component.set_material(0, mi)
            plate.set_actor_label(f"Unity_{mesh_path.rsplit('/', 1)[1]}")

        label = actors.spawn_actor_from_class(unreal.TextRenderActor, unreal.Vector(x - 200, y, 10),
                                              unreal.Rotator(0, 180, 0))
        text = label.get_editor_property("text_render")
        text.set_editor_property("text", f"{mesh_path.rsplit('/', 1)[1]}  ({size.x / 100:.2f} x {size.y / 100:.2f} x {size.z / 100:.2f} m)")
        text.set_editor_property("world_size", 18.0)
        text.set_editor_property("horizontal_alignment", unreal.HorizTextAligment.EHTA_CENTER)
        label.set_actor_label(f"Label_{mesh_path.rsplit('/', 1)[1]}")
        report.add("PLACED", mesh_path, f"scale {scale:.3f}")

    levels.save_current_level()
    report.add("BUILT", MAP_PATH, f"{len(models)} models")


def run(report, overwrite=False):
    if not os.path.exists(GALLERY_JSON):
        report.add("FAILED", GALLERY_JSON, "run the Unity tool Tools > Unreal Export > Capture Gallery first")
        return
    with open(GALLERY_JSON, encoding="utf-8") as f:
        models = json.load(f)["models"]
    to_ue = {r["UnityPath"]: r["UEPath"] for r in read_manifest() if r["UEPath"]}
    assigned = assign_materials(report, models, to_ue)
    build_map(report, models, to_ue, assigned)
    log("gallery done")
