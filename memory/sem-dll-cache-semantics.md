---
name: sem-dll-cache-semantics
description: How the surface_extender_mesher (SEM) DLL internal cache invalidates relative to scene loads
metadata:
  type: project
---

The prebuilt `surface_extender_mesher.dll` (source not in repo; only `source/external/sem_exports.h`) keeps an internal cache of the loaded contour, subdivision, offsets, and mesh.

- Only `SEM_LoadCSV3D` clears that cache. `Scene::AddFromCSV3D` is a **pure visual load** (CSV3DLoader only, no SEM call) and never touches the SEM cache — so after a subdivide you can `AddFromCSV3D` the serialized result and still recompute offsets/mesh against the live cache.
- `SEM_SubdivideContour` invalidates previously computed offsets and mesh, so when recomputing the pipeline after a subdivide you must recompute **offsets first, then mesh** (the mesh depends on the offsets the subdivide just dropped).

**Why:** confirmed by the user 2026-06-05 while adding yellow-vertex-point auto-regen to the SEM Subdivide button ([SEMWindow.h](source/graphics/gui/SEMWindow.h)). The Subdivide button now recomputes offsets+mesh (instead of dropping them) so their yellow VertexPointsGroups regenerate from the subdivided contour.

**Meshing instability:** `SEM_BuildMeshEx` can throw an access violation deep inside the bundled Triangle library (e.g. `BuildBandMeshMaxArea`/MAX_AREA → `transfernodes`, observed 2026-06-05). The DLL source is not in this repo, so it can't be fixed here — `SEMWindow.h` wraps the call in `SafeBuildMeshEx` (`__try`/`__except`, returns -100) so the host app survives instead of crashing. C++ `try/catch` does NOT catch these (they're SEH, not C++ exceptions).
