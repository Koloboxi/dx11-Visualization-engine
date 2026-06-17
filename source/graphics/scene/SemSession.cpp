#include "SemSession.h"
#include "../../utils/errorLogger.h"
#include "../../loaders/CSV3DLoader.h"
#include <map>
#include <set>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <functional>
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
    for (const auto& t : d.tets) {
        add(t.x, t.y); add(t.x, t.z); add(t.x, t.w);
        add(t.y, t.z); add(t.y, t.w); add(t.z, t.w);
    }

    s.edges = (int)edgeUse.size();
    if (s.tris > 0)
        for (const auto& kv : edgeUse) if (kv.second == 1) ++s.boundary;
    return s;
}

// Axis-aligned bounding box of a .csv3d node cloud. Returns false on an empty or
// unreadable file.
bool SourceBBox(const std::string& path, XMFLOAT3& lo, XMFLOAT3& hi) {
    CSV3DLoader::CSV3DData d;
    if (!CSV3DLoader::Load(path, d) || d.nodes.empty()) return false;
    lo = hi = d.nodes[0].pos;
    for (const auto& nd : d.nodes) {
        lo.x = std::min(lo.x, nd.pos.x); hi.x = std::max(hi.x, nd.pos.x);
        lo.y = std::min(lo.y, nd.pos.y); hi.y = std::max(hi.y, nd.pos.y);
        lo.z = std::min(lo.z, nd.pos.z); hi.z = std::max(hi.z, nd.pos.z);
    }
    return true;
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
int SafeSolveThermal3D(float max_inward) {
    __try { return SEM_SolveThermal3D(max_inward); }
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
SceneNode*  SemSession::OffsetsNode() const { return m_offsets; }
Primitive*  SemSession::MeshPrim()    const { return m_mesh; }
Primitive*  SemSession::IsolinePrim() const { return m_isoline; }
Primitive*  SemSession::SrcRevSurf()  const { return m_srcRevSurf; }
Primitive*  SemSession::IsoRevSurf()  const { return m_isoRevSurf; }
bool        SemSession::HasSource()   const { return m_srcPrim != nullptr && !m_srcPath.empty(); }
int         SemSession::Dim()         const { return dim; }
bool        SemSession::ThermalSolved() const { return m_thermalSolved; }
bool        SemSession::HasIsolinePath() const { return !m_isolinePath.empty(); }

double SemSession::TotalTimeMs() const {
    double t = 0.0;
    if (m_offsetsMs >= 0.0) t += m_offsetsMs;
    if (m_meshMs    >= 0.0) t += m_meshMs;
    if (m_thermalMs >= 0.0) t += m_thermalMs;
    return t;
}

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

std::string SemSession::SessionRoot() {
    std::error_code ec;
    auto tmp = fs::temp_directory_path(ec);
    if (ec) return std::string();
    return (tmp / "sem").string();
}

// Parse the trailing _<N> of a session folder name "<stem>_<N>". Returns 0 (not
// a session) unless the name is exactly stem + '_' + digits.
static int SessionIndex(const std::string& folderName, const std::string& stem) {
    const std::string prefix = stem + "_";
    if (folderName.size() <= prefix.size() ||
        folderName.compare(0, prefix.size(), prefix) != 0)
        return 0;
    const std::string num = folderName.substr(prefix.size());
    if (num.empty()) return 0;
    for (char c : num) if (c < '0' || c > '9') return 0;
    try { return std::stoi(num); } catch (...) { return 0; }
}

std::vector<std::string> SemSession::ListSessions(const std::string& srcPath) {
    std::vector<std::string> dirs;
    const std::string root = SessionRoot();
    if (root.empty() || srcPath.empty()) return dirs;
    const std::string stem = Stem(srcPath);

    std::vector<std::pair<int, std::string>> found;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(root, ec)) {
        if (ec) break;
        if (!e.is_directory()) continue;
        int n = SessionIndex(e.path().filename().string(), stem);
        if (n > 0) found.emplace_back(n, e.path().string());
    }
    std::sort(found.begin(), found.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& f : found) dirs.push_back(std::move(f.second));
    return dirs;
}

std::string SemSession::NewSessionDir(const std::string& srcPath) {
    const std::string root = SessionRoot();
    if (root.empty() || srcPath.empty()) return std::string();
    const std::string stem = Stem(srcPath);
    int maxN = 0;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(root, ec)) {
        if (ec) break;
        if (!e.is_directory()) continue;
        maxN = std::max(maxN, SessionIndex(e.path().filename().string(), stem));
    }
    return (fs::path(root) / (stem + "_" + std::to_string(maxN + 1))).string();
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
    m_clipViz   = nullptr;
    m_meshPath.clear();
    m_isolinePath.clear();
    m_thermalSolved = false;
    m_offStats  = Stats();
    m_meshStats = Stats();
    // A fresh source resets the SEM core (SEM_LoadSurface3D/SEM_LoadCSV3D below),
    // which also clears its clip planes; drop ours and the measured stage times.
    clipPlanes.clear();
    m_offsetsMs = m_meshMs = m_thermalMs = -1.0;
    // A fresh source clears the SEM core cache (SEM_LoadCSV3D below), so every
    // stage must be recomputed before it can be shown.
    m_dirty[0] = m_dirty[1] = m_dirty[2] = m_dirty[3] = true;
    if (!m_srcPath.empty()) {
        // Point the SEM core at this source's session folder, so the
        // deterministic output paths it writes (stem + suffix) can be
        // reconstructed by OutPath after each compute/build/extract call. The
        // folder is chosen at import (prim->semWorkDir); when a source is staged
        // without one (e.g. tree double-click), allocate a fresh session.
        m_workDir = prim ? prim->semWorkDir : std::string();
        if (m_workDir.empty()) {
            m_workDir = NewSessionDir(m_srcPath);
            if (prim) prim->semWorkDir = m_workDir;
        }
        std::error_code ec;
        fs::create_directories(m_workDir, ec);
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
    m_meshPath.clear(); m_isolinePath.clear();
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
    if (m_clipViz    && !Alive(scene, m_clipViz))    m_clipViz    = nullptr;
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

SceneNode* SemSession::StagePrim(Stage st) const {
    return st == STAGE_OFFSETS ? m_offsets
         : st == STAGE_MESH    ? static_cast<SceneNode*>(m_mesh)
         : st == STAGE_THERMAL ? static_cast<SceneNode*>(m_isoline)
         : nullptr;
}

void SemSession::SetStageVisible(Scene& scene, Stage st, bool show) {
    SceneNode* p = StagePrim(st);
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
    for (Primitive* p : { m_srcPrim, m_mesh, m_isoline })
        if (Alive(scene, p)) p->SetAlpha(a);
    // The offsets are a group of shell primitives; apply alpha to each.
    if (Alive(scene, m_offsets)) {
        std::function<void(SceneNode*)> rec = [&](SceneNode* n) {
            for (SceneNode* ch : n->children) {
                if (ch->IsPrimitive()) static_cast<Primitive*>(ch)->SetAlpha(a);
                rec(ch);
            }
        };
        rec(m_offsets);
    }
}

void SemSession::SetClipPlanes3D(Scene& scene) {
    if (dim != 3) { Report(scene, false, "Clip planes apply to the 3D pipeline only."); return; }
    if (AsyncRunning()) return;
    // Flatten to the (nx, ny, nz, d) layout SEM_SetClipPlanes3D expects.
    std::vector<double> flat;
    flat.reserve(clipPlanes.size() * 4);
    for (const auto& p : clipPlanes) {
        flat.push_back(p.x); flat.push_back(p.y); flat.push_back(p.z); flat.push_back(p.w);
    }
    int rc = SEM_SetClipPlanes3D(flat.empty() ? nullptr : flat.data(), (int)clipPlanes.size());
    if (!CheckRc(scene, false, "SEM_SetClipPlanes3D", rc,
                 { "", "No surface loaded", "Invalid planes" }))
        return;

    BuildClipPlaneViz(scene);
    // Clipping is applied during SEM_BuildMesh3D, so the cached mesh (and the
    // thermal field/isosurface downstream) is now stale.
    DropMesh(scene);
    m_dirty[STAGE_MESH] = m_dirty[STAGE_THERMAL] = true;
    scene.UpdateLight();
    snprintf(status, sizeof(status), "Clip planes: %d set.", (int)clipPlanes.size());
}

void SemSession::ClearClipPlanes3D(Scene& scene) {
    if (AsyncRunning()) return;
    int rc = SEM_ClearClipPlanes3D();
    CheckRc(scene, false, "SEM_ClearClipPlanes3D", rc, { "", "No surface loaded" });
    clipPlanes.clear();
    DropClipPlaneViz(scene);
    DropMesh(scene);
    m_dirty[STAGE_MESH] = m_dirty[STAGE_THERMAL] = true;
    scene.UpdateLight();
    snprintf(status, sizeof(status), "Clip planes cleared.");
}

void SemSession::RebuildClipPlaneViz(Scene& scene) {
    BuildClipPlaneViz(scene);
}

void SemSession::BuildClipPlaneViz(Scene& scene) {
    DropClipPlaneViz(scene);
    if (dim != 3 || clipPlanes.empty() || !HasSource()) return;

    XMFLOAT3 lo, hi;
    if (!SourceBBox(m_srcPath, lo, hi)) return;
    const XMFLOAT3 corners[8] = {
        { lo.x, lo.y, lo.z }, { hi.x, lo.y, lo.z }, { lo.x, hi.y, lo.z }, { hi.x, hi.y, lo.z },
        { lo.x, lo.y, hi.z }, { hi.x, lo.y, hi.z }, { lo.x, hi.y, hi.z }, { hi.x, hi.y, hi.z },
    };
    const XMVECTOR centre = XMVectorSet((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f,
                                        (lo.z + hi.z) * 0.5f, 0.0f);

    // Soft (desaturated) colours, translucent. Front face = kept side (normal
    // points there) = red; back face = removed side = blue.
    const XMFLOAT4 softRed (0.86f, 0.42f, 0.40f, 0.20f);
    const XMFLOAT4 softBlue(0.40f, 0.52f, 0.86f, 0.20f);

    m_clipViz = scene.AddGroupNode("clip_planes", m_srcPrim);

    for (int pi = 0; pi < (int)clipPlanes.size(); ++pi) {
        const XMFLOAT4& pl = clipPlanes[pi];
        XMVECTOR n = XMVectorSet(pl.x, pl.y, pl.z, 0.0f);
        const float len = XMVectorGetX(XMVector3Length(n));
        if (len < 1e-9f) continue;                 // degenerate normal — skip
        n = XMVectorScale(n, 1.0f / len);          // unit normal
        const float dUnit = pl.w / len;            // d for the unit-normal form

        // Foot of the perpendicular from the bbox centre onto the plane.
        const float sdist = XMVectorGetX(XMVector3Dot(n, centre)) + dUnit;
        const XMVECTOR P = XMVectorSubtract(centre, XMVectorScale(n, sdist));

        // In-plane orthonormal basis (u, v) chosen so that u x v = n. The pixel
        // shader treats the +normal side as the front face (FrontCounterClockwise
        // rasterizer), so the kept half-space shows the front (red) colour.
        XMVECTOR ref = (std::fabs(XMVectorGetX(n)) < 0.9f) ? XMVectorSet(1, 0, 0, 0)
                                                           : XMVectorSet(0, 1, 0, 0);
        XMVECTOR u = XMVector3Normalize(XMVector3Cross(ref, n));
        XMVECTOR v = XMVector3Cross(n, u);

        // Extent of the bbox footprint in the (u, v) basis, around P.
        float smin = 1e30f, smax = -1e30f, tmin = 1e30f, tmax = -1e30f;
        for (const XMFLOAT3& c : corners) {
            XMVECTOR w = XMVectorSubtract(XMLoadFloat3(&c), P);
            const float s = XMVectorGetX(XMVector3Dot(w, u));
            const float t = XMVectorGetX(XMVector3Dot(w, v));
            smin = std::min(smin, s); smax = std::max(smax, s);
            tmin = std::min(tmin, t); tmax = std::max(tmax, t);
        }
        // A small margin so the rectangle slightly overhangs the bounding box.
        const float ms = 0.05f * (smax - smin), mt = 0.05f * (tmax - tmin);
        smin -= ms; smax += ms; tmin -= mt; tmax += mt;

        auto pt = [&](float s, float t) {
            XMVECTOR q = XMVectorAdd(XMVectorAdd(P, XMVectorScale(u, s)), XMVectorScale(v, t));
            XMFLOAT3 f; XMStoreFloat3(&f, q); return f;
        };
        const XMFLOAT3 q00 = pt(smin, tmin), q10 = pt(smax, tmin),
                       q11 = pt(smax, tmax), q01 = pt(smin, tmax);
        // Winding (q00,q10,q11)/(q00,q11,q01) gives a face normal of +n.
        std::vector<XMFLOAT3> poses = { q00, q10, q11, q00, q11, q01 };
        std::vector<XMFLOAT4> cols(6, softRed);

        Primitive* rect = scene.AddColoredTriangles(poses, cols, "clip_" + std::to_string(pi),
                                                    m_clipViz, /*ensureCCW*/ false);
        if (!rect) continue;
        rect->SetIlluminationCapability(false);   // flat, unlit soft colour
        rect->SetUseVertexColor(false);
        rect->SetColor(softRed);                  // front (kept) side
        rect->SetTwoSided(true, softBlue);        // back (removed) side
    }
}

void SemSession::DropClipPlaneViz(Scene& scene) {
    if (Alive(scene, m_clipViz)) scene.RemoveNode(m_clipViz);
    m_clipViz = nullptr;
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
    // temperature in place AND rewrites the serialized mesh file (the pre-solve
    // <stem>_mesh[3d].csv3d had T = 0). Re-import it below so the displayed mesh
    // recolours by the solved field; the same field feeds the isotherm/isosurface.
    Timer t; t.Restart();
    int rc = (dim == 3) ? SafeSolveThermal3D(maxInward) : SafeSolveThermal();
    const double ms = t.GetMillisecondsElapsed();
    if (rc == -100) {
        Report(scene, silent, "Thermal solver crashed (access violation caught).");
        return false;
    }
    if (!CheckRc(scene, silent, dim == 3 ? "SEM_SolveThermal3D" : "SEM_SolveThermal", rc,
                 { "", "No source loaded", "No mesh built", "No offsets",
                   "", "No boundary nodes", "Solve failed" }))
        return false;
    m_thermalMs = ms;
    m_thermalSolved = true;
    ReloadMeshColored(scene);
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
        m_isoline = scene.AddFromCSV3D(p, namePrefix + Stem(m_srcPath), AttachParent(), &green, Colors::BLUE, Colors::RED);
        if (dim == 3) ConfigureSurface3D(m_isoline);
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

Primitive* SemSession::ImportSource(Scene& scene, const std::string& path,
                                    const std::string& workDir) {
    if (path.empty()) return nullptr;
    Primitive* src = nullptr;
    try {
        src = scene.AddFromCSV3D(path, "", nullptr, nullptr, Colors::BLUE, Colors::RED, false);
        if (!src) { Report(scene, false, "Import failed: could not load CSV3D."); return nullptr; }
        src->semSourcePath = path;
        // Session folder to serialize into; empty => Bind allocates a fresh one.
        src->semWorkDir = workDir;
        scene.stagingEnabled = true;
        scene.SetStaged(src);
        Bind(scene, src);
        if (dim == 3) {
            ConfigureSurface3D(src);
            src->SetColor(Colors::FRONT_FACE_WHITE);
            src->SetUseVertexColor(false);
            src->SetTwoSided(true, Colors::BACK_FACE_RED);
        } else {
            src->SetColor(Colors::WHITE);
            src->SetUseVertexColor(false);
        }
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

bool SemSession::ImportOffsets(Scene& scene, const std::string& dir) {
    if (!HasSource()) { Report(scene, false, "Import offsets: no staged source."); return false; }
    if (AsyncRunning()) return false;
    // dir null/empty => the SEM core reads from its working dir; pass the chosen
    // directory through so both the core and the shell loader agree on location.
    const char* d = dir.empty() ? nullptr : dir.c_str();
    int rc = (dim == 3) ? SEM_LoadOffsets3D(d) : SEM_LoadOffsets(d);
    if (!CheckRc(scene, false, dim == 3 ? "SEM_LoadOffsets3D" : "SEM_LoadOffsets", rc,
                 { "", "No source loaded", "No offset files found" }))
        return false;

    DropOffsets(scene);
    if (!LoadOffsetShells(scene, false, dir)) return false;
    if (m_offsets && Alive(scene, m_offsets)) scene.SetNodeVisibleCascade(m_offsets, true);

    // The reloaded offsets (and the subdivide they came from) now populate the
    // cache; recomputing either would clobber it, so mark both clean. The mesh
    // and everything downstream is stale relative to the imported offsets.
    DropMesh(scene);
    m_dirty[STAGE_SUBDIVIDE] = false;
    m_dirty[STAGE_OFFSETS]   = false;
    m_dirty[STAGE_MESH]      = true;
    m_dirty[STAGE_THERMAL]   = true;
    scene.UpdateLight();
    snprintf(status, sizeof(status), "Imported offsets.");
    return true;
}

bool SemSession::ImportMesh(Scene& scene, const std::string& path) {
    if (!HasSource()) { Report(scene, false, "Import mesh: no staged source."); return false; }
    if (AsyncRunning()) return false;
    int rc = (dim == 3) ? SEM_LoadMesh3D(path.c_str()) : SEM_LoadMesh(path.c_str());
    if (!CheckRc(scene, false, dim == 3 ? "SEM_LoadMesh3D" : "SEM_LoadMesh", rc,
                 { "", "No source loaded", "Load failed", "Missing #tets section" }))
        return false;

    DropMesh(scene);
    m_mesh = scene.AddFromCSV3D(path, "mesh_" + Stem(m_srcPath), AttachParent(),
                                nullptr, Colors::CYAN, Colors::YELLOW);
    if (!m_mesh) { Report(scene, false, "Import mesh: file has no drawable geometry."); return false; }
    m_meshPath = path;
    if (dim == 3) ConfigureSurface3D(m_mesh);
    m_meshStats = ComputeStats(path);
    if (m_mesh && Alive(scene, m_mesh)) scene.SetNodeVisibleCascade(m_mesh, true);

    // The reloaded mesh satisfies subdivide + offsets + mesh (BCs are distance-
    // based, so the offsets are not needed to re-solve). Require an explicit
    // thermal solve before an isotherm can be extracted.
    m_dirty[STAGE_SUBDIVIDE] = m_dirty[STAGE_OFFSETS] = m_dirty[STAGE_MESH] = false;
    m_dirty[STAGE_THERMAL]   = true;
    m_thermalSolved = false;
    scene.UpdateLight();
    snprintf(status, sizeof(status), "Imported mesh: %s", BaseName(path).c_str());
    return true;
}

void SemSession::LoadSessionStages(Scene& scene) {
    if (!HasSource() || m_workDir.empty()) return;

    // Offsets: probe the first shell; ImportOffsets reloads the whole set.
    const std::string offShell0 =
        (fs::path(m_workDir) /
         (Stem(m_srcPath) + (dim == 3 ? "_offset3d_0.csv3d" : "_offset_0.csv3d"))).string();
    if (fs::exists(offShell0))
        ImportOffsets(scene, m_workDir);

    // Mesh: a single serialized file. ImportMesh (called after ImportOffsets)
    // leaves subdivide+offsets+mesh clean and thermal stale/unsolved.
    const std::string meshPath =
        (fs::path(m_workDir) /
         (Stem(m_srcPath) + (dim == 3 ? "_mesh3d.csv3d" : "_mesh.csv3d"))).string();
    if (fs::exists(meshPath))
        ImportMesh(scene, meshPath);

    snprintf(status, sizeof(status), "Loaded session: %s",
             BaseName(m_workDir).c_str());
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

    // Clear stale per-shell offset files before the worker recomputes, so a run
    // producing fewer shells does not leave leftovers for the loader to pick up.
    if (runOff) CleanupOffsetFiles();

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
    m_job.tetMaxEdgeLen = (double)tetMaxEdgeLen;
    m_job.isoValue = isoValue;
    m_job.maxInward = maxInward;

    // Deterministic output paths the SEM core writes during each compute call.
    m_job.expOffsets = OutPath("_offsets3d.csv3d");
    m_job.expMesh    = OutPath("_mesh3d.csv3d");
    m_job.expIso     = OutPath("_isosurface3d.csv3d");

    m_job.offsetsPath.clear();
    m_job.meshPath.clear();
    m_job.isoPath.clear();
    m_job.error.clear();
    m_job.offsetsMs = m_job.meshMs = m_job.thermalMs = -1.0;
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

    // Record the measured durations of whichever stages this run computed; stages
    // that did not run keep their previous time so the total accumulates.
    if (m_job.offsetsMs >= 0.0) m_offsetsMs = m_job.offsetsMs;
    if (m_job.meshMs    >= 0.0) m_meshMs    = m_job.meshMs;
    if (m_job.thermalMs >= 0.0) m_thermalMs = m_job.thermalMs;

    // Offset shells.
    if (!m_job.offsetsPath.empty()) {
        DropOffsets(scene);
        LoadOffsetShells(scene, true);
        if (m_offsets && Alive(scene, m_offsets)) scene.SetNodeVisibleCascade(m_offsets, m_job.offVisible);
    }

    // Tetrahedral mesh.
    if (!m_job.meshPath.empty()) {
        DropMesh(scene);
        m_mesh = scene.AddFromCSV3D(m_job.meshPath, "mesh_" + Stem(m_srcPath),
                                    AttachParent(), nullptr, Colors::CYAN, Colors::YELLOW);
        m_meshPath = m_job.meshPath;
        ConfigureSurface3D(m_mesh);
        m_meshStats = ComputeStats(m_meshPath);
        if (m_job.runThermal) m_thermalSolved = true;
        if (m_mesh && Alive(scene, m_mesh)) scene.SetNodeVisibleCascade(m_mesh, m_job.meshVisible);
    }
    else if (m_job.runThermal) {
        // Thermal-only run: the mesh was not rebuilt above, but SEM_SolveThermal3D
        // overwrote its file with the solved T. Re-import in place so the existing
        // mesh recolours by the new field (same gradient AddFromCSV3D applies).
        m_thermalSolved = true;
        ReloadMeshColored(scene);
    }

    // Isosurface.
    if (!m_job.isoPath.empty()) {
        DropIsoline(scene);
        const XMFLOAT4 green(0.0f, 1.0f, 0.0f, 1.0f);
        m_isoline = scene.AddFromCSV3D(m_job.isoPath, "isosurface_" + Stem(m_srcPath),
                                       AttachParent(), &green, Colors::BLUE, Colors::RED);
        ConfigureSurface3D(m_isoline);
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
        Timer t; t.Restart();
        int rc = (m_job.offsetMode == OFFSET_GAPS && !m_job.gaps.empty())
               ? SEM_ComputeOffsetsAt3D(m_job.gaps.data(), (int)m_job.gaps.size())
               : SEM_ComputeOffsets3D(m_job.firstGap, m_job.numOffsets, m_job.grading);
        m_job.offsetsMs = t.GetMillisecondsElapsed();
        if (rc != 0) return Fail("SEM_ComputeOffsets3D failed (" + std::to_string(rc) + ")");
        m_job.offsetsPath = m_job.expOffsets;
        ++idx;
    }

    // Tetrahedral band mesh — SEM_BuildMesh3D.
    if (m_job.runMesh) {
        m_job.stageKind.store(1);
        m_job.progressStage.store(idx);
        SEM_MeshParams3D params3d{ m_job.tetMethod, m_job.tetParam, m_job.tetMaxEdgeLen };
        Timer t; t.Restart();
        int rc = SafeBuildMesh3D(&params3d);
        m_job.meshMs = t.GetMillisecondsElapsed();
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
        Timer t; t.Restart();
        int rc = SafeSolveThermal3D(m_job.maxInward);
        m_job.thermalMs = t.GetMillisecondsElapsed();
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

// A non-primitive grouping node (e.g. the offsets group) is not in
// scene.primitives, so locate it by walking the scene tree from the root.
bool SemSession::Alive(Scene& scene, SceneNode* q) const {
    if (!q) return false;
    std::function<bool(SceneNode*)> rec = [&](SceneNode* n) -> bool {
        if (n == q) return true;
        for (SceneNode* ch : n->children) if (rec(ch)) return true;
        return false;
    };
    return rec(&scene.root);
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
    const XMFLOAT4 frontCol(Colors::FRONT_FACE_WHITE.x, Colors::FRONT_FACE_WHITE.y, Colors::FRONT_FACE_WHITE.z, srcRevAlpha);
    m_srcRevSurf = scene.AddRevolutionSurface(prof, (UINT)revSegments, frontCol,
                                              "revsurf_src_" + Stem(m_srcPath), AttachParent());
    if (m_srcRevSurf) m_srcRevSurf->SetTwoSided(true, Colors::BACK_FACE_RED);
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
    const XMFLOAT4 frontCol(Colors::FRONT_FACE_WHITE.x, Colors::FRONT_FACE_WHITE.y, Colors::FRONT_FACE_WHITE.z, isoRevAlpha);
    SceneNode* parent = Alive(scene, m_isoline) ? static_cast<SceneNode*>(m_isoline) : AttachParent();
    m_isoRevSurf = scene.AddRevolutionSurface(prof, (UINT)revSegments, frontCol,
                                              "revsurf_iso_" + Stem(m_srcPath), parent);
    if (m_isoRevSurf) m_isoRevSurf->SetTwoSided(true, Colors::BACK_FACE_RED);
    scene.UpdateLight();
    if (m_isoRevSurf) snprintf(status, sizeof(status), "Isotherm revolution surface built.");
}

void SemSession::DropOffsets(Scene& scene) {
    if (Alive(scene, m_offsets)) scene.RemoveNode(m_offsets);
    m_offsets = nullptr; m_offStats = Stats();
}

// Called BEFORE a compute (never after) so a run producing fewer shells than a
// previous one does not reload leftovers — the per-shell loader probes indices
// 0.. until a file is missing.
void SemSession::CleanupOffsetFiles() {
    if (m_workDir.empty() || m_srcPath.empty()) return;
    const std::string stem  = Stem(m_srcPath);
    const std::string pfx2d = stem + "_offset_";
    const std::string pfx3d = stem + "_offset3d_";
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(m_workDir, ec)) {
        if (ec) break;
        if (!e.is_regular_file()) continue;
        const std::string fn = e.path().filename().string();
        if (fn.rfind(pfx2d, 0) == 0 || fn.rfind(pfx3d, 0) == 0)
            fs::remove(e.path(), ec);
    }
}

bool SemSession::LoadOffsetShells(Scene& scene, bool silent, const std::string& dir) {
    m_offsets = scene.AddGroupNode("offsets", m_srcPrim);
    const char* prefix = (dim == 3) ? "_offset3d_" : "_offset_";
    const std::string base = dir.empty() ? m_workDir : dir;

    Stats agg;
    int loaded = 0;
    for (int i = 0; ; ++i) {
        std::string suffix = std::string(prefix) + std::to_string(i) + ".csv3d";
        std::string p = (fs::path(base) / (Stem(m_srcPath) + suffix)).string();
        if (!fs::exists(p)) break;

        Primitive* shell = scene.AddFromCSV3D(p, "offset_" + std::to_string(i),
                                              m_offsets, nullptr, Colors::CYAN, Colors::YELLOW);
        if (!shell) continue;
        if (dim == 3) {
            ConfigureSurface3D(shell);
            shell->SetColor(Colors::FRONT_FACE_WHITE);
            shell->SetUseVertexColor(false);
            shell->SetTwoSided(true, Colors::BACK_FACE_RED);
        } else {
            shell->SetColor(Colors::WHITE);
            shell->SetUseVertexColor(false);
        }

        Stats s = ComputeStats(p);
        agg.valid = true;
        agg.verts += s.verts; agg.edges += s.edges;
        agg.tris  += s.tris;  agg.boundary += s.boundary;
        ++loaded;
    }

    if (loaded == 0) {
        scene.RemoveNode(m_offsets);
        m_offsets = nullptr; m_offStats = Stats();
        Report(scene, silent, "Offsets: no shell files produced.");
        return false;
    }
    m_offStats = agg;
    snprintf(status, sizeof(status), "Offsets: %d shells.", loaded);
    return true;
}
void SemSession::DropMesh(Scene& scene) {
    if (Alive(scene, m_mesh)) scene.RemovePrimitive(m_mesh);
    m_mesh = nullptr; m_meshStats = Stats(); m_meshPath.clear();
    m_thermalSolved = false;
    DropIsoline(scene);
}
void SemSession::ReloadMeshColored(Scene& scene) {
    if (m_meshPath.empty() || !Alive(scene, m_mesh)) return;
    // Snapshot what a focused reload must preserve; unlike DropMesh this neither
    // clears m_meshPath/m_thermalSolved nor drops the isotherm.
    const std::string path = m_meshPath;
    const bool wasVisible = m_mesh->visible;
    const Stats keepStats = m_meshStats;

    scene.RemovePrimitive(m_mesh);
    m_mesh = scene.AddFromCSV3D(path, "mesh_" + Stem(m_srcPath), AttachParent(),
                                nullptr, Colors::BLUE, Colors::RED);
    if (!m_mesh) { Report(scene, true, "Recolour mesh: reload failed."); return; }
    m_meshPath  = path;
    m_meshStats = keepStats;
    if (dim == 3) ConfigureSurface3D(m_mesh);
    if (Alive(scene, m_mesh)) scene.SetNodeVisibleCascade(m_mesh, wasVisible);
    scene.UpdateLight();
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
        DropOffsets(scene);
        CleanupOffsetFiles();
        int rc;
        Timer t; t.Restart();
        if (offsetMode == OFFSET_GAPS) {
            if (gaps.empty()) { Report(scene, silent, "Add at least one gap."); return false; }
            std::vector<double> g(gaps.begin(), gaps.end());
            rc = (dim == 3) ? SEM_ComputeOffsetsAt3D(g.data(), (int)g.size())
                            : SEM_ComputeOffsetsAt(g.data(), (int)g.size());
        } else {
            rc = (dim == 3) ? SEM_ComputeOffsets3D(firstGap, numOffsets, grading)
                            : SEM_ComputeOffsets(firstGap, numOffsets, grading);
        }
        const double ms = t.GetMillisecondsElapsed();
        if (!CheckRc(scene, silent, dim == 3 ? "SEM_ComputeOffsets3D" : "SEM_ComputeOffsets", rc,
                     { "", "No source loaded", "Invalid parameters" }))
            return false;
        m_offsetsMs = ms;

        return LoadOffsetShells(scene, silent);
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
            SEM_MeshParams3D params3d{ tetMethod, param, (double)tetMaxEdgeLen };
            Timer t; t.Restart();
            rc = SafeBuildMesh3D(&params3d);
            const double ms = t.GetMillisecondsElapsed();
            if (rc == -100) {
                Report(scene, silent, "TetGen DLL crashed (access violation caught). "
                                      "Try a larger volume or a looser quality bound.");
                return false;
            }
            if (!CheckRc(scene, silent, "SEM_BuildMesh3D", rc,
                         { "", "No surface loaded", "Compute offsets first",
                           "", "Tetrahedralization failed", "Invalid method" }))
                return false;
            m_meshMs = ms;
        } else {
            double param = (double)meshParam;
            if (meshParamEdgeUnits && meshParam > 0.0f &&
                (meshMethod == SEM_STEINER_MAX_AREA || meshMethod == SEM_STEINER_SIZING))
                param *= MeshParamFactor();
            SEM_MeshParams params{ meshMethod, param, (double)steinerMargin };
            Timer t; t.Restart();
            rc = SafeBuildMesh(&params);
            const double ms = t.GetMillisecondsElapsed();
            if (rc == -100) {
                Report(scene, silent, "Mesher DLL crashed (access violation caught). "
                                      "Try another Steiner method or a larger parameter.");
                return false;
            }
            if (!CheckRc(scene, silent, "SEM_BuildMesh", rc,
                         { "", "No source loaded", "Compute offsets first",
                           "Not enough valid lines", "Triangulation failed", "Invalid method" }))
                return false;
            m_meshMs = ms;
        }

        std::string p = OutPath(dim == 3 ? "_mesh3d.csv3d" : "_mesh.csv3d");
        DropMesh(scene);
        m_mesh = scene.AddFromCSV3D(p, "mesh_" + Stem(m_srcPath), AttachParent(), nullptr, Colors::CYAN, Colors::YELLOW);
        m_meshPath = p;
        if (dim == 3) ConfigureSurface3D(m_mesh);
        m_meshStats = ComputeStats(p);
        snprintf(status, sizeof(status), "Mesh: %s", BaseName(p).c_str());
        return true;
    }
    catch (const std::exception& e) { Report(scene, silent, std::string("Mesh exception: ") + e.what()); }
    catch (...)                     { Report(scene, silent, "Mesh: unknown exception."); }
    return false;
}

}
