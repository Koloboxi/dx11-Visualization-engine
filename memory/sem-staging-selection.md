---
name: sem-staging-selection
description: "Staging = second-level selection (distinct from gizmo selection); it now selects which SEM setup is active"
metadata:
  node_type: memory
  type: project
---

`Primitive::staging` is a *second-level selection*, separate from `Primitive::selected` (which drives the transform gizmo). It exists so a contour can be the SEM target without being gizmo-selected (the gizmo got in the way). Introduced 2026-06-05 when the SEM panel was reworked.

- Set: **double-click a primitive row in the tree** → `Scene::SetStaged(prim)` (single-target; clears any previous staged prim and also clears gizmo selection). Drawn with a green outline in the tree row ([PrimitivesWindow.h](source/graphics/gui/PrimitivesWindow.h)); a source that owns a SEM setup but is not the staged one gets an amber outline instead (`Primitive::semContext != 0`).
- Clear: **double-click empty 3D space** → `Scene::ClearStaged()` (hooked in `gui.cpp` via `IsMouseDoubleClicked && !io.WantCaptureMouse`). Double-clicking the staged row again also clears it.
- `Scene::stagedPrimitive` must never dangle, because the workspace sync dereferences it (`prim->semSourcePath`); a freed pointer yields a corrupted std::string → `length_error "string too long"`. It is nulled in `DestroyNodeRecursive` AND in `ClearScene` (the latter deletes primitives directly, bypassing `DestroyNodeRecursive`, so it needs its own reset — that was the Clear-button crash, fixed 2026-06-05).

**Staging selects a SETUP now (changed 2026-09-20).** The SEM panel no longer binds one session to `scene.stagedPrimitive` every frame. `SemWorkspace` ([SemWorkspace.h](source/SEM/SemWorkspace.h)) keeps one `SemSession` per open setup, each with its own SEM pipeline context, and `SEMWindow::Draw` calls `Workspace().Sync(scene)` instead of `Validate` + `Bind` + `PollAsync`:

- staging a source that owns a setup → that setup becomes active. A pure switch: nothing reloads, nothing recomputes.
- staging a source with no setup → it is adopted as a new setup (reloaded through `SEM_LoadSession3D` when its session folder has a `session3d.txt`, else bound fresh).
- staging **nothing** → the active setup is left alone. Clearing the staging used to unbind the session, and re-staging then re-ran `SEM_LoadSurface3D`, which wiped the clip planes and every computed stage. That is the bug this replaced.

A primitive only acts as a SEM source if it has a non-empty `semSourcePath` (set on CSV3D import); importing a CSV3D opens it as a setup and stages it.

See [[sem-dll-cache-semantics]] for the DLL cache/ordering rules the sessions encode.
