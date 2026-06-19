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

// Return true when at least one node row in the csv3d file has a non-zero T value
// (the fifth semicolon-separated column). Pre-solve mesh files have T=0 everywhere;
// post-solve files have a varying field with T=1 at the source boundary.
bool MeshFileHasTField(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    bool inNodes = false;
    std::string line;
    while (std::getline(f, line)) {
        size_t s = line.find_first_not_of(" \t\r");
        if (s == std::string::npos) continue;
        if (line[s] == '#') {
            std::string tag = line.substr(s + 1);
            size_t cut = tag.find_first_of("; \t\r");
            if (cut != std::string::npos) tag = tag.substr(0, cut);
            inNodes = (tag == "nodes");
            continue;
        }
        if (!inNodes) continue;
        auto tok = CSV3DLoader::detail::SplitSemi(line);
        if (tok.size() < 5) continue;
        if (!CSV3DLoader::detail::IsNumber(tok[1])) continue;
        try { if (std::stof(tok[4]) != 0.0f) return true; } catch (...) {}
    }
    return false;
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
int SafeSolveThermal3D(int use_source_sdf, float max_inward) {
    __try { return SEM_SolveThermal3D(use_source_sdf, max_inward); }
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
    // The grid mesher's knob is a spacing (a length), so the factor is just the
    // mean source edge length.
    double avg = SEM_GetAvgEdgeLen();
    if (avg <= 0.0) avg = 1.0;
    return avg;
}

// Edge-length multiplier for the 3D tet knob. The BAND method's knob is a
// Steiner grid cell size (a length), so the factor is the mean surface edge
// length. (LAYERED's knob is a flag and never uses this conversion.)
double SemSession::TetParamFactor() const {
    double avg = SEM_GetSurfaceAvgEdgeLen3D();
    if (avg <= 0.0) avg = 1.0;
    return avg;
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
    m_meshPath.clear();
    m_isolinePath.clear();
    m_thermalSolved = false;
    m_offStats  = Stats();
    m_meshStats = Stats();
    // A fresh source resets the SEM core (SEM_LoadSurface3D/SEM_LoadCSV3D below),
    // which also clears its clip planes; drop ours, their mirror copies and the
    // measured stage times.
    DropClipMirrors(scene);
    DropClipPlaneNodes(scene);
    m_appliedClipPlanes.clear();
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
        int rc = (dim == 3) ? SEM_LoadSurface3D(m_srcPath.c_str(), srcClosed3D ? 1 : 0)
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
    // The plane nodes were children of the (now-gone) source subtree; just drop
    // our dangling references — the scene already destroyed the nodes.
    clipPlaneNodes.clear();
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
    // Drop plane nodes whose subtree was removed externally (e.g. tree delete).
    for (auto it = clipPlaneNodes.begin(); it != clipPlaneNodes.end(); ) {
        if (*it && !Alive(scene, *it)) it = clipPlaneNodes.erase(it);
        else ++it;
    }
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
    // Read each plane node's current (normal, d) and flatten to the layout
    // SEM_SetClipPlanes3D expects.
    std::vector<double> flat;
    flat.reserve(clipPlaneNodes.size() * 4);
    for (ClipPlaneNode* node : clipPlaneNodes) {
        if (!node) continue;
        XMFLOAT4 p = node->GetPlane();
        flat.push_back(p.x); flat.push_back(p.y); flat.push_back(p.z); flat.push_back(p.w);
    }
    const int count = (int)(flat.size() / 4);
    int rc = SEM_SetClipPlanes3D(flat.empty() ? nullptr : flat.data(), count);
    if (!CheckRc(scene, false, "SEM_SetClipPlanes3D", rc,
                 { "", "No surface loaded", "Invalid planes" }))
        return;

    // Clipping is applied during SEM_BuildMesh3D, so the cached mesh (and the
    // thermal field/isosurface downstream) is now stale.
    DropMesh(scene);
    m_dirty[STAGE_MESH] = m_dirty[STAGE_THERMAL] = true;
    if (AnyClipMirror()) RebuildClipMirrors(scene);
    scene.UpdateLight();

    m_appliedClipPlanes.clear();
    m_appliedClipPlanes.reserve(count);
    for (int i = 0; i < count; ++i)
        m_appliedClipPlanes.push_back(XMFLOAT4((float)flat[i * 4 + 0], (float)flat[i * 4 + 1],
                                               (float)flat[i * 4 + 2], (float)flat[i * 4 + 3]));
    snprintf(status, sizeof(status), "Clip planes: %d set.", count);
}

bool SemSession::ClipPlanesChanged() const {
    size_t count = 0;
    for (ClipPlaneNode* node : clipPlaneNodes) if (node) ++count;
    if (count != m_appliedClipPlanes.size()) return true;
    size_t k = 0;
    for (ClipPlaneNode* node : clipPlaneNodes) {
        if (!node) continue;
        XMFLOAT4 p = node->GetPlane();
        const XMFLOAT4& q = m_appliedClipPlanes[k++];
        if (p.x != q.x || p.y != q.y || p.z != q.z || p.w != q.w) return true;
    }
    return false;
}

void SemSession::AutoApplyClipPlanes(Scene& scene) {
    if (dim != 3 || AsyncRunning()) return;
    if (ClipPlanesChanged()) SetClipPlanes3D(scene);
}

void SemSession::ClearClipPlanes3D(Scene& scene) {
    if (AsyncRunning()) return;
    int rc = SEM_ClearClipPlanes3D();
    CheckRc(scene, false, "SEM_ClearClipPlanes3D", rc, { "", "No surface loaded" });
    DropClipMirrors(scene);
    DropClipPlaneNodes(scene);
    DropMesh(scene);
    m_dirty[STAGE_MESH] = m_dirty[STAGE_THERMAL] = true;
    scene.UpdateLight();
    m_appliedClipPlanes.clear();
    snprintf(status, sizeof(status), "Clip planes cleared.");
}

ClipPlaneNode* SemSession::AddClipPlane(Scene& scene) {
    if (dim != 3 || !HasSource()) { Report(scene, false, "Clip planes apply to the 3D pipeline only."); return nullptr; }

    XMFLOAT3 lo{}, hi{};
    XMFLOAT3 centre{ 0, 0, 0 };
    if (SourceBBox(m_srcPath, lo, hi))
        centre = { (lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f };

    // Default plane: normal +X through the bbox centre.
    const XMFLOAT3 n0(1, 0, 0);
    const float d0 = -(n0.x * centre.x + n0.y * centre.y + n0.z * centre.z);

    ClipPlaneNode* node = new ClipPlaneNode();
    node->name = "clip_plane_" + std::to_string(clipPlaneNodes.size());
    AttachParent() ? AttachParent()->AddChild(node) : scene.root.AddChild(node);

    BuildClipPlaneRect(scene, node, XMFLOAT4(n0.x, n0.y, n0.z, d0));
    clipPlaneNodes.push_back(node);
    snprintf(status, sizeof(status), "Added clip plane %d.", (int)clipPlaneNodes.size() - 1);
    return node;
}

void SemSession::RemoveClipPlane(Scene& scene, int idx) {
    if (idx < 0 || idx >= (int)clipPlaneNodes.size()) return;
    ClipPlaneNode* node = clipPlaneNodes[idx];
    clipPlaneNodes.erase(clipPlaneNodes.begin() + idx);
    if (node && Alive(scene, node)) scene.RemoveNode(node);
    RebuildClipMirrors(scene);
    scene.UpdateLight();
}

ClipPlaneNode* SemSession::FindClipPlaneByRect(Primitive* prim) const {
    if (!prim) return nullptr;
    for (ClipPlaneNode* node : clipPlaneNodes)
        if (node && node->rect == prim) return node;
    return nullptr;
}

void SemSession::DropClipPlaneNodes(Scene& scene) {
    for (ClipPlaneNode* node : clipPlaneNodes)
        if (node && Alive(scene, node)) scene.RemoveNode(node);
    clipPlaneNodes.clear();
}

static const char* kClipMirrorPrefix = "semmirror_";

bool SemSession::AnyClipMirror() const {
    for (ClipPlaneNode* node : clipPlaneNodes) if (node && node->showMirror) return true;
    return false;
}

void SemSession::DropClipMirrors(Scene& scene) {
    scene.RemovePrimitivesByPrefix(kClipMirrorPrefix);
}

void SemSession::RebuildClipMirrors(Scene& scene) {
    DropClipMirrors(scene);
    if (!HasSource()) return;

    // The enabled planes generate a reflection orbit: starting from the original
    // (the empty sequence), each enabled plane reflects every image produced so
    // far — including the ones an earlier plane made — and appends the new images.
    // n enabled planes therefore yield 2^n - 1 mirror copies. Capped so a handful
    // of planes can't explode the scene.
    std::vector<int> enabled;
    for (int i = 0; i < (int)clipPlaneNodes.size(); ++i)
        if (clipPlaneNodes[i] && clipPlaneNodes[i]->showMirror) enabled.push_back(i);
    if (enabled.empty()) return;
    if (enabled.size() > 4) enabled.resize(4);

    std::vector<std::vector<int>> seqs = { {} };   // identity
    for (int planeIdx : enabled) {
        const size_t base = seqs.size();
        for (size_t s = 0; s < base; ++s) {
            std::vector<int> next = seqs[s];
            next.push_back(planeIdx);
            seqs.push_back(std::move(next));
        }
    }

    auto mirrorChain = [&](Primitive* src, const std::vector<int>& seq, const std::string& name,
                           SceneNode* parent) {
        // Build the composite by reflecting through each plane in order, deleting
        // the unregistered intermediates and registering only the final image.
        Primitive* cur = src;
        Primitive* prevTemp = nullptr;
        for (size_t k = 0; k < seq.size(); ++k) {
            XMFLOAT4 pl = clipPlaneNodes[seq[k]]->GetPlane();
            const bool last = (k + 1 == seq.size());
            if (last) {
                scene.AddMirroredCopy(cur, pl, name, parent);
            } else {
                Primitive* tmp = cur->CloneMirrored(pl);
                if (prevTemp) delete prevTemp;
                prevTemp = tmp;
                cur = tmp;
                if (!cur) break;
            }
        }
        if (prevTemp) delete prevTemp;
    };

    for (const auto& seq : seqs) {
        if (seq.empty()) continue;
        std::string suffix;
        for (int p : seq) suffix += std::to_string(p) + "-";
        if (Alive(scene, m_srcPrim))
            mirrorChain(m_srcPrim, seq, kClipMirrorPrefix + ("src_" + suffix), m_srcPrim);
        if (Alive(scene, m_isoline))
            mirrorChain(m_isoline, seq, kClipMirrorPrefix + ("iso_" + suffix), m_isoline);
    }
    scene.UpdateLight();
}

void SemSession::ShowClipMirror(Scene& scene, int planeIdx, bool show) {
    if (planeIdx < 0 || planeIdx >= (int)clipPlaneNodes.size()) return;
    if (clipPlaneNodes[planeIdx]) clipPlaneNodes[planeIdx]->showMirror = show;
    RebuildClipMirrors(scene);
    snprintf(status, sizeof(status), "Clip mirror %d: %s", planeIdx, show ? "on" : "off");
}

void SemSession::BuildClipPlaneRect(Scene& scene, ClipPlaneNode* node, const XMFLOAT4& plane) {
    if (!node) return;

    float half = 60.0f;
    XMFLOAT3 lo, hi;
    if (SourceBBox(m_srcPath, lo, hi)) {
        const float ex = hi.x - lo.x, ey = hi.y - lo.y, ez = hi.z - lo.z;
        const float diag = std::sqrt(ex * ex + ey * ey + ez * ez);
        half = 0.6f * (diag > 1e-3f ? diag : 100.0f);
    }

    // A square in the local XY plane (z = 0): its local +Z is the plane normal.
    const XMFLOAT3 q00(-half, -half, 0.0f), q10(half, -half, 0.0f),
                   q11(half, half, 0.0f),  q01(-half, half, 0.0f);
    std::vector<XMFLOAT3> poses = { q00, q10, q11, q00, q11, q01 };

    const XMFLOAT4 softRed (0.86f, 0.42f, 0.40f, 0.20f);
    const XMFLOAT4 softBlue(0.40f, 0.52f, 0.86f, 0.20f);
    std::vector<XMFLOAT4> cols(6, softRed);

    Primitive* rect = scene.AddColoredTriangles(poses, cols, node->name + "_rect", node, /*ensureCCW*/ false);
    if (!rect) return;
    rect->SetIlluminationCapability(false);
    rect->SetUseVertexColor(false);
    rect->SetColor(softRed);
    rect->SetTwoSided(true, softBlue);
    node->rect = rect;
    node->SetPlane(plane);
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
    int rc = (dim == 3) ? SafeSolveThermal3D(useSourceSdf, maxInward) : SafeSolveThermal();
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
        if (AnyClipMirror()) RebuildClipMirrors(scene);
        snprintf(status, sizeof(status), "%s T=%.3f: %s",
                 dim == 3 ? "Isosurface" : "Isoline", v, BaseName(p).c_str());
        return true;
    }
    catch (const std::exception& e) { Report(scene, silent, std::string("Isoline exception: ") + e.what()); }
    catch (...)                     { Report(scene, silent, "Isoline: unknown exception."); }
    return false;
}

void SemSession::SetBCView(Scene& scene, bool on) {
    if (bcView == on) return;
    bcView = on;
    // Only a solved mesh carries a meaningful T field. Switch the pre-built colour
    // set in place (fast, no file reload / geometry rebuild); fall back to a full
    // recolour reload if the sets are not present for some reason.
    if (m_thermalSolved && Alive(scene, m_mesh)) {
        if (!m_mesh->ActivateColorSet(on ? "bc" : "tfield"))
            ReloadMeshColored(scene);
    }
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

    // Cache-state sidecar (3D only): the exact signed offset distances, clip
    // planes, subdivision level and thermal-BC origin tags that the per-stage
    // geometry files cannot carry. Restore it LAST — after the offsets and mesh
    // are loaded — so the tags/distances line up with the loaded shells/mesh
    // (SEM_INTEGRATION.md §4a). Best-effort: a mismatch is reported but does not
    // abort the reload (the stages still loaded from their geometry files).
    if (dim == 3 && fs::exists(offShell0)) {
        const std::string statePath =
            (fs::path(m_workDir) / (Stem(m_srcPath) + "_state3d.txt")).string();
        if (fs::exists(statePath)) {
            int rc = SEM_LoadState3D(m_workDir.c_str());
            CheckRc(scene, false, "SEM_LoadState3D", rc,
                    { "", "No source loaded", "State file missing or unparseable",
                      "State does not match the loaded stages" });
        }
    }

    // Thermal: detected by two conditions — the mesh nodes carry a non-zero T field
    // (the solver rewrites the file in place) AND the isotherm/isosurface file exists.
    const std::string isoPath =
        (fs::path(m_workDir) /
         (Stem(m_srcPath) + (dim == 3 ? "_isosurface3d.csv3d" : "_isoline.csv3d"))).string();
    if (Alive(scene, m_mesh) && MeshFileHasTField(meshPath) && fs::exists(isoPath)) {
        m_thermalSolved = true;
        ReloadMeshColored(scene);
        DropIsoline(scene);
        const XMFLOAT4 green(0.0f, 1.0f, 0.0f, 1.0f);
        const std::string namePrefix = (dim == 3 ? "isosurface_" : "isoline_");
        m_isoline = scene.AddFromCSV3D(isoPath, namePrefix + Stem(m_srcPath),
                                       AttachParent(), &green, Colors::BLUE, Colors::RED);
        if (dim == 3 && m_isoline) ConfigureSurface3D(m_isoline);
        m_isolinePath = isoPath;
    }

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
    isoValue      = 0.8f;
    if (dim == 3) {
        tetMethod    = SEM_TET_LAYERED;
        tetParam     = 0.0f; tetParamEdgeUnits = false;
        tetMaxEdgeLen = 10.0f;
        useSourceSdf  = 1; maxInward = 0.04f;
    } else {
        meshMethod = SEM_STEINER_GRID;
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
        if (tetParamEdgeUnits && tetParam > 0.0f && tetMethod == SEM_TET_BAND)
            param *= TetParamFactor();
        m_job.tetParam = param;
    }
    m_job.tetMaxEdgeLen = (double)tetMaxEdgeLen;
    m_job.tetLayerSpan = tetLayerSpan;
    m_job.isoValue = isoValue;
    m_job.maxInward = maxInward;
    m_job.useSourceSdf = useSourceSdf;

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
    m_job.cancel.store(false);
    m_job.cancelled.store(false);
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

    const bool cancelled = m_job.cancelled.load();
    // A genuine failure aborts; a cancellation still applies whatever stages
    // finished before the stop (their paths are set, the skipped ones are empty).
    if (!m_job.ok.load() && !cancelled) {
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
        // When this run also solved the thermal field the mesh file already holds
        // the solved T, so colour it like the sync solve (blue..red, honouring the
        // BC view). An unsolved mesh keeps the neutral cyan..yellow gradient.
        const bool solved = m_job.runThermal;
        m_mesh = scene.AddFromCSV3D(m_job.meshPath, "mesh_" + Stem(m_srcPath),
                                    AttachParent(), nullptr,
                                    solved ? Colors::BLUE : Colors::CYAN,
                                    solved ? Colors::RED  : Colors::YELLOW,
                                    true, solved && bcView, /*registerColorSets*/ solved);
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

    // A planned stage's dirty flag was cleared up-front by RecomputeUpToAsync;
    // when cancellation skipped it (no output produced) restore that flag so the
    // next Apply recomputes it instead of trusting a stage that never ran.
    if (cancelled) {
        if (m_job.runOffsets && m_job.offsetsPath.empty()) m_dirty[STAGE_OFFSETS] = true;
        if (m_job.runMesh    && m_job.meshPath.empty())    m_dirty[STAGE_MESH]    = true;
        if (m_job.runThermal && m_job.isoPath.empty())     m_dirty[STAGE_THERMAL] = true;
    }

    if (AnyClipMirror()) RebuildClipMirrors(scene);
    scene.UpdateLight();
    snprintf(status, sizeof(status), cancelled ? "Cancelled." : "Done.");
}

void SemSession::CancelAsync() {
    if (m_job.running.load()) m_job.cancel.store(true);
}

bool SemSession::AsyncCancelRequested() const {
    return m_job.running.load() && m_job.cancel.load();
}

void SemSession::PipelineWorkerBody() {
    int idx = 0;   // index of the current stage among the planned ones

    // Stop here if cancellation was requested before this stage starts. The SEM
    // call already in flight cannot be interrupted, so we can only break between
    // stages: every stage still queued is skipped, finished ones keep their
    // results (their paths are already set). Returns true when it stopped.
    auto stopIfCancelled = [&]() -> bool {
        if (!m_job.cancel.load()) return false;
        m_job.cancelled.store(true);
        m_job.ok.store(false);
        m_job.done.store(true);
        return true;
    };

    // Offset shells — SEM_ComputeOffsets3D / SEM_ComputeOffsetsAt3D.
    if (m_job.runOffsets) {
        if (stopIfCancelled()) return;
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
        if (stopIfCancelled()) return;
        m_job.stageKind.store(1);
        m_job.progressStage.store(idx);
        SEM_MeshParams3D params3d{ m_job.tetMethod, m_job.tetParam, m_job.tetMaxEdgeLen, m_job.tetLayerSpan };
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
        if (stopIfCancelled()) return;
        m_job.stageKind.store(2);
        m_job.progressStage.store(idx);
        Timer t; t.Restart();
        int rc = SafeSolveThermal3D(m_job.useSourceSdf, m_job.maxInward);
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
    // Register both colour sets ("tfield"/"bc") so the BC/T toggle can switch the
    // mesh in place afterwards without reloading the file (see SetBCView).
    m_mesh = scene.AddFromCSV3D(path, "mesh_" + Stem(m_srcPath), AttachParent(),
                                nullptr, Colors::BLUE, Colors::RED, true, bcView, true);
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
            if (tetParamEdgeUnits && tetParam > 0.0f && tetMethod == SEM_TET_BAND)
                param *= TetParamFactor();
            SEM_MeshParams3D params3d{ tetMethod, param, (double)tetMaxEdgeLen, tetLayerSpan };
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
            if (meshParamEdgeUnits && meshParam > 0.0f && meshMethod == SEM_STEINER_GRID)
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
