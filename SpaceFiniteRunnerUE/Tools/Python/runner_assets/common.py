"""Shared paths and helpers for the M1 asset scripts (run inside Unreal)."""
import csv
import os

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
IMPORT_DIR = os.path.join(PROJECT_DIR, "Import")
MANIFEST = os.path.join(IMPORT_DIR, "AssetManifest.csv")
STAGING = os.path.join(PROJECT_DIR, "Saved", "RunnerImport", "Staging")
UNITY_PROJECT = os.path.normpath(os.path.join(PROJECT_DIR, "..", "..", "SpaceFiniteRunner"))

LOG_PREFIX = "[RunnerAssets]"


def log(msg):
    unreal.log(f"{LOG_PREFIX} {msg}")


def warn(msg):
    unreal.log_warning(f"{LOG_PREFIX} {msg}")


def error(msg):
    unreal.log_error(f"{LOG_PREFIX} {msg}")


def make(struct_type, **props):
    """Builds an Unreal struct from field values (struct constructors here take no keyword fields)."""
    value = struct_type()
    for k, v in props.items():
        value.set_editor_property(k, v)
    return value


def unity_file(unity_path):
    """'Assets/…' -> absolute path in the Unity repo."""
    return os.path.join(UNITY_PROJECT, unity_path.replace("/", os.sep))


def read_manifest():
    with open(MANIFEST, encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f))


def split_object_path(ue_path):
    """'/Game/Runner/Meshes/Ship/SM_Ship' -> ('/Game/Runner/Meshes/Ship', 'SM_Ship')."""
    folder, name = ue_path.rsplit("/", 1)
    return folder, name


def asset_exists(ue_path):
    return unreal.EditorAssetLibrary.does_asset_exist(ue_path)


def load(ue_path):
    if ue_path.startswith("/Engine/"):
        # Engine content is not always in the asset registry (headless runs): load it by object path.
        name = ue_path.rsplit("/", 1)[1]
        return unreal.load_object(None, f"{ue_path}.{name}")
    return unreal.EditorAssetLibrary.load_asset(ue_path)


def save(ue_path):
    unreal.EditorAssetLibrary.save_asset(ue_path, only_if_is_dirty=False)


def run_import_tasks(tasks):
    if tasks:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)


class Report:
    """Counts what each step did; printed at the end and written to Saved/RunnerImport/AssetReport.txt."""

    def __init__(self):
        self.lines = []
        self.counts = {}

    def add(self, kind, what, detail=""):
        self.counts[kind] = self.counts.get(kind, 0) + 1
        self.lines.append(f"{kind:9} {what}" + (f"  ({detail})" if detail else ""))
        if kind == "FAILED":
            error(f"{what} {detail}")

    def write(self):
        path = os.path.join(PROJECT_DIR, "Saved", "RunnerImport", "AssetReport.txt")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join(self.lines) + "\n")
        log("summary: " + ", ".join(f"{k}={v}" for k, v in sorted(self.counts.items())) + f" -> {path}")
        return self.counts.get("FAILED", 0)
