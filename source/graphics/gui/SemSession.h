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

enum Stage { STAGE_SUBDIVIDE = 0, STAGE_OFFSETS = 1, STAGE_MESH = 2 };
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

inline int SafeBuildMeshEx(const SEM_MeshParamsEx* params) {
    __try { return SEM_BuildMeshEx(params); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -100; }
}

class SemSession {
public:
    int   subMode    = 0;
    int   subN       = 2;
    bool  subEnabled = false;

    int   offsetMode = OFFSET_EVEN;
    float dMax       = 100.0f;
    int   numOffsets = 3;
    float grading    = 1.0f;
    std::vector<float> gaps = { 25.0f, 25.0f, 25.0f };
    bool  offEnabled = false;

    int   meshMethod    = SEM_STEINER_GRID;
    float meshParam     = -1.0f;
    float steinerMargin = 0.45f;
    bool  meshEnabled   = false;

    bool  subAuto  = false;
    bool  offAuto  = false;
    bool  meshAuto = false;

    char  status[256] = "Ready";

    const std::string& SourcePath() const { return m_srcPath; }
    Primitive*  SourcePrim()  const { return m_srcPrim; }
    Primitive*  OffsetsPrim() const { return m_offsets; }
    Primitive*  MeshPrim()    const { return m_mesh; }
    bool        HasSource()   const { return m_srcPrim != nullptr && !m_srcPath.empty(); }

    const Stats& SrcStats()  const { return m_srcStats; }
    const Stats& OffStats()  const { return m_offStats; }
    const Stats& MeshStats() const { return m_meshStats; }

    void Bind(Scene& scene, Primitive* prim) {
        if (prim == m_srcPrim) return;
        m_srcPrim   = prim;
        m_srcPath   = prim ? prim->semSourcePath : std::string();
        m_offsets   = nullptr;
        m_mesh      = nullptr;
        m_offStats  = Stats();
        m_meshStats = Stats();
        if (!m_srcPath.empty()) {
            int rc = SEM_LoadCSV3D(m_srcPath.c_str());
            if (rc != 0) Report(scene, true, "SEM_LoadCSV3D failed (" + std::to_string(rc) + ")");
        }
        m_srcStats = ComputeStats(m_srcPath);
        if (HasSource()) snprintf(status, sizeof(status), "Staged: %s", BaseName(m_srcPath).c_str());
        else             snprintf(status, sizeof(status), "Ready");
    }

    void Unbind() {
        m_srcPrim = nullptr; m_srcPath.clear();
        m_offsets = nullptr; m_mesh = nullptr;
        m_srcStats = m_offStats = m_meshStats = Stats();
        snprintf(status, sizeof(status), "Ready");
    }

    void Validate(Scene& scene) {
        if (m_srcPrim && !Alive(scene, m_srcPrim)) { Unbind(); return; }
        if (m_offsets && !Alive(scene, m_offsets)) { m_offsets = nullptr; m_offStats = Stats(); }
        if (m_mesh    && !Alive(scene, m_mesh))    { m_mesh    = nullptr; m_meshStats = Stats(); }
    }

    void RecomputeFrom(Scene& scene, Stage from, bool silent) {
        if (!HasSource()) return;
        if (from <= STAGE_SUBDIVIDE && !ApplySubdivide(scene, silent)) return;

        if (from <= STAGE_OFFSETS) {
            if (offEnabled) { if (!ApplyOffsets(scene, silent)) return; }
            else            { DropOffsets(scene); DropMesh(scene); return; }
        }

        if (from <= STAGE_MESH) {
            if (meshEnabled) ApplyMesh(scene, silent);
            else             DropMesh(scene);
        }
    }

private:
    std::string m_srcPath;
    Primitive*  m_srcPrim = nullptr;
    Primitive*  m_offsets = nullptr;
    Primitive*  m_mesh    = nullptr;
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

    void DropOffsets(Scene& scene) {
        if (Alive(scene, m_offsets)) scene.RemovePrimitive(m_offsets);
        m_offsets = nullptr; m_offStats = Stats();
    }
    void DropMesh(Scene& scene) {
        if (Alive(scene, m_mesh)) scene.RemovePrimitive(m_mesh);
        m_mesh = nullptr; m_meshStats = Stats();
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
                rc = SEM_ComputeOffsets(dMax, numOffsets, grading);
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
            SEM_MeshParamsEx params{ meshMethod, (double)meshParam, (double)steinerMargin };
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

} // namespace SemSessionNS
