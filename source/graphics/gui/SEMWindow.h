#pragma once
#include "../imgui/imgui.h"
#include "../scene/scene.h"
#include "../scene/SemSession.h"
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
// normal + d show in the transform window). "Apply clip planes" reads the planes
// off the rectangles and pushes them to the mesher (SEM_SetClipPlanes3D).
inline void DrawClipPlanesSection(Scene& scene, SemSessionNS::SemSession& S) {
    ImGui::PushID("clip");
    ImGui::Text("Clip planes");
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) S.ClearClipPlanes3D(scene);
    ImGui::SetItemTooltip("Half-space clips applied during 3D meshing. The normal points\n"
                          "into the kept region (shown soft red); the removed side is soft blue.\n"
                          "Select a plane rectangle in the scene and move/rotate it with the\n"
                          "gizmo (or edit normal/d in the transform window), then press Apply.");

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
    ImGui::SameLine();
    if (ImGui::Button("Apply clip planes")) S.SetClipPlanes3D(scene);
    ImGui::SetItemTooltip("Read the planes off the rectangles and send them to the mesher\n"
                          "(SEM_SetClipPlanes3D); rebuilds the mesh on the next Mesh/Thermal Apply.");

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
    ImGui::Separator();

    if (!is3D) {
        DrawRevolutionSection(scene, S);
        ImGui::Separator();
    }
    else {
        float a = S.surf3dAlpha;
        if (ImGui::DragFloat("Surface opacity", &a, 0.005f, 0.05f, 1.0f, "%.2f"))
            S.SetSurf3dAlpha(scene, a);
        ImGui::SetItemTooltip("Opacity of the 3D pipeline surfaces (source, offsets, mesh,\n"
                              "isosurface). Back-face culling is set globally via the\n"
                              "NavCube no-cull toggle.");
        ImGui::Separator();
        DrawClipPlanesSection(scene, S);
        ImGui::Separator();
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

    {
        ImGui::PushID("sub");
        ImGui::Text("Subdivide");
        autoTag(&S.subAuto);

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
        ImGui::PopID();
    }

    ImGui::Separator();

    {
        ImGui::PushID("off");
        ImGui::Text("Offsets");
        autoTag(&S.offAuto);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##off")) S.ResetStage(scene, STAGE_OFFSETS);
        stageTime(S.OffsetsTimeMs());

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
        ImGui::Unindent();
        ImGui::PopID();
    }

    ImGui::Separator();

    {
        ImGui::PushID("mesh");
        ImGui::Text("Mesh");
        autoTag(&S.meshAuto);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##mesh")) S.ResetStage(scene, STAGE_MESH);
        stageTime(S.MeshTimeMs());

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

                // layer_span carves the layered build by layer adjacency. The DLL
                // consults it ONLY in the SDF-free LAYERED path (ignored by BAND
                // and by LAYERED with use_sdf=1), so expose it only there.
                if (!useSdf) {
                    ImGui::DragInt("Layer span", &S.tetLayerSpan, 0.1f, 1, 16);
                    released |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::SetItemTooltip("Keep a tet when the spread of its vertices' layer indices\n"
                                          "(max - min) is <= this. 1 keeps only strictly adjacent\n"
                                          "layers; larger values bridge layer gaps to close holes at\n"
                                          "the cost of admitting some longer bridging tets.");
                    if (S.tetLayerSpan < 1) S.tetLayerSpan = 1;
                }
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
        ImGui::PopID();
    }

    ImGui::Separator();

    {
        ImGui::PushID("thermal");
        ImGui::Text("Thermal solve");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##thermal")) S.ResetStage(scene, STAGE_THERMAL);
        stageTime(S.ThermalTimeMs());

        ImGui::Indent();
        // SEM_SolveThermal3D knobs (3D only — the 2D SEM_SolveThermal takes no
        // parameters). use_source_sdf selects how the outermost T=0 nodes are
        // filtered; max_inward only has an effect when use_source_sdf is on, so it
        // is hidden otherwise. Both sit ABOVE Apply so the solve params read top
        // to bottom.
        if (is3D) {
            bool useSdf = (S.useSourceSdf != 0);
            if (ImGui::Checkbox("Use source SDF", &useSdf))
                S.useSourceSdf = useSdf ? 1 : 0;
            ImGui::SetItemTooltip("Off: keep every outermost-offset node as a T=0 boundary by tag\n"
                                  "alone (Max inward ignored; robust where the signed distance\n"
                                  "misbehaves).\n"
                                  "On: evaluate the source signed distance on those nodes and drop\n"
                                  "self-intersecting ones using Max inward.");
            if (useSdf) {
                ImGui::DragFloat("Max inward (0..1)", &S.maxInward, 0.005f, 0.0f, 1.0f, "%.3f");
                ImGui::SetItemTooltip("Maximum relative depth (fraction of the outer extent) an\n"
                                      "outermost node may sit inward of the true outer extent and\n"
                                      "still be kept as a T=0 node. Deeper nodes are dropped.");
                if (S.maxInward < 0.0f) S.maxInward = 0.0f;
                if (S.maxInward > 1.0f) S.maxInward = 1.0f;
            }
        }

        // Apply solves the steady-state field; it sits ABOVE the iso value so the
        // (expensive) solve and the (cheap) iso extraction read top to bottom.
        if (ImGui::Button("Apply", {160, 0})) S.RecomputeUpToAsync(scene, STAGE_THERMAL, false);
        ImGui::SetItemTooltip(is3D
            ? "Solve steady-state heat conduction on the tetrahedral band mesh\n"
              "(source surface T=1, farthest shell T=0), then extract the isosurface."
            : "Solve steady-state heat conduction on the band mesh (source T=1,\n"
              "farthest offset T=0), then extract the isotherm.");

        ImGui::DragFloat(is3D ? "Isosurface value (0..1)" : "Isoline value (0..1)",
                         &S.isoValue, 0.005f, 0.0f, 1.0f, "%.3f");
        ImGui::SetItemTooltip("Normalized temperature of the extracted %s. Changing it does NOT\n"
                              "re-solve the thermal field — press the button below to re-extract\n"
                              "the %s at the new temperature.",
                              is3D ? "isosurface" : "isotherm",
                              is3D ? "isosurface" : "isotherm");
        if (S.isoValue < 0.0f) S.isoValue = 0.0f;
        if (S.isoValue > 1.0f) S.isoValue = 1.0f;

        // The iso value only selects a level set of the already-solved field, so
        // applying it re-extracts the isotherm/isosurface in place without
        // touching the thermal solve.
        ImGui::BeginDisabled(!S.ThermalSolved());
        if (ImGui::Button(is3D ? "Apply isosurface" : "Apply isoline", {160, 0}))
            S.ApplyIsoline(scene, true);

        // Mesh colouring toggle (only meaningful once the field is solved):
        // unchecked = T-field gradient (blue..red), checked = BC view (blue T=0,
        // red T=1, light grey everywhere else).
        bool bc = S.bcView;
        if (ImGui::Checkbox("BC / T field", &bc)) S.SetBCView(scene, bc);
        ImGui::SetItemTooltip("Mesh colouring after the solve.\n"
                              "Off: temperature field as a blue (T=0) to red (T=1) gradient.\n"
                              "On: only the boundary-condition nodes are tinted — blue for\n"
                              "T=0, red for T=1 — and every interior node is light grey.");
        ImGui::EndDisabled();
        ImGui::Unindent();
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
