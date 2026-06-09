---
name: sem-dll-cache-semantics
description: How the surface_extender_mesher (SEM) DLL internal cache invalidates relative to scene loads
metadata:
  type: project
---

The prebuilt `surface_extender_mesher.dll` (source not in repo; only `source/external/sem_exports.h`) keeps an internal cache of the loaded contour, subdivision, offsets, and mesh.

- Only `SEM_LoadCSV3D` clears that cache. `Scene::AddFromCSV3D` is a **pure visual load** (CSV3DLoader only, no SEM call) and never touches the SEM cache — so after a subdivide you can `AddFromCSV3D` the serialized result and still recompute offsets/mesh against the live cache.
- `SEM_SubdivideContour` invalidates previously computed offsets and mesh, so when recomputing the pipeline after a subdivide you must recompute **offsets first, then mesh** (the mesh depends on the offsets the subdivide just dropped).

**Where this lives now (refactored 2026-06-05):** the whole pipeline moved out of the GUI into `SemSession` ([SemSession.h](source/graphics/gui/SemSession.h)). The cache-ordering rules are encoded once in `SemSession::RecomputeFrom(stage)` — recomputing a stage re-runs it and everything downstream (subdivide→offsets→mesh), so offsets-before-mesh is automatic. `SEMWindow.h` is now a thin modifier-stack view over the session. See [[sem-staging-selection]] for how the panel binds to a contour.

**2D vs 3D pipeline (added 2026-06-09):** `SemSession` now drives both the contour pipeline (`SEM_LoadCSV3D` + `SEM_*`) and the triangle-surface pipeline (`SEM_LoadSurface3D` + `SEM_*3D`, see `sem_exports.h`). Dimension is auto-detected on `Bind` by `DetectSemDim(path)`: a source is **3D** iff it has triangles AND its node cloud is non-coplanar (thinnest bounding-box extent > 1e-3 of the largest); a pure contour or planar patch is 2D. `SemSession::dim` (2/3) then branches every stage (`ApplySubdivide`/`Offsets`/`Mesh`/`Thermal`/`ApplyIsoline`). 3D meshing uses `SEM_MeshParams3D{maxVolume, radiusEdge}` (TetGen) instead of the Steiner method; isoline→isosurface. `SEMWindow::Draw` reads `S.Dim()` to swap the mesh controls and hide the (2D-only) revolution section. Sample 3D surfaces live in `Data/05_forgings_3d/*.csv3d`. The cache-ordering and SEH-wrapping rules below apply to both pipelines (`SafeBuildMesh3D`/`SafeSolveThermal3D`/`SafeExtractIsosurface3D` mirror the 2D wrappers).

**Meshing instability:** `SEM_BuildMeshEx` can throw an access violation deep inside the bundled Triangle library (e.g. `BuildBandMeshMaxArea`/MAX_AREA → `transfernodes`, observed 2026-06-05). The DLL source is not in this repo, so it can't be fixed here — `SafeBuildMeshEx` in `SemSession.h` wraps the call (`__try`/`__except`, returns -100) so the host app survives instead of crashing. C++ `try/catch` does NOT catch these (they're SEH, not C++ exceptions).
