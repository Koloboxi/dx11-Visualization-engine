# Memory index

- [SEM DLL cache semantics](sem-dll-cache-semantics.md) — the cache is per SEM context (thread-local active context) since 2026-09-20; within one context only SEM_LoadCSV3D clears it, AddFromCSV3D is a pure visual load, subdivide drops offsets/mesh so recompute offsets→mesh. Pipeline lives in SemSession, auto-branching 2D contour / 3D surface on import.
- [SEM staging selection](sem-staging-selection.md) — Primitive::staging is a second-level selection (not the gizmo); double-click tree row to stage, double-click 3D space to clear. It now picks which of the open SEM setups (SemWorkspace) is active instead of rebinding one session.
