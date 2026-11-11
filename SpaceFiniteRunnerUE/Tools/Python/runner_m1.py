"""M1 asset pipeline entry. Run headless (editor closed or not, project not open in a second editor):

    UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<repo>/SpaceFiniteRunnerUE/Tools/Python/runner_m1.py [options]"

Options:
    --steps import,materials,gallery   which steps to run (default: all three, in that order)
    --overwrite                        re-import / rebuild instances that already exist
    --only .fbx,/UI/Fonts/             restrict the import step to these extensions or UE path fragments

Writes Saved/RunnerImport/AssetReport.txt. Exit code 1 if anything FAILED.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import unreal  # noqa: E402

import importlib  # noqa: E402

from runner_assets import common, gallery, import_raw, materials  # noqa: E402

# In a running editor `py` keeps modules from the last run: reload so script edits take effect.
for _m in (common, materials, import_raw, gallery):
    importlib.reload(_m)


def _arg(name, default=None):
    if name in sys.argv:
        i = sys.argv.index(name)
        return sys.argv[i + 1] if i + 1 < len(sys.argv) else default
    return default


def main():
    steps = (_arg("--steps", "import,materials,gallery") or "").split(",")
    overwrite = "--overwrite" in sys.argv
    only = [o for o in (_arg("--only", "") or "").split(",") if o] or None
    report = common.Report()
    if "import" in steps:
        import_raw.run(report, overwrite, only)
    if "materials" in steps:
        materials.run(report, overwrite)
    if "gallery" in steps:
        gallery.run(report, overwrite)
    failed = report.write()
    if failed:
        unreal.log_error(f"[RunnerAssets] {failed} FAILED, see the report")
    return 1 if failed else 0


if __name__ == "__main__":
    code = main()
    if code:
        raise SystemExit(code)
