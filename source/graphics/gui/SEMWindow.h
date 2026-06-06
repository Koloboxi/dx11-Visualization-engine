#pragma once
#include "../imgui/imgui.h"
#include "../scene/scene.h"
#include "SemSession.h"
#include <cstdio>

namespace SEMWindow {

inline SemSessionNS::SemSession& Session() {
    static SemSessionNS::SemSession s;
    return s;
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

inline void DrawReadout(SemSessionNS::SemSession& S) {
    using namespace SemSessionNS;
    const Stats* st  = nullptr;
    const char*  tag = "Source";
    if      (S.MeshPrim()    && S.MeshStats().valid) { st = &S.MeshStats(); tag = "Mesh";    }
    else if (S.OffsetsPrim() && S.OffStats().valid)  { st = &S.OffStats();  tag = "Offsets"; }
    else if (S.SrcStats().valid)                     { st = &S.SrcStats();  tag = "Source";  }

    auto rightLine = [](const char* txt) {
        float w     = ImGui::CalcTextSize(txt).x;
        float avail = ImGui::GetContentRegionAvail().x;
        if (avail > w) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - w));
        ImGui::TextDisabled("%s", txt);
    };

    char l1[160], l2[160];
    if (st && st->tris > 0) {
        int   chi   = st->verts - st->edges + st->tris;
        int   holes = 1 - chi;
        float te    = st->edges ? (float)st->tris / st->edges : 0.0f;
        snprintf(l1, sizeof(l1), "%s   V %d   E %d   T %d", tag, st->verts, st->edges, st->tris);
        snprintf(l2, sizeof(l2), "chi %d   holes %d   bnd %d   T/E %.2f", chi, holes, st->boundary, te);
    } else if (st) {
        snprintf(l1, sizeof(l1), "%s   V %d   E %d", tag, st->verts, st->edges);
        snprintf(l2, sizeof(l2), "E-V %d", st->edges - st->verts);
    } else {
        snprintf(l1, sizeof(l1), "(no geometry)");
        l2[0] = '\0';
    }
    rightLine(l1);
    if (l2[0]) rightLine(l2);
}

inline void Draw(Scene& scene, bool& blockMousePick) {
    using namespace SemSessionNS;
    SemSession& S = Session();

    S.Validate(scene);
    S.Bind(scene, scene.stagedPrimitive);

    ImGui::Begin("SEM");

    // Staging mode is turned on automatically when a CSV3D contour is imported.
    // This button turns it back off so the tree's double-click reverts to rename.
    if (scene.stagingEnabled) {
        if (ImGui::Button("Disable staging")) {
            scene.stagingEnabled = false;
            scene.ClearStaged();
        }
        ImGui::SetItemTooltip("Stop binding contours via double-click. Re-enabled on the next CSV3D import.");
        ImGui::Separator();
    }

    if (!S.HasSource()) {
        if (S.SourcePrim()) ImGui::TextDisabled("Staged primitive is not a SEM contour.");
        else                ImGui::TextDisabled("No staged contour.");
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

    ImGui::TextDisabled("Staged: %s", BaseName(S.SourcePath()).c_str());
    DrawReadout(S);
    ImGui::Separator();

    if (ImGui::Button("Run full pipeline", {-1.0f, 0})) S.RunFullPipeline(scene);
    ImGui::SetItemTooltip("Adaptive subdivide -> graded offsets (first gap = 1 mean edge\n"
                          "length, grading 1.2, 8 offsets) -> max-area mesh (default area)\n"
                          "-> thermal solve (default) -> isotherm at T=0.5.");
    ImGui::Separator();

    auto autoTag = [&](bool* autoFlag) {
        ImGui::SameLine();
        ImGui::Checkbox("Auto-apply", autoFlag);
    };

    {
        ImGui::PushID("sub");
        bool en = S.subEnabled;
        if (ImGui::Checkbox("Subdivide", &en)) { S.subEnabled = en; S.RecomputeFrom(scene, STAGE_SUBDIVIDE, false); }
        autoTag(&S.subAuto);

        ImGui::BeginDisabled(!S.subEnabled);
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

        if (S.subAuto) { if (changed || released) S.RecomputeFrom(scene, STAGE_SUBDIVIDE, true); }
        else if (ImGui::Button("Apply", {160, 0})) S.RecomputeFrom(scene, STAGE_SUBDIVIDE, false);
        ImGui::Unindent();
        ImGui::EndDisabled();
        ImGui::PopID();
    }

    ImGui::Separator();

    {
        ImGui::PushID("off");
        bool en = S.offEnabled;
        if (ImGui::Checkbox("Offsets", &en)) { S.offEnabled = en; S.RecomputeFrom(scene, STAGE_OFFSETS, false); }
        autoTag(&S.offAuto);

        ImGui::BeginDisabled(!S.offEnabled);
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

        if (S.offAuto) { if (changed || released) S.RecomputeFrom(scene, STAGE_OFFSETS, true); }
        else if (ImGui::Button("Apply", {160, 0})) S.RecomputeFrom(scene, STAGE_OFFSETS, false);
        ImGui::Unindent();
        ImGui::EndDisabled();
        ImGui::PopID();
    }

    ImGui::Separator();

    {
        ImGui::PushID("mesh");
        bool en = S.meshEnabled;
        if (ImGui::Checkbox("Mesh", &en)) { S.meshEnabled = en; S.RecomputeFrom(scene, STAGE_MESH, false); }
        autoTag(&S.meshAuto);

        ImGui::BeginDisabled(!S.meshEnabled);
        ImGui::Indent();
        bool changed = false, released = false;
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

            // Length/area knobs can be entered as multiples of the source's mean
            // edge length. Toggling converts the value so the mesh is unchanged.
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

        if (S.meshAuto) { if (changed || released) S.RecomputeFrom(scene, STAGE_MESH, true); }
        else if (ImGui::Button("Apply", {160, 0})) S.RecomputeFrom(scene, STAGE_MESH, false);
        ImGui::Unindent();
        ImGui::EndDisabled();
        ImGui::PopID();
    }

    ImGui::Separator();

    {
        ImGui::PushID("thermal");
        ImGui::TextUnformatted("Thermal solve");
        ImGui::BeginDisabled(!S.MeshPrim());
        ImGui::Indent();
        ImGui::DragFloat("Conductivity", &S.thermalK, 0.05f, 0.0001f, 10000.0f, "%.4f");
        ImGui::SetItemTooltip("Steady-state heat conduction on the band mesh.\n"
                              "Source held at T=1, farthest offset at T=0.\n"
                              "Replaces each node's distance T with temperature.");
        if (S.thermalK <= 0.0f) S.thermalK = 0.0001f;
        if (ImGui::Button("Solve thermal", {160, 0})) S.ApplyThermal(scene, false);
        ImGui::Unindent();
        ImGui::EndDisabled();
        ImGui::PopID();
    }

    ImGui::Separator();

    {
        ImGui::PushID("isoline");
        ImGui::TextUnformatted("Isoline");
        ImGui::SameLine();
        ImGui::Checkbox("Auto-apply", &S.isoAuto);
        ImGui::BeginDisabled(!S.ThermalSolved());
        ImGui::Indent();
        // Extraction needs no re-solve, so Auto-apply re-extracts live on every
        // value change (real-time drag), not just when the drag ends.
        bool changed = ImGui::DragFloat("Value (0..1)", &S.isoValue, 0.005f, 0.0f, 1.0f, "%.3f");
        ImGui::SetItemTooltip("Isotherm at this normalized temperature, drawn in green.\n"
                              "Requires a thermal solve.");
        if (S.isoValue < 0.0f) S.isoValue = 0.0f;
        if (S.isoValue > 1.0f) S.isoValue = 1.0f;

        if (S.isoAuto) { if (changed) S.ApplyIsoline(scene, true); }
        else if (ImGui::Button("Extract isoline", {160, 0})) S.ApplyIsoline(scene, false);
        ImGui::Unindent();
        ImGui::EndDisabled();
        ImGui::PopID();
    }

    ImGui::Separator();
    ImGui::TextDisabled("%s", S.status);

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows))
        blockMousePick = true;
    ImGui::End();
}

}
