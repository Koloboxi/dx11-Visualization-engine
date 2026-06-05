---
name: sem-staging-selection
description: "Staging = second-level selection (distinct from gizmo selection) that the SEM panel binds to"
metadata:
  node_type: memory
  type: project
---

`Primitive::staging` is a *second-level selection*, separate from `Primitive::selected` (which drives the transform gizmo). It exists so a contour can be the SEM target without being gizmo-selected (the gizmo got in the way). Introduced 2026-06-05 when the SEM panel was reworked.

- Set: **double-click a primitive row in the tree** → `Scene::SetStaged(prim)` (single-target; clears any previous staged prim and also clears gizmo selection). Drawn with a green outline in the tree row ([PrimitivesWindow.h](source/graphics/gui/PrimitivesWindow.h)).
- Clear: **double-click empty 3D space** → `Scene::ClearStaged()` (hooked in `gui.cpp` via `IsMouseDoubleClicked && !io.WantCaptureMouse`). Double-clicking the staged row again also clears it.
- `Scene::stagedPrimitive` must never dangle, because `SemSession::Bind` dereferences it (`prim->semSourcePath`); a freed pointer yields a corrupted std::string → `length_error "string too long"`. It is nulled in `DestroyNodeRecursive` AND in `ClearScene` (the latter deletes primitives directly, bypassing `DestroyNodeRecursive`, so it needs its own reset — that was the Clear-button crash, fixed 2026-06-05).
- The SEM panel each frame calls `SemSession::Validate` then `Bind(scene.stagedPrimitive)`. A primitive only acts as a SEM source if it has a non-empty `semSourcePath` (set on CSV3D import); `Bind` reloads that path into the DLL cache. Importing a CSV3D auto-stages it.

See [[sem-dll-cache-semantics]] for the DLL cache/ordering rules the session encodes.
