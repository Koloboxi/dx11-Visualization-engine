#pragma once
#include "../imgui/imgui.h"
#include "../scene/scene.h"
#include "../../SEM/SemSession.h"
#include "../../SEM/SemWorkspace.h"
#include <cstdio>
#include <string>
#include <filesystem>
#include <commdlg.h>

namespace SEMWindow {

// The setup this window edits. Several setups can be open at once, each with its
// own SEM pipeline context; see SemWorkspace.
inline SemSessionNS::SemSession& Session() {
    return SemSessionNS::Workspace().Active();
}

// initialDir: folder the dialog opens in. Empty => the exe-relative Data folder
// (used for the first source import); stage re-imports pass the SEM working dir.
inline std::string BrowseCsv3dFile(const std::string& initialDir = {}) {
    std::string startDir = initialDir;
    if (startDir.empty()) {
        char exe[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        std::string s(exe);
        auto pos = s.find_last_of("\\/");
        startDir = (pos != std::string::npos ? s.substr(0, pos) : s) + "\\Data";
    }

    char fileBuf[MAX_PATH] = {};
    OPENFILENAMEA ofn   = {};
    ofn.lStructSize     = sizeof(ofn);
    ofn.lpstrFilter     = "CSV3D contour (*.csv3d)\0*.csv3d\0All files (*.*)\0*.*\0";
    ofn.lpstrFile       = fileBuf;
    ofn.nMaxFile        = MAX_PATH;
    ofn.lpstrInitialDir = startDir.c_str();
    ofn.lpstrTitle      = "Import CSV3D contour";
    ofn.Flags           = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn)) return std::string(fileBuf);
    return {};
}

// State for the "load existing session vs. new session" modal raised when the
// user imports a source that already has serialized sessions under %TEMP%/sem/.
struct ImportChoice {
    bool                     open     = false;  // request popup open this frame
    std::string              path;              // source .csv3d being imported
    std::vector<std::string> sessions;          // existing session folders (full paths)
    int                      sel      = 0;      // 0..n-1 = session, n = "[New session]"
};
inline ImportChoice& PendingImport() {
    static ImportChoice ic;
    return ic;
}

// Workspace-tab label for a source: the file name without directory or extension.
inline std::string WorkspaceLabelFor(const std::string& path) {
    return std::filesystem::path(path).stem().string();
}

// Add a source to the scene as a NEW setup: its own SemSession and its own SEM
// pipeline context, alongside whatever setups are already open (nothing is wiped —
// the setups coexist and the SEM window switches between them). The workspace tab
// is named after the first import only.
inline Primitive* AddImport(Scene& scene, const std::string& path,
                            const std::string& workDir = std::string(),
                            bool reload = false) {
    if (path.empty()) return nullptr;
    SemSessionNS::SemWorkspace& W = SemSessionNS::Workspace();
    // That session folder may already be open as a setup; switch to it rather than
    // opening a second setup that would serialize over the first one's stages.
    const int existing = W.IndexOfWorkDir(workDir);
    if (existing >= 0) { W.SetActive(scene, existing); return W.Active().SourcePrim(); }
    if (W.Count() == 0) scene.workspaceLabel = WorkspaceLabelFor(path);
    SemSessionNS::SemSession& S = W.AddSetup();
    return S.ImportSource(scene, path, workDir, reload);
}

// Begin importing a source: if it already has sessions, defer to the choice
// modal; otherwise import straight into a fresh session.
inline void BeginSourceImport(Scene& scene, const std::string& path) {
    if (path.empty()) return;
    auto sessions = SemSessionNS::SemSession::ListSessions(path);
    if (sessions.empty()) { AddImport(scene, path); return; }
    ImportChoice& ic = PendingImport();
    ic.path     = path;
    ic.sessions = std::move(sessions);
    ic.sel      = (int)ic.sessions.size();   // default to "[New session]"
    ic.open     = true;
}

// Render the session-choice modal. Call once per frame from the SEM window.
inline void DrawImportSessionModal(Scene& scene) {
    ImportChoice& ic = PendingImport();
    if (ic.open) { ImGui::OpenPopup("Import session##sem"); ic.open = false; }
    if (!ImGui::BeginPopupModal("Import session##sem", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextWrapped("This source already has saved pipeline states.\n"
                       "Load an existing session or start a new one:");
    ImGui::Spacing();

    auto base = [](const std::string& p) {
        auto s = p.find_last_of("\\/");
        return s != std::string::npos ? p.substr(s + 1) : p;
    };
    const int newIdx = (int)ic.sessions.size();
    std::string preview = (ic.sel >= 0 && ic.sel < newIdx)
                              ? base(ic.sessions[ic.sel]) : std::string("[New session]");
    if (ImGui::BeginCombo("State", preview.c_str())) {
        for (int i = 0; i < newIdx; ++i)
            if (ImGui::Selectable(base(ic.sessions[i]).c_str(), ic.sel == i)) ic.sel = i;
        if (ImGui::Selectable("[New session]", ic.sel == newIdx)) ic.sel = newIdx;
        ImGui::EndCombo();
    }

    ImGui::Spacing();
    if (ImGui::Button("Load", {120, 0})) {
        if (ic.sel >= 0 && ic.sel < newIdx) {
            SemSessionNS::SemWorkspace& W = SemSessionNS::Workspace();
            // Already open as a setup? Then AddImport only switches to it and its
            // stages are live — reloading them would throw the live state away.
            const bool fresh = W.IndexOfWorkDir(ic.sessions[ic.sel]) < 0;
            AddImport(scene, ic.path, ic.sessions[ic.sel], /*reload=*/true);
            if (fresh) W.Active().LoadSessionStages(scene);
        } else {
            AddImport(scene, ic.path);  // fresh session
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", {120, 0})) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// One tab per open setup, plus its close button. Clicking a tab makes that setup
// active — a pure switch: each setup keeps its own SEM pipeline context, so no
// stage is dropped and nothing is recomputed. The tab selection also follows the
// tree (staging a source switches setup), which is what the SetSelected flag below
// pushes back into the tab bar.
inline void DrawSetupTabs(Scene& scene) {
    using namespace SemSessionNS;
    SemWorkspace& W = Workspace();
    if (W.Count() == 0) return;

    static int s_shownActive = -1;
    const int  active  = W.ActiveIndex();
    const bool refocus = (active != s_shownActive);
    s_shownActive = active;

    int closeIdx = -1;
    if (ImGui::BeginTabBar("##semsetups", ImGuiTabBarFlags_FittingPolicyScroll |
                                          ImGuiTabBarFlags_TabListPopupButton)) {
        for (int i = 0; i < W.Count(); ++i) {
            bool open = true;
            const ImGuiTabItemFlags flags =
                (refocus && i == active) ? ImGuiTabItemFlags_SetSelected : 0;
            const std::string label = W.Label(i) + "###setup" + std::to_string(i);
            if (ImGui::BeginTabItem(label.c_str(), &open, flags)) {
                // Only a real tab click switches the setup. On a refocus frame the
                // selection we just forced would otherwise be written straight
                // back as a "switch", and staging would fight the tab bar one
                // frame on, one frame off.
                if (!refocus && i != W.ActiveIndex()) W.SetActive(scene, i);
                ImGui::EndTabItem();
            }
            ImGui::SetItemTooltip("Setup %d of %d. Switching keeps every computed stage of\n"
                                  "both setups (each has its own SEM cache); the close button\n"
                                  "drops this setup and removes its geometry from the scene.",
                                  i + 1, W.Count());
            if (!open) closeIdx = i;
        }
        ImGui::EndTabBar();
    }
    if (closeIdx >= 0) { W.CloseSetup(scene, closeIdx); s_shownActive = -1; }
}

inline const char* MeshParamLabel(int /*m*/) {
    return "Grid spacing";
}

inline const char* MeshParamHelp(int /*m*/) {
    return "Regular grid of interior Steiner points + constrained Delaunay.\n"
           "Negative => source average edge length.";
}

inline const char* TetParamLabel(int m) {
    return (m == SEM_TET_BAND) ? "Steiner grid cell" : "Use source SDF";
}

inline const char* TetParamHelp(int m) {
    switch (m) {
        case SEM_TET_BAND:    return "Delaunay of source + outermost-offset + an interior Steiner\n"
                                     "grid, carved to the band by the source signed distance.\n"
                                     "Parameter = Steiner grid cell size. Negative => avg edge length.";
        case SEM_TET_LAYERED: return "Delaunay of a layered point cloud across all offset shells,\n"
                                     "carved by layer adjacency. Off keeps the build fully SDF-free;\n"
                                     "on carves and colours by the source signed distance instead.";
        default:              return "";
    }
}

inline void DrawReadout(SemSessionNS::SemSession& S) {
    using namespace SemSessionNS;
    const Stats* st  = nullptr;
    const char*  tag = "Source";
    if      (S.MeshPrim()    && S.MeshStats().valid) { st = &S.MeshStats(); tag = "Mesh";    }
    else if (S.OffsetsNode() && S.OffStats().valid)  { st = &S.OffStats();  tag = "Offsets"; }
    else if (S.SrcStats().valid)                     { st = &S.SrcStats();  tag = "Source";  }

    char line[256];
    if (st && st->tris > 0) {
        int   chi   = st->verts - st->edges + st->tris;
        int   holes = 1 - chi;
        float te    = st->edges ? (float)st->tris / st->edges : 0.0f;
        snprintf(line, sizeof(line),
                 "%s   V %d   E %d   T %d   chi %d   holes %d   bnd %d   T/E %.2f",
                 tag, st->verts, st->edges, st->tris, chi, holes, st->boundary, te);
    } else if (st) {
        snprintf(line, sizeof(line), "%s   V %d   E %d   E-V %d",
                 tag, st->verts, st->edges, st->edges - st->verts);
    } else {
        snprintf(line, sizeof(line), "(no geometry)");
    }
    ImGui::TextDisabled("%s", line);
}

inline void DrawRevolutionSection(Scene& scene, SemSessionNS::SemSession& S) {
    using namespace SemSessionNS;
    ImGui::PushID("rev");

    if (!ImGui::CollapsingHeader("Revolution", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PopID();
        return;
    }

    bool rm = S.revolutionMode;
    if (ImGui::Checkbox("Revolution mode (around Y)", &rm)) {
        S.SetRevolutionMode(scene, rm);
    }
    ImGui::SetItemTooltip("Treat the staged contour as a half-profile and build surfaces of\n"
                          "revolution around the Y axis. The contour must lie entirely on\n"
                          "one side of the Y axis (all X>=0 or all X<=0).");

    if (S.revolutionMode) {
        ImGui::Indent();

        {
            Primitive* sp = S.SrcRevSurf();
            bool show = sp && sp->visible;
            if (ImGui::Checkbox("Source surface", &show)) S.ShowSourceRevolution(scene, show);
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset##srcrev")) S.DropSrcRev(scene);
            float a = S.srcRevAlpha;
            if (ImGui::DragFloat("Source opacity", &a, 0.005f, 0.05f, 1.0f, "%.2f"))
                S.SetSrcRevAlpha(scene, a);
        }

        {
            Primitive* ip = S.IsoRevSurf();
            bool show = ip && ip->visible;
            ImGui::BeginDisabled(!S.HasIsolinePath() && !(ip && ip->visible));
            if (ImGui::Checkbox("Isotherm surface", &show)) S.ShowIsolineRevolution(scene, show);
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset##isorev")) S.DropIsoRev(scene);
            float a = S.isoRevAlpha;
            if (ImGui::DragFloat("Isotherm opacity", &a, 0.005f, 0.05f, 1.0f, "%.2f"))
                S.SetIsoRevAlpha(scene, a);
            ImGui::EndDisabled();
            if (!S.HasIsolinePath()) ImGui::TextDisabled("Run the thermal isotherm first.");
        }

        ImGui::Unindent();
    }

    ImGui::PopID();
}

// Clip-plane editor (3D pipeline only). Each plane is a draggable rectangle in
// the scene: select it and move/rotate it with the orientation transformer (its
// normal + d show in the transform window). The planes are read off the
// rectangles and pushed to the mesher automatically whenever they change
// (SemSession::AutoApplyClipPlanes -> SEM_SetClipPlanes3D).
inline void DrawClipPlanesSection(Scene& scene, SemSessionNS::SemSession& S) {
    // Push the live plane transforms to the mesher every frame (even when the
    // section is collapsed) so gizmo edits keep applying — but never while ANY
    // setup is computing. The rectangles stay draggable in the 3D view even then,
    // and pushing them would both race that worker and, after a Cancel, hit the
    // library's cancel flag (it is process-wide) and drop this setup's offsets.
    // The edit is applied as soon as the run ends, since the rectangles moved.
    if (!SemSessionNS::Workspace().AnyAsyncRunning()) S.AutoApplyClipPlanes(scene);
    // A clip change re-snaps the source in the core; rebuild the displayed source
    // once the user is no longer dragging the gizmo or editing a field, so the
    // gizmo / Transform window are not yanked mid-edit.
    if (S.SrcRebuildPending() && !scene.orientationTransformer.HasActiveObject() &&
        !ImGui::IsAnyItemActive())
        S.RebuildSourcePrim(scene);

    ImGui::PushID("clip");
    if (!ImGui::CollapsingHeader("Clip planes", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PopID();
        return;
    }
    if (ImGui::SmallButton("Clear")) S.ClearClipPlanes3D(scene);
    ImGui::SetItemTooltip("Half-space clips applied during 3D meshing. The normal points\n"
                          "into the kept region (shown soft red); the removed side is soft blue.\n"
                          "Select a plane rectangle in the scene and move/rotate it with the\n"
                          "gizmo (or edit normal/d in the transform window); changes are\n"
                          "pushed to the mesher automatically.");

    ImGui::Indent();
    int removeIdx = -1;
    for (int i = 0; i < (int)S.clipPlaneNodes.size(); ++i) {
        ClipPlaneNode* node = S.clipPlaneNodes[i];
        if (!node) continue;
        ImGui::PushID(i);
        XMFLOAT4 pl = node->GetPlane();
        ImGui::Text("Plane %d: n(%.2f, %.2f, %.2f) d %.2f", i, pl.x, pl.y, pl.z, pl.w);
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) removeIdx = i;
        bool mirror = node->showMirror;
        if (ImGui::Checkbox("Show mirror primitives", &mirror))
            S.ShowClipMirror(scene, i, mirror);
        ImGui::SetItemTooltip("Mirror the source surface and the solution isotherm across\n"
                              "this plane; the copies are children of the mirrored primitive.\n"
                              "With several planes enabled the reflections compose.");
        ImGui::PopID();
    }
    if (removeIdx >= 0) S.RemoveClipPlane(scene, removeIdx);

    if (ImGui::Button("+ Add plane")) S.AddClipPlane(scene);
    ImGui::SetItemTooltip("Create a draggable clip-plane rectangle in the scene.");

    // Snap tolerance pushed to the mesher (SEM_SetClipPlanes3D's on_plane_rel_tol):
    // source geometry within this band of a plane is snapped exactly onto it before
    // the cut. Re-apply the planes once the edit settles.
    {
        float tol = S.clipPlaneTol;
        if (ImGui::DragFloat("On-plane snap tol (x edge)", &tol, 0.001f, 0.0f, 100.0f, "%.4f"))
            S.clipPlaneTol = (tol < 0.0f ? 0.0f : tol);
        if (ImGui::IsItemDeactivatedAfterEdit()) S.SetClipPlanes3D(scene);
        ImGui::SetItemTooltip("Source vertices within this distance of a clip plane are snapped\n"
                              "exactly onto it before cutting, in multiples of the source surface's\n"
                              "mean edge length (default 0.01; 0 = exact, no snapping).");
    }

    // Overlay of the geometry lying exactly on the clip planes, for the source
    // surface (white), the source isosurface (orange) and the final isosurface
    // (cyan). Planar triangles draw as filled faces, on-plane edges as lines and
    // isolated on-plane vertices as points (any may appear).
    {
        bool show = S.ClipOnPlaneShown();
        if (ImGui::Checkbox("Show geometry on planes", &show))
            S.ShowClipOnPlane(scene, show);
        ImGui::SetItemTooltip("Draw the vertices, edges and triangles that lie on the clip planes\n"
                              "for three surfaces at once: the source surface (white), the source\n"
                              "isosurface (orange) and the final isosurface (cyan). A triangle with\n"
                              "all three vertices on one plane draws as a filled face; an edge with\n"
                              "both endpoints on a plane draws as a line; an on-plane vertex covered\n"
                              "by neither draws as a point. The isosurface overlays appear once those\n"
                              "stages have been extracted. In standalone offset-remesh mode the source\n"
                              "surface (white) and the offset-remeshed result (cyan) are drawn.");
    }

    ImGui::Unindent();
    ImGui::PopID();
}

// Footer of every standalone section: where the finished result was written and
// what can be done with it. A standalone run is the one thing the pipeline does
// not serialize under the session folder - it is written next to the source, so
// the file is the product and these two buttons are how it is picked up.
inline void DrawSavedFileRow(Scene& scene, const std::string& path) {
    ImGui::Separator();
    if (path.empty()) {
        ImGui::TextDisabled("The result is written next to the source when a run finishes.");
        return;
    }
    const size_t cut = path.find_last_of("\\/");
    ImGui::TextDisabled("Saved: %s",
                        cut == std::string::npos ? path.c_str() : path.c_str() + cut + 1);
    ImGui::SetItemTooltip("%s", path.c_str());
    if (ImGui::Button("Show in Explorer", {160, 0})) SemSessionNS::SemSession::RevealFile(path);
    ImGui::SetItemTooltip("Open the folder the result was written to - the source's own\n"
                          "folder - with the file selected.");
    ImGui::SameLine();
    if (ImGui::Button("Open as setup", {140, 0})) BeginSourceImport(scene, path);
    ImGui::SetItemTooltip("Import the written result as an ADDITIONAL setup with its own SEM\n"
                          "cache, so the pipeline (or another standalone pass) can run on it.");
}

inline void DrawResultFileRow(Scene& scene, SemSessionNS::SemSession& S) {
    DrawSavedFileRow(scene, S.ResultPath());
}

// Standalone offset-remesh mode (SESSION_OFFSET_REMESH): offset-and-remesh the
// imported surface directly via SEM_OffsetRemeshInPlaneSurface3D, applying the clip
// planes set above. No offsets / mesh / thermal stages run. 3D only.
inline void DrawStandaloneRemeshSection(Scene& scene, SemSessionNS::SemSession& S) {
    using namespace SemSessionNS;
    ImGui::PushID("standalone_remesh");
    if (ImGui::CollapsingHeader("Standalone offset-remesh", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        ImGui::TextWrapped("Offset-and-remesh the imported surface directly, applying the "
                           "clip planes above. The offsets / mesh / thermal pipeline is skipped.");

        ImGui::DragFloat3("Base-plane normal", &S.soAxisN.x, 0.01f, 0.0f, 0.0f, "%.3f");
        ImGui::SetItemTooltip("Normal of the projection plane the sheet is re-triangulated over\n"
                              "(any direction; need not be unit). The sheet must be a single-valued\n"
                              "height field along it, else the projection is rejected as folded.");
        ImGui::TextUnformatted("Preset:"); ImGui::SameLine();
        if (ImGui::SmallButton("X")) S.soAxisN = { 1.0f, 0.0f, 0.0f }; ImGui::SameLine();
        if (ImGui::SmallButton("Y")) S.soAxisN = { 0.0f, 1.0f, 0.0f }; ImGui::SameLine();
        if (ImGui::SmallButton("Z")) S.soAxisN = { 0.0f, 0.0f, 1.0f };

        ImGui::DragFloat("Offset (world)", &S.soOffset, 0.05f, 0.0f, 0.0f, "%.3f");
        ImGui::SetItemTooltip("Shift along the surface pseudonormals before re-triangulation, as a\n"
                              "distance in world units. Positive shifts inward.");

        ImGui::DragFloat("Min offset (fraction)", &S.soMinOffset, 0.01f, 0.0f, 1.0f, "%.3f");
        ImGui::SetItemTooltip("Minimum clearance a shifted vertex must keep from the surface before\n"
                              "it is pruned, as a fraction c in [0,1] of Offset (absolute clearance\n"
                              "= c * Offset). c = 1 = fold cull, 0 = signed-crossing cull.");

        ImGui::DragFloat("Final target edge (x sheet edge)", &S.soTargetMult, 0.02f, 0.0f, 0.0f, "%.3f");
        if (S.soTargetMult < 0.0f) S.soTargetMult = 0.0f;
        ImGui::SetItemTooltip("Target edge length for the final isotropic cleanup, in multiples of the\n"
                              "remeshed sheet's average edge length. 0 uses the median.");

        ImGui::DragInt("Final iterations", &S.soIters, 0.1f, 0, 20);
        if (S.soIters < 0)  S.soIters = 0;
        if (S.soIters > 50) S.soIters = 50;
        ImGui::SetItemTooltip("Collapse / split / relax sweeps of the final cleanup. 0 skips it.");

        if (ImGui::Button("Apply offset-remesh", {180, 0}))
            S.ApplyStandaloneOffsetRemeshAsync(scene, false);
        ImGui::SetItemTooltip("Run SEM_OffsetRemeshInPlaneSurface3D on the imported surface with the\n"
                              "current clip planes and show the result. Runs on a worker thread with\n"
                              "a progress bar.");

        // Non-manifold highlight: paint triangles at welded/pinched topology defects
        // bright magenta. Applied instantly to the already-shown surface (no re-run).
        bool nonman = S.isoShowNonManifold;
        if (ImGui::Checkbox("Highlight non-manifold", &nonman))
            S.SetIsoNonManifold(scene, nonman);
        ImGui::SetItemTooltip("Paint triangles that touch a non-manifold edge (shared by three or\n"
                              "more faces) or a bowtie vertex bright magenta, keeping the rest green.\n"
                              "These are the defects vertex welding can leave where two sheets of the\n"
                              "offset surface pass within the weld tolerance. Nothing turns magenta on\n"
                              "a clean two-manifold sheet.");

        // Flat base-plane projection of the offset-remeshed sheet, fetched from the
        // cache (SEM_GetIsosurfaceProjection3D) — the same in-plane geometry the main
        // pipeline's isosurface section shows. Available once a standalone offset-remesh
        // has produced it (HasIsoProjection).
        ImGui::BeginDisabled(!S.HasIsoProjection());
        bool projShown = S.IsoProjectionPrim() != nullptr;
        if (ImGui::Checkbox("Show iso projection", &projShown))
            S.ShowIsosurfaceProjection3D(scene, projShown, false);
        ImGui::SetItemTooltip("Show the flat base-plane re-meshed projection (with its wireframe\n"
                              "edges) of the offset-remeshed surface — its in-plane geometry.");
        ImGui::EndDisabled();
        DrawResultFileRow(scene, S);
        ImGui::Unindent();
    }
    ImGui::PopID();
}

// The parting plane: add it, aim it, remove it. Shared by the body split and by
// the two standalone modes that need a plane, so they stay identical to use.
// Returns true when a plane exists.
inline bool DrawPartingPlaneControls(Scene& scene, SemSessionNS::SemSession& S,
                                     const char* addLabel) {
    ClipPlaneNode* node = S.SplitPlaneNode();
    if (!node) {
        if (ImGui::Button(addLabel, {200, 0})) S.AddSplitPlane(scene);
        ImGui::SetItemTooltip("Add the plane. Move and rotate its rectangle with the gizmo,\n"
                              "or type the normal and d in the Transform window. The kept\n"
                              "side is the one the normal points at.");
        return false;
    }
    const XMFLOAT4 pl = node->GetPlane();
    ImGui::TextDisabled("n = (%.3f, %.3f, %.3f)  d = %.3f", pl.x, pl.y, pl.z, pl.w);
    if (ImGui::SmallButton("Remove plane")) S.RemoveSplitPlane(scene);
    ImGui::SameLine();
    if (ImGui::SmallButton("Select")) {
        for (Primitive* p : scene.primitives) p->selected = false;
        if (node->rect) {
            node->rect->selected = true;
            scene.orientationTransformer.SetTargetObjects({ node->rect });
        }
    }
    ImGui::SetItemTooltip("Select the plane rectangle so the gizmo and the Transform\n"
                          "window edit it.");
    float tol = S.splitPlaneTol;
    if (ImGui::DragFloat("On-plane tol", &tol, 0.001f, 0.0f, 1.0f, "%.3f"))
        S.splitPlaneTol = tol;
    ImGui::SetItemTooltip("Vertices within this fraction of the mean edge length are\n"
                          "snapped exactly onto the plane first. 0 = exact.");
    return true;
}

// Standalone draft taper of an already-open surface about a parting plane its
// boundary lies in.
inline void DrawStandaloneTaperSection(Scene& scene, SemSessionNS::SemSession& S) {
    ImGui::PushID("sotaper");
    if (ImGui::CollapsingHeader("Standalone taper", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped("Add draft to the imported open surface about a parting plane its "
                           "boundary lies in. Anything poking through to the far side of the "
                           "plane is cut away first. Material is only added: the top of each "
                           "wall stays put and the outline grows outward.");
        ImGui::Spacing();
        const bool hasPlane = DrawPartingPlaneControls(scene, S, "Add parting plane");

        ImGui::BeginDisabled(!hasPlane);
        float ang = S.taperAngle;
        if (ImGui::DragFloat("Draft angle", &ang, 0.1f, 0.0f, 89.0f, "%.2f deg"))
            S.taperAngle = ang;
        ImGui::SetItemTooltip("Minimum wall slope from the draw direction, which is the\n"
                              "parting plane's normal.");
        ImGui::DragInt("Remesh after (sweeps)", &S.taperRemeshIters, 0.1f, 0, 20);
        if (S.taperRemeshIters < 0) S.taperRemeshIters = 0;
        ImGui::SetItemTooltip("Isotropic remesh sweeps run on the tapered sheet right after the\n"
                              "push, with the parting plane as a constraint. The push stretches\n"
                              "the triangles it moves; this evens them out. 0 skips it.");
        if (ImGui::Button("Apply taper", {200, 0}))
            S.ApplyStandaloneTaperAsync(scene, false);
        ImGui::EndDisabled();
        if (!hasPlane) ImGui::TextDisabled("Add a parting plane to enable the taper.");

        int moved = 0, bad = 0;
        double worst = 0.0, growth = 0.0;
        if (!S.AsyncRunning() && S.StandaloneTaperReport(moved, bad, worst, growth)) {
            ImGui::Separator();
            ImGui::TextDisabled("moved %d, outline +%.5f", moved, growth);
            ImGui::TextDisabled("%d facets short, min draft %.2f deg", bad, worst);
            ImGui::TextWrapped("'facets short' is the independent per-facet check over the whole "
                               "surface. A negative min draft is an undercut - a facet facing away "
                               "from the draw direction, which no in-plane push can fix.");
        }
        DrawResultFileRow(scene, S);
    }
    ImGui::PopID();
}

// The knobs both standalone remesh sections share: the plain isotropic remesh, or
// its adaptive variant that sizes the triangles by the curvature and keeps sharp
// edges. Adaptive has knobs of its own and its own sweep count.
inline void DrawRemeshKnobs(SemSessionNS::SemSession& S) {
    ImGui::Checkbox("Adaptive", &S.rmAdaptive);
    ImGui::SetItemTooltip("Plain: one edge length everywhere, and creases get rounded off.\n"
                          "Adaptive: edges shrink where the surface is curved, so a facet\n"
                          "sags from it by at most 'Max deviation', and edges sharper than\n"
                          "'Sharp edge angle' stay sharp. Run it on the ORIGINAL surface:\n"
                          "creases a plain remesh already rounded off cannot be recovered.");
    ImGui::DragFloat(S.rmAdaptive ? "Flat-region length" : "Target length", &S.rmTargetMult,
                     0.01f, 0.0f, 0.0f, "%.3f");
    ImGui::SetItemTooltip(S.rmAdaptive
        ? "Edge length where the surface is flat, as a multiple of the surface's\n"
          "own average edge length. 0 or less uses the median instead."
        : "Target edge length as a multiple of the surface's own average\n"
          "edge length. 0 or less uses the median instead.");
    if (S.rmAdaptive) {
        ImGui::DragFloat("Min length", &S.rmMinMult, 0.005f, 0.0f, 0.0f, "%.3f");
        ImGui::SetItemTooltip("Shortest edge, same unit. 0 = a tenth of the flat-region length.");
        ImGui::DragFloat("Max deviation", &S.rmMaxDevMult, 0.001f, 0.0f, 0.0f, "%.4f");
        ImGui::SetItemTooltip("How far a facet may sag from the curved input, same unit.\n"
                              "Smaller = finer triangles on fillets and other curves.\n"
                              "0 = a fiftieth of the flat-region length.");
        ImGui::DragFloat("Sharp edge angle", &S.rmFeatureDeg, 0.5f, 0.0f, 180.0f, "%.1f deg");
        ImGui::SetItemTooltip("An edge whose two faces turn by more than this is kept as a\n"
                              "crease: its vertices slide only along it, and the corners where\n"
                              "creases meet stay put. 0 = no creases, curvature only.");
        ImGui::DragInt("Iterations", &S.rmAdaptiveIters, 0.1f, 0, 50);
        ImGui::SetItemTooltip("Sweeps of split / collapse / flip / relaxation. 0 is a no-op.");
    } else {
        ImGui::DragInt("Iterations", &S.rmIters, 0.1f, 0, 50);
        ImGui::SetItemTooltip("Sweeps of collapse / split / relaxation. 0 is a no-op.");
    }
}

// What the last adaptive remesh kept, under its section.
inline void DrawAdaptiveRemeshReport(SemSessionNS::SemSession& S) {
    int creases = 0, corners = 0;
    double lo = 0.0, hi = 0.0;
    if (S.rmAdaptive && !S.AsyncRunning() && S.AdaptiveRemeshReport(creases, corners, lo, hi))
        ImGui::TextDisabled("kept %d sharp edges, %d corners; edges %.3g..%.3g", creases, corners, lo, hi);
}

// Standalone isotropic remesh: triangulation only, the shape is preserved.
inline void DrawStandaloneRemeshPlainSection(Scene& scene, SemSessionNS::SemSession& S) {
    ImGui::PushID("soremesh");
    if (ImGui::CollapsingHeader("Standalone remesh", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped("Re-triangulate the imported surface into near-equilateral "
                           "triangles of a uniform size. Relaxation projects back onto the "
                           "input, so the SHAPE is preserved and only the triangulation "
                           "changes - it evens out quality, it does not smooth the surface.");
        ImGui::Spacing();
        const bool hasPlane = DrawPartingPlaneControls(scene, S, "Add constraint plane");
        ImGui::TextDisabled(hasPlane
            ? "Boundary vertices in the plane slide within it."
            : "Without a plane every boundary vertex is frozen.");

        DrawRemeshKnobs(S);
        if (ImGui::Button("Apply remesh", {200, 0}))
            S.ApplyStandaloneRemeshAsync(scene, false);
        DrawAdaptiveRemeshReport(S);
        DrawResultFileRow(scene, S);
    }
    ImGui::PopID();
}

// Closed body, Remesh mode: the same plain isotropic remesh, run on the closed
// surface itself rather than on a half. It leaves the body cache alone - the
// result is shown and written next to the source, so a cleanly triangulated body
// can be opened as its own setup and split THERE. Hence: before the split.
inline void DrawBodyRemeshSection(Scene& scene, SemSessionNS::SemSession& S) {
    ImGui::PushID("bodyremesh");
    if (ImGui::CollapsingHeader("Standalone remesh")) {
        ImGui::TextWrapped("Re-triangulate the CLOSED surface into near-equilateral triangles of "
                           "a uniform size. Relaxation projects back onto the input, so the shape "
                           "is preserved and only the triangulation changes - the usual first "
                           "pass over an imported body whose facets are uneven.");
        ImGui::TextDisabled("The body's own cache is untouched: the result is");
        ImGui::TextDisabled("written next to the source, to be opened as a setup.");
        ImGui::Spacing();
        ImGui::BeginDisabled(S.BodySplit());
        DrawRemeshKnobs(S);
        if (ImGui::Button("Apply remesh", {200, 0}))
            S.ApplyStandaloneRemeshAsync(scene, false);
        ImGui::SetItemTooltip("Remesh the closed surface. The parting plane is ignored here -\n"
                              "it would cut the body in half instead of constraining it.");
        ImGui::EndDisabled();
        if (S.BodySplit())
            ImGui::TextDisabled("Remesh the body before splitting it.");
        DrawAdaptiveRemeshReport(S);
        DrawResultFileRow(scene, S);
    }
    ImGui::PopID();
}

// Body session (closed source): the parting plane and the split. The closed
// surface is only hidden once the split succeeds, and the two halves become its
// children, each in its own colour.
inline void DrawBodySplitSection(Scene& scene, SemSessionNS::SemSession& S) {
    using namespace SemSessionNS;
    ImGui::PushID("bodysplit");
    if (ImGui::CollapsingHeader("Body / split plane", ImGuiTreeNodeFlags_DefaultOpen)) {
        const double vol = S.BodyVolume();
        if (vol >= 0.0) ImGui::TextDisabled("Closed body, volume %.4g", vol);
        else            ImGui::TextDisabled("Closed body (no volume cached)");

        if (DrawPartingPlaneControls(scene, S, "Add parting plane")) {
            if (ImGui::Button(S.BodySplit() ? "Split again" : "Split", {200, 0}))
                S.ApplySplit(scene, false);
            ImGui::SetItemTooltip("Cut the body along the plane into two open halves, hide the\n"
                                  "closed surface and add a half under it per side. Each half\n"
                                  "gets its own SEM cache; the stages below then run on both.");
        }

        if (S.BodySplit()) {
            for (int side = 0; side < 2; ++side) {
                Primitive* hp = S.HalfPrim((side == 0) ? +1 : -1);
                if (!hp) continue;
                bool vis = hp->visible;
                ImGui::PushID(side);
                if (ImGui::Checkbox(side == 0 ? "Show + half" : "Show - half", &vis))
                    scene.SetNodeVisibleCascade(hp, vis);
                ImGui::PopID();
            }
            if (S.SkirtActive()) {
                ImGui::TextDisabled("The halves are extended past this plane (skirt):");
                ImGui::TextDisabled("their offsets and distances run on half + skirt,");
                ImGui::TextDisabled("lighter tint = the skirt. Their domains still end");
                ImGui::TextDisabled("on the plane.");
            } else {
                ImGui::TextDisabled("Both halves carry this plane as a clip plane,");
                ImGui::TextDisabled("each keeping its own side, so their stages are");
                ImGui::TextDisabled("cut flush at it instead of crossing over.");
            }
            bool onPlane = S.SplitOnPlaneShown();
            if (ImGui::Checkbox("Show geometry on split-plane", &onPlane))
                S.ShowSplitOnPlane(scene, onPlane);
            ImGui::SetItemTooltip("Draw the geometry of both halves that lies exactly on the\n"
                                  "parting plane: the cut outline, plus any face the snap\n"
                                  "flattened onto it. Each half in its own colour.");
        }
    }
    ImGui::PopID();
}

// Body session: the draft taper of both halves.
inline void DrawBodyTaperSection(Scene& scene, SemSessionNS::SemSession& S) {
    ImGui::PushID("bodytaper");
    if (ImGui::CollapsingHeader("Taper (draft)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginDisabled(!S.BodySplit());
        float ang = S.taperAngle;
        if (ImGui::DragFloat("Draft angle", &ang, 0.1f, 0.0f, 89.0f, "%.2f deg"))
            S.taperAngle = ang;
        ImGui::SetItemTooltip("Minimum wall slope from the draw direction (the parting plane\n"
                              "normal). Material is only added: the top of each wall stays\n"
                              "put and the cut outline grows outward.");

        ImGui::DragInt("Remesh after (sweeps)", &S.taperRemeshIters, 0.1f, 0, 20);
        if (S.taperRemeshIters < 0) S.taperRemeshIters = 0;
        ImGui::SetItemTooltip("Isotropic remesh sweeps the core runs over each half right after\n"
                              "the push, with the parting plane as a constraint so the grown\n"
                              "outline stays in it. The push stretches the triangles it moves,\n"
                              "so this is what leaves the draft cone evenly meshed. 0 keeps the\n"
                              "taper's own triangulation. The per-facet numbers below are\n"
                              "re-measured after the sweeps.");

        // The skirt is the default mode and the split already builds it, so its
        // controls stay live before the split too.
        ImGui::EndDisabled();
        ImGui::Checkbox("Extend past the parting plane (skirt)", &S.skirtEnabled);
        ImGui::SetItemTooltip("Carry each half's walls on below the parting plane at the draft\n"
                              "angle. The halves' offsets and distances then run on half + skirt,\n"
                              "so near the parting line the BC levels follow the drafted walls\n"
                              "instead of rounding off at the cut; the domain still ends on the\n"
                              "plane. On by default: Split builds it straight down, Apply taper\n"
                              "rebuilds it at the draft angle.");
        ImGui::BeginDisabled(!S.skirtEnabled);
        ImGui::DragFloat("Skirt depth", &S.skirtDepth, 0.01f, 0.0f, 0.0f, "%.4f");
        if (S.skirtDepth < 0.0f) S.skirtDepth = 0.0f;
        ImGui::SetItemTooltip("How far below the plane each skirt goes. 0 = down to the other\n"
                              "half's height, i.e. through the whole other half. It should also\n"
                              "be a few times the total offset, so the free edge of the skirt\n"
                              "stays clear of the parting line.");
        ImGui::DragFloat("Skirt thinning", &S.skirtGrowth, 0.01f, 0.0f, 1.0f, "%.2f");
        S.skirtGrowth = (std::min)(1.0f, (std::max)(0.0f, S.skirtGrowth));
        ImGui::SetItemTooltip("How fast the skirt gets sparser away from the parting line.\n"
                              "0: rings one outline edge h apart, each with every outline vertex.\n"
                              "Above 0 each gap is h + thinning * (depth so far) - 0.5 puts the\n"
                              "rings at h, 2.5h, 4.75h, ... - and every ring keeps only the\n"
                              "vertices about a gap apart along it (the outline's corners always),\n"
                              "so the far skirt, which only carries the draft on, costs few points.");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!S.BodySplit());

        if (ImGui::Button("Apply taper", {200, 0})) S.ApplyTaper(scene, false);
        ImGui::SetItemTooltip("Taper both halves, then extend them by the skirt when it is on -\n"
                              "one core call. The core always tapers the pristine cut, so\n"
                              "applying another angle rebuilds the halves instead of stacking\n"
                              "a second draft on them. Their pipeline stages are dropped,\n"
                              "because their source geometry changed.");
        if (S.BodySplit() && S.SkirtActive() != S.skirtEnabled)
            ImGui::TextDisabled(S.skirtEnabled ? "Skirt not built yet - Apply taper."
                                               : "Skirt still on - Apply taper to drop it.");

        // The readouts below ask the core about the halves, so they must not run
        // while the taper worker is rewriting them in the same context.
        if (S.SkirtActive() && S.BodySplit() && !S.AsyncRunning()) {
            double d = 0.0, rp = 0.0, rn = 0.0;
            if (S.SkirtState(d, rp, rn))
                ImGui::TextDisabled("Skirt reached: + half %.4g, - half %.4g", rp, rn);
            ImGui::SetItemTooltip("The least depth over each half's boundary loops. A loop whose\n"
                                  "rings cannot be kept from folding over (a hole of the outline\n"
                                  "closing up) stops early.");
        }
        if (S.BodySplit() && !S.AsyncRunning()) {
            for (int side = 0; side < 2; ++side) {
                const int sgn = (side == 0) ? +1 : -1;
                int moved = 0, bad = 0;
                double worst = 0.0, growth = 0.0;
                if (S.TaperReport(sgn, moved, bad, worst, growth) && moved > 0)
                    ImGui::TextDisabled("%s half: moved %d, outline +%.4f",
                                        side == 0 ? "+" : "-", moved, growth);
                // Measured live, so it reads the same way before and after a taper.
                int badNow = 0; double worstNow = 0.0;
                if (S.MeasureDraftNow(sgn, (double)S.taperAngle, badNow, worstNow))
                    ImGui::TextDisabled("%s half now: %d facets short, min draft %.2f deg",
                                        side == 0 ? "+" : "-", badNow, worstNow);
            }
            ImGui::TextWrapped("'facets short' is the independent per-facet check over the WHOLE "
                               "half. A negative min draft is an undercut (the facet faces away "
                               "from the draw direction). The taper only drafts walls against the "
                               "outer parting outline, so interior walls and overhangs are "
                               "reported here but not corrected.");
        }
        ImGui::EndDisabled();
    }
    ImGui::PopID();
}

// Body session, last stage: glue the two halves back into one closed body on
// their own geometry. Both end on the parting plane; the plane closes whatever only
// one of them covers there (a flat land), so the parting line lies in it.
inline void DrawBodyPreformSection(Scene& scene, SemSessionNS::SemSession& S) {
    using namespace SemSessionNS;
    ImGui::PushID("bodypreform");
    if (ImGui::CollapsingHeader("Preform (glue halves)", ImGuiTreeNodeFlags_DefaultOpen)) {
        const bool haveIso = S.HalvesHaveIsosurfaces();
        ImGui::TextUnformatted("Halves");
        ImGui::SameLine();
        ImGui::RadioButton("Isosurfaces", &S.preformSource, SemSession::PREFORM_FROM_ISOSURFACES);
        ImGui::SetItemTooltip("The isosurface each half extracted last. Both end on the parting\n"
                              "plane, the halves' domains being cut there. Extract it on both\n"
                              "halves first.");
        ImGui::SameLine();
        ImGui::RadioButton("Split / tapered", &S.preformSource, SemSession::PREFORM_FROM_HALVES);
        ImGui::SetItemTooltip("The halves as the split cut them, or as the last taper left them\n"
                              "(without their skirts).");
        if (S.preformSource == SemSession::PREFORM_FROM_ISOSURFACES && !haveIso)
            ImGui::TextDisabled("No isosurface on both halves yet.");

        ImGui::BeginDisabled(S.preformSource == SemSession::PREFORM_FROM_ISOSURFACES && !haveIso);
        if (ImGui::Button("Apply preform", {200, 0})) S.ApplyPreform(scene, false);
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Glue both halves into one closed surface on their own geometry: no\n"
                              "vertex moves and nothing is resampled. Where only one half reaches\n"
                              "the parting plane, the plane closes it with a flat land; where the\n"
                              "two outlines cross, both are split at the crossing, so the seam is\n"
                              "welded vertex for vertex.");

        if (Primitive* pp = S.PreformPrim()) {
            bool vis = pp->visible;
            if (ImGui::Checkbox("Show preform", &vis)) scene.SetNodeVisibleCascade(pp, vis);
            if (!S.AsyncRunning()) {
                double volume = 0.0, land = 0.0;
                int lp = 0, ln = 0, cross = 0, open = 0;
                if (S.PreformReport(volume, land, lp, ln, cross, open)) {
                    ImGui::TextDisabled("Volume %.6g, lands %.4g", volume, land);
                    ImGui::SetItemTooltip("Lands: the area of the parting plane that closes what only one\n"
                                          "half reaches.");
                    ImGui::TextDisabled("Parting loops %d / %d, outline crossings %d", lp, ln, cross);
                    if (open > 0)
                        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                                           "%d edges open: a half has a boundary off the plane.", open);
                }
            }
            DrawSavedFileRow(scene, S.PreformPath());
        }
    }
    ImGui::PopID();
}

// The running SEM job for the top strip, between the toolbar and the FPS readout:
// the stage's bar (naming the half of a body), the bar of the phase running inside
// it, and Cancel. Returns the width it needs and draws only when `draw` is set, so
// the strip can measure it first to right-align it; 0 while no setup computes.
inline float DrawTopStripProgress(bool draw) {
    using namespace SemSessionNS;
    SemSession* S = nullptr;
    SemWorkspace& ws = Workspace();
    for (int i = 0; i < ws.Count() && !S; ++i) {
        SemSession* s = ws.At(i);
        if (s && s->AsyncRunning()) S = s;
    }
    if (!S) return 0.0f;

    const float kMainW = 280.0f, kSubW = 220.0f;
    const ImGuiStyle& st = ImGui::GetStyle();
    const char* sub = S->AsyncSubStageName();
    const bool hasSub = sub && sub[0];
    const bool stopping = S->AsyncCancelRequested();
    const char* btn = stopping ? "Stopping..." : "Cancel";
    const float btnW = ImGui::CalcTextSize(btn).x + 2.0f * st.FramePadding.x;
    const float w = kMainW + (hasSub ? st.ItemSpacing.x + kSubW : 0.0f) + st.ItemSpacing.x + btnW;
    if (!draw) return w;

    const float h = ImGui::GetFrameHeight() - 2.0f;
    char overlay[160];
    if (S->IsBody() && S->BodyPhase() > 0)
        snprintf(overlay, sizeof(overlay), "Half %d/2: %s  %.0f%%", S->BodyPhase(),
                 S->AsyncStageName(), S->AsyncProgress() * 100.0f);
    else
        snprintf(overlay, sizeof(overlay), "%s  %.0f%%", S->AsyncStageName(),
                 S->AsyncProgress() * 100.0f);
    ImGui::ProgressBar(S->AsyncProgress(), ImVec2(kMainW, h), overlay);
    ImGui::SetItemTooltip("%s", overlay);
    // The phase running now inside this stage and its own fraction
    // (SEM_GetProgressStage / SEM_GetProgressStageFraction).
    if (hasSub) {
        char subOverlay[160];
        snprintf(subOverlay, sizeof(subOverlay), "%s  %.0f%%", sub,
                 S->AsyncSubStageProgress() * 100.0f);
        ImGui::SameLine();
        ImGui::ProgressBar(S->AsyncSubStageProgress(), ImVec2(kSubW, h), subOverlay);
        ImGui::SetItemTooltip("%s", subOverlay);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(stopping);
    if (ImGui::Button(btn, ImVec2(btnW, h))) S->CancelAsync();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Stop the 3D pipeline now: the stage running is interrupted at\n"
                          "its next progress checkpoint and rolled back, and every queued\n"
                          "stage after it is skipped. Stages that already completed are\n"
                          "kept. A few third-party kernels (Delaunay, the sparse solve,\n"
                          "the CDT) can only be stopped before they start, so a stop can\n"
                          "take until one of those finishes.");
    return w;
}

// The offset spacing knobs, shared by the 2D Offsets section and the 3D Domain:
// even spacing (first gap, count, grading) or a list of custom gaps. `changed`
// reports a click edit, `released` the end of a drag edit.
inline void DrawOffsetSpacing(SemSessionNS::SemSession& S, bool& changed, bool& released) {
    using namespace SemSessionNS;
    const char* modes = "Even spacing\0Custom gaps\0";
    changed |= ImGui::Combo("Spacing", &S.offsetMode, modes);

    if (S.offsetMode == OFFSET_EVEN) {
        ImGui::DragFloat("First gap", &S.firstGap, 0.05f, -1000.0f, 1000.0f, "%.3f");
        released |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetItemTooltip("Size of the first gap, as a distance in world units.\n"
                              "Sign selects the side (left/right of travel).\n"
                              "Successive gaps scale by Grading.");
        ImGui::DragInt("Num offsets", &S.numOffsets, 0.2f, 1, 500);
        released |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("Grading", &S.grading, 0.01f, 0.05f, 20.0f, "%.3f");
        released |= ImGui::IsItemDeactivatedAfterEdit();
        if (S.numOffsets < 1) S.numOffsets = 1;
        if (S.grading <= 0.0f) S.grading = 1.0f;
    } else {
        int removeIdx = -1;
        for (int i = 0; i < (int)S.gaps.size(); ++i) {
            ImGui::PushID(i);
            ImGui::SetNextItemWidth(140);
            ImGui::DragFloat("##gap", &S.gaps[i], 0.5f, -5000.0f, 5000.0f, "%.2f");
            released |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SameLine();
            if (ImGui::Button("X")) removeIdx = i;
            ImGui::SameLine();
            ImGui::Text("gap %d", i);
            ImGui::PopID();
        }
        if (removeIdx >= 0) { S.gaps.erase(S.gaps.begin() + removeIdx); changed = true; }
        if (ImGui::Button("+ Add gap")) {
            S.gaps.push_back(S.gaps.empty() ? 25.0f : S.gaps.back());
            changed = true;
        }
    }
}

// The thermal BCs (3D): the field runs from 0 on one level to 1 on another, chosen
// among the source and the offsets the grid embeds. A change goes to the core at
// once and the domain is redrawn by it; the field is re-solved by the next
// isosurface Apply.
inline void DrawBcLevelChoice(Scene& scene, SemSessionNS::SemSession& S) {
    int ni = 0;
    const std::vector<double>& lv = S.OffsetLevels(ni);
    if (lv.empty()) {
        ImGui::TextDisabled("Apply the domain to choose the BC levels.");
        return;
    }
    const int no = (int)lv.size() - 1 - ni;
    auto label = [&](int k) {
        char buf[64];
        if (k == 0)     snprintf(buf, sizeof(buf), "source");
        else if (k > 0) snprintf(buf, sizeof(buf), "outer %d (%+.4g)", k, lv[(size_t)(k + ni)]);
        else            snprintf(buf, sizeof(buf), "inner %d (%+.4g)", -k, lv[(size_t)(k + ni)]);
        return std::string(buf);
    };
    auto resolve = [&](int k) {
        if (k >= SEM_LEVEL_OUTERMOST) return no;
        if (k <= SEM_LEVEL_INNERMOST) return -ni;
        return (std::max)(-ni, (std::min)(no, k));
    };
    // The extreme levels are kept as such, so they follow the offsets.
    auto store = [&](int k) {
        if (k == no) return (int)SEM_LEVEL_OUTERMOST;
        if (k == -ni && ni > 0) return (int)SEM_LEVEL_INNERMOST;
        return k;
    };
    bool bcChanged = false;
    ImGui::BeginDisabled(S.AsyncRunning());
    for (int which = 0; which < 2; ++which) {
        int& slot = (which == 0) ? S.bcFrom : S.bcTo;
        const int cur = resolve(slot);
        if (ImGui::BeginCombo(which == 0 ? "From (T = 0)" : "To (T = 1)", label(cur).c_str())) {
            for (int k = -ni; k <= no; ++k)
                if (ImGui::Selectable(label(k).c_str(), k == cur) && k != cur) {
                    slot = store(k);
                    bcChanged = true;
                }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip(which == 0
            ? "Level the field starts from (T = 0): it and everything beyond it, away\n"
              "from To, is held at 0. Smoothing 0 gives this level back."
            : "Level the field reaches (T = 1): it and everything beyond it is held\n"
              "at 1. The distance between From and To sets how far the shift reaches.");
    }
    ImGui::EndDisabled();
    if (resolve(S.bcFrom) == resolve(S.bcTo))
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "From and To are the same level.");
    else if (bcChanged)
        S.ApplyThermalBC(scene);
    if (S.MeshBuilt())
        ImGui::TextDisabled("On the domain: blue = held at 0, red = held at 1.");
}

// Show or hide the grid box of the domain - of both halves for a body.
inline void DrawGridBoxToggle(Scene& scene, SemSessionNS::SemSession& S) {
    std::vector<Primitive*> boxes;
    if (S.IsBody()) {
        for (int side : { +1, -1 })
            if (SemSessionNS::SemSession* h = S.HalfSession(side))
                if (Primitive* b = h->GridBoxPrim()) boxes.push_back(b);
    } else if (Primitive* b = S.GridBoxPrim()) {
        boxes.push_back(b);
    }
    if (boxes.empty()) return;
    bool vis = boxes.front()->visible;
    if (ImGui::Checkbox("Show grid box", &vis))
        for (Primitive* b : boxes) scene.SetNodeVisibleCascade(b, vis);
    ImGui::SetItemTooltip("The box the domain fills, as grey lines.");
}

// 3D: the domain the thermal problem is solved in - a box around the part meshed
// as a regular grid, with the source and every offset level embedded in it as
// nodes - and the BC levels chosen among those levels. The offset levels and the
// mesh are computed by one Apply.
inline void DrawDomainSection(Scene& scene, SemSessionNS::SemSession& S) {
    using namespace SemSessionNS;
    ImGui::PushID("domain");
    if (ImGui::CollapsingHeader("Domain", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::SmallButton("Reset##domain")) S.ResetStage(scene, STAGE_OFFSETS);
        const double offMs = S.OffsetsTimeMs(), meshMs = S.MeshTimeMs();
        if (offMs >= 0.0 || meshMs >= 0.0) {
            ImGui::SameLine();
            ImGui::TextDisabled("%.0f ms", (std::max)(0.0, offMs) + (std::max)(0.0, meshMs));
        }

        ImGui::Indent();
        bool meshChanged = false, offChanged = false, offReleased = false;
        ImGui::DragFloat3("Box (x part extents)", &S.gridBoxScale.x, 0.01f, 1.0f, 20.0f, "%.2f");
        meshChanged |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetItemTooltip("The box the thermal problem is solved in, as multiples of the part's\n"
                              "extents along x, y, z, centred on it. It is grown as needed to hold\n"
                              "every offset level with a grid step to spare. The domain of a body\n"
                              "half ends on the parting plane.");
        ImGui::DragFloat("Grid step", &S.gridStep, 0.05f, 0.0f, 1e6f, "%.3f");
        meshChanged |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetItemTooltip("Spacing of the regular grid, world units. 0 = auto: about 1e5 grid\n"
                              "points in the box. The offset levels are sampled at half the step,\n"
                              "and grid points closer to them than that are left out.");
        ImGui::SameLine();
        if (ImGui::SmallButton("auto")) { S.gridStep = 0.0f; meshChanged = true; }
        S.gridBoxScale.x = (std::max)(1.0f, S.gridBoxScale.x);
        S.gridBoxScale.y = (std::max)(1.0f, S.gridBoxScale.y);
        S.gridBoxScale.z = (std::max)(1.0f, S.gridBoxScale.z);
        if (S.gridStep < 0.0f) S.gridStep = 0.0f;

        ImGui::Separator();
        ImGui::TextDisabled("Offset levels, embedded in the grid as nodes");
        DrawOffsetSpacing(S, offChanged, offReleased);
        offChanged |= ImGui::Checkbox("Inward offsets", &S.innerEnabled);
        ImGui::SetItemTooltip("Also offset the part into its material. The grid embeds these levels\n"
                              "too, and the thermal BCs can then be set on them.");
        ImGui::BeginDisabled(!S.innerEnabled);
        ImGui::DragFloat("Inner first gap", &S.innerFirstGap, 0.05f, 0.0f, 1000.0f, "%.3f");
        offReleased |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetItemTooltip("Depth of the first inner offset, world units. Successive gaps scale\n"
                              "by Inner grading.");
        ImGui::DragInt("Inner offsets", &S.innerNum, 0.2f, 1, 100);
        offReleased |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("Inner grading", &S.innerGrading, 0.01f, 0.05f, 20.0f, "%.3f");
        offReleased |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::EndDisabled();
        if (S.innerFirstGap <= 0.0f) S.innerFirstGap = 0.001f;
        if (S.innerNum < 1) S.innerNum = 1;
        if (S.innerGrading <= 0.0f) S.innerGrading = 1.0f;

        if (offChanged || offReleased) S.MarkStageDirty(STAGE_OFFSETS);
        if (meshChanged) S.MarkStageDirty(STAGE_MESH);
        if (ImGui::Button("Apply domain", {160, 0})) S.RecomputeUpToAsync(scene, STAGE_MESH, false);
        ImGui::SetItemTooltip("Compute the offset levels when they changed, then mesh the box as\n"
                              "a regular grid with the source and every level embedded as nodes.\n"
                              "The domain is then drawn by its current BCs.");
        int ni = 0;
        const std::vector<double>& lv = S.OffsetLevels(ni);
        if (!lv.empty())
            ImGui::TextDisabled("Levels: %d inner, %d outer, %.4g .. %+.4g",
                                ni, (int)lv.size() - 1 - ni, lv.front(), lv.back());
        XMFLOAT3 glo, ghi;
        double gst = 0.0;
        if (S.GridInfo(glo, ghi, gst))
            ImGui::TextDisabled("Grid: step %.4g, box %.4g x %.4g x %.4g", gst,
                                ghi.x - glo.x, ghi.y - glo.y, ghi.z - glo.z);
        DrawGridBoxToggle(scene, S);

        ImGui::Separator();
        ImGui::TextDisabled("Boundary conditions");
        DrawBcLevelChoice(scene, S);
        ImGui::Unindent();
    }
    ImGui::PopID();
}

inline void Draw(Scene& scene, bool& blockMousePick) {
    using namespace SemSessionNS;

    // Drop dead setups, follow the staged primitive, apply finished background
    // results and select the active setup's SEM context (see SemWorkspace).
    Workspace().Sync(scene);

    ImGui::Begin("SEM");

    // Before binding S: a tab click switches the active setup (and its SEM
    // context) right here, and the rest of the frame must draw that setup.
    DrawSetupTabs(scene);
    SemSession& S = Session();

    if (!S.HasSource()) {
        if (S.SourcePrim()) ImGui::TextDisabled("Staged primitive is not a SEM contour.");
        else                ImGui::TextDisabled("No setup open.");
        ImGui::Spacing();
        if (ImGui::Button("Import CSV3D...", {200, 0}))
            BeginSourceImport(scene, BrowseCsv3dFile());
        ImGui::SetItemTooltip("Browse for a .csv3d contour or surface, add it to the scene and\n"
                              "open it as a SEM setup.");
        DrawImportSessionModal(scene);
        ImGui::Spacing();
        if (scene.stagingEnabled) {
            ImGui::TextWrapped("Double-click a CSV3D source in the tree to open it here.");
        } else {
            ImGui::TextWrapped("Staging is off. Import a CSV3D contour to re-enable it.");
        }
        ImGui::Separator();
        ImGui::TextDisabled("%s", S.status);
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows))
            blockMousePick = true;
        ImGui::End();
        return;
    }

    const float kItemW = 180.0f;
    ImGui::PushItemWidth(kItemW);

    // While a 3D pipeline runs on its worker thread, disable every control so
    // parameters can't change mid-computation. Its progress bars and Cancel live in
    // the top strip (DrawTopStripProgress). The library reports progress
    // process-globally, so only one setup may compute at a time: a job in ANY setup
    // disables the controls here.
    const bool busy      = S.AsyncRunning();
    const bool otherBusy = !busy && Workspace().AnyAsyncRunning();
    if (busy) {
        ImGui::TextDisabled("Computing - progress and Cancel are in the top bar.");
        ImGui::Separator();
    } else if (otherBusy) {
        ImGui::TextDisabled("Another setup is computing...");
        ImGui::Separator();
    }
    ImGui::BeginDisabled(busy || otherBusy);

    if (ImGui::Button("Add surface...", {kItemW, 0}))
        BeginSourceImport(scene, BrowseCsv3dFile());
    ImGui::SetItemTooltip("Browse for a .csv3d contour or surface and open it as an ADDITIONAL\n"
                          "setup: it joins the scene next to the setups already open, with its\n"
                          "own SEM cache, and the tabs above switch between them.\n"
                          "If the source has saved sessions you can reload one instead.");
    DrawImportSessionModal(scene);
    // An import above opened a new setup and selected ITS context; the rest of this
    // frame still draws S, so put S's context back. The new setup takes over on the
    // next frame's Sync (its source is staged).
    S.Activate();
    ImGui::Separator();

    const bool isBody = S.IsBody();
    const bool is3D   = S.Dim() == 3;
    if (isBody)    ImGui::TextDisabled("Mode: closed body (split into two halves)");
    else if (is3D) ImGui::TextDisabled("Mode: 3D surface (tetrahedral pipeline)");
    else           ImGui::TextDisabled("Mode: 2D contour");

    // 3D session mode: the full pipeline, or a standalone offset-remesh of the
    // imported surface (clip planes still apply; the pipeline stages are hidden).
    if (is3D && !isBody) {
        ImGui::RadioButton("Pipeline", &S.sessionMode, SemSession::SESSION_PIPELINE);
        ImGui::SameLine();
        ImGui::RadioButton("Offset-remesh", &S.sessionMode, SemSession::SESSION_OFFSET_REMESH);
        ImGui::RadioButton("Taper", &S.sessionMode, SemSession::SESSION_TAPER);
        ImGui::SameLine();
        ImGui::RadioButton("Remesh", &S.sessionMode, SemSession::SESSION_REMESH);
        ImGui::SetItemTooltip("Pipeline: load -> offsets -> mesh -> thermal -> isosurface.\n"
                              "The other three work on the imported surface directly and leave\n"
                              "the pipeline cache alone: Offset-remesh shifts it along its\n"
                              "pseudonormals and re-meshes over a base plane, Taper adds draft\n"
                              "about a parting plane, Remesh only evens out the triangulation.");
    } else if (isBody) {
        // A closed body has its own two jobs; same radio row, different pair.
        ImGui::RadioButton("Body (split)", &S.bodyMode, SemSession::BODYMODE_SPLIT);
        ImGui::SameLine();
        ImGui::RadioButton("Remesh", &S.bodyMode, SemSession::BODYMODE_REMESH);
        ImGui::SetItemTooltip("Body: the parting plane, the split into two halves and the\n"
                              "pipeline stages that then run on both.\n"
                              "Remesh: a plain isotropic remesh of the closed surface. It leaves\n"
                              "this setup's cache alone and writes the result next to the\n"
                              "source, to be opened as a setup of its own.");
    }

    DrawReadout(S);

    {
        ImGui::PushID("srcsurf");
        if (ImGui::CollapsingHeader("Source surface", ImGuiTreeNodeFlags_DefaultOpen)) {
            // Show/hide the source surface on its own, without touching the pipeline
            // products parented under it (offsets, mesh, isosurface). The tree eye
            // instead hides the whole subtree (and shows the node alone).
            Primitive* sp = S.SourcePrim();
            bool srcVisible = sp && sp->visible;
            if (ImGui::Checkbox("Show source surface", &srcVisible))
                S.ShowSource(scene, srcVisible);
            ImGui::SetItemTooltip("Show or hide the staged source surface only. The pipeline\n"
                                  "products parented under it (offsets, mesh, isosurface) keep\n"
                                  "their own visibility.");

            if (is3D) {
                float a = S.surf3dAlpha;
                if (ImGui::DragFloat("Surface opacity", &a, 0.005f, 0.05f, 1.0f, "%.2f"))
                    S.SetSurf3dAlpha(scene, a);
                ImGui::SetItemTooltip("Opacity of the 3D pipeline surfaces (source, offsets, mesh,\n"
                                      "isosurface). Back-face culling is set globally via the\n"
                                      "NavCube no-cull toggle.");

                // Angle-weighted vertex pseudonormals of the source surface, drawn
                // as short yellow segments.
                bool show = S.SrcNormalsPrim() != nullptr;
                if (ImGui::Checkbox("Source pseudonormals", &show))
                    S.ShowSourcePseudonormals(scene, show, false);
                ImGui::SetItemTooltip("Draw the angle-weighted vertex pseudonormals of the source\n"
                                      "surface as short yellow line segments.");

                // Clip-plane geometry changes logged for the SOURCE restriction:
                // yellow snap segments (vertex -> its snapped position) and the
                // translucent red geometry the half-space cut removed.
                bool srcChg = S.ClipChangesShown(SemSessionNS::CLIPCHG_SOURCE);
                if (ImGui::Checkbox("Source clip changes", &srcChg))
                    S.ShowClipChanges(scene, SemSessionNS::CLIPCHG_SOURCE, srcChg);
                ImGui::SetItemTooltip("Show what the clip planes did to the SOURCE surface: the snap\n"
                                      "displacements as short yellow segments (each vertex to where it\n"
                                      "was snapped onto a plane) and the clipped-away geometry as a\n"
                                      "translucent red surface. Needs clip planes set.");
            }
        }
        ImGui::PopID();
    }

    if (S.BodyRemeshMode()) {
        DrawBodyRemeshSection(scene, S);
    } else if (isBody) {
        // Stages 1 and 2 of a body session; the shared pipeline stack follows below.
        DrawBodySplitSection(scene, S);
        DrawBodyTaperSection(scene, S);
    } else if (!is3D) {
        DrawRevolutionSection(scene, S);
    } else if (S.sessionMode == SemSession::SESSION_TAPER ||
               S.sessionMode == SemSession::SESSION_REMESH) {
        // Those two carry their own parting plane in their own section.
    } else {
        DrawClipPlanesSection(scene, S);
    }

    // The 3D pipeline stages have no auto-apply: their compute is heavy and runs
    // on a worker thread, so every stage is driven explicitly by its Apply button.
    auto autoTag = [&](bool* autoFlag) {
        if (is3D) return;
        ImGui::SameLine();
        ImGui::Checkbox("Auto-apply", autoFlag);
    };

    // Disabled "<n> ms" tag shown next to a stage header once it has been
    // computed (ms < 0 => not yet measured, nothing drawn).
    auto stageTime = [&](double ms) {
        if (ms < 0.0) return;
        ImGui::SameLine();
        ImGui::TextDisabled("%.0f ms", ms);
    };

    // Standalone mode replaces the whole pipeline-stage stack with a single
    // offset-remesh section; the source + clip-plane sections above stay shared.
    if (S.BodyRemeshMode()) {
        // Drawn above: the stage stack belongs to the body's split mode.
    } else if (is3D && S.sessionMode == SemSession::SESSION_OFFSET_REMESH) {
        DrawStandaloneRemeshSection(scene, S);
    } else if (is3D && S.sessionMode == SemSession::SESSION_TAPER) {
        DrawStandaloneTaperSection(scene, S);
    } else if (is3D && S.sessionMode == SemSession::SESSION_REMESH) {
        DrawStandaloneRemeshPlainSection(scene, S);
    } else {
    if (isBody) {
        ImGui::TextDisabled(S.BodySplit()
            ? "The stages below run on BOTH halves, one after the other."
            : "Split the body to enable the stages below.");
        ImGui::BeginDisabled(!S.BodySplit());
    }

    {
        ImGui::PushID("sub");
        if (ImGui::CollapsingHeader("Subdivide", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!is3D) ImGui::Checkbox("Auto-apply", &S.subAuto);

        ImGui::Indent();
        bool changed = false, released = false;
        changed |= ImGui::RadioButton("Clear",    &S.subMode, 0); ImGui::SameLine();
        changed |= ImGui::RadioButton("Adaptive", &S.subMode, 1); ImGui::SameLine();
        changed |= ImGui::RadioButton("Fixed N",  &S.subMode, 2);
        if (S.subMode == 2) {
            ImGui::DragInt("Parts per edge", &S.subN, 0.1f, 1, 100);
            released |= ImGui::IsItemDeactivatedAfterEdit();
        }
        if (S.subN < 1) S.subN = 1;

        if (changed || released) S.MarkStageDirty(STAGE_SUBDIVIDE);
        if (!is3D && S.subAuto) { if (changed || released) S.RecomputeUpToAsync(scene, STAGE_SUBDIVIDE, true); }
        else if (ImGui::Button("Apply", {160, 0})) S.RecomputeUpToAsync(scene, STAGE_SUBDIVIDE, false);
        ImGui::Unindent();
        }
        ImGui::PopID();
    }

    if (!is3D) {
    {
        ImGui::PushID("off");
        if (ImGui::CollapsingHeader("Offsets", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::SmallButton("Reset##off")) S.ResetStage(scene, STAGE_OFFSETS);
        stageTime(S.OffsetsTimeMs());
        autoTag(&S.offAuto);

        ImGui::Indent();
        bool changed = false, released = false;
        DrawOffsetSpacing(S, changed, released);
        if (changed || released) S.MarkStageDirty(STAGE_OFFSETS);
        if (S.offAuto) { if (changed || released) S.RecomputeUpToAsync(scene, STAGE_OFFSETS, true); }
        else if (ImGui::Button("Apply", {160, 0})) S.RecomputeUpToAsync(scene, STAGE_OFFSETS, false);
        ImGui::Unindent();
        }
        ImGui::PopID();
    }

    {
        ImGui::PushID("mesh");
        if (ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::SmallButton("Reset##mesh")) S.ResetStage(scene, STAGE_MESH);
        stageTime(S.MeshTimeMs());
        autoTag(&S.meshAuto);

        ImGui::Indent();
        bool changed = false, released = false;
        // Only the free grid CDT mesher remains (SEM_STEINER_GRID).
        ImGui::DragFloat(MeshParamLabel(S.meshMethod), &S.meshParam, 0.5f, -1.0f, 5000.0f, "%.3f");
        released |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetItemTooltip("%s", MeshParamHelp(S.meshMethod));
        ImGui::SameLine();
        if (ImGui::SmallButton("auto")) { S.meshParam = -1.0f; changed = true; }

        ImGui::DragFloat("Steiner margin", &S.steinerMargin, 0.005f, 0.0f, 1.0f, "%.3f");
        released |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetItemTooltip("Minimum spacing from offset lines, in multiples of the grid\n"
                              "spacing. 0 or less => automatic (0.45).");

        if (changed || released) S.MarkStageDirty(STAGE_MESH);
        if (S.meshAuto) { if (changed || released) S.RecomputeUpToAsync(scene, STAGE_MESH, true); }
        else if (ImGui::Button("Apply", {160, 0})) S.RecomputeUpToAsync(scene, STAGE_MESH, false);
        ImGui::Unindent();
        }
        ImGui::PopID();
    }
    } else {
        DrawDomainSection(scene, S);
    }

    {
        // The thermal solve is part of this stage: Apply solves the field first
        // whenever it is stale (a new domain, or other BC levels), then extracts.
        ImGui::PushID("iso");
        if (ImGui::CollapsingHeader(is3D ? "Isosurface" : "Isoline", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::SmallButton("Reset##iso")) S.ResetStage(scene, STAGE_THERMAL);
        if (S.ThermalTimeMs() >= 0.0) {
            ImGui::SameLine();
            ImGui::TextDisabled("solve %.0f ms", S.ThermalTimeMs());
        }
        if (S.IsoTimeMs() >= 0.0) {
            ImGui::SameLine();
            ImGui::TextDisabled("extract %.0f ms", S.IsoTimeMs());
        }

        ImGui::Indent();
        if (is3D) {
            ImGui::DragFloat("Smoothing (0..1)", &S.isoValue, 0.005f, 0.0f, 1.0f, "%.3f");
            if (ImGui::IsItemDeactivatedAfterEdit()) S.MarkStageDirty(STAGE_ISOSURFACE);
            ImGui::SetItemTooltip("Level of the field the isosurface is taken at: 0 on From, 1 on To\n"
                                  "(the BC levels under Domain). With the shift on it is moved back\n"
                                  "onto From, so this sets how much the surface is smoothed, not\n"
                                  "where it lies. Changing it does not re-solve the field.");
            if (ImGui::Checkbox("Shift back along the gradient", &S.isoShift))
                S.MarkStageDirty(STAGE_ISOSURFACE);
            ImGui::SetItemTooltip("Before taking the isosurface, move every value of the field back down\n"
                                  "its gradient by smoothing x the gap between the From and To levels\n"
                                  "(the distance between them): the level then comes out about where\n"
                                  "From is, smoothed. Off: the plain isosurface, which lies between\n"
                                  "From and To.");
            double gap = 0.0;
            if (S.isoShift && S.IsoShiftGap(gap))
                ImGui::TextDisabled("Shift: %.3f x gap %.4g = %.4g", S.isoValue, gap, S.isoValue * gap);
        } else {
            ImGui::DragFloat("Isoline value (0..1)", &S.isoValue, 0.005f, 0.0f, 1.0f, "%.3f");
            if (ImGui::IsItemDeactivatedAfterEdit()) S.MarkStageDirty(STAGE_ISOSURFACE);
            ImGui::SetItemTooltip("Normalized temperature of the extracted isotherm. Changing it does NOT\n"
                                  "re-solve the thermal field — press Apply to re-extract the isotherm at\n"
                                  "the new temperature.");
        }
        if (S.isoValue < 0.0f) S.isoValue = 0.0f;
        if (S.isoValue > 1.0f) S.isoValue = 1.0f;

        // Apply solves the thermal field when it is stale and extracts the iso sheet
        // from it, both on the worker (long-running, progress-reporting SEM calls).
        if (ImGui::Button(is3D ? "Apply isosurface" : "Apply isoline", {160, 0}))
            S.RecomputeUpToAsync(scene, STAGE_ISOSURFACE, false);
        ImGui::SetItemTooltip(is3D
            ? "Solve steady-state heat conduction on the domain between the BC levels\n"
              "chosen under Domain (From held at T=0, To at T=1) when the field is\n"
              "stale - a new domain or other BC levels - then extract the isosurface\n"
              "at the smoothing above. Changing only the smoothing re-extracts\n"
              "without re-solving."
            : "Solve steady-state heat conduction on the band mesh (source T=1,\n"
              "farthest offset T=0) when the field is stale, then extract the\n"
              "isotherm at the chosen value.");

        // Mesh colouring: checked = the BCs (the fixed nodes only), unchecked = the
        // solved T field. Until the field is solved for the BC levels as they stand,
        // the domain shows its BCs either way.
        ImGui::BeginDisabled(!S.ThermalSolved());
        bool bc = S.bcView;
        if (ImGui::Checkbox("BC / T field", &bc)) S.SetBCView(scene, bc);
        ImGui::SetItemTooltip(is3D
            ? "How the domain is drawn (show it to see it). On: only the nodes the\n"
              "BC levels hold fixed and the edges between them - blue T=0, red T=1;\n"
              "the free nodes and every edge touching one are not drawn. Off: the\n"
              "solved temperature over every edge, blue (T=0) to red (T=1). Until\n"
              "the field is solved for the BC levels as they stand, the BCs are\n"
              "drawn either way."
            : "Mesh colouring after the solve (show the mesh to see it).\n"
              "Off: temperature field as a blue (T=0) to red (T=1) gradient.\n"
              "On: only the boundary-condition nodes are tinted — blue for\n"
              "T=0, red for T=1 — and every interior node is light grey.");
        ImGui::EndDisabled();
        ImGui::Unindent();
        }
        ImGui::PopID();
    }


    if (isBody) {
        DrawBodyPreformSection(scene, S);
        ImGui::EndDisabled();
    }
    } // end pipeline-stage stack (else of the standalone-mode branch)

    ImGui::EndDisabled();

    ImGui::Separator();
    // Cumulative compute time across the offsets, mesh and thermal stages. Always
    // shown; it grows as each stage is computed (0 ms until the first compute).
    ImGui::TextDisabled("Total compute: %.0f ms", S.TotalTimeMs());
    ImGui::TextDisabled("%s", S.status);

    ImGui::PopItemWidth();

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows))
        blockMousePick = true;
    ImGui::End();
}

}
