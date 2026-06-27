#include "SemSessionDetail.h"
#include <map>
#include <set>
#include <cmath>
#include <fstream>
#include <algorithm>
#include <excpt.h>

namespace SemSessionNS {
namespace detail {

std::string BaseName(const std::string& path) {
    auto slash = path.find_last_of("\\/");
    return (slash != std::string::npos) ? path.substr(slash + 1) : path;
}
std::string Stem(const std::string& path) {
    std::string file = BaseName(path);
    auto dot = file.find_last_of('.');
    return (dot != std::string::npos) ? file.substr(0, dot) : file;
}

Stats ComputeStatsData(const CSV3DLoader::CSV3DData& d) {
    Stats s;
    if (d.nodes.empty()) return s;
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

Stats ComputeStats(const std::string& path) {
    CSV3DLoader::CSV3DData d;
    if (path.empty() || !CSV3DLoader::Load(path, d)) return Stats();
    return ComputeStatsData(d);
}

CSV3DLoader::CSV3DData ViewToData(const SEM_MeshView& v) {
    CSV3DLoader::CSV3DData d;
    if (v.num_nodes > 0 && v.coords) {
        d.nodes.reserve(v.num_nodes);
        for (int i = 0; i < v.num_nodes; ++i) {
            CSV3DLoader::Node n{};
            n.pos = { (float)v.coords[3 * i + 0], (float)v.coords[3 * i + 1], (float)v.coords[3 * i + 2] };
            n.T   = v.T ? (float)v.T[i] : 0.0f;
            d.nodes.push_back(n);
        }
    }
    if (v.num_tris > 0 && v.tris) {
        d.triangles.reserve(v.num_tris);
        for (int i = 0; i < v.num_tris; ++i)
            d.triangles.push_back({ (unsigned)v.tris[3 * i + 0], (unsigned)v.tris[3 * i + 1], (unsigned)v.tris[3 * i + 2] });
    }
    if (v.num_edges > 0 && v.edges) {
        d.edges.reserve(v.num_edges);
        for (int i = 0; i < v.num_edges; ++i)
            d.edges.push_back({ (unsigned)v.edges[2 * i + 0], (unsigned)v.edges[2 * i + 1] });
    }
    if (v.num_tets > 0 && v.tets) {
        d.tets.reserve(v.num_tets);
        for (int i = 0; i < v.num_tets; ++i)
            d.tets.push_back({ (unsigned)v.tets[4 * i + 0], (unsigned)v.tets[4 * i + 1],
                               (unsigned)v.tets[4 * i + 2], (unsigned)v.tets[4 * i + 3] });
    }
    return d;
}

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

// True when the .csv3d is an OPEN contour (a single polyline, exactly two degree-1
// endpoints) whose endpoints both lie on the Y axis (x≈0, z≈0): the half-profile
// shape surface-of-revolution mode expects. A closed loop or branching contour => false.
bool IsOpenContourOnYAxis(const std::string& path) {
    CSV3DLoader::CSV3DData d;
    if (!CSV3DLoader::Load(path, d)) return false;
    const size_t n = d.nodes.size();
    if (n < 2) return false;

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
    if (ends.size() != 2) return false;

    auto onYAxis = [](const XMFLOAT3& p) {
        return std::fabs(p.x) < 1e-4f && std::fabs(p.z) < 1e-4f;
    };
    return onYAxis(d.nodes[ends[0]].pos) && onYAxis(d.nodes[ends[1]].pos);
}

// 3 when triangles are present AND the node cloud is not coplanar (thinnest bbox
// extent is a non-trivial fraction of the largest); 2 otherwise (contour, planar
// patch, or unreadable file).
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

// True when at least one node row has a non-zero T (the fifth column). Pre-solve
// mesh files have T=0 everywhere; post-solve files vary with T=1 at the boundary.
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
int SafeOffsetRemeshInPlaneSurface3D(int axis, double offset_value,
                                     const double* xyz, int num_nodes,
                                     const int* tris, int num_tris, SEM_MeshView* out) {
    __try { return SEM_OffsetRemeshInPlaneSurface3D(axis, offset_value, xyz, num_nodes, tris, num_tris, out); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -100; }
}
int SafeFlipIsosurface3D() {
    __try { return SEM_FlipIsosurface3D(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -100; }
}

std::string SemDetail() {
    const char* e = SEM_GetLastError();
    return (e && *e) ? std::string(" — ") + e : std::string();
}

} // namespace detail
} // namespace SemSessionNS
