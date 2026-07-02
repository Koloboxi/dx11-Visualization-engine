#pragma once
#include "../imgui/imgui.h"
#include "../scene/scene.h"
#include "../../SEM/SemSession.h"
#include <cstdio>
#include <string>
#include <filesystem>
#include <commdlg.h>

namespace SEMWindow {

inline SemSessionNS::SemSession& Session() {
    static SemSessionNS::SemSession s;
    return s;
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

// Begin importing a source: if it already has sessions, defer to the choice
// modal; otherwise import straight into a fresh session.
inline void BeginSourceImport(Scene& scene, SemSessionNS::SemSession& S,
                              const std::string& path) {
    if (path.empty()) return;
    auto sessions = SemSessionNS::SemSession::ListSessions(path);
    if (sessions.empty()) { S.ImportSource(scene, path); return; }
    ImportChoice& ic = PendingImport();
    ic.path     = path;
    ic.sessions = std::move(sessions);
    ic.sel      = (int)ic.sessions.size();   // default to "[New session]"
    ic.open     = true;
}

// Render the session-choice modal. Call once per frame from the SEM window.
inline void DrawImportSessionModal(Scene& scene, SemSessionNS::SemSession& S) {
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
            S.ImportSource(scene, ic.path, ic.sessions[ic.sel]);
            S.LoadSessionStages(scene);
        } else {
            S.ImportSource(scene, ic.path);  // fresh session
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", {120, 0})) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
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
    // section is collapsed) so gizmo edits keep applying.
    S.AutoApplyClipPlanes(scene);
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
                              "stages have been extracted.");
    }

    ImGui::Unindent();
    ImGui::PopID();
}

inline void Draw(Scene& scene, bool& blockMousePick) {
    using namespace SemSessionNS;
    SemSession& S = Session();

    S.Validate(scene);
    // Don't re-bind to a newly staged primitive while a background computation
    // is in flight — that would reset the SEM cache the worker is using.
    if (!S.AsyncRunning()) S.Bind(scene, scene.stagedPrimitive);
    // Apply any finished background-pipeline results to the scene (main thread).
    S.PollAsync(scene);

    ImGui::Begin("SEM");

    if (!S.HasSource()) {
        if (S.SourcePrim()) ImGui::TextDisabled("Staged primitive is not a SEM contour.");
        else                ImGui::TextDisabled("No staged contour.");
        ImGui::Spacing();
        if (ImGui::Button("Import CSV3D...", {200, 0}))
            BeginSourceImport(scene, S, BrowseCsv3dFile());
        ImGui::SetItemTooltip("Browse for a .csv3d contour, add it to the scene and stage it\n"
                              "as the source of the SEM pipeline.");
        DrawImportSessionModal(scene, S);
        ImGui::Spacing();
        if (scene.stagingEnabled) {
            ImGui::TextWrapped("Double-click a CSV3D contour in the tree to stage it here.");
            ImGui::TextWrapped("Double-click empty 3D space to unstage.");
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

    // While the 3D pipeline runs on its worker thread, show a progress bar
    // (driven by SEM_GetProgress via the session) and disable every control
    // so parameters can't change mid-computation.
    const bool busy = S.AsyncRunning();
    if (busy) {
        char overlay[32];
        snprintf(overlay, sizeof(overlay), "%.0f%%", S.AsyncProgress() * 100.0f);
        ImGui::Text("%s...", S.AsyncStageName());
        ImGui::ProgressBar(S.AsyncProgress(), ImVec2(kItemW, 0.0f), overlay);
        if (S.AsyncCancelRequested()) {
            ImGui::TextDisabled("Cancelling after current stage...");
        } else if (ImGui::Button("Cancel", {kItemW, 0})) {
            S.CancelAsync();
        }
        ImGui::SetItemTooltip("Stop the 3D pipeline: the stage running now finishes (SEM\n"
                              "calls can't be interrupted), then every queued stage after\n"
                              "it is skipped. Stages that already completed are kept.");
        ImGui::Separator();
    }
    ImGui::BeginDisabled(busy);

    if (ImGui::Button("Import CSV3D...", {kItemW, 0}))
        BeginSourceImport(scene, S, BrowseCsv3dFile());
    ImGui::SetItemTooltip("Browse for a .csv3d contour, add it to the scene and stage it\n"
                          "as the source of the SEM pipeline (replaces the current source).\n"
                          "If the source has saved sessions you can reload one instead.");
    DrawImportSessionModal(scene, S);
    ImGui::Separator();

    const bool is3D = S.Dim() == 3;
    if (is3D) ImGui::TextDisabled("Mode: 3D surface (tetrahedral pipeline)");
    else      ImGui::TextDisabled("Mode: 2D contour");

    DrawReadout(S);

    {
        ImGui::PushID("srcsurf");
        if (ImGui::CollapsingHeader("Source surface", ImGuiTreeNodeFlags_DefaultOpen)) {
            // Show/hide the source surface on its own, without touching the pipeline
            // products parented under it (offsets, mesh, isosurface). Independent of
            // the tree eye, whose toggle cascades to the whole subtree.
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

    if (!is3D) DrawRevolutionSection(scene, S);
    else       DrawClipPlanesSection(scene, S);

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

    {
        ImGui::PushID("off");
        if (ImGui::CollapsingHeader("Offsets", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::SmallButton("Reset##off")) S.ResetStage(scene, STAGE_OFFSETS);
        stageTime(S.OffsetsTimeMs());
        autoTag(&S.offAuto);

        ImGui::Indent();
        bool changed = false, released = false;
        const char* modes = "Even spacing\0Custom gaps\0";
        changed |= ImGui::Combo("Spacing", &S.offsetMode, modes);

        if (S.offsetMode == OFFSET_EVEN) {
            ImGui::DragFloat("First gap", &S.firstGap, 0.05f, -1000.0f, 1000.0f, "%.3f");
            released |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SetItemTooltip("Size of the first gap, in multiples of the source's\n"
                                  "mean edge length (1 = one mean edge length).\n"
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

        if (changed || released) S.MarkStageDirty(STAGE_OFFSETS);
        if (!is3D && S.offAuto) { if (changed || released) S.RecomputeUpToAsync(scene, STAGE_OFFSETS, true); }
        else if (ImGui::Button("Apply", {160, 0})) S.RecomputeUpToAsync(scene, STAGE_OFFSETS, false);

        // Clip-plane geometry changes logged for the OFFSET shells: the translucent
        // red geometry each shell's half-space cut removed (offset shells are not
        // snapped, so there are no snap segments here).
        if (is3D) {
            bool offChg = S.ClipChangesShown(SemSessionNS::CLIPCHG_OFFSETS);
            if (ImGui::Checkbox("Offset clip changes", &offChg))
                S.ShowClipChanges(scene, SemSessionNS::CLIPCHG_OFFSETS, offChg);
            ImGui::SetItemTooltip("Show the geometry the clip planes cut away from the offset shells,\n"
                                  "as a translucent red surface. Needs clip planes set.");
        }
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
        if (is3D) {
            const char* tetItems = "Band\0" "Layered\0";
            if (ImGui::Combo("Tet method", &S.tetMethod, tetItems)) {
                S.tetParam = -1.0f;
                changed = true;
            }
            if (S.tetMethod == SEM_TET_BAND) {
                ImGui::DragFloat(TetParamLabel(S.tetMethod), &S.tetParam, 0.5f, -1.0f, 1e7f, "%.3f");
                released |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::SetItemTooltip("%s", TetParamHelp(S.tetMethod));
                ImGui::SameLine();
                if (ImGui::SmallButton("auto")) { S.tetParam = -1.0f; changed = true; }

                // BAND's knob is the Steiner grid cell size (a length), so it can
                // be expressed in multiples of the source surface's mean edge length.
                ImGui::SameLine();
                const char* ulabel = !S.tetParamEdgeUnits ? "units: model" : "units: x edge";
                if (ImGui::SmallButton(ulabel)) {
                    double f = S.TetParamFactor();
                    if (S.tetParam > 0.0f && f > 0.0) {
                        if (!S.tetParamEdgeUnits) S.tetParam = (float)(S.tetParam / f);
                        else                      S.tetParam = (float)(S.tetParam * f);
                    }
                    S.tetParamEdgeUnits = !S.tetParamEdgeUnits;
                }
                ImGui::SetItemTooltip("Toggle the cell size between model units and multiples of\n"
                                      "the source surface's mean edge length. The value is\n"
                                      "converted so meshing is unaffected.");
            } else { // SEM_TET_LAYERED: the knob is a use_sdf flag (0/1).
                bool useSdf = (S.tetParam > 0.5f);
                if (ImGui::Checkbox(TetParamLabel(S.tetMethod), &useSdf)) {
                    S.tetParam = useSdf ? 1.0f : 0.0f;
                    changed = true;
                }
                ImGui::SetItemTooltip("%s", TetParamHelp(S.tetMethod));
            }
        } else {
        // Only the free grid CDT mesher remains (SEM_STEINER_GRID).
        ImGui::DragFloat(MeshParamLabel(S.meshMethod), &S.meshParam, 0.5f, -1.0f, 5000.0f, "%.3f");
        released |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetItemTooltip("%s", MeshParamHelp(S.meshMethod));
        ImGui::SameLine();
        if (ImGui::SmallButton("auto")) { S.meshParam = -1.0f; changed = true; }

        // The grid spacing is a length, so it can be expressed in multiples of
        // the source contour's mean edge length.
        ImGui::SameLine();
        const char* ulabel = !S.meshParamEdgeUnits ? "units: model" : "units: x edge";
        if (ImGui::SmallButton(ulabel)) {
            double f = S.MeshParamFactor();
            if (S.meshParam > 0.0f && f > 0.0) {
                if (!S.meshParamEdgeUnits) S.meshParam = (float)(S.meshParam / f);
                else                       S.meshParam = (float)(S.meshParam * f);
            }
            S.meshParamEdgeUnits = !S.meshParamEdgeUnits;
        }
        ImGui::SetItemTooltip("Toggle the spacing between model units and multiples of the\n"
                              "source contour's mean edge length. The value is converted so\n"
                              "meshing is unaffected.");

        ImGui::DragFloat("Steiner margin", &S.steinerMargin, 0.005f, 0.0f, 1.0f, "%.3f");
        released |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetItemTooltip("Minimum spacing from offset lines, in multiples of the grid\n"
                              "spacing. 0 or less => automatic (0.45).");
        } // end 2D grid mesh UI

        // Max tet edge length filter (3D only), in multiples of the source
        // surface's mean edge length. Sits just above Apply so the meshing
        // parameters read top to bottom. <= 0 disables the filter.
        if (is3D) {
            ImGui::DragFloat("Max edge (x edge, 0=off)", &S.tetMaxEdgeLen, 0.05f, 0.0f, 100.0f, "%.3f");
            released |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SetItemTooltip("After meshing, remove any tet with an edge longer than this,\n"
                                  "measured in multiples of the source surface's mean edge length.\n"
                                  "Vertices are kept. 0 or less disables the filter.");
            if (S.tetMaxEdgeLen < 0.0f) S.tetMaxEdgeLen = 0.0f;
        }

        if (changed || released) S.MarkStageDirty(STAGE_MESH);
        if (!is3D && S.meshAuto) { if (changed || released) S.RecomputeUpToAsync(scene, STAGE_MESH, true); }
        else if (ImGui::Button("Apply", {160, 0})) S.RecomputeUpToAsync(scene, STAGE_MESH, false);
        ImGui::Unindent();
        }
        ImGui::PopID();
    }

    {
        ImGui::PushID("thermal");
        if (ImGui::CollapsingHeader("Thermal solve", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::SmallButton("Reset##thermal")) S.ResetStage(scene, STAGE_THERMAL);
        stageTime(S.ThermalTimeMs());

        ImGui::Indent();
        // SEM_SolveThermal3D knob (3D only — the 2D SEM_SolveThermal takes no
        // parameters). max_inward filters the outermost T=0 nodes by the source
        // signed distance. It sits ABOVE Apply so the solve params read top to
        // bottom.
        if (is3D) {
            ImGui::DragFloat("Max inward (0..1)", &S.maxInward, 0.005f, 0.0f, 1.0f, "%.3f");
            if (ImGui::IsItemDeactivatedAfterEdit()) S.MarkStageDirty(STAGE_THERMAL);
            ImGui::SetItemTooltip("Maximum relative depth (fraction of the outer extent) an\n"
                                  "outermost node may sit inward of the true outer extent and\n"
                                  "still be kept as a T=0 node. Deeper nodes are dropped.");
            if (S.maxInward < 0.0f) S.maxInward = 0.0f;
            if (S.maxInward > 1.0f) S.maxInward = 1.0f;
        }

        // Apply solves the steady-state field only; the isosurface is extracted by
        // the separate stage below.
        if (ImGui::Button("Apply", {160, 0})) S.RecomputeUpToAsync(scene, STAGE_THERMAL, false);
        ImGui::SetItemTooltip(is3D
            ? "Solve steady-state heat conduction on the tetrahedral band mesh\n"
              "(source surface T=1, farthest shell T=0) and recolour the mesh by the\n"
              "solved field. Does not extract the isosurface."
            : "Solve steady-state heat conduction on the band mesh (source T=1,\n"
              "farthest offset T=0) and recolour the mesh by the solved field.");

        // Mesh colouring toggle (only meaningful once the field is solved):
        // unchecked = T-field gradient (blue..red), checked = BC view (blue T=0,
        // red T=1, light grey everywhere else).
        ImGui::BeginDisabled(!S.ThermalSolved());
        bool bc = S.bcView;
        if (ImGui::Checkbox("BC / T field", &bc)) S.SetBCView(scene, bc);
        ImGui::SetItemTooltip("Mesh colouring after the solve.\n"
                              "Off: temperature field as a blue (T=0) to red (T=1) gradient.\n"
                              "On: only the boundary-condition nodes are tinted — blue for\n"
                              "T=0, red for T=1 — and every interior node is light grey.");
        ImGui::EndDisabled();
        ImGui::Unindent();
        }
        ImGui::PopID();
    }

    {
        ImGui::PushID("iso");
        if (ImGui::CollapsingHeader(is3D ? "Isosurface" : "Isoline", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::SmallButton("Reset##iso")) S.ResetStage(scene, STAGE_ISOSURFACE);
        stageTime(S.IsoTimeMs());

        ImGui::Indent();
        ImGui::DragFloat(is3D ? "Isosurface value (0..1)" : "Isoline value (0..1)",
                         &S.isoValue, 0.005f, 0.0f, 1.0f, "%.3f");
        if (ImGui::IsItemDeactivatedAfterEdit()) S.MarkStageDirty(STAGE_ISOSURFACE);
        ImGui::SetItemTooltip("Normalized temperature of the extracted %s. Changing it does NOT\n"
                              "re-solve the thermal field — press Apply to re-extract the %s at\n"
                              "the new temperature.",
                              is3D ? "isosurface" : "isotherm",
                              is3D ? "isosurface" : "isotherm");
        if (S.isoValue < 0.0f) S.isoValue = 0.0f;
        if (S.isoValue > 1.0f) S.isoValue = 1.0f;

        // Offset-and-remesh mode (3D only). "None" extracts the open/closed iso
        // sheet as-is; picking an axis shifts an open sheet back toward the source
        // and re-triangulates it in the plane perpendicular to that axis.
        if (is3D) {
            const char* axisItems[] = { "None (plain)", "X", "Y", "Z" };
            if (ImGui::Combo("Iso offset axis", &S.isoAxis, axisItems, IM_ARRAYSIZE(axisItems)))
                S.MarkStageDirty(STAGE_ISOSURFACE);
            ImGui::SetItemTooltip("None: extract the isosurface as-is (clip planes only).\n"
                                  "X/Y/Z: shift an OPEN iso sheet back toward the source and\n"
                                  "re-triangulate it in the plane perpendicular to the chosen axis.\n"
                                  "Requires offsets and an open isosurface.");
            if (S.isoAxis < 0) S.isoAxis = 0;
            if (S.isoAxis > 3) S.isoAxis = 3;

            ImGui::BeginDisabled(S.isoAxis == 0);
            // v_min == v_max => ImGui applies no clamp: the shift is a signed
            // multiple of the source's mean edge length, with no range limit.
            ImGui::DragFloat("Iso offset (x edge)", &S.isoOffsetValue, 0.05f, 0.0f, 0.0f, "%.3f");
            if (ImGui::IsItemDeactivatedAfterEdit()) S.MarkStageDirty(STAGE_ISOSURFACE);
            ImGui::SetItemTooltip("Shift of the iso sheet along its pseudonormals before\n"
                                  "re-triangulation, in multiples of the source surface's mean\n"
                                  "edge length. Positive shifts inward (toward the source),\n"
                                  "negative outward. Ignored when the axis is None.");

            // Minimum clearance a shifted vertex must keep from the iso before it is
            // pruned, in the same edge-length units as the shift. v_min == v_max =>
            // no ImGui clamp (the meaningful range is [0, |Iso offset|]).
            ImGui::DragFloat("Iso min offset (x edge)", &S.isoMinOffsetValue, 0.05f, 0.0f, 0.0f, "%.3f");
            if (ImGui::IsItemDeactivatedAfterEdit()) S.MarkStageDirty(STAGE_ISOSURFACE);
            ImGui::SetItemTooltip("Minimum clearance a shifted vertex must keep from the iso before\n"
                                  "it is pruned, in multiples of the source's mean edge length (same\n"
                                  "unit as the shift). With c = (min offset) / (iso offset):\n"
                                  "  c = 1 (min == shift): fold cull - drop every concave fold that\n"
                                  "    folded the sheet onto itself.\n"
                                  "  c = 0 (min == 0): signed-crossing cull - keep the folds and drop\n"
                                  "    only vertices that crossed THROUGH the iso to the wrong side.");

            // Re-meshing strategy of the offset-remesh (SEM_IsoRemeshMode). Only
            // matters once an offset axis is chosen, hence inside the same disabled
            // block. Changing it re-extracts on the next Apply.
            const char* modeItems = "Height field (single CDT)\0Top-down sweep\0";
            if (ImGui::Combo("Iso remesh mode", &S.isoMode, modeItems))
                S.MarkStageDirty(STAGE_ISOSURFACE);
            ImGui::SetItemTooltip("How the offset iso point cloud is re-triangulated:\n"
                                  "Height field: one constrained Delaunay of the whole projection\n"
                                  "  using the iso boundary loops as constraints; needs a single-valued\n"
                                  "  projection (a fold is rejected).\n"
                                  "Top-down sweep: sweep the points from the top downward in height\n"
                                  "  bands, locking each higher band's edges as Delaunay constraints\n"
                                  "  before the lower points fill in. Tolerates mild folds; ignores the\n"
                                  "  boundary loops, so it fills the convex projected footprint.");
            if (S.isoMode < 0) S.isoMode = 0;
            if (S.isoMode > 1) S.isoMode = 1;

            // Sweep pass direction — only meaningful in the top-down sweep mode.
            ImGui::BeginDisabled(S.isoMode != SEM_REMESH_TOPDOWN_SWEEP);
            const char* dirItems = "Top-down\0Bottom-up\0";
            if (ImGui::Combo("Sweep direction", &S.isoSweepDir, dirItems))
                S.MarkStageDirty(STAGE_ISOSURFACE);
            ImGui::SetItemTooltip("Which end of the offset axis the height-band sweep starts from:\n"
                                  "Top-down: start at the largest coordinate along the axis and\n"
                                  "  descend; a fold collapses to the topmost sheet.\n"
                                  "Bottom-up: start at the smallest coordinate and ascend; a fold\n"
                                  "  collapses to the bottom-most sheet and its connectivity locks first.");
            if (S.isoSweepDir < 0) S.isoSweepDir = 0;
            if (S.isoSweepDir > 1) S.isoSweepDir = 1;
            ImGui::EndDisabled();
            ImGui::EndDisabled();

            // Uneven-winding highlight: colour the minority-oriented triangles pure
            // red. Applied instantly to the already-extracted surface (no re-extract).
            bool win = S.isoShowWinding;
            if (ImGui::Checkbox("Highlight uneven winding", &win))
                S.SetIsoWinding(scene, win);
            ImGui::SetItemTooltip("When the isosurface winding is uneven (not every shared edge\n"
                                  "runs opposite ways in its two triangles), paint the minority-\n"
                                  "oriented triangles pure red and keep the majority green.\n"
                                  "Nothing turns red when the winding is uniform.");
        }

        // Apply extracts the iso sheet from the solved field. It runs on the worker
        // (it is now a long-running, progress-reporting SEM call) and solves the
        // thermal field first if it is still stale.
        if (ImGui::Button(is3D ? "Apply isosurface" : "Apply isoline", {160, 0}))
            S.RecomputeUpToAsync(scene, STAGE_ISOSURFACE, false);
        ImGui::SetItemTooltip(is3D
            ? "Extract the isosurface at the chosen value from the solved field\n"
              "(applying the offset/remesh axis and shift). Solves the thermal\n"
              "field first if it is not yet solved."
            : "Extract the isotherm at the chosen value from the solved field.\n"
              "Solves the thermal field first if it is not yet solved.");

        if (is3D) {
            // Angle-weighted vertex pseudonormals of the extracted isosurface,
            // drawn as short cyan segments. Needs an extracted surface.
            ImGui::BeginDisabled(!S.HasIsolinePath());
            bool isoNrm = S.IsoNormalsPrim() != nullptr;
            if (ImGui::Checkbox("Isosurface pseudonormals", &isoNrm))
                S.ShowIsoPseudonormals(scene, isoNrm, false);
            ImGui::SetItemTooltip("Draw the angle-weighted vertex pseudonormals of the extracted\n"
                                  "isosurface as short cyan line segments.");
            ImGui::EndDisabled();

            // Intermediate stages of an offset-and-remesh extraction. Available
            // only after extracting with an offset axis; fetched from the cache
            // on demand (not serialized), so they go stale on the next extraction.
            ImGui::BeginDisabled(!S.HasIsoProjection());
            bool srcShown = S.IsoSourcePrim() != nullptr;
            if (ImGui::Checkbox("Show source isosurface", &srcShown))
                S.ShowSourceIsosurface3D(scene, srcShown, false);
            ImGui::SetItemTooltip("Show the original extracted iso sheet, before the offset/remesh\n"
                                  "stage (oriented outward). Requires an offset-axis extraction.");
            // Pseudonormals of that orange source iso sheet — the directions the
            // offset-and-remesh shifted each vertex along. Same gate as above.
            bool srcNrm = S.IsoSrcNormalsPrim() != nullptr;
            if (ImGui::Checkbox("Source isosurface pseudonormals", &srcNrm))
                S.ShowSourceIsoPseudonormals(scene, srcNrm, false);
            ImGui::SetItemTooltip("Draw the angle-weighted vertex pseudonormals of the pre-offset\n"
                                  "source isosurface (the orange sheet) as short orange line segments —\n"
                                  "the directions the offset/remesh shifted each iso vertex along.");
            bool projShown = S.IsoProjectionPrim() != nullptr;
            if (ImGui::Checkbox("Show iso projection", &projShown))
                S.ShowIsosurfaceProjection3D(scene, projShown, false);
            ImGui::SetItemTooltip("Show the flat base-plane re-meshed projection (with its\n"
                                  "wireframe edges) built when extracting with an offset axis.");

            // Boundary loops of the source isosurface used as the offset-remesh CDT
            // constraints (SEM_GetIsosurfaceLoops3D), drawn as a thick magenta
            // wireframe. Same offset-axis gate as the stages above.
            bool loopsShown = S.IsoLoopsPrim() != nullptr;
            if (ImGui::Checkbox("Show isosurface loops", &loopsShown))
                S.ShowIsosurfaceLoops3D(scene, loopsShown, false);
            ImGui::SetItemTooltip("Draw the constrained-CDT boundary loops of the source isosurface\n"
                                  "(the re-triangulation constraints of the offset/remesh) as a thick\n"
                                  "magenta line wireframe. Requires an offset-axis extraction.");
            ImGui::EndDisabled();

            // Clip-plane geometry changes logged for the ISOSURFACE restriction and
            // the final remesh cut: yellow snap segments and the translucent red
            // clipped-away/dropped-coplanar geometry. Independent of the offset-axis
            // gate above — a plain (axis = None) extraction still snaps and cuts.
            bool isoChg = S.ClipChangesShown(SemSessionNS::CLIPCHG_ISO);
            if (ImGui::Checkbox("Isosurface clip changes", &isoChg))
                S.ShowClipChanges(scene, SemSessionNS::CLIPCHG_ISO, isoChg);
            ImGui::SetItemTooltip("Show what the clip planes did to the isosurface (and the final\n"
                                  "offset-remesh sheet): the snap displacements as short yellow\n"
                                  "segments and the clipped-away / dropped coplanar geometry as a\n"
                                  "translucent red surface. Needs clip planes set.");
        }
        ImGui::Unindent();
        }
        ImGui::PopID();
    }

    // Final remesh: an optional cleanup pass over the offset-remeshed isosurface that
    // splits the tall, narrow triangles a height-field re-mesh leaves on a near-vertical
    // wall. Runs standalone on the cached offset-remesh result (no re-extract), so it is
    // 3D-only and needs an offset-axis extraction to have produced that result.
    if (is3D) {
        ImGui::PushID("isofinal");
        if (ImGui::CollapsingHeader("Final remesh")) {
        ImGui::Indent();
        ImGui::BeginDisabled(!S.HasIsoProjection());

        ImGui::DragFloat("Target edge (x sheet edge)", &S.isoFinalTargetMult, 0.02f, 0.0f, 0.0f, "%.3f");
        ImGui::SetItemTooltip("Target triangle edge length for the final remesh, in multiples of the\n"
                              "offset-remeshed sheet's own average edge length. Long edges are split\n"
                              "toward this size and short ones collapsed. 0 uses the median edge length.");
        if (S.isoFinalTargetMult < 0.0f) S.isoFinalTargetMult = 0.0f;

        ImGui::DragInt("Iterations", &S.isoFinalIters, 0.1f, 1, 20);
        ImGui::SetItemTooltip("Number of collapse / split / relax sweeps. More iterations even out the\n"
                              "distribution further at some cost.");
        if (S.isoFinalIters < 1)  S.isoFinalIters = 1;
        if (S.isoFinalIters > 50) S.isoFinalIters = 50;

        if (ImGui::Button("Apply final remesh", {160, 0}))
            S.ApplyIsoFinalRemeshAsync(scene, false);
        ImGui::SetItemTooltip("Re-mesh the offset-remeshed isosurface in place: split the long wall\n"
                              "triangles, collapse the short edges and relax. Operates on the last\n"
                              "offset-remesh result (does NOT re-extract). Requires an offset-axis\n"
                              "isosurface extraction first.");

        ImGui::EndDisabled();
        ImGui::Unindent();
        }
        ImGui::PopID();
    }

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
