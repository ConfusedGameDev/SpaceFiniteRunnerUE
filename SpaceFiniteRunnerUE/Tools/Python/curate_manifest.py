"""Fills the Action, UEPath and Notes columns of Import/AssetManifest.csv from PRD Appendix A.

Plain Python (no Unreal). Run from anywhere:
    python Tools/Python/curate_manifest.py [--check]

The Unity exporter keeps these three columns on re-export, so this only has to run again when the
manifest gains rows. --check changes nothing and exits 1 if any Port row has no UEPath.
Rules are ordered: the first match wins. A rule's UEPath may use {name} (the Unity file name in
PascalCase, see ue_name) and {stem} (the raw file name without extension).
"""
import csv
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MANIFEST = os.path.normpath(os.path.join(HERE, "..", "..", "Import", "AssetManifest.csv"))

R = "/Game/Runner"


def ue_name(stem):
    """'road-straight_v1' -> 'RoadStraight_v1', 'Kenney Future Narrow' -> 'KenneyFutureNarrow'."""
    parts = re.split(r"[\s\-]+", stem)
    return "".join(p[:1].upper() + p[1:] for p in parts if p)


# (regex on UnityPath, Action, UEPath template or None, Notes)
RULES = [
    # A.1 / A.13: lighting is rebaked with Lumen
    (r"^Assets/05\.Scenes/.*\.exr$", "Not ported", None, "Reflection probe; rebaked with Lumen (A.1)"),
    (r"^Assets/05\.Scenes/.*\.unity$", "Rebuild", None, "Map rebuilt in UE (A.1)"),
    # A.7: brake pads are not ported
    (r"/slime_000\.ogg$", "Not ported", None, "Brake pad sound (A.7)"),

    # A.2 meshes
    (r"/99\.Test/Diego/3DModels/nabucodonosor\.fbx$", "Port", R + "/Meshes/Ship/SM_Ship",
     "Imported from 99.Test/Diego (A.13.1: left in place in Unity, noted here)"),
    (r"/FiniteRunner/(OriginalModels/)?(Laser[AB]|laserTypeA)\.fbx$", "Port", R + "/Meshes/Laser/SM_{name}", ""),
    (r"/OriginalModels/(Ultraboost|Boost_v2|MedPack)\.fbx$", "Port", R + "/Meshes/Orbs/SM_{name}", ""),
    (r"/Prototype/indicator-[^/]+\.fbx$", "Port", R + "/Meshes/Orbs/SM_{name}", "Kenney Prototype Kit (CC0)"),
    (r"/FiniteRunner/(OriginalModels/)?road-[^/]+\.fbx$", "Port", R + "/Meshes/Track/SM_{name}", ""),
    (r"/Cyberpunk_Megapolis/Models/Car/[^/]+\.fbx$", "Port", R + "/Meshes/Vehicles/SM_{name}", "Cyberpunk Megapolis"),

    # A.5 textures
    (r"/FiniteRunner/Textures/colormap\.png$", "Port", R + "/Textures/Track/T_Colormap_Roads", "Kenney City Kit Roads (CC0)"),
    (r"/Prototype/Textures/colormap\.png$", "Port", R + "/Textures/Track/T_Colormap_Prototype", "Kenney Prototype Kit (CC0)"),
    (r"/OriginalTextures/multicolor-industrial-glitcho\.jpg$", "Port", R + "/Textures/Track/T_MulticolorIndustrialGlitcho_Jpg", ""),
    (r"/OriginalTextures/[^/]+\.png$", "Port", R + "/Textures/Track/T_{name}", ""),
    (r"/aerial-drone-panorama-chisinau-seamless\.jpg$", "Port", R + "/Textures/Sky/T_AerialDronePanorama", "Stock photo"),
    (r"/03\.Textures/FiniteRunner/ShipBoostSparks_Sheet\.png$", "Port", R + "/Textures/FX/T_ShipBoostSparks_Sheet", "SubUV"),
    (r"/SmokeAndExplosions/Black smoke/[^/]+\.png$", "Port", R + "/Textures/FX/T_BlackSmoke_SubUV",
     "Frame packed into the SubUV sheet (Kenney Smoke Particles, CC0)"),
    (r"/SmokeAndExplosions/Explosion/[^/]+\.png$", "Port", R + "/Textures/FX/T_Explosion_SubUV",
     "Frame packed into the SubUV sheet (Kenney Smoke Particles, CC0)"),
    (r"/05\.Particles/GeneralParticles/(Rotated/)?[^/]+\.png$", "Port", R + "/Textures/FX/T_{name}", "Kenney Particle Pack (CC0)"),
    (r"/Cyberpunk_Megapolis/Textures/[^/]+/[^/]+\.tga$", "Port", R + "/Textures/Vehicles/T_{name}", "Cyberpunk Megapolis"),

    # A.9 UI
    (r"/06\.UI/01\.Sprites/Keyboard & Mouse/Double/[^/]+\.png$", "Port", R + "/UI/Glyphs/KBM/T_{name}", "Kenney Input Prompts (CC0)"),
    (r"/06\.UI/01\.Sprites/Xbox Series/Double/[^/]+\.png$", "Port", R + "/UI/Glyphs/Xbox/T_{name}", "Kenney Input Prompts (CC0)"),
    (r"/06\.UI/01\.Sprites/PNG/(?P<c>\w+)/(?P<v>\w+)/[^/]+\.png$", "Port", R + "/UI/Panels/T_{c}{v}_{name}",
     "Kenney UI Pack (CC0); brush BR_ beside it carries the Unity 9-slice border"),
    (r"/06\.UI/02\.Fonts/[^/]+\.ttf$", "Port", R + "/UI/Fonts/FF_{name}",
     "Font Face; Font F_{name} beside it, Japanese fallback = engine DroidSansFallback (A.13.6)"),

    # A.7 audio (UE 5.9 decodes OGG/MP3 to PCM on import: A.13.4)
    (r"/01\.SFX/FiniteRunner/Music Finite runner\.mp3$", "Port", R + "/Audio/Music/S_RunnerMusic", "Streamed; decoded to PCM on import"),
    (r"/03\.Music/[^/]+/[^/]+\.mp3$", "Port", R + "/Audio/Music/S_{name}", "Streamed; decoded to PCM on import"),
    (r"/01\.SFX/FiniteRunner/computerNoise_002\.ogg$", "Port", R + "/Audio/UI/S_{name}", "Kenney Sci-Fi Sounds (CC0)"),
    (r"/01\.SFX/FiniteRunner/[^/]+\.ogg$", "Port", R + "/Audio/SFX/S_{name}", "Kenney Sci-Fi Sounds (CC0)"),
    (r"/07\.Audio/02\.UI/[^/]+\.ogg$", "Port", R + "/Audio/UI/S_{name}", "Kenney UI Audio (CC0)"),

    # A.8 video: the file goes to Content/Movies, the media source points at it
    (r"/07\.Videos/Cutscenes/mtch\.mp4$", "Port", R + "/Media/FMS_FailReaction", "File copied to Content/Movies/mtch.mp4"),

    # A.3 materials rebuilt in M1 as instances of the master materials
    (r"/02\.Materials/FiniteRunner/(NeonRoad_Mat|RoadHologram)\.mat$", "Rebuild", R + "/Materials/Track/MI_{name}", "Instance of M_Road"),
    (r"/02\.Materials/FiniteRunner/(NeonEdge_Mat|NeonTube_Mat|NeonWall_Mat)\.mat$", "Rebuild", R + "/Materials/Track/MI_{name}", "Instance of M_Neon"),
    (r"/02\.Materials/FiniteRunner/(NeonBarrier_Mat|LaserEnemy)\.mat$", "Rebuild", R + "/Materials/Laser/MI_{name}", "Instance of M_Laser"),
    (r"/02\.Materials/FiniteRunner/hologram\.mat$", "Rebuild", R + "/Materials/Orbs/MI_Hologram", "Instance of M_Hologram"),
    (r"/02\.Materials/FiniteRunner/(BoostPad_Mat|Ramp_Mat|RoadSurface_Mat)\.mat$", "Port", R + "/Materials/Track/MI_{name}", "Instance of M_Lit"),
    (r"/02\.Materials/FiniteRunner/Patrol\w+_Mat\.mat$", "Port", R + "/Materials/Vehicles/MI_{name}", "Instance of M_Lit"),
    (r"/OriginalTextures/Materials/roadTexture_v2\.mat$", "Port", R + "/Materials/Track/MI_RoadTexture_v2", "Instance of M_Lit"),
    (r"/01\.Models/Pickups/Mat_PowerUp_Brake\.mat$", "Not ported", None, "Brake pads"),
    (r"/01\.Models/Pickups/Mat_\w+\.mat$", "Rebuild", R + "/Materials/Orbs/MI_{name}", "Instance of M_Toon (was RealToon)"),
    (r"/99\.Test/Diego/3DModels/\w+\.mat$", "Rebuild", R + "/Materials/Ship/MI_{name}", "Instance of M_Toon (was RealToon)"),
    (r"/99\.Test/Diego/Materials/\w+\.mat$", "Port", R + "/Materials/Ship/MI_{name}", "Instance of M_Lit"),
    (r"/Cyberpunk_Megapolis/Materials/Environment/\w+\.mat$", "Rebuild", R + "/Materials/Vehicles/MI_{name}",
     "Instance of M_Toon (was RealToon; _A main, _E self-lit mask)"),
    (r"/02\.Materials/Mat_CharacterColor\.mat$", "Rebuild", R + "/Materials/Laser/MI_Mat_CharacterColor",
     "Instance of M_Toon; PF_LaserSystem uses it on LaserA / LaserB"),
    # A.3 rebuilt later, with their system
    (r"/02\.Materials/FiniteRunner/(CrtScreen|PsxLook|VhsTape|Mat_SkyboxPolice|hologram 1)\.mat$", "Not ported", None, "D14 / unreferenced (A.3)"),
    (r"/02\.Materials/FiniteRunner/(DashGhost_Mat|BarrelRollTrail_Mat|LaserBeam_Mat|LaserFloorSparks_Mat|ShipBoostSparks_Mat)\.mat$",
     "Rebuild", None, "FX material, rebuilt with its effect (M3 / M5 / M13)"),
    (r"/02\.Materials/FiniteRunner/(skyboxFinite|SkyboxHyperspace|SpeedLines)\.mat$", "Rebuild", None, "Sky / post material, rebuilt in M13"),
    (r"/02\.Materials/InfiniteCity/\w+\.mat$", "Rebuild", None, "Runner post material in a city folder (A.13.3), rebuilt in M13"),
]


def curate(rows):
    changed = 0
    for row in rows:
        path = row["UnityPath"]
        for pattern, action, template, notes in RULES:
            m = re.search(pattern, path)
            if not m:
                continue
            stem = os.path.splitext(os.path.basename(path))[0]
            ue = ""
            if template:
                ue = template.format(name=ue_name(stem), stem=stem, **{k: v for k, v in m.groupdict().items() if v})
            new = (action, ue, notes.format(name=ue_name(stem)) if "{name}" in notes else notes)
            if (row["Action"], row["UEPath"], row["Notes"]) != new:
                row["Action"], row["UEPath"], row["Notes"] = new
                changed += 1
            break
    return changed


def main():
    check = "--check" in sys.argv
    with open(MANIFEST, encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        fields = reader.fieldnames
        rows = list(reader)

    if not check:
        changed = curate(rows)
        with open(MANIFEST, "w", encoding="utf-8", newline="") as f:
            w = csv.DictWriter(f, fieldnames=fields, lineterminator="\n")
            w.writeheader()
            w.writerows(rows)
        print(f"curated {changed} rows")

    missing = [r["UnityPath"] for r in rows if r["Action"] == "Port" and not r["UEPath"]]
    for m in missing:
        print("Port row without UEPath:", m)
    counts = {}
    for r in rows:
        counts[r["Action"]] = counts.get(r["Action"], 0) + 1
    print("actions:", counts, "| Port rows without UEPath:", len(missing))
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
