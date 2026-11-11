"""M1.2: imports every manifest row marked Port (meshes, textures, UI sprites, fonts, audio, video).

Settings come from the Unity side where Unity has them: a texture's .meta gives sRGB, normal-map and
9-slice border; FBX files use file scale in both engines, so no scale is applied. Assets that exist are
skipped unless overwrite is set (once an asset is in the project it is tuned here).
"""
import json
import math
import os
import re
import shutil
import subprocess
from collections import OrderedDict

import unreal

from .common import (IMPORT_DIR, STAGING, PROJECT_DIR, asset_exists, load, log, read_manifest, run_import_tasks, save,
                     split_object_path, unity_file, warn)

ENGINE_FONTS = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.engine_content_dir()), "Slate", "Fonts")
JA_FALLBACK_FACE = "/Game/Runner/UI/Fonts/FF_DroidSansFallback"
FLIPBOOK_CELL = 512

TEXTURE_EXT = {".png", ".jpg", ".jpeg", ".tga", ".exr", ".psd"}
AUDIO_EXT = {".ogg", ".mp3", ".wav"}


# ---------------------------------------------------------------------------------------------------- helpers

def _unity_meta(unity_path):
    try:
        with open(unity_file(unity_path) + ".meta", encoding="utf-8") as f:
            return f.read()
    except OSError:
        return ""


def _meta_int(meta, key, default=None):
    m = re.search(rf"^\s*{key}:\s*(-?\d+)", meta, re.M)
    return int(m.group(1)) if m else default


def _sprite_border(meta):
    """Unity spriteBorder {x: left, y: bottom, z: right, w: top} in pixels, or None."""
    m = re.search(r"spriteBorder:\s*\{x:\s*([\d.]+),\s*y:\s*([\d.]+),\s*z:\s*([\d.]+),\s*w:\s*([\d.]+)\}", meta)
    if not m:
        return None
    left, bottom, right, top = (float(v) for v in m.groups())
    return None if left == bottom == right == top == 0 else (left, top, right, bottom)


def _task(source, ue_path, options=None, factory=None):
    folder, name = split_object_path(ue_path)
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", source)
    t.set_editor_property("destination_path", folder)
    t.set_editor_property("destination_name", name)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("replace_existing_settings", True)
    t.set_editor_property("automated", True)
    t.set_editor_property("save", False)
    if options is not None:
        t.set_editor_property("options", options)
    if factory is not None:
        t.set_editor_property("factory", factory)
    return t


def _import_one(source, ue_path, report, options=None, factory=None):
    task = _task(source, ue_path, options, factory)
    run_import_tasks([task])
    if not asset_exists(ue_path):
        imported = list(task.get_editor_property("imported_object_paths") or [])
        report.add("FAILED", ue_path, f"import of {source} produced {imported or 'nothing'}")
        return None
    return load(ue_path)


# ---------------------------------------------------------------------------------------------------- meshes

def _fbx_options():
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", False)
    ui.set_editor_property("import_animations", False)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("automated_import_should_detect_type", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    sm = ui.get_editor_property("static_mesh_import_data")
    sm.set_editor_property("combine_meshes", True)
    sm.set_editor_property("import_mesh_lo_ds", True)  # Cyberpunk cars carry LOD groups (Unity LODGroup)
    sm.set_editor_property("auto_generate_collision", False)
    sm.set_editor_property("generate_lightmap_u_vs", False)
    sm.set_editor_property("convert_scene", True)
    sm.set_editor_property("force_front_x_axis", False)
    sm.set_editor_property("convert_scene_unit", False)
    sm.set_editor_property("import_uniform_scale", 1.0)
    # Axes and pivots are fixed after import (fit_to_unity): the legacy FBX path ignores import_rotation.
    sm.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)
    # Vertex colours carry real colour on several models (RealToon's MVCOL, laser.shadergraph): keep them.
    sm.set_editor_property("vertex_color_import_option", unreal.VertexColorImportOption.REPLACE)
    return ui


def _gallery_models():
    """Per FBX: Unity root-space bounds and renderers (Import/Gallery.json, written by UnrealGalleryCapture)."""
    path = os.path.join(IMPORT_DIR, "Gallery.json")
    if not os.path.exists(path):
        return {}
    with open(path, encoding="utf-8") as f:
        return {m["fbx"]: m for m in json.load(f)["models"]}


LOD_SUFFIX = re.compile(r"_LOD(\d+)$", re.I)
TEMP_MESHES = "/Game/Runner/_ImportTemp"


def _read_mesh(mesh, lod_index=0):
    dm = unreal.DynamicMesh()
    lod = unreal.GeometryScriptMeshReadLOD()
    lod.set_editor_property("lod_index", lod_index)
    dm, outcome = unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(
        mesh, dm, unreal.GeometryScriptCopyMeshFromAssetOptions(), lod)
    if outcome != unreal.GeometryScriptOutcomePins.SUCCESS:
        raise RuntimeError(f"copy_mesh_from_static_mesh failed ({mesh.get_name()} LOD {lod_index})")
    return dm


def _lod_meshes(row, node_names, report):
    """Unity reads LODs from node names (_LOD0, _LOD1, ...); Unreal only from FBX LOD groups, so it merges them.
    Imports the nodes as separate meshes and returns one dynamic mesh per LOD (nodes without a suffix are in
    every LOD), in the same space as the combined import."""
    lods = sorted({int(LOD_SUFFIX.search(n).group(1)) for n in node_names if LOD_SUFFIX.search(n)})
    folder = f"{TEMP_MESHES}/{split_object_path(row['UEPath'])[1]}"
    ui = _fbx_options()
    ui.get_editor_property("static_mesh_import_data").set_editor_property("combine_meshes", False)
    task = _task(unity_file(row["UnityPath"]), f"{folder}/Node", ui)
    run_import_tasks([task])
    parts = [load(p) for p in task.get_editor_property("imported_object_paths") or []]
    parts = [p for p in parts if isinstance(p, unreal.StaticMesh)]
    result = []
    for lod in lods:
        dm = unreal.DynamicMesh()
        for part in parts:
            m = LOD_SUFFIX.search(part.get_name())
            if m is None or int(m.group(1)) == lod:
                unreal.GeometryScript_MeshEdits.append_mesh(dm, _read_mesh(part), unreal.Transform())
        result.append(dm)
    unreal.EditorAssetLibrary.delete_directory(folder)
    report.add("LODS", row["UEPath"], f"{len(lods)} LODs from {len(parts)} nodes")
    return result


def fit_to_unity(mesh, unity_center, lod_meshes=None):
    """Moves the mesh into the project's axes with the Unity pivot: Unity (x, y, z) -> Unreal (z, x, y).
    The FBX importer lands Unity +Z on Unreal +Y and Unity +X on Unreal -X, so a -90 yaw finishes the axis
    move; it also bakes the FBX root node's offset into the vertices, which Unity does not (the root node is
    the prefab root and every prefab sets its transform), so the mesh is then translated until its bounds
    centre is the Unity root-space centre. lod_meshes, if given, replace the mesh's LODs first.
    Returns the translation applied (cm)."""
    tools = unreal.GeometryScript_AssetUtils
    if lod_meshes:
        mesh.set_num_source_models(len(lod_meshes))
    delta = None
    for lod_index in range(len(lod_meshes) if lod_meshes else mesh.get_num_lods()):
        dm = lod_meshes[lod_index] if lod_meshes else _read_mesh(mesh, lod_index)
        unreal.GeometryScript_MeshTransforms.transform_mesh(
            dm, unreal.Transform(rotation=unreal.Rotator(roll=0.0, pitch=0.0, yaw=-90.0)))
        if delta is None:  # LOD 0 decides the move; every LOD gets the same rigid transform
            box = unreal.GeometryScript_MeshQueries.get_mesh_bounding_box(dm)
            target = unreal.Vector(unity_center[2] * 100.0, unity_center[0] * 100.0, unity_center[1] * 100.0)
            delta = target - (box.min + box.max) * 0.5
        unreal.GeometryScript_MeshTransforms.translate_mesh(dm, delta)
        write = unreal.GeometryScriptCopyMeshToAssetOptions()
        write.set_editor_property("enable_recompute_normals", False)
        write.set_editor_property("enable_recompute_tangents", False)
        write.set_editor_property("replace_materials", False)
        wlod = unreal.GeometryScriptMeshWriteLOD()
        wlod.set_editor_property("lod_index", lod_index)
        dm, outcome = tools.copy_mesh_to_static_mesh(dm, mesh, write, wlod)
        if outcome != unreal.GeometryScriptOutcomePins.SUCCESS:
            raise RuntimeError(f"copy_mesh_to_static_mesh failed (LOD {lod_index})")
    return delta


def import_mesh(row, report):
    if asset_exists(row["UEPath"]):
        # A re-import keeps the settings stored on the asset over the task's: bring them in line first.
        data = load(row["UEPath"]).get_editor_property("asset_import_data")
        if isinstance(data, unreal.FbxStaticMeshImportData):
            data.set_editor_property("vertex_color_import_option", unreal.VertexColorImportOption.REPLACE)
    mesh = _import_one(unity_file(row["UnityPath"]), row["UEPath"], report, _fbx_options())
    unity = _gallery_models().get(row["UnityPath"])
    if mesh and unity:
        nodes = [r["mesh"] for r in unity["renderers"]]
        lods = _lod_meshes(row, nodes, report) if any(LOD_SUFFIX.search(n) for n in nodes) else None
        delta = fit_to_unity(mesh, unity["boundsCenter"], lods)
        report.add("FITTED", row["UEPath"], f"yaw -90, moved {delta.x:.1f}, {delta.y:.1f}, {delta.z:.1f} cm, "
                                            f"{mesh.get_num_lods()} LODs, {mesh.get_num_vertices(0)} verts in LOD 0")
    elif mesh:
        report.add("FAILED", row["UEPath"], "no Unity bounds in Gallery.json: axes not fixed (run the capture)")
    if mesh:
        box = mesh.get_bounding_box()
        size = box.max - box.min
        report.add("IMPORTED", row["UEPath"], f"bounds {size.x:.1f} x {size.y:.1f} x {size.z:.1f} cm, "
                                              f"{mesh.get_num_sections(0)} sections")
        save(row["UEPath"])


# ---------------------------------------------------------------------------------------------------- textures

def _apply_texture_settings(tex, ue_path, meta):
    srgb = _meta_int(meta, "sRGBTexture", 1) == 1
    is_normal = _meta_int(meta, "textureType", 0) == 1
    tex.set_editor_property("srgb", srgb and not is_normal)
    if is_normal:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("flip_green_channel", True)  # Unity normal maps are Y+ (OpenGL)
    elif not srgb:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)

    if "/UI/" in ue_path:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        tex.set_editor_property("never_stream", True)
    elif "/Textures/FX/" in ue_path:
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_EFFECTS)
    elif "/Textures/Sky/" in ue_path:
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_SKYBOX)

    wrap = _meta_int(meta, "wrapU", 0)
    if wrap == 1:  # Unity Clamp
        tex.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
        tex.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    return is_normal


def import_texture(row, report, source=None):
    meta = _unity_meta(row["UnityPath"])
    tex = _import_one(source or unity_file(row["UnityPath"]), row["UEPath"], report)
    if not tex:
        return
    is_normal = _apply_texture_settings(tex, row["UEPath"], meta)
    save(row["UEPath"])
    report.add("IMPORTED", row["UEPath"], f"{tex.blueprint_get_size_x()}x{tex.blueprint_get_size_y()}"
                                          + (", normal map (green flipped)" if is_normal else ""))
    if "/UI/Panels/" in row["UEPath"]:
        _make_panel_brush(tex, row["UEPath"], _sprite_border(meta), report)


def _make_panel_brush(tex, tex_path, border, report):
    folder, name = split_object_path(tex_path)
    brush_path = f"{folder}/BR_{name[2:]}"
    asset = load(brush_path) if asset_exists(brush_path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        f"BR_{name[2:]}", folder, unreal.SlateBrushAsset, unreal.SlateBrushAssetFactory())
    w, h = tex.blueprint_get_size_x(), tex.blueprint_get_size_y()
    brush = asset.get_editor_property("brush")
    brush.set_editor_property("resource_object", tex)
    brush.set_editor_property("image_size", unreal.Vector2D(w, h))
    if border:
        left, top, right, bottom = border
        brush.set_editor_property("draw_as", unreal.SlateBrushDrawType.BOX)
        brush.set_editor_property("margin", unreal.Margin(left / w, top / h, right / w, bottom / h))
    else:
        brush.set_editor_property("draw_as", unreal.SlateBrushDrawType.IMAGE)
    asset.set_editor_property("brush", brush)
    save(brush_path)
    report.add("CREATED", brush_path, f"9-slice {border}" if border else "plain image")


def import_flipbook(ue_path, rows, report):
    """Packs the frames (in file-name order, as GameSettings lists them) into one SubUV sheet."""
    frames = sorted(rows, key=lambda r: r["UnityPath"])
    cols = math.ceil(math.sqrt(len(frames)))
    rws = math.ceil(len(frames) / cols)
    out = os.path.join(STAGING, split_object_path(ue_path)[1] + ".png")
    os.makedirs(STAGING, exist_ok=True)
    c = FLIPBOOK_CELL
    cmd = ["ffmpeg", "-y", "-v", "error"]
    for r in frames:
        cmd += ["-i", unity_file(r["UnityPath"])]
    # Each frame is fitted into a square cell, centred on transparent padding, then the cells are stacked.
    chains = [f"[{i}:v]format=rgba,scale={c}:{c}:force_original_aspect_ratio=decrease,"
              f"pad={c}:{c}:(ow-iw)/2:(oh-ih)/2:color=0x00000000[f{i}]" for i in range(len(frames))]
    pads = cols * rws - len(frames)
    for j in range(pads):
        chains.append(f"color=c=0x00000000:s={c}x{c},format=rgba[e{j}]")
    inputs = "".join(f"[f{i}]" for i in range(len(frames))) + "".join(f"[e{j}]" for j in range(pads))
    layout = "|".join(f"{(k % cols) * c}_{(k // cols) * c}" for k in range(cols * rws))
    graph = ";".join(chains) + f";{inputs}xstack=inputs={cols * rws}:layout={layout}:fill=0x00000000,format=rgba"
    cmd += ["-filter_complex", graph, "-frames:v", "1", out]
    try:
        subprocess.run(cmd, check=True, capture_output=True, text=True)
    except (OSError, subprocess.CalledProcessError) as e:
        report.add("FAILED", ue_path, f"ffmpeg: {getattr(e, 'stderr', e)}")
        return
    tex = _import_one(out, ue_path, report)
    if tex:
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_EFFECTS)
        save(ue_path)
        report.add("IMPORTED", ue_path, f"SubUV {cols}x{rws} ({len(frames)} frames, {c}px cells)")


# ---------------------------------------------------------------------------------------------------- audio, fonts, video

# The Unity music files run about an hour; the port keeps a seamless loop of this length (Jorge, 2026-11-11:
# "trim to a loop"), since a full hour is ~0.65 GB of PCM per asset. Tracks shorter than this are kept whole.
MUSIC_LOOP_SECONDS = 180.0
MUSIC_CROSSFADE_SECONDS = 4.0


def _music_loop(source, ue_path, report):
    """Writes a WAV of the first MUSIC_LOOP_SECONDS whose tail crossfades into its head, so it loops seamlessly:
    out = src[X, L] followed by (src[L, L+X] fading out + src[0, X] fading in), X = crossfade, L = loop length."""
    probe = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", source],
                           capture_output=True, text=True, check=True)
    duration = float(probe.stdout.strip())
    if duration <= MUSIC_LOOP_SECONDS + MUSIC_CROSSFADE_SECONDS:
        return source, f"kept whole ({duration:.0f} s)"
    L, X = MUSIC_LOOP_SECONDS, MUSIC_CROSSFADE_SECONDS
    out = os.path.join(STAGING, split_object_path(ue_path)[1] + ".wav")
    os.makedirs(STAGING, exist_ok=True)
    graph = (f"[0:a]asplit=3[a][b][c];"
             f"[a]atrim={X}:{L},asetpts=PTS-STARTPTS[body];"
             f"[b]atrim={L}:{L + X},asetpts=PTS-STARTPTS,afade=t=out:st=0:d={X}[tail];"
             f"[c]atrim=0:{X},asetpts=PTS-STARTPTS,afade=t=in:st=0:d={X}[head];"
             f"[tail][head]amix=inputs=2:normalize=0[seam];"
             f"[body][seam]concat=n=2:v=0:a=1[out]")
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-i", source, "-filter_complex", graph, "-map", "[out]",
                    "-c:a", "pcm_s16le", out], check=True, capture_output=True, text=True)
    return out, f"looped {L:.0f} s of {duration:.0f} s, {X:.0f} s crossfade"


def import_audio(row, report):
    source, note = unity_file(row["UnityPath"]), ""
    is_music = "/Audio/Music/" in row["UEPath"]
    if is_music:
        try:
            source, note = _music_loop(source, row["UEPath"], report)
        except (OSError, subprocess.CalledProcessError) as e:
            report.add("FAILED", row["UEPath"], f"ffmpeg: {getattr(e, 'stderr', e)}")
            return
    wave = _import_one(source, row["UEPath"], report)
    if not wave:
        return
    if is_music:
        wave.set_editor_property("loading_behavior", unreal.SoundWaveLoadingBehavior.LOAD_ON_DEMAND)
        wave.set_editor_property("looping", True)
    save(row["UEPath"])
    report.add("IMPORTED", row["UEPath"], f"{wave.get_editor_property('duration'):.1f} s" + (f", {note}" if note else ""))


def _import_font_face(source, ue_path, report):
    face = _import_one(source, ue_path, report)
    if face and not isinstance(face, unreal.FontFace):
        report.add("FAILED", ue_path, f"imported as {type(face).__name__}, expected FontFace")
        return None
    if face:
        face.set_editor_property("loading_policy", unreal.FontLoadingPolicy.INLINE)
        save(ue_path)
    return face


def import_font(row, report, overwrite):
    if overwrite or not asset_exists(JA_FALLBACK_FACE):
        _import_font_face(os.path.join(ENGINE_FONTS, "DroidSansFallback.ttf"), JA_FALLBACK_FACE, report)
    face = _import_font_face(unity_file(row["UnityPath"]), row["UEPath"], report)
    if not face:
        return
    report.add("IMPORTED", row["UEPath"], "font face")
    _build_font(row["UEPath"], face, report)


def _font_path(face_path):
    folder, name = split_object_path(face_path)
    return f"{folder}/F_{name[3:]}"


def _build_font(face_path, face, report):
    font_path = _font_path(face_path)
    lib = getattr(unreal, "RunnerAssetScriptLibrary", None)
    if lib is None:  # RunnerEditor not rebuilt yet (the helper is C++)
        report.add("DEFERRED", font_path, "build RunnerEditor, then re-run with --only .ttf")
        return
    font = lib.build_composite_font(font_path, face, load(JA_FALLBACK_FACE))
    report.add("CREATED" if font else "FAILED", font_path, "composite font, fallback DroidSansFallback")


def import_video(row, report):
    movies = os.path.join(PROJECT_DIR, "Content", "Movies")
    os.makedirs(movies, exist_ok=True)
    target = os.path.join(movies, os.path.basename(row["UnityPath"]))
    shutil.copyfile(unity_file(row["UnityPath"]), target)
    folder, name = split_object_path(row["UEPath"])
    src = load(row["UEPath"]) if asset_exists(row["UEPath"]) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, folder, unreal.FileMediaSource, unreal.FileMediaSourceFactoryNew())
    src.set_file_path(target)
    save(row["UEPath"])
    report.add("IMPORTED", row["UEPath"], f"file {src.get_editor_property('file_path')}")


# ---------------------------------------------------------------------------------------------------- entry

def run(report, overwrite=False, only=None):
    """only: optional set of extensions or substrings of UE paths to restrict the run."""
    # Legacy FBX path, so FbxImportUI options apply exactly (Interchange would translate them).
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX false")

    units = OrderedDict()
    for row in read_manifest():
        if row["Action"] == "Port" and row["UEPath"] and row["Ext"] != ".mat":
            units.setdefault(row["UEPath"], []).append(row)

    for ue_path, rows in units.items():
        row = rows[0]
        ext = row["Ext"].lower()
        if only and not any(o == ext or o in ue_path for o in only):
            continue
        if not os.path.exists(unity_file(row["UnityPath"])):
            report.add("FAILED", ue_path, f"source missing: {row['UnityPath']}")
            continue
        if asset_exists(ue_path) and not overwrite:
            if ext in (".ttf", ".otf") and not asset_exists(_font_path(ue_path)):
                _build_font(ue_path, load(ue_path), report)  # face imported earlier, font still owed
            else:
                report.add("SKIPPED", ue_path, "exists")
            continue
        try:
            if ext == ".fbx":
                import_mesh(row, report)
            elif ext in TEXTURE_EXT and len(rows) > 1:
                import_flipbook(ue_path, rows, report)
            elif ext in TEXTURE_EXT:
                import_texture(row, report)
            elif ext in AUDIO_EXT:
                import_audio(row, report)
            elif ext in (".ttf", ".otf"):
                import_font(row, report, overwrite)
            elif ext == ".mp4":
                import_video(row, report)
            else:
                report.add("FAILED", ue_path, f"no importer for {ext}")
        except Exception as e:  # keep going; the report lists every failure
            report.add("FAILED", ue_path, repr(e))
    log(f"import: {len(units)} Port assets in the manifest")
