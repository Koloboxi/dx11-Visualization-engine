#include "SemSession.h"
#include "../../utils/errorLogger.h"
#include "../../loaders/CSV3DLoader.h"
#include <map>
#include <set>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <excpt.h>

namespace fs = std::filesystem;

namespace SemSessionNS {

namespace {

std::string BaseName(const std::string& path) {
    auto slash = path.find_last_of("\\/");
    return (slash != std::string::npos) ? path.substr(slash + 1) : path;
}
std::string Stem(const std::string& path) {
    std::string file = BaseName(path);
    auto dot = file.find_last_of('.');
    return (dot != std::string::npos) ? file.substr(0, dot) : file;
}

Stats ComputeStats(const std::string& path) {
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

bool OrderedContourFromCSV3D(const std::string& path, std::vector<XMFLOAT3>& out) {
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
bool IsOpenContourOnYAxis(const std::string& path) {
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
int DetectSemDim(const std::string& path) {
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

// SEH guards around the meshing/solver entry points: the bundled Triangle and
// TetGen libraries can raise access violations deep inside, which C++ try/catch
// cannot intercept. The wrappers return -100 so the host app survives.
int SafeBuildMesh(const SEM_MeshParams* params) {
    __try { return SEM_BuildMesh(params); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -100; }
}
int SafeBuildMesh3D(const SEM_MeshParams3D* params) {
    __try { return SEM_BuildMesh3D(params); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -100; }
}
int SafeSolveThermal() {
    __try { return SEM_SolveThermal(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -100; }
}
int SafeSolveThermal3D() {
    __try { return SEM_SolveThermal3D(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -100; }
}
int SafeExtractIsoline(double value) {
    __try { return SEM_ExtractIsoline(value); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -100; }
}
int SafeExtractIsosurface3D(double value) {
    __try { return SEM_ExtractIsosurface3D(value); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -100; }
}

// Verbose detail for the most recent SEM_* failure, taken from the library's
// SEM_GetLastError(). Returned pre-formatted as " — <text>" so it can be
// appended directly to a status/log message, or "" when the library has no
// detail. MUST be evaluated immediately after the failing SEM_* call and
// before any other SEM_* call — the library owns the buffer and overwrites it
// on the next call.
std::string SemDetail() {
    const char* e = SEM_GetLastError();
    return (e && *e) ? std::string(" — ") + e : std::string();
}

} // namespace

const std::string& SemSession::SourcePath() const { return m_srcPath; }
Primitive*  SemSession::SourcePrim()  const { return m_srcPrim; }
Primitive*  SemSession::OffsetsPrim() const { return m_offsets; }
Primitive*  SemSession::MeshPrim()    const { return m_mesh; }
Primitive*  SemSession::IsolinePrim() const { return m_isoline; }
Primitive*  SemSession::SrcRevSurf()  const { return m_srcRevSurf; }
Primitive*  SemSession::IsoRevSurf()  const { return m_isoRevSurf; }
bool        SemSession::HasSource()   const { return m_srcPrim != nullptr && !m_srcPath.empty(); }
int         SemSession::Dim()         const { return dim; }
bool        SemSession::ThermalSolved() const { return m_thermalSolved; }
bool        SemSession::HasIsolinePath() const { return !m_isolinePath.empty(); }

double SemSession::MeshParamFactor() const {
    double avg = SEM_GetAvgEdgeLen();
    if (avg <= 0.0) avg = 1.0;
    return (meshMethod == SEM_STEINER_MAX_AREA) ? avg * avg : avg;
}

// Edge-length multiplier for the 3D tet knob: cube of the mean surface edge
// length for the volume method, the length itself for sizing.
double SemSession::TetParamFactor() const {
    double avg = SEM_GetSurfaceAvgEdgeLen3D();
    if (avg <= 0.0) avg = 1.0;
    return (tetMethod == SEM_TET_MAX_VOL) ? avg * avg * avg : avg;
}

const Stats& SemSession::SrcStats()  const { return m_srcStats; }
const Stats& SemSession::OffStats()  const { return m_offStats; }
const Stats& SemSession::MeshStats() const { return m_meshStats; }

std::string SemSession::OutPath(const char* suffix) const {
    return (fs::path(m_workDir) / (Stem(m_srcPath) + suffix)).string();
}

void SemSession::Bind(Scene& scene, Primitive* prim) {
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
        // Point the SEM core at a working directory we control, so the
        // deterministic output paths it writes (stem + suffix) can be
        // reconstructed by OutPath after each compute/build/extract call.
        m_workDir = fs::temp_directory_path().string();
        SEM_SetWorkingDir(m_workDir.c_str());
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

void SemSession::Unbind() {
    m_srcPrim = nullptr; m_srcPath.clear();
    m_offsets = nullptr; m_mesh = nullptr; m_isoline = nullptr;
    m_srcRevSurf = nullptr; m_isoRevSurf = nullptr;
    m_offsetsPath.clear(); m_meshPath.clear(); m_isolinePath.clear();
    m_thermalSolved = false;
    m_srcStats = m_offStats = m_meshStats = Stats();
    snprintf(status, sizeof(status), "Ready");
}

void SemSession::Validate(Scene& scene) {
    if (m_srcPrim && !Alive(scene, m_srcPrim)) { Unbind(); return; }
    if (m_offsets && !Alive(scene, m_offsets)) { m_offsets = nullptr; m_offStats = Stats(); }
    if (m_mesh    && !Alive(scene, m_mesh))    { m_mesh    = nullptr; m_meshStats = Stats(); m_thermalSolved = false; }
    if (m_isoline && !Alive(scene, m_isoline)) { m_isoline = nullptr; }
    if (m_srcRevSurf && !Alive(scene, m_srcRevSurf)) m_srcRevSurf = nullptr;
    if (m_isoRevSurf && !Alive(scene, m_isoRevSurf)) m_isoRevSurf = nullptr;
}

void SemSession::MarkStageDirty(Stage st) {
    if (st >= STAGE_SUBDIVIDE && st <= STAGE_THERMAL) m_dirty[st] = true;
}

void SemSession::RecomputeUpTo(Scene& scene, Stage to, bool silent) {
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

Primitive* SemSession::StagePrim(Stage st) const {
    return st == STAGE_OFFSETS ? m_offsets
         : st == STAGE_MESH    ? m_mesh
         : st == STAGE_THERMAL ? m_isoline
         : nullptr;
}

void SemSession::SetStageVisible(Scene& scene, Stage st, bool show) {
    Primitive* p = StagePrim(st);
    if (show && !(p && Alive(scene, p))) {
        RecomputeUpTo(scene, st, false);
        p = StagePrim(st);
    }
    if (p && Alive(scene, p)) scene.SetNodeVisibleCascade(p, show);
}

void SemSession::ResetStage(Scene& scene, Stage st) {
    if (st <= STAGE_OFFSETS)      { DropOffsets(scene); DropMesh(scene);
                                    m_dirty[STAGE_OFFSETS] = m_dirty[STAGE_MESH] = m_dirty[STAGE_THERMAL] = true; }
    else if (st == STAGE_MESH)    { DropMesh(scene);
                                    m_dirty[STAGE_MESH] = m_dirty[STAGE_THERMAL] = true; }
    else if (st == STAGE_THERMAL) { DropIsoline(scene);
                                    m_dirty[STAGE_THERMAL] = true; }
    snprintf(status, sizeof(status), "Reset stage.");
}

bool SemSession::ValidateRevolutionContour(Scene& scene) {
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

bool SemSession::SetRevolutionMode(Scene& scene, bool enable) {
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

void SemSession::ShowSourceRevolution(Scene& scene, bool show) {
    if (show) BuildSourceRevolution(scene);
    else if (Alive(scene, m_srcRevSurf)) m_srcRevSurf->visible = false;
}

void SemSession::ShowIsolineRevolution(Scene& scene, bool show) {
    if (show) BuildIsolineRevolution(scene);
    else if (Alive(scene, m_isoRevSurf)) m_isoRevSurf->visible = false;
}

void SemSession::SetSrcRevAlpha(Scene& scene, float a) {
    srcRevAlpha = a;
    if (Alive(scene, m_srcRevSurf)) { XMFLOAT4 c = m_srcRevSurf->GetColor(); c.w = a; m_srcRevSurf->SetColor(c); }
}
void SemSession::SetIsoRevAlpha(Scene& scene, float a) {
    isoRevAlpha = a;
    if (Alive(scene, m_isoRevSurf)) { XMFLOAT4 c = m_isoRevSurf->GetColor(); c.w = a; m_isoRevSurf->SetColor(c); }
}

void SemSession::SetSurf3dAlpha(Scene& scene, float a) {
    surf3dAlpha = a;
    for (Primitive* p : { m_srcPrim, m_offsets, m_mesh, m_isoline })
        if (Alive(scene, p)) p->SetAlpha(a);
}

void SemSession::DropSrcRev(Scene& scene) {
    if (Alive(scene, m_srcRevSurf)) scene.RemovePrimitive(m_srcRevSurf);
    m_srcRevSurf = nullptr;
}
void SemSession::DropIsoRev(Scene& scene) {
    if (Alive(scene, m_isoRevSurf)) scene.RemovePrimitive(m_isoRevSurf);
    m_isoRevSurf = nullptr;
}

bool SemSession::ApplyThermalStage(Scene& scene, bool silent) {
    if (!HasSource()) return false;
    if (!Alive(scene, m_mesh)) { Report(scene, silent, "Build the mesh first."); return false; }
    if (!m_thermalSolved) { if (!ApplyThermal(scene, silent)) return false; }
    return ApplyIsoline(scene, silent);
}

bool SemSession::ApplyThermal(Scene& scene, bool silent) {
    if (!HasSource()) return false;
    if (!Alive(scene, m_mesh)) { Report(scene, silent, "Build the mesh first."); return false; }
    // SEM_SolveThermal overwrites each cached mesh node's T with the steady-state
    // temperature in place; the field is consumed by the isotherm/isosurface
    // extraction below. The core no longer serializes the mesh here, so the
    // displayed mesh keeps its build-time (distance-field) colouring.
    int rc = (dim == 3) ? SafeSolveThermal3D() : SafeSolveThermal();
    if (rc == -100) {
        Report(scene, silent, "Thermal solver crashed (access violation caught).");
        return false;
    }
    if (!CheckRc(scene, silent, dim == 3 ? "SEM_SolveThermal3D" : "SEM_SolveThermal", rc,
                 { "", "No source loaded", "No mesh built", "No offsets",
                   "", "No boundary nodes", "Solve failed" }))
        return false;
    m_thermalSolved = true;
    snprintf(status, sizeof(status), "Thermal solved.");
    return true;
}

bool SemSession::ApplyIsoline(Scene& scene, bool silent) {
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
                     { "", "No source loaded", "No mesh built", "Invalid value", "Extraction failed" }))
            return false;

        std::string p = OutPath(dim == 3 ? "_isosurface3d.csv3d" : "_isoline.csv3d");
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

Primitive* SemSession::ImportSource(Scene& scene, const std::string& path) {
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

void SemSession::RunFullPipeline(Scene& scene) {
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

bool SemSession::AsyncRunning() const { return m_job.running.load(); }

const char* SemSession::AsyncStageName() const {
    switch (m_job.stageKind.load()) {
        case 0:  return "Computing offset shells";
        case 1:  return "Building tetrahedral mesh";
        case 2:  return "Solving thermal field";
        default: return "Extracting isosurface";
    }
}

float SemSession::AsyncProgress() const {
    if (!m_job.running.load()) return 0.0f;
    int total = m_job.totalStages.load();
    if (total < 1) total = 1;
    // The library exposes a single progress value (0..1) for the call currently
    // running; it resets to 0 between calls and the quick isosurface step does
    // not report at all. Fold it into the per-stage band and clamp monotonic.
    float p = SEM_GetProgress();
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    float overall = ((float)m_job.progressStage.load() + p) / (float)total;
    float prev = m_progressShown.load();
    if (overall < prev) overall = prev;
    m_progressShown.store(overall);
    return overall;
}

void SemSession::RecomputeUpToAsync(Scene& scene, Stage to, bool silent) {
    if (!HasSource()) return;
    if (dim != 3) { RecomputeUpTo(scene, to, silent); return; }
    if (m_job.running.load()) return;
    if (m_job.worker.joinable()) m_job.worker.join();

    bool ran = false;

    // Subdivide runs synchronously: it is quick, reports no progress and only
    // updates the SEM cache (no scene work). It must precede the offsets.
    if (m_dirty[STAGE_SUBDIVIDE]) {
        if (!ApplySubdivide(scene, silent)) return;
        m_dirty[STAGE_SUBDIVIDE] = false;
        ran = true;
    }

    // Plan the heavy stages exactly as RecomputeUpTo would execute them.
    bool runOff = false, runMesh = false, runTherm = false;
    if (to >= STAGE_OFFSETS && (m_dirty[STAGE_OFFSETS] || ran)) {
        if (offEnabled || (m_offsets && Alive(scene, m_offsets))) { runOff = true; ran = true; }
        m_dirty[STAGE_OFFSETS] = false;
    }
    if (to >= STAGE_MESH && (m_dirty[STAGE_MESH] || ran)) {
        if (meshEnabled || (m_mesh && Alive(scene, m_mesh))) { runMesh = true; ran = true; }
        m_dirty[STAGE_MESH] = false;
    }
    if (to >= STAGE_THERMAL && (m_dirty[STAGE_THERMAL] || ran)) {
        if (thermalEnabled || (m_isoline && Alive(scene, m_isoline))) { runTherm = true; ran = true; }
        m_dirty[STAGE_THERMAL] = false;
    }

    // Rebuilding a stage invalidates everything after 'to' we did not touch.
    if (ran)
        for (int s = (int)to + 1; s <= STAGE_THERMAL; ++s) m_dirty[s] = true;

    // No heavy work (e.g. just a subdivide, or nothing dirty): no worker,
    // no progress bar — just refresh and return.
    if (!runOff && !runMesh && !runTherm) { scene.UpdateLight(); return; }

    // Precondition the worker cannot check itself (it has no Scene access):
    // a thermal-only run needs a mesh already built and present.
    if (runTherm && !runMesh && !(m_mesh && Alive(scene, m_mesh))) {
        Report(scene, silent, "Build the mesh first.");
        return;
    }

    // Snapshot the plan, parameters and visibility intent for the worker /
    // PollAsync (mutable session state must not be read once running).
    m_job.runOffsets  = runOff;
    m_job.runMesh     = runMesh;
    m_job.runThermal  = runTherm;
    m_job.offVisible  = offEnabled;
    m_job.meshVisible = meshEnabled;
    m_job.isoVisible  = thermalEnabled;
    m_job.totalStages.store((int)runOff + (int)runMesh + (int)runTherm);

    m_job.offsetMode  = offsetMode;
    m_job.firstGap    = firstGap;
    m_job.numOffsets  = numOffsets;
    m_job.grading     = grading;
    m_job.gaps.assign(gaps.begin(), gaps.end());
    m_job.tetMethod   = tetMethod;
    {
        double param = (double)tetParam;
        if (tetParamEdgeUnits && tetParam > 0.0f &&
            (tetMethod == SEM_TET_MAX_VOL || tetMethod == SEM_TET_SIZING))
            param *= TetParamFactor();
        m_job.tetParam = param;
    }
    m_job.isoValue = isoValue;

    // Deterministic output paths the SEM core writes during each compute call.
    m_job.expOffsets = OutPath("_offsets3d.csv3d");
    m_job.expMesh    = OutPath("_mesh3d.csv3d");
    m_job.expIso     = OutPath("_isosurface3d.csv3d");

    m_job.offsetsPath.clear();
    m_job.meshPath.clear();
    m_job.isoPath.clear();
    m_job.error.clear();
    m_job.stageKind.store(runOff ? 0 : runMesh ? 1 : 2);
    m_job.progressStage.store(0);
    m_job.ok.store(false);
    m_job.done.store(false);
    m_progressShown.store(0.0f);
    m_job.running.store(true);

    snprintf(status, sizeof(status), "Computing...");
    m_job.worker = std::thread(&SemSession::PipelineWorkerBody, this);
}

void SemSession::PollAsync(Scene& scene) {
    if (!m_job.running.load() || !m_job.done.load()) return;
    if (m_job.worker.joinable()) m_job.worker.join();
    m_job.running.store(false);
    m_job.done.store(false);

    if (!m_job.ok.load()) {
        Report(scene, false, m_job.error.empty() ? "3D pipeline failed." : m_job.error);
        return;
    }

    // Offset shells.
    if (!m_job.offsetsPath.empty()) {
        DropOffsets(scene);
        m_offsets = scene.AddFromCSV3D(m_job.offsetsPath, "offsets_" + Stem(m_srcPath),
                                       AttachParent(), nullptr, Colors::CYAN, Colors::YELLOW, renderTrisAsLines);
        m_offsetsPath = m_job.offsetsPath;
        if (!renderTrisAsLines) ConfigureSurface3D(m_offsets);
        if (m_offsets) scene.AttachVertexPointsGroup(m_offsets);
        m_offStats = ComputeStats(m_offsetsPath);
        if (m_offsets && Alive(scene, m_offsets)) scene.SetNodeVisibleCascade(m_offsets, m_job.offVisible);
    }

    // Tetrahedral mesh (build-time colouring; the thermal solve updates T in the
    // SEM cache only and does not re-serialize, matching the synchronous path).
    if (!m_job.meshPath.empty()) {
        DropMesh(scene);
        m_mesh = scene.AddFromCSV3D(m_job.meshPath, "mesh_" + Stem(m_srcPath),
                                    AttachParent(), nullptr, Colors::CYAN, Colors::YELLOW, renderTrisAsLines);
        m_meshPath = m_job.meshPath;
        if (!renderTrisAsLines) ConfigureSurface3D(m_mesh);
        if (m_mesh) scene.AttachVertexPointsGroup(m_mesh);
        m_meshStats = ComputeStats(m_meshPath);
        if (m_job.runThermal) m_thermalSolved = true;
        if (m_mesh && Alive(scene, m_mesh)) scene.SetNodeVisibleCascade(m_mesh, m_job.meshVisible);
    }

    // Isosurface.
    if (!m_job.isoPath.empty()) {
        DropIsoline(scene);
        const XMFLOAT4 green(0.0f, 1.0f, 0.0f, 1.0f);
        m_isoline = scene.AddFromCSV3D(m_job.isoPath, "isosurface_" + Stem(m_srcPath),
                                       AttachParent(), &green, Colors::BLUE, Colors::RED, renderTrisAsLines);
        if (!renderTrisAsLines) ConfigureSurface3D(m_isoline);
        m_isolinePath = m_job.isoPath;
        if (m_isoline && Alive(scene, m_isoline)) scene.SetNodeVisibleCascade(m_isoline, m_job.isoVisible);
    }

    scene.UpdateLight();
    snprintf(status, sizeof(status), "Done.");
}

void SemSession::PipelineWorkerBody() {
    int idx = 0;   // index of the current stage among the planned ones

    // Offset shells — SEM_ComputeOffsets3D / SEM_ComputeOffsetsAt3D.
    if (m_job.runOffsets) {
        m_job.stageKind.store(0);
        m_job.progressStage.store(idx);
        int rc = (m_job.offsetMode == OFFSET_GAPS && !m_job.gaps.empty())
               ? SEM_ComputeOffsetsAt3D(m_job.gaps.data(), (int)m_job.gaps.size())
               : SEM_ComputeOffsets3D(m_job.firstGap, m_job.numOffsets, m_job.grading);
        if (rc != 0) return Fail("SEM_ComputeOffsets3D failed (" + std::to_string(rc) + ")");
        m_job.offsetsPath = m_job.expOffsets;
        ++idx;
    }

    // Tetrahedral band mesh — SEM_BuildMesh3D.
    if (m_job.runMesh) {
        m_job.stageKind.store(1);
        m_job.progressStage.store(idx);
        SEM_MeshParams3D params3d{ m_job.tetMethod, m_job.tetParam };
        int rc = SafeBuildMesh3D(&params3d);
        if (rc == -100) return Fail("TetGen DLL crashed (access violation caught). "
                                    "Try a larger volume or a looser quality bound.");
        if (rc != 0) return Fail("SEM_BuildMesh3D failed (" + std::to_string(rc) + ")");
        m_job.meshPath = m_job.expMesh;
        ++idx;
    }

    // Steady-state thermal solve + isosurface extraction.
    if (m_job.runThermal) {
        m_job.stageKind.store(2);
        m_job.progressStage.store(idx);
        int rc = SafeSolveThermal3D();
        if (rc == -100) return Fail("Thermal solver crashed (access violation caught).");
        if (rc != 0) return Fail("SEM_SolveThermal3D failed (" + std::to_string(rc) + ")");

        // Isosurface extraction (quick — no SEM progress; keep progressStage
        // pinned so the bar holds at the end of the thermal stage).
        m_job.stageKind.store(3);
        double v = m_job.isoValue;
        if (v < 0.0) v = 0.0;
        if (v > 1.0) v = 1.0;
        rc = SafeExtractIsosurface3D(v);
        if (rc == -100) return Fail("Isosurface extraction crashed (access violation caught).");
        if (rc != 0) return Fail("SEM_ExtractIsosurface3D failed (" + std::to_string(rc) + ")");
        m_job.isoPath = m_job.expIso;
        ++idx;
    }

    m_job.ok.store(true);
    m_job.done.store(true);
}

void SemSession::Fail(const std::string& msg) {
    m_job.error = msg + SemDetail();
    m_job.ok.store(false);
    m_job.done.store(true);
}

bool SemSession::Alive(Scene& scene, Primitive* q) const {
    if (!q) return false;
    for (Primitive* p : scene.primitives) if (p == q) return true;
    return false;
}

void SemSession::Report(Scene& scene, bool silent, const std::string& msg) {
    (void)scene;
    snprintf(status, sizeof(status), "%s", msg.c_str());
    if (!silent) ErrorLogger::Log(msg);
}

bool SemSession::CheckRc(Scene& scene, bool silent, const char* call, int rc,
                         std::initializer_list<const char*> errs) {
    if (rc == 0) return true;
    const char* msg = "";
    int idx = -rc, i = 0;
    for (const char* e : errs) { if (i == idx) { msg = e; break; } ++i; }
    Report(scene, silent, std::string(call) + " failed (" + std::to_string(rc) + "): " + msg + SemDetail());
    return false;
}

SceneNode* SemSession::AttachParent() { return m_srcPrim ? static_cast<SceneNode*>(m_srcPrim) : nullptr; }

void SemSession::ConfigureSurface3D(Primitive* p) {
    if (!p) return;
    p->SetAlpha(surf3dAlpha);
}

void SemSession::BuildSourceRevolution(Scene& scene) {
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

void SemSession::BuildIsolineRevolution(Scene& scene) {
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

void SemSession::DropOffsets(Scene& scene) {
    if (Alive(scene, m_offsets)) scene.RemovePrimitive(m_offsets);
    m_offsets = nullptr; m_offStats = Stats(); m_offsetsPath.clear();
}
void SemSession::DropMesh(Scene& scene) {
    if (Alive(scene, m_mesh)) scene.RemovePrimitive(m_mesh);
    m_mesh = nullptr; m_meshStats = Stats(); m_meshPath.clear();
    m_thermalSolved = false;
    DropIsoline(scene);
}
void SemSession::DropIsoline(Scene& scene) {
    if (Alive(scene, m_isoline)) scene.RemovePrimitive(m_isoline);
    m_isoline = nullptr;
    DropIsoRev(scene);
    m_isolinePath.clear();
}

bool SemSession::ApplySubdivide(Scene& scene, bool silent) {
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

bool SemSession::ApplyOffsets(Scene& scene, bool silent) {
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

        std::string p = OutPath(dim == 3 ? "_offsets3d.csv3d" : "_offsets.csv3d");
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

bool SemSession::ApplyMesh(Scene& scene, bool silent) {
    try {
        int rc;
        if (dim == 3) {
            double param = (double)tetParam;
            if (tetParamEdgeUnits && tetParam > 0.0f &&
                (tetMethod == SEM_TET_MAX_VOL || tetMethod == SEM_TET_SIZING))
                param *= TetParamFactor();
            SEM_MeshParams3D params3d{ tetMethod, param };
            rc = SafeBuildMesh3D(&params3d);
            if (rc == -100) {
                Report(scene, silent, "TetGen DLL crashed (access violation caught). "
                                      "Try a larger volume or a looser quality bound.");
                return false;
            }
            if (!CheckRc(scene, silent, "SEM_BuildMesh3D", rc,
                         { "", "No surface loaded", "Compute offsets first",
                           "", "Tetrahedralization failed", "Invalid method" }))
                return false;
        } else {
            double param = (double)meshParam;
            if (meshParamEdgeUnits && meshParam > 0.0f &&
                (meshMethod == SEM_STEINER_MAX_AREA || meshMethod == SEM_STEINER_SIZING))
                param *= MeshParamFactor();
            SEM_MeshParams params{ meshMethod, param, (double)steinerMargin };
            rc = SafeBuildMesh(&params);
            if (rc == -100) {
                Report(scene, silent, "Mesher DLL crashed (access violation caught). "
                                      "Try another Steiner method or a larger parameter.");
                return false;
            }
            if (!CheckRc(scene, silent, "SEM_BuildMesh", rc,
                         { "", "No source loaded", "Compute offsets first",
                           "Not enough valid lines", "Triangulation failed", "Invalid method" }))
                return false;
        }

        std::string p = OutPath(dim == 3 ? "_mesh3d.csv3d" : "_mesh.csv3d");
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

}
