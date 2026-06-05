#pragma once
#include "../imgui/imgui.h"
#include "../scene/scene.h"
#include "../../external/sem_exports.h"
#include "../../utils/errorLogger.h"
#include "../../loaders/CSV3DLoader.h"
#include <string>
#include <vector>
#include <map>
#include <excpt.h>

namespace SEMWindow {

namespace detail {
    // The CSV3D source contour most recently loaded into SEM. Offset lines and
    // the band mesh are attached as children of this primitive. Updated by the
    // import code each time a new contour is loaded; the previous contour stays
    // in the scene and simply stops receiving new SEM children.
    inline std::string&  SourcePath() { static std::string s;            return s; }
    inline Primitive*&   SourcePrim() { static Primitive*  p = nullptr;   return p; }

    // The offset / mesh primitives last produced by this window. Each action
    // replaces its own previous primitive instead of stacking new ones.
    inline Primitive*&   LastOffsets() { static Primitive* p = nullptr; return p; }
    inline Primitive*&   LastMesh()    { static Primitive* p = nullptr; return p; }

    inline std::string BaseName(const std::string& path) {
        auto slash = path.find_last_of("\\/");
        return (slash != std::string::npos) ? path.substr(slash + 1) : path;
    }
    inline std::string Stem(const std::string& path) {
        std::string file = BaseName(path);
        auto dot = file.find_last_of('.');
        return (dot != std::string::npos) ? file.substr(0, dot) : file;
    }

    // Vertex / edge / triangle counts for a CSV3D file, plus a couple of values
    // derived from them. Computed once when a result file is produced (never per
    // frame) and cached in the window's static state.
    struct Stats {
        bool valid    = false;
        int  verts    = 0;
        int  edges    = 0;   // unique undirected edges (triangles + faces + explicit)
        int  tris     = 0;
        int  boundary = 0;   // edges used by exactly one triangle (tris > 0 only)
    };

    inline Stats ComputeStats(const std::string& path) {
        Stats s;
        CSV3DLoader::CSV3DData d;
        if (path.empty() || !CSV3DLoader::Load(path, d)) return s;
        s.valid = true;
        s.verts = (int)d.nodes.size();
        s.tris  = (int)d.triangles.size();

        // Count how many faces each undirected edge belongs to so we can also
        // report the boundary (edges touched by a single triangle).
        std::map<std::pair<unsigned, unsigned>, int> edgeUse;
        auto add = [&](unsigned a, unsigned b) {
            if (a == b) return;
            edgeUse[a < b ? std::make_pair(a, b) : std::make_pair(b, a)]++;
        };
        for (const auto& t : d.triangles) { add(t.x, t.y); add(t.y, t.z); add(t.z, t.x); }
        for (const auto& f : d.faces) {
            size_t m = f.size();
            for (size_t i = 0; i < m; ++i) add(f[i], f[(i + 1) % m]);
        }
        for (const auto& e : d.edges) add(e.first, e.second);

        s.edges = (int)edgeUse.size();
        if (s.tris > 0)
            for (const auto& kv : edgeUse) if (kv.second == 1) ++s.boundary;
        return s;
    }
}

// Wrap the meshing entry point so an access violation inside the prebuilt
// Triangle-based DLL is turned into an error code (-100) instead of taking the
// whole application down. SEH and C++ stack unwinding cannot share a scope, so
// this helper holds no C++ objects with destructors.
inline int SafeBuildMeshEx(const SEM_MeshParamsEx* params) {
    __try { return SEM_BuildMeshEx(params); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -100; }
}

// Called by the importer after a successful SEM_LoadCSV3D. The new contour
// starts with no SEM children of its own; whatever the previous contour
// produced stays attached to it.
inline void SetSource(const std::string& path, Primitive* prim) {
    detail::SourcePath()  = path;
    detail::SourcePrim()  = prim;
    detail::LastOffsets() = nullptr;
    detail::LastMesh()    = nullptr;
}

inline void Draw(Scene& scene, bool& blockMousePick) {
    using namespace detail;

    static int   s_mode           = 0;       // 0 = Manual, 1 = Real-time

    // Subdivision.
    static int   s_subMode        = 0;       // 0 = Clear, 1 = Adaptive, 2 = Fixed N
    static int   s_subN           = 2;

    // Offsets (uniform / graded).
    static float s_dMax           = 100.0f;
    static int   s_numOffsets     = 3;
    static float s_grading        = 1.0f;

    // Offsets at explicit gaps.
    static std::vector<float> s_gaps = { 25.0f, 25.0f, 25.0f };

    // Mesh (selectable Steiner strategy).
    static int   s_meshMethod     = SEM_STEINER_GRID;
    static float s_meshParam      = -1.0f;   // negative => per-method auto default
    static float s_steinerMargin  = 0.45f;

    static char  s_status[256]    = "Ready";

    // Cached geometry readouts (vertices / edges / triangles + derived values).
    // Refreshed only when their underlying file is (re)produced, never per frame.
    static Stats       s_srcStats, s_offStats, s_meshStats;
    static std::string s_srcStatsPath;

    auto alive = [&](Primitive* q) {
        if (!q) return false;
        for (Primitive* p : scene.primitives) if (p == q) return true;
        return false;
    };

    SceneNode* attachTo = alive(SourcePrim()) ? static_cast<SceneNode*>(SourcePrim()) : nullptr;
    const std::string& loaded = SourcePath();

    // Source stats: recompute only when the loaded contour file changes.
    if (loaded != s_srcStatsPath) { s_srcStats = ComputeStats(loaded); s_srcStatsPath = loaded; }

    // In Manual mode failures pop an ErrorLogger message box; in Real-time mode
    // (silent) they only update the status line, so dragging a slider over an
    // invalid range does not spawn a flood of modal dialogs.
    auto report = [&](bool silent, const std::string& msg) {
        if (silent) snprintf(s_status, sizeof(s_status), "%s", msg.c_str());
        else        ErrorLogger::Log(msg);
    };

    // Pull the serialized offsets back into the scene, replacing the previous
    // offset primitive. Shared by both offset paths (uniform and explicit gaps).
    auto loadOffsetResult = [&](bool silent) {
        const char* outPath = SEM_SerializeOffsets(nullptr);
        if (!outPath) { report(silent, "SEM_SerializeOffsets failed to write the offset file."); return; }
        std::string p(outPath);
        if (alive(LastOffsets())) scene.RemovePrimitive(LastOffsets());
        LastOffsets() = scene.AddFromCSV3D(p, "offsets_" + Stem(loaded), attachTo);
        if (LastOffsets()) { LastOffsets()->semStaging = true; scene.AttachVertexPointsGroup(LastOffsets()); }
        s_offStats = ComputeStats(p);
        snprintf(s_status, sizeof(s_status), "Offsets: %s", BaseName(p).c_str());
    };

    // Subdivide the cached source contour. Returns true on success. The caller
    // decides what to do with the now-stale offsets/mesh (drop in Manual mode,
    // recompute live in Real-time mode).
    auto doSubdivide = [&](bool silent) -> bool {
        try {
            int n = (s_subMode == 0) ? -1 : (s_subMode == 1) ? 0 : (s_subN < 1 ? 1 : s_subN);
            int rc = SEM_SubdivideContour(n);
            if (rc != 0) {
                const char* errs[] = { "", "No source loaded", "Invalid argument" };
                int idx = (-rc < 3) ? -rc : 0;
                report(silent, "SEM_SubdivideContour failed (" + std::to_string(rc) + "): " + errs[idx]);
                return false;
            }
            if (n == -1)      snprintf(s_status, sizeof(s_status), "Subdivision cleared");
            else if (n == 0)  snprintf(s_status, sizeof(s_status), "Subdivided (adaptive)");
            else              snprintf(s_status, sizeof(s_status), "Subdivided (n=%d)", n);

            // Force the per-vertex markers on for the whole pipeline, as if every
            // "Vertex points" group's eye had been switched on at once.
            if (alive(SourcePrim()))  scene.ShowVertexPointsFor(SourcePrim(),  true);
            if (alive(LastOffsets())) scene.ShowVertexPointsFor(LastOffsets(), true);
            if (alive(LastMesh()))    scene.ShowVertexPointsFor(LastMesh(),    true);
            return true;
        }
        catch (const std::exception& e) { report(silent, std::string("SEM_SubdivideContour exception: ") + e.what()); }
        catch (...)                     { report(silent, "SEM_SubdivideContour: unknown exception."); }
        return false;
    };

    // Compute uniform / graded offsets, then replace the previous offset primitive.
    auto doOffsets = [&](bool silent) {
        try {
            int rc = SEM_ComputeOffsets(s_dMax, s_numOffsets, s_grading);
            if (rc != 0) {
                const char* errs[] = { "", "No source loaded", "Invalid parameters" };
                int idx = (-rc < 3) ? -rc : 0;
                report(silent, "SEM_ComputeOffsets failed (" + std::to_string(rc) + "): " + errs[idx]);
                return;
            }
            loadOffsetResult(silent);
        }
        catch (const std::exception& e) { report(silent, std::string("SEM_ComputeOffsets exception: ") + e.what()); }
        catch (...)                     { report(silent, "SEM_ComputeOffsets: unknown exception."); }
    };

    // Compute offsets from the explicit per-line gap list.
    auto doOffsetsAt = [&](bool silent) {
        try {
            if (s_gaps.empty()) { report(silent, "Add at least one gap."); return; }
            std::vector<double> gaps(s_gaps.begin(), s_gaps.end());
            int rc = SEM_ComputeOffsetsAt(gaps.data(), (int)gaps.size());
            if (rc != 0) {
                const char* errs[] = { "", "No source loaded", "Invalid parameters" };
                int idx = (-rc < 3) ? -rc : 0;
                report(silent, "SEM_ComputeOffsetsAt failed (" + std::to_string(rc) + "): " + errs[idx]);
                return;
            }
            loadOffsetResult(silent);
        }
        catch (const std::exception& e) { report(silent, std::string("SEM_ComputeOffsetsAt exception: ") + e.what()); }
        catch (...)                     { report(silent, "SEM_ComputeOffsetsAt: unknown exception."); }
    };

    // Build + serialize the mesh with the selected Steiner strategy.
    auto doMesh = [&](bool silent) {
        try {
            SEM_MeshParamsEx params{ s_meshMethod, (double)s_meshParam, (double)s_steinerMargin };
            int rc = SafeBuildMeshEx(&params);
            if (rc == -100) {
                report(silent, "SEM_BuildMeshEx crashed inside the mesher DLL (access violation caught). "
                               "Try a different Steiner method or a larger parameter.");
                return;
            }
            if (rc != 0) {
                const char* errs[] = { "", "No source loaded", "Compute offsets first",
                                       "Not enough valid lines", "Triangulation failed",
                                       "Invalid method" };
                int idx = (-rc < 6) ? -rc : 0;
                report(silent, "SEM_BuildMeshEx failed (" + std::to_string(rc) + "): " + errs[idx]);
                return;
            }
            const char* outPath = SEM_SerializeMesh(nullptr);
            if (!outPath) { report(silent, "SEM_SerializeMesh failed to write the mesh file."); return; }

            std::string p(outPath);
            if (alive(LastMesh())) scene.RemovePrimitive(LastMesh());
            LastMesh() = scene.AddFromCSV3D(p, "mesh_" + Stem(loaded), attachTo);
            if (LastMesh()) { LastMesh()->semStaging = true; scene.AttachVertexPointsGroup(LastMesh()); }
            s_meshStats = ComputeStats(p);
            snprintf(s_status, sizeof(s_status), "Mesh: %s", BaseName(p).c_str());
        }
        catch (const std::exception& e) { report(silent, std::string("SEM_BuildMeshEx exception: ") + e.what()); }
        catch (...)                     { report(silent, "SEM_BuildMeshEx: unknown exception."); }
    };

    // Per-method label + tooltip for the single variable parameter field.
    auto meshParamLabel = [&]() -> const char* {
        switch (s_meshMethod) {
            case SEM_STEINER_GRID:       return "Grid spacing";
            case SEM_STEINER_NONE:       return "(no parameter)";
            case SEM_STEINER_MIN_ANGLE:  return "Min angle (deg)";
            case SEM_STEINER_MAX_AREA:   return "Max triangle area";
            case SEM_STEINER_CONFORMING: return "Min angle (deg, 0=off)";
            case SEM_STEINER_SIZING:     return "Max edge length";
            default:                     return "Parameter";
        }
    };
    auto meshParamHelp = [&]() -> const char* {
        switch (s_meshMethod) {
            case SEM_STEINER_GRID:       return "Regular grid of interior points + CDT. Negative => avg edge length.";
            case SEM_STEINER_NONE:       return "Constrained Delaunay, no interior points. Parameter ignored.";
            case SEM_STEINER_MIN_ANGLE:  return "Triangle -q: Ruppert refinement. Typical 20..33 deg. Negative => default.";
            case SEM_STEINER_MAX_AREA:   return "Triangle -a: bounded triangle area. Negative => default.";
            case SEM_STEINER_CONFORMING: return "Triangle -D: conforming Delaunay. 0 => conforming only.";
            case SEM_STEINER_SIZING:     return "Triangle -u: bounded longest edge. Negative => default.";
            default:                     return "";
        }
    };

    ImGui::Begin("SEM");

    ImGui::TextDisabled("Source: %s", loaded.empty() ? "(none loaded)" : BaseName(loaded).c_str());

    // Right-aligned geometry readout. Reports the most-derived artefact present
    // (mesh > offsets > source contour): vertex / edge / triangle counts plus a
    // few values derived from them.
    {
        const Stats* st  = nullptr;
        const char*  tag = "Source";
        if      (alive(LastMesh())    && s_meshStats.valid) { st = &s_meshStats; tag = "Mesh";    }
        else if (alive(LastOffsets()) && s_offStats.valid)  { st = &s_offStats;  tag = "Offsets"; }
        else if (s_srcStats.valid)                          { st = &s_srcStats;  tag = "Source";  }

        auto rightLine = [](const char* txt) {
            float w     = ImGui::CalcTextSize(txt).x;
            float avail = ImGui::GetContentRegionAvail().x;
            if (avail > w) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - w));
            ImGui::TextDisabled("%s", txt);
        };

        char l1[160], l2[160];
        if (st && st->tris > 0) {
            int   chi = st->verts - st->edges + st->tris;        // Euler characteristic
            int   holes = 1 - chi;                               // connected planar region => holes
            float te  = st->edges ? (float)st->tris / st->edges : 0.0f;
            snprintf(l1, sizeof(l1), "%s   V %d   E %d   T %d", tag, st->verts, st->edges, st->tris);
            snprintf(l2, sizeof(l2), "chi %d   holes %d   bnd %d   T/E %.2f", chi, holes, st->boundary, te);
        } else if (st) {
            snprintf(l1, sizeof(l1), "%s   V %d   E %d", tag, st->verts, st->edges);
            snprintf(l2, sizeof(l2), "E-V %d", st->edges - st->verts);   // 0 ~ single closed loop
        } else {
            snprintf(l1, sizeof(l1), "(no geometry)");
            l2[0] = '\0';
        }
        rightLine(l1);
        if (l2[0]) rightLine(l2);
    }

    ImGui::RadioButton("Manual", &s_mode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Real-time", &s_mode, 1);
    ImGui::Separator();

    const bool live = (s_mode == 1);

    // ----------------------------------------------------------------- Subdivide
    ImGui::SeparatorText(live ? "Subdivide contour (live)" : "Subdivide contour");
    {
        bool subChanged = false;
        subChanged |= ImGui::RadioButton("Clear",    &s_subMode, 0); ImGui::SameLine();
        subChanged |= ImGui::RadioButton("Adaptive", &s_subMode, 1); ImGui::SameLine();
        subChanged |= ImGui::RadioButton("Fixed N",  &s_subMode, 2);
        if (s_subMode == 2) {
            if (live) subChanged |= ImGui::DragInt("Parts per edge", &s_subN, 0.1f, 1, 100);
            else      ImGui::InputInt("Parts per edge", &s_subN);
        }
        if (s_subN < 1) s_subN = 1;

        if (live) {
            // Auto-apply, then refresh whatever the pipeline already produced
            // (the offsets/mesh are derived from the subdivided source).
            if (subChanged && doSubdivide(true)) {
                if (alive(LastOffsets())) doOffsets(true);
                if (alive(LastMesh()))    doMesh(true);
            }
        } else if (ImGui::Button("Subdivide", {160, 0})) {
            // The offsets/mesh are derived from the (now subdivided) source, so
            // recompute whatever the pipeline already produced instead of dropping
            // it. AddFromCSV3D is a pure visual load and never touches the SEM
            // cache (only SEM_LoadCSV3D clears it), so the offsets recompute and
            // then the mesh recompute both see the freshly subdivided contour.
            // Offsets must come first: SEM_SubdivideContour invalidated the cached
            // offsets the mesh depends on. Turning the markers on afterwards
            // regenerates the yellow vertex points against the new geometry.
            if (doSubdivide(false)) {
                if (alive(LastOffsets())) doOffsets(false);
                if (alive(LastMesh()))    doMesh(false);
                if (alive(LastOffsets())) scene.ShowVertexPointsFor(LastOffsets(), true);
                if (alive(LastMesh()))    scene.ShowVertexPointsFor(LastMesh(),    true);
            }
        }
    }

    // ------------------------------------------------------------------- Offsets
    ImGui::SeparatorText(live ? "Offsets (live)" : "Offsets");
    {
        bool changed = false;
        if (live) {
            changed |= ImGui::DragFloat("Max distance", &s_dMax, 0.5f, -5000.0f, 5000.0f, "%.2f");
            changed |= ImGui::DragInt("Num offsets", &s_numOffsets, 0.2f, 1, 500);
            changed |= ImGui::DragFloat("Grading", &s_grading, 0.01f, 0.05f, 20.0f, "%.3f");
        } else {
            ImGui::DragFloat("Max distance", &s_dMax, 1.0f, -5000.0f, 5000.0f, "%.3f");
            ImGui::InputInt("Num offsets", &s_numOffsets);
            if (s_numOffsets < 1) s_numOffsets = 1;
            ImGui::InputFloat("Grading", &s_grading, 0.05f, 0.5f, "%.3f");
            if (s_grading <= 0.0f) s_grading = 1.0f;
        }
        ImGui::SetItemTooltip("Sign of Max distance selects the side (left/right of travel).");
        if (live) { if (changed) { doOffsets(true); if (alive(LastMesh())) doMesh(true); } }
        else if (ImGui::Button("Compute Offsets", {160, 0})) doOffsets(false);
    }

    // -------------------------------------------------------- Offsets at gaps
    ImGui::SeparatorText("Offsets at explicit gaps");
    {
        int removeIdx = -1;
        bool gapsChanged = false;
        for (int i = 0; i < (int)s_gaps.size(); ++i) {
            ImGui::PushID(i);
            ImGui::SetNextItemWidth(140);
            gapsChanged |= ImGui::DragFloat("##gap", &s_gaps[i], 0.5f, -5000.0f, 5000.0f, "%.2f");
            ImGui::SameLine();
            if (ImGui::Button("X")) removeIdx = i;
            ImGui::SameLine();
            ImGui::Text("gap %d", i);
            ImGui::PopID();
        }
        if (removeIdx >= 0) { s_gaps.erase(s_gaps.begin() + removeIdx); gapsChanged = true; }
        if (ImGui::Button("+ Add gap")) {
            s_gaps.push_back(s_gaps.empty() ? 25.0f : s_gaps.back());
            gapsChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Compute Offsets (gaps)")) doOffsetsAt(false);
        if (live && gapsChanged && !s_gaps.empty()) { doOffsetsAt(true); if (alive(LastMesh())) doMesh(true); }
    }

    // ---------------------------------------------------------------------- Mesh
    ImGui::SeparatorText(live ? "Mesh (live)" : "Mesh");
    {
        const char* methodItems =
            "Grid\0"
            "None (CDT)\0"
            "Min angle (-q)\0"
            "Max area (-a)\0"
            "Conforming (-D)\0"
            "Sizing (-u)\0";
        bool meshChanged = false;
        if (ImGui::Combo("Steiner method", &s_meshMethod, methodItems)) {
            s_meshParam = -1.0f;   // reset to per-method auto default on switch
            meshChanged = true;
        }

        // Variable parameter field — hidden / disabled for the NONE method.
        if (s_meshMethod != SEM_STEINER_NONE) {
            if (live) meshChanged |= ImGui::DragFloat(meshParamLabel(), &s_meshParam, 0.5f, -1.0f, 5000.0f, "%.3f");
            else      ImGui::InputFloat(meshParamLabel(), &s_meshParam, 1.0f, 10.0f, "%.3f");
            ImGui::SetItemTooltip("%s", meshParamHelp());
        }

        // Margin only applies to the GRID strategy.
        if (s_meshMethod == SEM_STEINER_GRID) {
            if (live) meshChanged |= ImGui::DragFloat("Steiner margin", &s_steinerMargin, 0.005f, 0.0f, 1.0f, "%.3f");
            else      ImGui::InputFloat("Steiner margin", &s_steinerMargin, 0.05f, 0.5f, "%.3f");
        }

        if (live) { if (meshChanged) doMesh(true); }
        else if (ImGui::Button("Build Mesh", {160, 0})) doMesh(false);
    }

    ImGui::Separator();
    ImGui::TextDisabled("%s", s_status);

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows))
        blockMousePick = true;
    ImGui::End();
}

}
