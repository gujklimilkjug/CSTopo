# CSTopo

CSTopo is a Windows desktop first-person topographic survey app scaffold. The target product lets a survey/CAD user import large LAS/LAZ point clouds, walk or fly through them, collect coded topo shots, draw linework, and export DXF plus CSV deliverables.

This repository currently contains:

- An Unreal Engine project scaffold with survey/import/export C++ modules.
- A runnable Python core that mirrors the project data model, fitted-surface shot behavior, save/load, CSV export, and DXF export.
- Unit tests for the non-rendering survey workflow.

## Current Architecture

- Unreal Engine is the planned desktop app shell and FPS interaction layer.
- PDAL is the planned import backend for LAS/LAZ metadata and COPC cache creation.
- COPC is the preferred large-cloud cache format because it supports spatially indexed LAZ access.
- `.cstopo` projects now persist a project-local `.cachemanifest.json` sidecar that tracks per-cloud cache path, cache/runtime status, timestamps, and the preferred runtime display path.
- `.cstopo` projects now also carry runtime streaming settings so CSTopo can build camera-local COPC window subsets instead of always displaying the whole cache.
- Survey coordinates are stored and exported in source northing/easting/elevation values and source linear units. For example, `KT-192.las` declares `US survey foot`, so collected/exported NEZ values remain US survey feet.
- Unreal rendering uses a local centered display of the point cloud and scales source linear units into Unreal centimeters using the LAS unit metadata. Survey storage/export stays in source units; the viewport scale is only for stable rendering and normal-feeling navigation.
- Runtime pawn dimensions and movement now use normal Unreal-scale centimeters, while shot messages and stored survey data still report source linear units.
- The point-cloud render is not the survey surface. CSTopo now builds a project-managed derived surface cache from ground-classified points and treats that surface as the primary walk/measurement substrate once ready.
- The first derived surface implementation is a tiled triangulated ground-grid mesh cache: PDAL extracts existing class-2 ground points when available, or runs ground classification before extracting ground points, then CSTopo writes per-tile mesh JSON plus a `surface_manifest.json`.
- Walk mode requires a ready derived surface. Before the surface is ready, fly mode remains available for inspection and the app reports that walk mode is waiting on the surface build.
- The runtime now starts a first real streaming path for large cache-backed COPC clouds: when the active cloud is too large for comfortable direct-open viewing, CSTopo asks PDAL for a bounded window subset around the camera, samples it down toward a target point budget, and swaps the viewport onto that subset once it is ready. Direct-open-eligible clouds continue to open as a whole cloud.

## Implemented MVP Surface

- `.cstopo` project document with point-cloud source records, code palette, shots, and figures.
- Fitted-surface topo shots using a least-squares plane from nearby point samples.
- Point numbering, active code tracking, code-based figure creation, figure split/close operations.
- CSV export with `PointNumber, Northing, Easting, Elevation, Code, FigureId, Notes`.
- DXF export with code/layer tables, 3D point entities, and 3D polyline entities.
- Unreal survey pawn with walk/fly modes, mouse look, movement bindings, and a Blueprint hook for shot collection.
- Unreal survey subsystem for new/load/save project, fitted-shot collection, active code changes, and CAD export.

## Default Controls

- CSTopo starts at a blocking home screen. Survey movement, reticle hover, and shot collection are disabled until a project is opened or a point cloud is imported into a saved `.cstopo` project and its derived surface is ready.
- `WASD`: horizontal movement
- `Mouse`: look
- `F`: toggle walk/fly mode
- `Left Shift`: sprint / fast-move
- `Q/E`: descend/ascend in fly mode
- `Left Mouse`: request shot collection
- `Tab`: reserved for future modal survey UI; active survey navigation remains non-focusable
- `1-4`: switch active code using the first four code-palette entries (`EOP`, `CL`, `FL`, `GB` by default)
- When a point cloud becomes the active loaded cloud, the pawn recenters to the cloud's middle in source space so you start inside the dataset instead of stranded elsewhere in the level.

## Movement System Guardrails

The survey movement system has a few non-obvious pieces that are intentional. Please preserve these unless you are deliberately rebuilding navigation and have manually validated all controls in Play In Editor.

- `ACSTopoPlayerController` is the single owner of survey input. Keep `WASD`, `Q/E`, `F`, mouse wheel, mouse look, `Tab`, `Escape`, `1-4`, and shot-click routing in the controller instead of moving decision-making back into the pawn.
- Survey controls use Unreal Enhanced Input. Do not reintroduce `InputKey` routing, Slate keyboard preprocessors, Windows `GetAsyncKeyState`, or legacy per-frame key polling for movement.
- `ACSTopoSurveyPawn` remains an `ACharacter`, and survey locomotion should route through `AddMovementInput`, `AddControllerYawInput`, `AddControllerPitchInput`, and `UCharacterMovementComponent` modes.
- Active survey UI must stay non-focusable. Keep startup/open/import UI before survey begins, but do not add editable boxes, buttons, or focusable Slate panels to active navigation.
- Fly mode uses `MOVE_Flying`, camera-relative `WASD`, and `Q/E` vertical input. Walk mode uses `MOVE_Walking` and requires a ready derived surface.
- Walk collision is intentionally narrow: derived-surface procedural tiles use `ECC_GameTraceChannel1`, and the pawn capsule blocks only that channel in walk mode. Fly mode disables capsule collision so hidden/editor/level collision cannot trap the surveyor above or below an invisible plane.
- Do not reintroduce old always-on idle Z snapping. Entering walk may land the character on the surface; normal walk should be owned by CharacterMovement and floor collision.
- Auto precision is automatic. It should reduce look/move scale only after the reticle is over a measurable target and movement input has been idle briefly; movement input, fly-band changes, or mode toggles should exit precision immediately.

Manual movement validation checklist:

- Open a ready-surface project and confirm the minimal overlay reports active survey status.
- Move the mouse and confirm camera yaw/pitch are correct and not inverted.
- Hold `WASD` in walk mode and confirm visible surface walking.
- Press `F` and confirm the HUD changes between `Mode: Walk` and `Mode: Fly`.
- In fly mode, confirm `WASD` flies camera-relative and `Q/E` descend/ascend.
- Use the mouse wheel in fly mode and confirm the fly band changes.
- Stop over a measurable point and confirm `Precise` appears, then move and confirm it clears.
- Left-click in both walk and fly modes and confirm shot collection still uses the reticle.

## Point-Cloud Import UI

When Play starts, CSTopo shows a full-screen home menu instead of dropping you into the world. Choose `Open Project...` for an existing `.cstopo`, or `Import Point Cloud...` to select a `.las`, `.laz`, or `.copc.laz` and immediately create a saved `.cstopo` project for it.

The point-cloud import workflow creates the project first, reads source metadata and units, imports the cloud into the project-local cache path, starts/reuses the derived surface build, and waits at the home/progress screen until the surface is ready. Once ready, CSTopo hides the home screen, shows the reticle/runtime menu, and places the pawn near the center of the active surface.

After the app enters survey mode, CSTopo shows only the reticle and a small non-focusable survey status overlay. Active survey navigation should not depend on any focusable HUD widget.

The point-cloud manager is startup/import tooling and should not be part of active survey navigation.

- Enter a LAS/LAZ/COPC path such as `KT-192.las` or an absolute file path, then click `Add` to import and display it.
- `Active` marks which cloud future picking should use.
- `Show`/`Hide` changes cloud actor visibility.
- `Load`/`Unload` spawns or removes the cloud display actor.
- `COPC` runs a PDAL conversion for that cloud's configured cache path and refreshes runtime status when the build completes.
- `Build Surface` / `Rebuild Surface` starts the tiled derived-surface cache build for the cloud.
- `Show Surface` / `Hide Surface` controls whether the derived surface is shown and used as the primary survey substrate.
- `Show Overlay` / `Hide Overlay` controls whether the point cloud is visible as context over the surface.
- `View Mode` cycles practical point-cloud display modes: surface shaded, RGB/data, elevation, classification, and intensity-biased display.
- True EDL/post-process rendering is still deferred; the current milestone adds practical display modes and keeps the rendering path ready for a later custom shader.
- `Remove` removes the cloud from the active project.
- Each cloud row shows source cache path, chosen runtime path, and current cache status so you can see whether CSTopo is using direct LAS/LAZ or a COPC cache.
- Each cloud row also reports runtime-window status so you can tell when CSTopo is still using the project runtime path versus a camera-local COPC subset.
- Once the derived surface is ready, left-clicking collects a surface-authoritative topo shot with the active code, draws a persistent marker, and connects shots in the same figure with a 3D line.
- Point picking uses the center reticle, not the mouse cursor position. Aim the reticle at the cloud, then left-click.
- The hover message at the reticle tells you whether the current location would measure from the derived surface, a raw point-cloud point, or is currently unmeasurable.
- The reticle now prefers the derived surface once it is ready. The cloud remains an optional visual overlay and fallback before the surface build completes.
- Imported point clouds are rendered with a 1-pixel point sprite by default so you can validate the shot target more precisely.
- Picking expands both the reticle trace radius and the sample neighborhood when the first search is too sparse. If a stable local plane cannot be fit, the app records a `NearestPoint` shot rather than dropping the click.
- If the reticle is still resolving to the same previous shot location, CSTopo now warns instead of silently adding a duplicate point that would collapse the linework.
- The manager shows the five most recent collected shots with point number, code, northing, easting, and elevation.

Current rendering still uses Unreal's LiDAR Point Cloud plugin for displayable LAS/LAZ files, but the project/runtime path now treats cache-backed clouds as first-class assets. CSTopo persists cache state into the `.cstopo` plus `.cachemanifest.json` sidecar, prefers ready COPC caches at runtime, and now begins to replace whole-cache display with a background-built camera window for active COPC clouds. Direct-open LAS/LAZ remains the fallback path when a cache is not ready or runtime windowing is unavailable.

## Local Validation

The Python core can be tested without Unreal or PDAL:

```powershell
python -m unittest discover -s tests
```

Create a sample project and exports:

```powershell
python scripts/cstopo_cli.py sample --out demo
```

That command writes a `.cstopo` project, a point CSV, and a DXF containing coded 3D points and polylines.

## LAS/COPC Workflow

CSTopo looks for PDAL in this order:

- `CSTOPO_PDAL_PATH`
- `pdal.exe` on `PATH`
- `C:\Program Files\QGIS 3.40.12\bin\pdal.exe`

Inspect a source LAS/LAZ/COPC file:

```powershell
python scripts/cstopo_cli.py pdal-info KT-192.las
```

Create a COPC cache for large-cloud streaming:

```powershell
python scripts/cstopo_cli.py translate-copc KT-192.las --cache-dir cache
```

Create a CSTopo project that references the source metadata, cache status, and project-local cache manifest:

```powershell
python scripts/cstopo_cli.py import-las KT-192.las --out imports\KT-192.cstopo --cache-dir cache
```

Rebuild the cache manifest for an existing project:

```powershell
python scripts/cstopo_cli.py build-cache-manifest imports\KT-192.cstopo
```

Extract a camera-local runtime window from the active project cloud:

```powershell
python scripts/cstopo_cli.py extract-runtime-window imports\KT-192.cstopo 109900 70550 --cache-dir cache
```

Build a tiled derived surface cache from a source cloud:

```powershell
python scripts/cstopo_cli.py build-surface --source KT-192.las --source-id KT192SURF --cache-dir cache --force
```

For `KT-192.las`, the builder detects existing class-2 ground points and produces a `cache\surfaces\<source-id>\surface_manifest.json` plus per-tile triangulated mesh files while preserving the US survey foot unit metadata.

## Unreal Build Notes

Open `CSTopo.uproject` from an Unreal Engine 5.7 install on Windows. The project enables the LiDAR Point Cloud plugin for early visualization/prototyping. The production streaming path should replace any whole-asset loading for 500M+ point clouds.

PDAL is not vendored here. This workspace currently uses the QGIS-bundled PDAL 2.9.0 executable when `pdal.exe` is not on PATH.

The Unreal C++ module has been compiled against UE 5.7. The Python core and export/import workflow are covered by unit tests.
