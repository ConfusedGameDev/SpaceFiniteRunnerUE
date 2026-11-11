# SpaceFiniteRunnerUE

The Finite Runner as a standalone Unreal Engine 5.9 game (Windows PC, keyboard + gamepad), ported
from the Unity project at `C:/Users/jorge/Documents/Jorge/Claude/SpaceFiniteRunner` (the source of
truth for behaviour; it gains only an exporter).

Plans, PRDs and design docs for this project live in the JorgesDatabase vault, **not in this repo**:
`C:/Users/jorge/Documents/Jorge/JorgesDatabase/JorgesDatabase/Projects/SpaceFiniteRunnerUE`

Current plan (auto-loaded):
@~/Documents/Jorge/JorgesDatabase/JorgesDatabase/Projects/SpaceFiniteRunnerUE/Plans/Plan.md

The scope, the design decisions (D1–D16) and every requirement ID (R1.1 …) are in
`PRDs/UnrealPortPRD.md` in that folder. Read the section for the system you are touching.

## Rules
- Write new plans to `Plans/` and PRDs to `PRDs/` in the vault folder above, starting from `C:/Users/jorge/Documents/Jorge/JorgesDatabase/JorgesDatabase/_Templates/Plan.md`. Never create plan or PRD files in this repo.
- Code, assets and project-specific skills (`.claude/skills/`) live in this repo.
- When a milestone is done: tick its checkbox, update `status`/`updated` in the frontmatter and add a dated Log line in the vault plan.
- The vault and this repo are separate git repos; commit each on its own.
- One branch per milestone: `port/m<N>-<topic>`.

## Layout
- `SpaceFiniteRunnerUE/` is the Unreal project (`SpaceFiniteRunnerUE.uproject`).
- `SpaceFiniteRunnerUE/Source/Runner*/` holds the eight modules. References go one way only:

| Module | Holds | May reference |
|---|---|---|
| `RunnerContracts` | Interfaces only | — |
| `RunnerSave` | Save profile, stats, rank table | — |
| `RunnerUI` | CommonUI menus, localization, bindings, loading screen, dialogue, video plate | Save |
| `RunnerFX` | Post-process subsystem, glitch, fog, speed lines, sky, Niagara drivers, haptics | UI |
| `RunnerCameras` | Chase rig, view modes, shakes | UI, FX |
| `RunnerShip` | Chaos hover ship, guide, recovery, pickup sweep | UI, Cameras, Contracts |
| `RunnerCore` | Track, game mode and rules, orbs, lasers, patrol, traffic, race, time trial, HUD, audio (primary game module) | all above |
| `RunnerEditor` | Track editor, importers, catalog sync, sandbox tools (editor only) | Core |

- `SpaceFiniteRunnerUE/Import/` is the Unity exporter's output, checked in: `Data/<UnityType>/<Asset>.json`, `Schema/<UnityType>.json` (fields with tooltip, range, group, unit), `AssetManifest.csv`, and the hand-written `ImportMap.json` (where each asset lands).
- Content goes under `/Game/Runner/` (`Data`, `Maps`, `Meshes`, `Materials`, `Textures`, `FX`, `Audio`, `UI`).

## Build and import
- Build: `"C:/Program Files/Epic Games/UE_5.9/Engine/Build/BatchFiles/Build.bat" SpaceFiniteRunnerUEEditor Win64 Development -Project="<repo>/SpaceFiniteRunnerUE/SpaceFiniteRunnerUE.uproject" -WaitMutex`. The editor must be closed (Live Coding blocks the build, and new modules or UPROPERTY changes need a restart anyway).
- Export from Unity: `Tools ▸ Unreal Export ▸ Export All`, or batch `-executeMethod ConfusedGameDev.FiniteRunner.EditorTools.UnrealExport.UnrealExporter.ExportAll`.
- Import into Unreal: `UnrealEditor-Cmd.exe <uproject> -run=RunnerImport [-Types=A,B] [-Overwrite] [-DryRun]`, or `Runner.ImportUnityData` in the editor console. What did not carry over is listed in `Saved/RunnerImport/Report.txt`. `-run=RunnerImport -Dump` reloads the DataAssets and writes them to `Saved/RunnerImport/Dump/*.json`, for comparing against Unity.
- The import **skips assets that already exist** unless `-Overwrite` is passed: once an asset is in the project it is tuned here.

## Porting a Unity ScriptableObject
1. Read `Import/Schema/<UnityType>.json` and the Unity script it names.
2. Write a `UPrimaryDataAsset` subclass in the module of its system with `UCLASS(meta = (UnityType = "<UnityType>"))`.
3. Name each property after the Unity field in PascalCase (bools get the `b` prefix); the importer matches names case-insensitively. Use `meta = (UnityName = "…")` only when the name must differ.
4. Carry the tooltip over as the property comment, `[PropertyRange]` as `ClampMin`/`ClampMax`, `[MinMaxSlider]` bands as `FFloatInterval` with accessors, `[ToggleGroup]` as an `EditCondition` on the group's bool.
5. Add the asset's destination to `Import/ImportMap.json`, then run the import for that type.

## Invariants (PRD §8)
1. Gameplay never writes a DataAsset; runtime copies carry changed values.
2. Distance from the track start is the authoritative coordinate.
3. **Tuning data is in metres, m/s and degrees**, as in Unity. Convert to Unreal units (cm) only where a value is handed to the engine, and UI converts speed with × 3.6.
4. Nothing fast is detected by a moving trigger: pickups, traffic, rivals and patrol contact use sweep queries along the step path; laser gates stay analytic.
5. Only `UPostProcessSubsystem` writes the global post-process volume.
6. Systems stay independent: each boots and idles alone in its sandbox map; cross-system access goes through `RunnerContracts` interfaces, the game mode's binding, registries or delegates, never by searching the world for another system's class.
7. Every player-facing string is a String Table entry in EN / ES / JA / FR; plates auto-fit.
8. Serialized enums are append-only and keep the Unity values.
9. Every map trip goes through the loading screen.
10. Gameplay reads input through Enhanced Input actions only; menus may poll devices.
11. C++ follows Unreal naming (PascalCase parameters, `b` bool prefix, `F`/`U`/`A`/`E` type prefixes). The Unity project's lowerCamelCase parameter rule does not apply here.

## Axes
Unity is X right, Y up, Z forward; Unreal is X forward, Y right, Z up. Imported positions and
rotations are moved with `(x, y, z) → (z, x, y)` (properties marked `meta = (UnitySpace = …)`),
which keeps handedness, and are left in metres.
