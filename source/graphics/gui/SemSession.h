#pragma once
#include "../scene/scene.h"
#include "../../external/sem_exports.h"
#include "../../utils/errorLogger.h"
#include "../../loaders/CSV3DLoader.h"
#include <string>
#include <vector>
#include <map>
#include <cstdio>
#include <initializer_list>
#include <excpt.h>

namespace SemSessionNS {

enum Stage { STAGE_SUBDIVIDE = 0, STAGE_OFFSETS = 1, STAGE_MESH = 2, STAGE_THERMAL = 3 };
enum OffsetMode { OFFSET_EVEN = 0, OFFSET_GAPS = 1 };

struct Stats {
    bool valid    = false;
    int  verts    = 0;
    int  edges    = 0;
    int  tris     = 0;
    int  boundary = 0;
};

inline std::string BaseName(const std::string& path) {
    auto slash = path.find_last_of("\\/");
    return (slash != std::string::npos) ? path.substr(slash + 1) : path;
}
inline std::string Stem(const std::string& path) {
    std::string file = BaseName(path);
    auto dot = file.find_last_of('.');
    return (dot != std::string::npos) ? file.substr(0, dot) : file;
}

inline Stats ComputeStats(const std::string& path) {
    Stats s;
    CSV3DLoader::CSV3DData d;
    if (path.empty() || !CSV3DLoader::Load(path, d)) return s;
    s.valid = true;
    s.verts = (int)d.nodes.size();
    s.tris  = (int)d.triangles.size();

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

inline bool OrderedContourFromCSV3D(const std::string& path, std::vector<XMFLOAT3>& out) {
    out.clear();
    CSV3DLoader::CSV3DData d;
    if (!CSV3DLoader::Load(path, d)) return false;
    const size_t n = d.nodes.size();
    if (n == 0) return false;

    std::vector<std::pair<unsigned, unsigned>> E = d.edges;
    for (const auto& f : d.faces) {
        size_t m = f.size();
        for (size_t i = 0; i + 1 < m; ++i) E.push_back({ f[i], f[i + 1] });
    }
    for (const auto& t : d.triangles) {
        E.push_back({ t.x, t.y }); E.push_back({ t.y, t.z }); E.push_back({ t.z, t.x });
    }

    if (E.empty()) {
        for (const auto& nd : d.nodes) out.push_back(nd.pos);
        return out.size() >= 2;
    }

    std::vector<std::vector<unsigned>> adj(n);
    for (const auto& e : E)
        if (e.first < n && e.second < n && e.first != e.second) {
            adj[e.first].push_back(e.second);
            adj[e.second].push_back(e.first);
        }

    int start = -1;
    for (size_t i = 0; i < n; ++i) if (adj[i].size() == 1) { start = (int)i; break; }
    if (start < 0) start = (int)E[0].first;

    std::vector<char> visited(n, 0);
    int cur = start, prev = -1;
    while (cur >= 0 && !visited[cur]) {
        visited[cur] = 1;
        out.push_back(d.nodes[cur].pos);
        int next = -1;
        for (unsigned nb : adj[cur])
            if ((int)nb != prev && !visited[nb]) { next = (int)nb; break; }
        prev = cur; cur = next;
    }
    return out.size() >= 2;
}

inline int SafeBuildMeshEx(const SEM_MeshParamsEx* params) {
    __try { return SEM_BuildMeshEx(params); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -100; }
}

inline int SafeSolveThermal(double conductivity) {
    __try { return SEM_SolveThermal(conductivity); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -100; }
}

inline int SafeExtractIsoline(double value) {
    __try { return SEM_ExtractIsoline(value); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -100; }
}

class SemSession {
public:
    int   subMode    = 1;
    int   subN       = 2;
    bool  subEnabled = true;

    int   offsetMode = OFFSET_EVEN;
    float firstGap   = 1.0f;
    int   numOffsets = 8;
    float grading    = 1.2f;
    std::vector<float> gaps = { 25.0f, 25.0f, 25.0f };
    bool  offEnabled = true;

    int   meshMethod    = SEM_STEINER_MAX_AREA;
    float meshParam     = -1.0f;
    float steinerMargin = 0.45f;
    bool  meshEnabled   = true;
    bool  meshParamEdgeUnits = false;

    bool  thermalEnabled = true;
    float isoValue   = 0.5f;

    bool  subAuto     = false;
    bool  offAuto     = false;
    bool  meshAuto    = false;
    bool  thermalAuto = false;

    bool  revolutionMode = false;
    int   revSegments    = 48;
    float srcRevAlpha    = 0.8f;
    float isoRevAlpha    = 0.5f;

    char  status[256] = "Ready";

    const std::string& SourcePath() const { return m_srcPath; }
    Primitive*  SourcePrim()  const { return m_srcPrim; }
    Primitive*  OffsetsPrim() const { return m_offsets; }
    Primitive*  MeshPrim()    const { return m_mesh; }
    Primitive*  IsolinePrim() const { return m_isoline; }
    Primitive*  SrcRevSurf()  const { return m_srcRevSurf; }
    Primitive*  IsoRevSurf()  const { return m_isoRevSurf; }
    bool        HasSource()   const { return m_srcPrim != nullptr && !m_srcPath.empty(); }
    bool        ThermalSolved() const { return m_thermalSolved; }
    bool        HasIsolinePath() const { return !m_isolinePath.empty(); }

    double MeshParamFactor() const {
        double avg = SEM_GetAvgEdgeLen();
        if (avg <= 0.0) avg = 1.0;
        return (meshMethod == SEM_STEINER_MAX_AREA) ? avg * avg : avg;
    }

    const Stats& SrcStats()  const { return m_srcStats; }
    const Stats& OffStats()  const { return m_offStats; }
    const Stats& MeshStats() const { return m_meshStats; }

    void Bind(Scene& scene, Primitive* prim) {
        if (prim == m_srcPrim) return;
        m_srcPrim   = prim;
        m_srcPath   = prim ? prim->semSourcePath : std::string();
        m_offsets   = nullptr;
        m_mesh      = nullptr;
        m_isoline   = nullptr;
        m_srcRevSurf = nullptr;
        m_isoRevSurf = nullptr;
        m_isolinePath.clear();
        m_thermalSolved = false;
        m_offStats  = Stats();
        m_meshStats = Stats();
        if (!m_srcPath.empty()) {
            int rc = SEM_LoadCSV3D(m_srcPath.c_str());
            if (rc != 0) Report(scene, false, "SEM_LoadCSV3D failed (" + std::to_string(rc) + ")");
        }
        m_srcStats = ComputeStats(m_srcPath);
        if (HasSource()) snprintf(status, sizeof(status), "Staged: %s", BaseName(m_srcPath).c_str());
        else             snprintf(status, sizeof(status), "Ready");
    }

    void Unbind() {
        m_srcPrim = nullptr; m_srcPath.clear();
        m_offsets = nullptr; m_mesh = nullptr; m_isoline = nullptr;
        m_srcRevSurf = nullptr; m_isoRevSurf = nullptr;
        m_isolinePath.clear();
        m_thermalSolved = false;
        m_srcStats = m_offStats = m_meshStats = Stats();
        snprintf(status, sizeof(status), "Ready");
    }

    void Validate(Scene& scene) {
        if (m_srcPrim && !Alive(scene, m_srcPrim)) { Unbind(); return; }
        if (m_offsets && !Alive(scene, m_offsets)) { m_offsets = nullptr; m_offStats = Stats(); }
        if (m_mesh    && !Alive(scene, m_mesh))    { m_mesh    = nullptr; m_meshStats = Stats(); m_thermalSolved = false; }
        if (m_isoline && !Alive(scene, m_isoline)) { m_isoline = nullptr; }
        if (m_srcRevSurf && !Alive(scene, m_srcRevSurf)) m_srcRevSurf = nullptr;
        if (m_isoRevSurf && !Alive(scene, m_isoRevSurf)) m_isoRevSurf = nullptr;
    }

    void RecomputeFrom(Scene& scene, Stage from, bool silent) {
        if (!HasSource()) return;
        if (from <= STAGE_SUBDIVIDE && !ApplySubdivide(scene, silent)) return;

        if (from <= STAGE_OFFSETS) {
            if (offEnabled || (m_offsets && Alive(scene, m_offsets))) {
                if (!ApplyOffsets(scene, silent)) return;
                if (m_offsets && Alive(scene, m_offsets)) scene.SetNodeVisibleCascade(m_offsets, offEnabled);
            }
        }

        if (from <= STAGE_MESH) {
            if (meshEnabled || (m_mesh && Alive(scene, m_mesh))) {
                if (!ApplyMesh(scene, silent)) return;
                if (m_mesh && Alive(scene, m_mesh)) scene.SetNodeVisibleCascade(m_mesh, meshEnabled);
            }
        }

        if (from <= STAGE_THERMAL) {
            if (thermalEnabled || (m_isoline && Alive(scene, m_isoline))) {
                if (ApplyThermalStage(scene, silent) && m_isoline && Alive(scene, m_isoline))
                    scene.SetNodeVisibleCascade(m_isoline, thermalEnabled);
            }
        }
    }

    Primitive* StagePrim(Stage st) const {
        return st == STAGE_OFFSETS ? m_offsets
             : st == STAGE_MESH    ? m_mesh
             : st == STAGE_THERMAL ? m_isoline
             : nullptr;
    }

    void SetStageVisible(Scene& scene, Stage st, bool show) {
        Primitive* p = StagePrim(st);
        if (show && !(p && Alive(scene, p))) {
            RecomputeFrom(scene, st, false);
            p = StagePrim(st);
        }
        if (p && Alive(scene, p)) scene.SetNodeVisibleCascade(p, show);
    }

    void ResetStage(Scene& scene, Stage st) {
        if (st <= STAGE_OFFSETS)      { DropOffsets(scene); DropMesh(scene); }
        else if (st == STAGE_MESH)    { DropMesh(scene); }
        else if (st == STAGE_THERMAL) { DropIsoline(scene); }
        snprintf(status, sizeof(status), "Reset stage.");
    }

    bool ValidateRevolutionContour(Scene& scene) {
        if (!HasSource()) { Report(scene, false, "Revolution: no staged contour."); return false; }
        CSV3DLoader::CSV3DData d;
        if (!CSV3DLoader::Load(m_srcPath, d)) { Report(scene, false, "Revolution: cannot load contour."); return false; }
        int pos = 0, neg = 0;
        for (const auto& nd : d.nodes) {
            if (nd.pos.x >  1e-4f) ++pos;
            else if (nd.pos.x < -1e-4f) ++neg;
        }
        if (pos > 0 && neg > 0) {
            Report(scene, false,
                "Revolution contour invalid: vertices on both sides of the Y axis "
                "(X>0 and X<0). A revolution profile must lie entirely on one side.");
            return false;
        }
        if (pos == 0 && neg == 0) {
            Report(scene, false, "Revolution contour invalid: contour lies on the Y axis (zero radius).");
            return false;
        }
        return true;
    }

    void ShowSourceRevolution(Scene& scene, bool show) {
        if (show) BuildSourceRevolution(scene);
        else if (Alive(scene, m_srcRevSurf)) m_srcRevSurf->visible = false;
    }

    void ShowIsolineRevolution(Scene& scene, bool show) {
        if (show) BuildIsolineRevolution(scene);
        else if (Alive(scene, m_isoRevSurf)) m_isoRevSurf->visible = false;
    }

    void SetSrcRevAlpha(Scene& scene, float a) {
        srcRevAlpha = a;
        if (Alive(scene, m_srcRevSurf)) { XMFLOAT4 c = m_srcRevSurf->GetColor(); c.w = a; m_srcRevSurf->SetColor(c); }
    }
    void SetIsoRevAlpha(Scene& scene, float a) {
        isoRevAlpha = a;
        if (Alive(scene, m_isoRevSurf)) { XMFLOAT4 c = m_isoRevSurf->GetColor(); c.w = a; m_isoRevSurf->SetColor(c); }
    }

    void DropSrcRev(Scene& scene) {
        if (Alive(scene, m_srcRevSurf)) scene.RemovePrimitive(m_srcRevSurf);
        m_srcRevSurf = nullptr;
    }
    void DropIsoRev(Scene& scene) {
        if (Alive(scene, m_isoRevSurf)) scene.RemovePrimitive(m_isoRevSurf);
        m_isoRevSurf = nullptr;
    }

    bool ApplyThermalStage(Scene& scene, bool silent) {
        if (!HasSource()) return false;
        if (!Alive(scene, m_mesh)) { Report(scene, silent, "Build the mesh first."); return false; }
        if (!m_thermalSolved) { if (!ApplyThermal(scene, silent)) return false; }
        return ApplyIsoline(scene, silent);
    }

    bool ApplyThermal(Scene& scene, bool silent) {
        if (!HasSource()) return false;
        if (!Alive(scene, m_mesh)) { Report(scene, silent, "Build the mesh first."); return false; }
        try {
            int rc = SafeSolveThermal(1.0);
            if (rc == -100) {
                Report(scene, silent, "Thermal solver crashed (access violation caught).");
                return false;
            }
            if (!CheckRc(scene, silent, "SEM_SolveThermal", rc,
                         { "", "No mesh built", "Invalid conductivity", "Solve failed" }))
                return false;

            const char* outPath = SEM_SerializeMesh(nullptr);
            if (!outPath) { Report(scene, silent, "SEM_SerializeMesh failed."); return false; }
            std::string p(outPath);
            DropMesh(scene);
            m_mesh = scene.AddFromCSV3D(p, "mesh_" + Stem(m_srcPath), AttachParent());
            if (m_mesh) scene.AttachVertexPointsGroup(m_mesh);
            m_meshStats = ComputeStats(p);
            m_thermalSolved = true;
            snprintf(status, sizeof(status), "Thermal: %s", BaseName(p).c_str());
            return true;
        }
        catch (const std::exception& e) { Report(scene, silent, std::string("Thermal exception: ") + e.what()); }
        catch (...)                     { Report(scene, silent, "Thermal: unknown exception."); }
        return false;
    }

    bool ApplyIsoline(Scene& scene, bool silent) {
        if (!HasSource()) return false;
        if (!Alive(scene, m_mesh) || !m_thermalSolved) {
            Report(scene, silent, "Solve thermal first."); return false;
        }
        try {
            double v = isoValue;
            if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0;
            int rc = SafeExtractIsoline(v);
            if (rc == -100) { Report(scene, silent, "Isoline extraction crashed (access violation caught)."); return false; }
            if (!CheckRc(scene, silent, "SEM_ExtractIsoline", rc,
                         { "", "No mesh/field", "Invalid value", "Extraction failed" }))
                return false;

            const char* outPath = SEM_SerializeIsoline(nullptr);
            if (!outPath) { Report(scene, silent, "SEM_SerializeIsoline failed."); return false; }
            std::string p(outPath);
            DropIsoline(scene);
            const XMFLOAT4 green(0.0f, 1.0f, 0.0f, 1.0f);
            m_isoline = scene.AddFromCSV3D(p, "isoline_" + Stem(m_srcPath), AttachParent(), &green);
            m_isolinePath = p;
            snprintf(status, sizeof(status), "Isoline T=%.3f: %s", v, BaseName(p).c_str());
            return true;
        }
        catch (const std::exception& e) { Report(scene, silent, std::string("Isoline exception: ") + e.what()); }
        catch (...)                     { Report(scene, silent, "Isoline: unknown exception."); }
        return false;
    }

    Primitive* ImportSource(Scene& scene, const std::string& path) {
        if (path.empty()) return nullptr;
        Primitive* src = nullptr;
        try {
            src = scene.AddFromCSV3D(path);
            if (!src) { Report(scene, false, "Import failed: could not load CSV3D."); return nullptr; }
            src->semSourcePath = path;
            scene.AttachVertexPointsGroup(src);
            scene.stagingEnabled = true;
            scene.SetStaged(src);
            Bind(scene, src);
        }
        catch (const std::exception& e) { Report(scene, false, std::string("Import failed: ") + e.what()); }
        catch (...)                     { Report(scene, false, "Import failed: unknown exception."); }
        return src;
    }

    void RunFullPipeline(Scene& scene) {
        if (!HasSource()) return;

        subEnabled = true;  subMode    = 1;
        offEnabled = true;  offsetMode = OFFSET_EVEN;
        firstGap   = 1.0f;  numOffsets = 8; grading = 1.2f;
        meshEnabled = true; meshMethod = SEM_STEINER_MAX_AREA;
        meshParam  = -1.0f; meshParamEdgeUnits = false;
        thermalEnabled = true;
        isoValue   = 0.5f;

        if (!ApplySubdivide(scene, false)) return;
        if (!ApplyOffsets(scene, false))   return;
        if (!ApplyMesh(scene, false))      return;
        if (!ApplyThermal(scene, false))   return;
        if (!ApplyIsoline(scene, false))   return;
        snprintf(status, sizeof(status), "Pipeline complete.");
    }

private:
    std::string m_srcPath;
    std::string m_isolinePath;
    Primitive*  m_srcPrim = nullptr;
    Primitive*  m_offsets = nullptr;
    Primitive*  m_mesh    = nullptr;
    Primitive*  m_isoline = nullptr;
    Primitive*  m_srcRevSurf = nullptr;
    Primitive*  m_isoRevSurf = nullptr;
    bool        m_thermalSolved = false;
    Stats m_srcStats, m_offStats, m_meshStats;

    bool Alive(Scene& scene, Primitive* q) const {
        if (!q) return false;
        for (Primitive* p : scene.primitives) if (p == q) return true;
        return false;
    }

    void Report(Scene& scene, bool silent, const std::string& msg) {
        snprintf(status, sizeof(status), "%s", msg.c_str());
        if (!silent) ErrorLogger::Log(msg);
    }

    bool CheckRc(Scene& scene, bool silent, const char* call, int rc,
                 std::initializer_list<const char*> errs) {
        if (rc == 0) return true;
        const char* msg = "";
        int idx = -rc, i = 0;
        for (const char* e : errs) { if (i == idx) { msg = e; break; } ++i; }
        Report(scene, silent, std::string(call) + " failed (" + std::to_string(rc) + "): " + msg);
        return false;
    }

    SceneNode* AttachParent() { return m_srcPrim ? static_cast<SceneNode*>(m_srcPrim) : nullptr; }

    void BuildSourceRevolution(Scene& scene) {
        if (Alive(scene, m_srcRevSurf)) { m_srcRevSurf->visible = true; return; }
        std::vector<XMFLOAT3> prof;
        if (!OrderedContourFromCSV3D(m_srcPath, prof)) {
            Report(scene, false, "Revolution: cannot read source contour."); return;
        }
        const XMFLOAT4 steel(0.70f, 0.72f, 0.78f, srcRevAlpha);
        m_srcRevSurf = scene.AddRevolutionSurface(prof, (UINT)revSegments, steel,
                                                  "revsurf_src_" + Stem(m_srcPath), AttachParent());
        if (m_srcRevSurf) snprintf(status, sizeof(status), "Source revolution surface built.");
    }

    void BuildIsolineRevolution(Scene& scene) {
        if (Alive(scene, m_isoRevSurf)) { m_isoRevSurf->visible = true; return; }
        if (m_isolinePath.empty()) { Report(scene, false, "Revolution: extract the isotherm first."); return; }
        std::vector<XMFLOAT3> prof;
        if (!OrderedContourFromCSV3D(m_isolinePath, prof)) {
            Report(scene, false, "Revolution: cannot read isotherm contour."); return;
        }
        const XMFLOAT4 green(0.10f, 0.90f, 0.20f, isoRevAlpha);
        m_isoRevSurf = scene.AddRevolutionSurface(prof, (UINT)revSegments, green,
                                                  "revsurf_iso_" + Stem(m_srcPath), AttachParent());
        if (m_isoRevSurf) snprintf(status, sizeof(status), "Isotherm revolution surface built.");
    }

    void DropOffsets(Scene& scene) {
        if (Alive(scene, m_offsets)) scene.RemovePrimitive(m_offsets);
        m_offsets = nullptr; m_offStats = Stats();
    }
    void DropMesh(Scene& scene) {
        if (Alive(scene, m_mesh)) scene.RemovePrimitive(m_mesh);
        m_mesh = nullptr; m_meshStats = Stats();
        m_thermalSolved = false;
        DropIsoline(scene);
    }
    void DropIsoline(Scene& scene) {
        if (Alive(scene, m_isoline)) scene.RemovePrimitive(m_isoline);
        m_isoline = nullptr;
        DropIsoRev(scene);
        m_isolinePath.clear();
    }

    bool ApplySubdivide(Scene& scene, bool silent) {
        try {
            int n = !subEnabled ? -1
                  : (subMode == 0) ? -1
                  : (subMode == 1) ? 0
                  : (subN < 1 ? 1 : subN);
            int rc = SEM_SubdivideContour(n);
            return CheckRc(scene, silent, "SEM_SubdivideContour", rc,
                           { "", "No source loaded", "Invalid argument" });
        }
        catch (const std::exception& e) { Report(scene, silent, std::string("Subdivide exception: ") + e.what()); }
        catch (...)                     { Report(scene, silent, "Subdivide: unknown exception."); }
        return false;
    }

    bool ApplyOffsets(Scene& scene, bool silent) {
        try {
            int rc;
            if (offsetMode == OFFSET_GAPS) {
                if (gaps.empty()) { Report(scene, silent, "Add at least one gap."); return false; }
                std::vector<double> g(gaps.begin(), gaps.end());
                rc = SEM_ComputeOffsetsAt(g.data(), (int)g.size());
            } else {
                rc = SEM_ComputeOffsets(firstGap, numOffsets, grading);
            }
            if (!CheckRc(scene, silent, "SEM_ComputeOffsets", rc,
                         { "", "No source loaded", "Invalid parameters" }))
                return false;

            const char* outPath = SEM_SerializeOffsets(nullptr);
            if (!outPath) { Report(scene, silent, "SEM_SerializeOffsets failed."); return false; }
            std::string p(outPath);
            DropOffsets(scene);
            m_offsets = scene.AddFromCSV3D(p, "offsets_" + Stem(m_srcPath), AttachParent());
            if (m_offsets) scene.AttachVertexPointsGroup(m_offsets);
            m_offStats = ComputeStats(p);
            snprintf(status, sizeof(status), "Offsets: %s", BaseName(p).c_str());
            return true;
        }
        catch (const std::exception& e) { Report(scene, silent, std::string("Offsets exception: ") + e.what()); }
        catch (...)                     { Report(scene, silent, "Offsets: unknown exception."); }
        return false;
    }

    bool ApplyMesh(Scene& scene, bool silent) {
        try {
            double param = (double)meshParam;
            if (meshParamEdgeUnits && meshParam > 0.0f &&
                (meshMethod == SEM_STEINER_MAX_AREA || meshMethod == SEM_STEINER_SIZING))
                param *= MeshParamFactor();
            SEM_MeshParamsEx params{ meshMethod, param, (double)steinerMargin };
            int rc = SafeBuildMeshEx(&params);
            if (rc == -100) {
                Report(scene, silent, "Mesher DLL crashed (access violation caught). "
                                      "Try another Steiner method or a larger parameter.");
                return false;
            }
            if (!CheckRc(scene, silent, "SEM_BuildMeshEx", rc,
                         { "", "No source loaded", "Compute offsets first",
                           "Not enough valid lines", "Triangulation failed", "Invalid method" }))
                return false;

            const char* outPath = SEM_SerializeMesh(nullptr);
            if (!outPath) { Report(scene, silent, "SEM_SerializeMesh failed."); return false; }
            std::string p(outPath);
            DropMesh(scene);
            m_mesh = scene.AddFromCSV3D(p, "mesh_" + Stem(m_srcPath), AttachParent());
            if (m_mesh) scene.AttachVertexPointsGroup(m_mesh);
            m_meshStats = ComputeStats(p);
            snprintf(status, sizeof(status), "Mesh: %s", BaseName(p).c_str());
            return true;
        }
        catch (const std::exception& e) { Report(scene, silent, std::string("Mesh exception: ") + e.what()); }
        catch (...)                     { Report(scene, silent, "Mesh: unknown exception."); }
        return false;
    }
};

}
