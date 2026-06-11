#pragma once
#include "../imgui/imgui.h"
#include "../scene/scene.h"
#include "SemSession.h"
#include <cstdio>
#include <string>
#include <commdlg.h>

namespace SEMWindow {

inline SemSessionNS::SemSession& Session() {
    static SemSessionNS::SemSession s;
    return s;
}

inline std::string BrowseCsv3dFile() {
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string s(exe);
    auto pos = s.find_last_of("\\/");
    std::string dataDir = (pos != std::string::npos ? s.substr(0, pos) : s) + "\\Data";

    char fileBuf[MAX_PATH] = {};
    OPENFILENAMEA ofn   = {};
    ofn.lStructSize     = sizeof(ofn);
    ofn.lpstrFilter     = "CSV3D contour (*.csv3d)\0*.csv3d\0All files (*.*)\0*.*\0";
    ofn.lpstrFile       = fileBuf;
    ofn.nMaxFile        = MAX_PATH;
    ofn.lpstrInitialDir = dataDir.c_str();
    ofn.lpstrTitle      = "Import CSV3D contour";
    ofn.Flags           = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn)) return std::string(fileBuf);
    return {};
}

inline const char* MeshParamLabel(int m) {
    switch (m) {
        case SEM_STEINER_GRID:       return "Grid spacing";
        case SEM_STEINER_NONE:       return "(no parameter)";
        case SEM_STEINER_MIN_ANGLE:  return "Min angle (deg)";
        case SEM_STEINER_MAX_AREA:   return "Max triangle area";
        case SEM_STEINER_CONFORMING: return "Min angle (deg, 0=off)";
        case SEM_STEINER_SIZING:     return "Max edge length";
        default:                     return "Parameter";
    }
}

inline const char* MeshParamHelp(int m) {
    switch (m) {
        case SEM_STEINER_GRID:       return "Regular grid of interior points + CDT. Negative => avg edge length.";
        case SEM_STEINER_NONE:       return "Constrained Delaunay, no interior points. Parameter ignored.";
        case SEM_STEINER_MIN_ANGLE:  return "Triangle -q: Ruppert refinement. Typical 20..33 deg. Negative => default.";
        case SEM_STEINER_MAX_AREA:   return "Triangle -a: bounded triangle area. Negative => default.";
        case SEM_STEINER_CONFORMING: return "Triangle -D: conforming Delaunay. 0 => conforming only.";
        case SEM_STEINER_SIZING:     return "Triangle -u: bounded longest edge. Negative => default.";
        default:                     return "";
    }
}

inline const char* TetParamLabel(int m) {
    switch (m) {
        case SEM_TET_QUALITY: return "Radius-edge (-q)";
        case SEM_TET_NONE:    return "(no parameter)";
        case SEM_TET_MAX_VOL: return "Max tet volume";
        case SEM_TET_SIZING:  return "Max edge length";
        default:              return "Parameter";
    }
}

inline const char* TetParamHelp(int m) {
    switch (m) {
        case SEM_TET_QUALITY: return "TetGen -q: radius-edge quality bound. Lower => better-shaped\n"
                                     "tetrahedra but more of them. Typical 1.4..2.0. Negative => default (~2.0).";
        case SEM_TET_NONE:    return "-p only: conforming Delaunay, no size/quality refinement. Parameter ignored.";
        case SEM_TET_MAX_VOL: return "TetGen -a: bounded tetrahedron volume, in model units^3.\n"
                                     "Negative => automatic (~avg_edge^3/6).";
        case SEM_TET_SIZING:  return "TetGen -a derived from a target edge length. Negative => default.";
        default:              return "";
    }
}

inline void DrawReadout(SemSessionNS::SemSession& S) {
    using namespace SemSessionNS;
    const Stats* st  = nullptr;
    const char*  tag = "Source";
    if      (S.MeshPrim()    && S.MeshStats().valid) { st = &S.MeshStats(); tag = "Mesh";    }
    else if (S.OffsetsPrim() && S.OffStats().valid)  { st = &S.OffStats();  tag = "Offsets"; }
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

inline void Draw(Scene& scene, bool& blockMousePick) {
    using namespace SemSessionNS;
    SemSession& S = Session();

    S.Validate(scene);
    S.Bind(scene, scene.stagedPrimitive);

    ImGui::Begin("SEM");

    if (!S.HasSource()) {
        if (S.SourcePrim()) ImGui::TextDisabled("Staged primitive is not a SEM contour.");
        else                ImGui::TextDisabled("No staged contour.");
        ImGui::Spacing();
        if (ImGui::Button("Import CSV3D...", {200, 0})) {
            std::string p = BrowseCsv3dFile();
            if (!p.empty()) S.ImportSource(scene, p);
        }
        ImGui::SetItemTooltip("Browse for a .csv3d contour, add it to the scene and stage it\n"
                              "as the source of the SEM pipeline.");
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

    if (ImGui::Button("Import CSV3D...", {kItemW, 0})) {
        std::string p = BrowseCsv3dFile();
        if (!p.empty()) S.ImportSource(scene, p);
    }
    ImGui::SetItemTooltip("Browse for a .csv3d contour, add it to the scene and stage it\n"
                          "as the source of the SEM pipeline (replaces the current source).");
    ImGui::Separator();

    const bool is3D = S.Dim() == 3;
    if (is3D) ImGui::TextDisabled("Mode: 3D surface (tetrahedral pipeline)");
    else      ImGui::TextDisabled("Mode: 2D contour");

    DrawReadout(S);
    ImGui::Separator();

    if (ImGui::Button("Run full pipeline", {kItemW, 0})) S.RunFullPipeline(scene);
    if (is3D)
        ImGui::SetItemTooltip("Adaptive subdivide -> graded offset shells (first gap = 1 mean\n"
                              "edge length, grading 1.2, 8 offsets) -> tetrahedral band mesh\n"
                              "(auto volume) -> thermal solve (default) -> isosurface at T=0.5.");
    else
        ImGui::SetItemTooltip("Adaptive subdivide -> graded offsets (first gap = 1 mean edge\n"
                              "length, grading 1.2, 8 offsets) -> max-area mesh (default area)\n"
                              "-> thermal solve (default) -> isotherm at T=0.5.");
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
    }

    auto autoTag = [&](bool* autoFlag) {
        ImGui::SameLine();
        ImGui::Checkbox("Auto-apply", autoFlag);
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
        if (S.subAuto) { if (changed || released) S.RecomputeUpTo(scene, STAGE_SUBDIVIDE, true); }
        else if (ImGui::Button("Apply", {160, 0})) S.RecomputeUpTo(scene, STAGE_SUBDIVIDE, false);
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
        if (S.offAuto) { if (changed || released) S.RecomputeUpTo(scene, STAGE_OFFSETS, true); }
        else if (ImGui::Button("Apply", {160, 0})) S.RecomputeUpTo(scene, STAGE_OFFSETS, false);
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

        ImGui::Indent();
        bool changed = false, released = false;
        if (is3D) {
            const char* tetItems =
                "Quality (-q)\0" "None (-p)\0" "Max volume (-a)\0" "Sizing (edge len)\0";
            if (ImGui::Combo("TetGen method", &S.tetMethod, tetItems)) {
                S.tetParam = -1.0f;
                changed = true;
            }
            if (S.tetMethod != SEM_TET_NONE) {
                ImGui::DragFloat(TetParamLabel(S.tetMethod), &S.tetParam, 0.5f, -1.0f, 1e7f, "%.3f");
                released |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::SetItemTooltip("%s", TetParamHelp(S.tetMethod));
                ImGui::SameLine();
                if (ImGui::SmallButton("auto")) { S.tetParam = -1.0f; changed = true; }

                bool lenVol = (S.tetMethod == SEM_TET_MAX_VOL ||
                               S.tetMethod == SEM_TET_SIZING);
                if (lenVol) {
                    ImGui::SameLine();
                    const char* ulabel = !S.tetParamEdgeUnits ? "units: model"
                        : (S.tetMethod == SEM_TET_MAX_VOL ? "units: x edge^3"
                                                          : "units: x edge");
                    if (ImGui::SmallButton(ulabel)) {
                        double f = S.TetParamFactor();
                        if (S.tetParam > 0.0f && f > 0.0) {
                            if (!S.tetParamEdgeUnits) S.tetParam = (float)(S.tetParam / f);
                            else                      S.tetParam = (float)(S.tetParam * f);
                        }
                        S.tetParamEdgeUnits = !S.tetParamEdgeUnits;
                    }
                    ImGui::SetItemTooltip("Toggle the parameter's units between model units and\n"
                                          "multiples of the source surface's mean edge length\n"
                                          "(cubed for volume). The value is converted so meshing\n"
                                          "is unaffected.");
                }
            }
        } else {
        const char* methodItems =
            "Grid\0" "None (CDT)\0" "Min angle (-q)\0" "Max area (-a)\0"
            "Conforming (-D)\0" "Sizing (-u)\0";
        if (ImGui::Combo("Steiner method", &S.meshMethod, methodItems)) {
            S.meshParam = -1.0f;
            changed = true;
        }
        if (S.meshMethod != SEM_STEINER_NONE) {
            ImGui::DragFloat(MeshParamLabel(S.meshMethod), &S.meshParam, 0.5f, -1.0f, 5000.0f, "%.3f");
            released |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SetItemTooltip("%s", MeshParamHelp(S.meshMethod));
            ImGui::SameLine();
            if (ImGui::SmallButton("auto")) { S.meshParam = -1.0f; changed = true; }

            bool lenArea = (S.meshMethod == SEM_STEINER_MAX_AREA ||
                            S.meshMethod == SEM_STEINER_SIZING);
            if (lenArea) {
                ImGui::SameLine();
                const char* ulabel = !S.meshParamEdgeUnits ? "units: model"
                    : (S.meshMethod == SEM_STEINER_MAX_AREA ? "units: x edge^2"
                                                            : "units: x edge");
                if (ImGui::SmallButton(ulabel)) {
                    double f = S.MeshParamFactor();
                    if (S.meshParam > 0.0f && f > 0.0) {
                        if (!S.meshParamEdgeUnits) S.meshParam = (float)(S.meshParam / f);
                        else                       S.meshParam = (float)(S.meshParam * f);
                    }
                    S.meshParamEdgeUnits = !S.meshParamEdgeUnits;
                }
                ImGui::SetItemTooltip("Toggle the parameter's units between model units and\n"
                                      "multiples of the source contour's mean edge length.\n"
                                      "The value is converted so meshing is unaffected.");
            }
        }
        if (S.meshMethod == SEM_STEINER_GRID) {
            ImGui::DragFloat("Steiner margin", &S.steinerMargin, 0.005f, 0.0f, 1.0f, "%.3f");
            released |= ImGui::IsItemDeactivatedAfterEdit();
        }
        } // end 2D Steiner UI

        if (changed || released) S.MarkStageDirty(STAGE_MESH);
        if (S.meshAuto) { if (changed || released) S.RecomputeUpTo(scene, STAGE_MESH, true); }
        else if (ImGui::Button("Apply", {160, 0})) S.RecomputeUpTo(scene, STAGE_MESH, false);
        ImGui::Unindent();
        ImGui::PopID();
    }

    ImGui::Separator();

    {
        ImGui::PushID("thermal");
        ImGui::Text("Thermal solve");
        autoTag(&S.thermalAuto);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##thermal")) S.ResetStage(scene, STAGE_THERMAL);

        ImGui::Indent();
        bool changed = ImGui::DragFloat(is3D ? "Isosurface value (0..1)" : "Isoline value (0..1)",
                                        &S.isoValue, 0.005f, 0.0f, 1.0f, "%.3f");
        if (is3D)
            ImGui::SetItemTooltip("Steady-state heat conduction on the tetrahedral band mesh\n"
                                  "(source surface T=1, farthest shell T=0). The isosurface at\n"
                                  "this normalized temperature is drawn in green.");
        else
            ImGui::SetItemTooltip("Steady-state heat conduction on the band mesh (source T=1,\n"
                                  "farthest offset T=0). The isotherm at this normalized\n"
                                  "temperature is drawn in green.");
        if (S.isoValue < 0.0f) S.isoValue = 0.0f;
        if (S.isoValue > 1.0f) S.isoValue = 1.0f;

        if (changed) S.MarkStageDirty(STAGE_THERMAL);
        if (S.thermalAuto) { if (changed) S.RecomputeUpTo(scene, STAGE_THERMAL, true); }
        else if (ImGui::Button("Apply", {160, 0})) S.RecomputeUpTo(scene, STAGE_THERMAL, false);
        ImGui::Unindent();
        ImGui::PopID();
    }

    ImGui::Separator();
    ImGui::TextDisabled("%s", S.status);

    ImGui::PopItemWidth();

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows))
        blockMousePick = true;
    ImGui::End();
}

}
