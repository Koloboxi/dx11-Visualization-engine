#pragma once
#include "SemSession.h"
#include <memory>
#include <string>
#include <vector>

namespace SemSessionNS {

// Several SEM setups open at once. A "setup" is one source surface/contour plus
// everything the pipeline built from it: its SemSession (scene primitives, stage
// parameters, clip planes, dirty flags) and its own SEM pipeline context, which
// holds the library-side cache of every computed stage.
//
// Switching setups therefore preserves everything — it only changes which context
// is active and which session the SEM window draws. Nothing is reloaded, nothing
// is recomputed, and a background job started in one setup keeps running while
// another is edited (the SEM active context is per thread).
//
// The staged primitive (double-click in the tree) selects the active setup:
// staging a source that owns one switches to it, staging a source that has none
// adopts it as a new setup. Staging nothing — double-clicking empty 3D space —
// leaves the active setup alone, so the SEM window never loses its state.
class SemWorkspace {
public:
    int  Count() const { return (int)m_sessions.size(); }
    int  ActiveIndex() const { return m_active; }

    // The session the SEM window edits. With no setups open this is a permanently
    // empty session (HasSource() false), so the window shows its import prompt.
    SemSession& Active();
    SemSession* At(int i);
    const SemSession* At(int i) const;

    SemSession* SessionFor(const Primitive* p) const;
    int         IndexOf(const Primitive* p) const;
    // The setup already serializing into `dir`, or -1. Two setups must not share a
    // session folder: they would overwrite each other's stage files and manifest.
    int         IndexOfWorkDir(const std::string& dir) const;
    Primitive*  ActiveSourcePrim() const;

    // Clip-plane rectangles of every open setup share the scene, so a selected one
    // need not belong to the active setup. Returns the plane node for `rect` and,
    // through `owner`, the session that owns it.
    ClipPlaneNode* FindClipPlane(Primitive* rect, SemSession** owner = nullptr);

    // Label shown on a setup's tab: the source file stem, plus its session folder
    // when several setups share one source.
    std::string Label(int i) const;

    // Make setup `i` active and stage its source, so the tree selection follows.
    void SetActive(Scene& scene, int i);

    // Once per frame, before the SEM window draws: forget setups whose source was
    // deleted, follow the staged primitive, apply finished background results for
    // every setup, and leave the active setup's context current.
    void Sync(Scene& scene);

    // Start an empty setup and make it active; the caller then drives its
    // ImportSource. Returns the new session.
    SemSession& AddSetup();

    // Close setup `i`: release its SEM context and remove its source subtree (the
    // offsets, mesh, isosurface and clip planes parented under it) from the scene.
    void CloseSetup(Scene& scene, int i);

    // True while any setup has a background job in flight. Only one job may run at
    // a time: the library reports progress process-globally, so the stage Apply
    // buttons stay disabled everywhere until it finishes.
    bool AnyAsyncRunning() const;

private:
    // Adopt a source primitive that has no setup yet (its setup was closed, or the
    // scene came from elsewhere): bind a fresh session to it, reloading its saved
    // session folder when there is one. Returns the new index, or -1.
    int Adopt(Scene& scene, Primitive* prim);

    std::vector<std::unique_ptr<SemSession>> m_sessions;
    int m_active = -1;
};

SemWorkspace& Workspace();

}
