#pragma once
#include "../scene/scene.h"
#include "../../external/sem_exports.h"
#include "../../utils/errorLogger.h"
#include "../../loaders/CSV3DLoader.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <cmath>
#include <cstdio>
#include <algorithm>
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

// True when the .csv3d at 'path' is an OPEN contour — a single polyline with
// exactly two endpoints (degree-1 nodes) — whose two endpoints both lie on the
// Y axis (x≈0, z≈0). That is the half-profile shape expected by surface-of-
// revolution mode, so import can auto-enable it. A closed loop (no degree-1
// nodes) or a branching/meshed contour returns false.
inline bool IsOpenContourOnYAxis(const std::string& path) {
    CSV3DLoader::CSV3DData d;
    if (!CSV3DLoader::Load(path, d)) return false;
    const size_t n = d.nodes.size();
    if (n < 2) return false;

    // Gather every edge (explicit edges, face boundaries, triangle sides), then
    // dedupe so a shared edge is not counted twice when computing node degree.
    std::set<std::pair<unsigned, unsigned>> uniq;
    auto add = [&](unsigned a, unsigned b) {
        if (a == b || a >= n || b >= n) return;
        uniq.insert(a < b ? std::make_pair(a, b) : std::make_pair(b, a));
    };
    for (const auto& e : d.edges) add(e.first, e.second);
    for (const auto& f : d.faces) {
        const size_t m = f.size();
        for (size_t i = 0; i + 1 < m; ++i) add(f[i], f[i + 1]);
    }
    for (const auto& t : d.triangles) { add(t.x, t.y); add(t.y, t.z); add(t.z, t.x); }
    if (uniq.empty()) return false;

    std::vector<int> degree(n, 0);
    for (const auto& e : uniq) { ++degree[e.first]; ++degree[e.second]; }

    std::vector<unsigned> ends;
    for (size_t i = 0; i < n; ++i)
        if (degree[i] == 1) ends.push_back((unsigned)i);
    if (ends.size() != 2) return false;   // closed loop or branching → not an open profile

    auto onYAxis = [](const XMFLOAT3& p) {
        return std::fabs(p.x) < 1e-4f && std::fabs(p.z) < 1e-4f;
    };
    return onYAxis(d.nodes[ends[0]].pos) && onYAxis(d.nodes[ends[1]].pos);
}

// Decide whether a freshly imported .csv3d should drive the 2D contour
// pipeline (SEM_LoadCSV3D + SEM_*) or the 3D surface pipeline (SEM_LoadSurface3D
// + SEM_*3D). A 2D SEM source is a contour: edges only, lying in a plane. A 3D
// source is a triangle surface mesh that spans all three axes. Rule: triangles
// present AND the node cloud is not coplanar (its thinnest bounding-box extent
// is a non-trivial fraction of the largest) => 3. Everything else — a pure
// contour, a planar triangulated patch, or an unreadable file — => 2.
inline int DetectSemDim(const std::string& path) {
    CSV3DLoader::CSV3DData d;
    if (!CSV3DLoader::Load(path, d) || d.nodes.empty()) return 2;
    if (d.triangles.empty()) return 2;

    XMFLOAT3 lo = d.nodes[0].pos, hi = d.nodes[0].pos;
    for (const auto& nd : d.nodes) {
        lo.x = std::min(lo.x, nd.pos.x); hi.x = std::max(hi.x, nd.pos.x);
        lo.y = std::min(lo.y, nd.pos.y); hi.y = std::max(hi.y, nd.pos.y);
        lo.z = std::min(lo.z, nd.pos.z); hi.z = std::max(hi.z, nd.pos.z);
    }
    float ex = hi.x - lo.x, ey = hi.y - lo.y, ez = hi.z - lo.z;
    float maxE = std::max(ex, std::max(ey, ez));
    float minE = std::min(ex, std::min(ey, ez));
    if (maxE <= 1e-6f) return 2;
    return (minE / maxE > 1e-3f) ? 3 : 2;
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

inline int SafeBuildMesh3D(const SEM_MeshParams3D* params) {
    __try { return SEM_BuildMesh3D(params); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -100; }
}

inline int SafeBuildMesh3DEx(const SEM_MeshParams3DEx* params) {
    __try { return SEM_BuildMesh3DEx(params); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -100; }
}

inline int SafeSolveThermal3D(double conductivity) {
    __try { return SEM_SolveThermal3D(conductivity); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -100; }
}

inline int SafeExtractIsosurface3D(double value) {
    __try { return SEM_ExtractIsosurface3D(value); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -100; }
}

// Verbose detail for the most recent SEM_* failure, taken from the library's
// SEM_GetLastError(). Returned pre-formatted as " — <text>" so it can be
// appended directly to a status/log message, or "" when the library has no
// detail. MUST be evaluated immediately after the failing SEM_* call and
// before any other SEM_* call — the library owns the buffer and overwrites it
// on the next call.
inline std::string SemDetail() {
    const char* e = SEM_GetLastError();
    return (e && *e) ? std::string(" — ") + e : std::string();
}

class SemSession {
public:
    // Pipeline dimension of the staged source, auto-detected on Bind:
    //   2 = contour pipeline (SEM_LoadCSV3D, SEM_*),
    //   3 = triangle-surface pipeline (SEM_LoadSurface3D, SEM_*3D).
    int   dim        = 2;

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

    // 3D (surface) mesh tuning — TetGen, via SEM_BuildMesh3DEx. Parallel to the
    // 2D meshMethod/meshParam pair: tetMethod picks the refinement strategy
    // (SEM_TetMethod), tetParam is its primary knob (<0 => per-method auto).
    // tetParamEdgeUnits expresses the volume/length knob in multiples of the
    // source surface's mean edge length instead of model units.
    int   tetMethod    = SEM_TET_MAX_VOL;
    float tetParam     = -1.0f;
    bool  tetParamEdgeUnits = false;

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

    // Opacity applied to every ColoredTriangles surface built by the 3D SEM
    // pipeline (source / offsets / mesh / isosurface). Default 0.5.
    float surf3dAlpha    = 0.5f;

    // When true, every triangle-bearing pipeline primitive is rebuilt as edge
    // wireframe (ColoredLine) instead of a filled ColoredTriangles surface.
    // Toggled from the SEM window; RegenerateGeometry applies it by reloading
    // each primitive from its saved CSV3D file.
    bool  renderTrisAsLines = false;

    char  status[256] = "Ready";

    const std::string& SourcePath() const { return m_srcPath; }
    Primitive*  SourcePrim()  const { return m_srcPrim; }
    Primitive*  OffsetsPrim() const { return m_offsets; }
    Primitive*  MeshPrim()    const { return m_mesh; }
    Primitive*  IsolinePrim() const { return m_isoline; }
    Primitive*  SrcRevSurf()  const { return m_srcRevSurf; }
    Primitive*  IsoRevSurf()  const { return m_isoRevSurf; }
    bool        HasSource()   const { return m_srcPrim != nullptr && !m_srcPath.empty(); }
    int         Dim()         const { return dim; }
    bool        ThermalSolved() const { return m_thermalSolved; }
    bool        HasIsolinePath() const { return !m_isolinePath.empty(); }

    double MeshParamFactor() const {
        double avg = SEM_GetAvgEdgeLen();
        if (avg <= 0.0) avg = 1.0;
        return (meshMethod == SEM_STEINER_MAX_AREA) ? avg * avg : avg;
    }

    // Edge-length multiplier for the 3D tet knob: cube of the mean surface edge
    // length for the volume method, the length itself for sizing.
    double TetParamFactor() const {
        double avg = SEM_GetSurfaceAvgEdgeLen3D();
        if (avg <= 0.0) avg = 1.0;
        return (tetMethod == SEM_TET_MAX_VOL) ? avg * avg * avg : avg;
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
        m_offsetsPath.clear();
        m_meshPath.clear();
        m_isolinePath.clear();
        m_thermalSolved = false;
        m_offStats  = Stats();
        m_meshStats = Stats();
        // A fresh source clears the SEM core cache (SEM_LoadCSV3D below), so every
        // stage must be recomputed before it can be shown.
        m_dirty[0] = m_dirty[1] = m_dirty[2] = m_dirty[3] = true;
        if (!m_srcPath.empty()) {
            dim = DetectSemDim(m_srcPath);
            int rc = (dim == 3) ? SEM_LoadSurface3D(m_srcPath.c_str())
                                : SEM_LoadCSV3D(m_srcPath.c_str());
            if (rc != 0)
                Report(scene, false, (dim == 3 ? "SEM_LoadSurface3D failed ("
                                               : "SEM_LoadCSV3D failed (")
                                    + std::to_string(rc) + ")" + SemDetail());
        }
        m_srcStats = ComputeStats(m_srcPath);
        if (HasSource()) snprintf(status, sizeof(status), "Staged: %s", BaseName(m_srcPath).c_str());
        else             snprintf(status, sizeof(status), "Ready");
    }

    void Unbind() {
        m_srcPrim = nullptr; m_srcPath.clear();
        m_offsets = nullptr; m_mesh = nullptr; m_isoline = nullptr;
        m_srcRevSurf = nullptr; m_isoRevSurf = nullptr;
        m_offsetsPath.clear(); m_meshPath.clear(); m_isolinePath.clear();
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

    // Mark a stage's result stale because its own parameters changed. The next
    // Apply (or auto-apply) of this or any later stage will recompute it.
    void MarkStageDirty(Stage st) {
        if (st >= STAGE_SUBDIVIDE && st <= STAGE_THERMAL) m_dirty[st] = true;
    }

    // Bring the pipeline up to (and including) stage 'to', and no further. Each
    // stage is recomputed only when its own parameters changed since it was last
    // applied (m_dirty), or when an upstream stage was just rebuilt this pass and
    // so invalidated its input. Stages downstream of 'to' are left untouched, but
    // flagged dirty if anything was rebuilt, so a later Apply of theirs redoes
    // them. This replaces the old "recompute everything from 'from' to the end".
    void RecomputeUpTo(Scene& scene, Stage to, bool silent) {
        if (!HasSource()) return;
        bool ran = false;

        if (m_dirty[STAGE_SUBDIVIDE]) {
            if (!ApplySubdivide(scene, silent)) return;
            m_dirty[STAGE_SUBDIVIDE] = false;
            ran = true;
        }

        if (to >= STAGE_OFFSETS && (m_dirty[STAGE_OFFSETS] || ran)) {
            if (offEnabled || (m_offsets && Alive(scene, m_offsets))) {
                if (!ApplyOffsets(scene, silent)) return;
                if (m_offsets && Alive(scene, m_offsets)) scene.SetNodeVisibleCascade(m_offsets, offEnabled);
                ran = true;
            }
            m_dirty[STAGE_OFFSETS] = false;
        }

        if (to >= STAGE_MESH && (m_dirty[STAGE_MESH] || ran)) {
            if (meshEnabled || (m_mesh && Alive(scene, m_mesh))) {
                if (!ApplyMesh(scene, silent)) return;
                if (m_mesh && Alive(scene, m_mesh)) scene.SetNodeVisibleCascade(m_mesh, meshEnabled);
                ran = true;
            }
            m_dirty[STAGE_MESH] = false;
        }

        if (to >= STAGE_THERMAL && (m_dirty[STAGE_THERMAL] || ran)) {
            if (thermalEnabled || (m_isoline && Alive(scene, m_isoline))) {
                if (ApplyThermalStage(scene, silent) && m_isoline && Alive(scene, m_isoline))
                    scene.SetNodeVisibleCascade(m_isoline, thermalEnabled);
                ran = true;
            }
            m_dirty[STAGE_THERMAL] = false;
        }

        // Rebuilding a stage invalidates everything after 'to' that we did not
        // touch — mark it stale so its own Apply rebuilds it on demand.
        if (ran)
            for (int s = (int)to + 1; s <= STAGE_THERMAL; ++s) m_dirty[s] = true;

        scene.UpdateLight();
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
            RecomputeUpTo(scene, st, false);
            p = StagePrim(st);
        }
        if (p && Alive(scene, p)) scene.SetNodeVisibleCascade(p, show);
    }

    void ResetStage(Scene& scene, Stage st) {
        if (st <= STAGE_OFFSETS)      { DropOffsets(scene); DropMesh(scene);
                                        m_dirty[STAGE_OFFSETS] = m_dirty[STAGE_MESH] = m_dirty[STAGE_THERMAL] = true; }
        else if (st == STAGE_MESH)    { DropMesh(scene);
                                        m_dirty[STAGE_MESH] = m_dirty[STAGE_THERMAL] = true; }
        else if (st == STAGE_THERMAL) { DropIsoline(scene);
                                        m_dirty[STAGE_THERMAL] = true; }
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

    // SEM_SetRevolution axis selector: 1 = X, 2 = Y, 3 = Z. Profiles revolve
    // around the Y axis here.
    static constexpr int kRevolutionAxisY = 2;

    // Enable/disable revolution mode and keep the SEM core in sync via
    // SEM_SetRevolution. When enabling, the staged contour is validated first
    // and the core's own checks (endpoints on axis, no crossing) are surfaced.
    // Returns the resulting revolutionMode state.
    bool SetRevolutionMode(Scene& scene, bool enable) {
        if (!enable) {
            SEM_SetRevolution(0, kRevolutionAxisY);
            revolutionMode = false;
            return false;
        }
        if (!ValidateRevolutionContour(scene)) {
            SEM_SetRevolution(0, kRevolutionAxisY);
            revolutionMode = false;
            return false;
        }
        int rc = SEM_SetRevolution(1, kRevolutionAxisY);
        if (rc != 0) {
            CheckRc(scene, false, "SEM_SetRevolution", rc,
                    { "", "no source", "bad axis", "endpoints off axis", "contour crosses axis" });
            SEM_SetRevolution(0, kRevolutionAxisY);
            revolutionMode = false;
            return false;
        }
        revolutionMode = true;
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

    // Apply the 3D surface opacity to every currently-built 3D pipeline surface.
    void SetSurf3dAlpha(Scene& scene, float a) {
        surf3dAlpha = a;
        for (Primitive* p : { m_srcPrim, m_offsets, m_mesh, m_isoline })
            if (Alive(scene, p)) p->SetAlpha(a);
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
            int rc = (dim == 3) ? SafeSolveThermal3D(1.0) : SafeSolveThermal(1.0);
            if (rc == -100) {
                Report(scene, silent, "Thermal solver crashed (access violation caught).");
                return false;
            }
            if (!CheckRc(scene, silent, dim == 3 ? "SEM_SolveThermal3D" : "SEM_SolveThermal", rc,
                         { "", "No mesh built", "Invalid conductivity", "Solve failed" }))
                return false;

            const char* outPath = (dim == 3) ? SEM_SerializeMesh3D(nullptr)
                                             : SEM_SerializeMesh(nullptr);
            if (!outPath) { Report(scene, silent, "SEM_SerializeMesh failed." + SemDetail()); return false; }
            std::string p(outPath);
            DropMesh(scene);
            m_mesh = scene.AddFromCSV3D(p, "mesh_" + Stem(m_srcPath), AttachParent(), nullptr, Colors::BLUE, Colors::RED, renderTrisAsLines);
            m_meshPath = p;
            if (dim == 3 && !renderTrisAsLines) ConfigureSurface3D(m_mesh);
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
        bool revWasShown = Alive(scene, m_isoRevSurf) && m_isoRevSurf->visible;
        try {
            double v = isoValue;
            if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0;
            int rc = (dim == 3) ? SafeExtractIsosurface3D(v) : SafeExtractIsoline(v);
            if (rc == -100) {
                Report(scene, silent, (dim == 3 ? "Isosurface" : "Isoline")
                       + std::string(" extraction crashed (access violation caught)."));
                return false;
            }
            if (!CheckRc(scene, silent, dim == 3 ? "SEM_ExtractIsosurface3D" : "SEM_ExtractIsoline", rc,
                         { "", "No mesh/field", "Invalid value", "Extraction failed" }))
                return false;

            const char* outPath = (dim == 3) ? SEM_SerializeIsosurface3D(nullptr)
                                             : SEM_SerializeIsoline(nullptr);
            if (!outPath) {
                Report(scene, silent, (dim == 3 ? "SEM_SerializeIsosurface3D failed."
                                                : "SEM_SerializeIsoline failed.") + SemDetail());
                return false;
            }
            std::string p(outPath);
            DropIsoline(scene);
            const XMFLOAT4 green(0.0f, 1.0f, 0.0f, 1.0f);
            const std::string namePrefix = (dim == 3 ? "isosurface_" : "isoline_");
            m_isoline = scene.AddFromCSV3D(p, namePrefix + Stem(m_srcPath), AttachParent(), &green, Colors::BLUE, Colors::RED, renderTrisAsLines);
            if (dim == 3 && !renderTrisAsLines) ConfigureSurface3D(m_isoline);   // isosurface is a triangle mesh
            m_isolinePath = p;
            if (revWasShown && m_isoline) BuildIsolineRevolution(scene);
            snprintf(status, sizeof(status), "%s T=%.3f: %s",
                     dim == 3 ? "Isosurface" : "Isoline", v, BaseName(p).c_str());
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
            // Source surface is shown exactly as authored: no winding fix-up.
            src = scene.AddFromCSV3D(path, "", nullptr, nullptr, Colors::BLUE, Colors::RED, false, false);
            if (!src) { Report(scene, false, "Import failed: could not load CSV3D."); return nullptr; }
            src->semSourcePath = path;
            scene.AttachVertexPointsGroup(src);
            scene.stagingEnabled = true;
            scene.SetStaged(src);
            Bind(scene, src);
            // Bind auto-detects the pipeline dimension; a 3D source surface is a
            // ColoredTriangles mesh, so make it semi-transparent.
            if (dim == 3) ConfigureSurface3D(src);
            scene.UpdateLight();
            // Revolution mode is a 2D-contour feature only. For a 2D source, an
            // open half-profile whose endpoints sit on the Y axis is a surface-
            // of-revolution contour — turn the mode on automatically (and sync
            // the SEM core); otherwise make sure it is off. A 3D surface source
            // never uses revolution.
            if (dim == 2) SetRevolutionMode(scene, IsOpenContourOnYAxis(path));
            else          { SEM_SetRevolution(0, kRevolutionAxisY); revolutionMode = false; }
        }
        catch (const std::exception& e) { Report(scene, false, std::string("Import failed: ") + e.what()); }
        catch (...)                     { Report(scene, false, "Import failed: unknown exception."); }
        return src;
    }

    void RunFullPipeline(Scene& scene) {
        if (!HasSource()) return;

        subEnabled = false;
        offEnabled = true;  offsetMode = OFFSET_EVEN;
        firstGap   = 1.0f;  numOffsets = 8; grading = 1.2f;
        meshEnabled = true;
        thermalEnabled = true;
        isoValue   = 0.5f;
        if (dim == 3) {
            tetMethod = SEM_TET_MAX_VOL;
            tetParam  = -1.0f; tetParamEdgeUnits = false;
        } else {
            meshMethod = SEM_STEINER_MAX_AREA;
            meshParam  = -1.0f; meshParamEdgeUnits = false;
        }

        if (!ApplySubdivide(scene, false)) return;
        if (!ApplyOffsets(scene, false))   return;
        if (!ApplyMesh(scene, false))      return;
        if (!ApplyThermal(scene, false))   return;
        if (!ApplyIsoline(scene, false))   return;
        m_dirty[0] = m_dirty[1] = m_dirty[2] = m_dirty[3] = false;
        snprintf(status, sizeof(status), "Pipeline complete.");
    }

private:
    std::string m_srcPath;
    std::string m_offsetsPath;   // last serialized offsets file (for regeneration)
    std::string m_meshPath;      // last serialized mesh file (for regeneration)
    std::string m_isolinePath;
    Primitive*  m_srcPrim = nullptr;
    Primitive*  m_offsets = nullptr;
    Primitive*  m_mesh    = nullptr;
    Primitive*  m_isoline = nullptr;
    Primitive*  m_srcRevSurf = nullptr;
    Primitive*  m_isoRevSurf = nullptr;
    bool        m_thermalSolved = false;
    Stats m_srcStats, m_offStats, m_meshStats;

    // Per-stage staleness, indexed by Stage. A stage is dirty when its own
    // parameters changed since it was last applied, or when it has never been
    // applied against the currently loaded source (Bind resets all to true).
    bool        m_dirty[4] = { true, true, true, true };

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
        Report(scene, silent, std::string(call) + " failed (" + std::to_string(rc) + "): " + msg + SemDetail());
        return false;
    }

    SceneNode* AttachParent() { return m_srcPrim ? static_cast<SceneNode*>(m_srcPrim) : nullptr; }

    // Every ColoredTriangles surface produced by the 3D pipeline is drawn at the
    // configurable surface opacity. Called right after such a surface is created
    // (only when dim == 3). Back-face culling is controlled globally from the
    // NavCube no-cull toggle (Scene::rsNoCull).
    void ConfigureSurface3D(Primitive* p) {
        if (!p) return;
        p->SetAlpha(surf3dAlpha);
    }

    void BuildSourceRevolution(Scene& scene) {
        if (Alive(scene, m_srcRevSurf)) { m_srcRevSurf->visible = true; return; }
        std::vector<XMFLOAT3> prof;
        if (!OrderedContourFromCSV3D(m_srcPath, prof)) {
            Report(scene, false, "Revolution: cannot read source contour."); return;
        }
        const XMFLOAT4 steel(0.70f, 0.72f, 0.78f, srcRevAlpha);
        m_srcRevSurf = scene.AddRevolutionSurface(prof, (UINT)revSegments, steel,
                                                  "revsurf_src_" + Stem(m_srcPath), AttachParent());
		scene.UpdateLight();
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
        SceneNode* parent = Alive(scene, m_isoline) ? static_cast<SceneNode*>(m_isoline) : AttachParent();
        m_isoRevSurf = scene.AddRevolutionSurface(prof, (UINT)revSegments, green,
                                                  "revsurf_iso_" + Stem(m_srcPath), parent);
        scene.UpdateLight();
        if (m_isoRevSurf) snprintf(status, sizeof(status), "Isotherm revolution surface built.");
    }

    void DropOffsets(Scene& scene) {
        if (Alive(scene, m_offsets)) scene.RemovePrimitive(m_offsets);
        m_offsets = nullptr; m_offStats = Stats(); m_offsetsPath.clear();
    }
    void DropMesh(Scene& scene) {
        if (Alive(scene, m_mesh)) scene.RemovePrimitive(m_mesh);
        m_mesh = nullptr; m_meshStats = Stats(); m_meshPath.clear();
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
            if (dim == 3) {
                int rc = SEM_SubdivideSurface3D(n);
                return CheckRc(scene, silent, "SEM_SubdivideSurface3D", rc,
                               { "", "No surface loaded", "Invalid argument" });
            }
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
                rc = (dim == 3) ? SEM_ComputeOffsetsAt3D(g.data(), (int)g.size())
                                : SEM_ComputeOffsetsAt(g.data(), (int)g.size());
            } else {
                rc = (dim == 3) ? SEM_ComputeOffsets3D(firstGap, numOffsets, grading)
                                : SEM_ComputeOffsets(firstGap, numOffsets, grading);
            }
            if (!CheckRc(scene, silent, dim == 3 ? "SEM_ComputeOffsets3D" : "SEM_ComputeOffsets", rc,
                         { "", "No source loaded", "Invalid parameters" }))
                return false;

            const char* outPath = (dim == 3) ? SEM_SerializeOffsets3D(nullptr)
                                             : SEM_SerializeOffsets(nullptr);
            if (!outPath) { Report(scene, silent, "SEM_SerializeOffsets failed." + SemDetail()); return false; }
            std::string p(outPath);
            DropOffsets(scene);
            m_offsets = scene.AddFromCSV3D(p, "offsets_" + Stem(m_srcPath), AttachParent(), nullptr, Colors::CYAN, Colors::YELLOW, renderTrisAsLines);
            m_offsetsPath = p;
            if (dim == 3 && !renderTrisAsLines) ConfigureSurface3D(m_offsets);
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
            int rc;
            if (dim == 3) {
                double param = (double)tetParam;
                if (tetParamEdgeUnits && tetParam > 0.0f &&
                    (tetMethod == SEM_TET_MAX_VOL || tetMethod == SEM_TET_SIZING))
                    param *= TetParamFactor();
                SEM_MeshParams3DEx params3d{ tetMethod, param };
                rc = SafeBuildMesh3DEx(&params3d);
                if (rc == -100) {
                    Report(scene, silent, "TetGen DLL crashed (access violation caught). "
                                          "Try a larger volume or a looser quality bound.");
                    return false;
                }
                if (!CheckRc(scene, silent, "SEM_BuildMesh3DEx", rc,
                             { "", "No surface loaded", "Compute offsets first",
                               "Tetrahedralization failed", "Invalid parameters", "Invalid method" }))
                    return false;
            } else {
                double param = (double)meshParam;
                if (meshParamEdgeUnits && meshParam > 0.0f &&
                    (meshMethod == SEM_STEINER_MAX_AREA || meshMethod == SEM_STEINER_SIZING))
                    param *= MeshParamFactor();
                SEM_MeshParamsEx params{ meshMethod, param, (double)steinerMargin };
                rc = SafeBuildMeshEx(&params);
                if (rc == -100) {
                    Report(scene, silent, "Mesher DLL crashed (access violation caught). "
                                          "Try another Steiner method or a larger parameter.");
                    return false;
                }
                if (!CheckRc(scene, silent, "SEM_BuildMeshEx", rc,
                             { "", "No source loaded", "Compute offsets first",
                               "Not enough valid lines", "Triangulation failed", "Invalid method" }))
                    return false;
            }

            const char* outPath = (dim == 3) ? SEM_SerializeMesh3D(nullptr)
                                             : SEM_SerializeMesh(nullptr);
            if (!outPath) { Report(scene, silent, "SEM_SerializeMesh failed." + SemDetail()); return false; }
            std::string p(outPath);
            DropMesh(scene);
            m_mesh = scene.AddFromCSV3D(p, "mesh_" + Stem(m_srcPath), AttachParent(), nullptr, Colors::CYAN, Colors::YELLOW, renderTrisAsLines);
            m_meshPath = p;
            if (dim == 3 && !renderTrisAsLines) ConfigureSurface3D(m_mesh);
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
