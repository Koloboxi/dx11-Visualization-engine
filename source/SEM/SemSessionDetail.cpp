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

std::vector<XMFLOAT3> ViewNormals(const SEM_MeshView& v) {
    std::vector<XMFLOAT3> out;
    if (v.num_nodes > 0 && v.normals) {
        out.reserve(v.num_nodes);
        for (int i = 0; i < v.num_nodes; ++i)
            out.push_back(XMFLOAT3((float)v.normals[3 * i + 0],
                                   (float)v.normals[3 * i + 1],
                                   (float)v.normals[3 * i + 2]));
    }
    return out;
}

std::vector<XMFLOAT3> VertexPseudonormals(const CSV3DLoader::CSV3DData& d) {
    const size_t n = d.nodes.size();
    std::vector<XMFLOAT3> normals(n, XMFLOAT3(0.0f, 0.0f, 0.0f));
    if (n == 0) return normals;

    auto sub = [](const XMFLOAT3& a, const XMFLOAT3& b) {
        return XMFLOAT3(a.x - b.x, a.y - b.y, a.z - b.z);
    };
    auto cross = [](const XMFLOAT3& a, const XMFLOAT3& b) {
        return XMFLOAT3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
    };
    auto dot = [](const XMFLOAT3& a, const XMFLOAT3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
    auto len = [&](const XMFLOAT3& a) { return std::sqrt(dot(a, a)); };

    for (const auto& t : d.triangles) {
        const unsigned idx[3] = { t.x, t.y, t.z };
        if (idx[0] >= n || idx[1] >= n || idx[2] >= n) continue;
        const XMFLOAT3& A = d.nodes[idx[0]].pos;
        const XMFLOAT3& B = d.nodes[idx[1]].pos;
        const XMFLOAT3& C = d.nodes[idx[2]].pos;

        XMFLOAT3 fn = cross(sub(B, A), sub(C, A));
        float fl = len(fn);
        if (fl < 1e-20f) continue;                   // degenerate triangle
        fn = XMFLOAT3(fn.x / fl, fn.y / fl, fn.z / fl);

        const XMFLOAT3 P[3] = { A, B, C };
        for (int k = 0; k < 3; ++k) {
            XMFLOAT3 e0 = sub(P[(k + 1) % 3], P[k]);
            XMFLOAT3 e1 = sub(P[(k + 2) % 3], P[k]);
            float l0 = len(e0), l1 = len(e1);
            if (l0 < 1e-20f || l1 < 1e-20f) continue;
            float c = dot(e0, e1) / (l0 * l1);
            if (c < -1.0f) c = -1.0f; if (c > 1.0f) c = 1.0f;
            float w = std::acos(c);                  // interior angle weight
            XMFLOAT3& vn = normals[idx[k]];
            vn.x += fn.x * w; vn.y += fn.y * w; vn.z += fn.z * w;
        }
    }

    for (auto& vn : normals) {
        float l = len(vn);
        if (l > 1e-20f) { vn.x /= l; vn.y /= l; vn.z /= l; }
    }
    return normals;
}

float MeanTriEdgeLen(const CSV3DLoader::CSV3DData& d) {
    const size_t n = d.nodes.size();
    double total = 0.0;
    size_t count = 0;
    auto edge = [&](unsigned a, unsigned b) {
        if (a >= n || b >= n) return;
        const XMFLOAT3& pa = d.nodes[a].pos;
        const XMFLOAT3& pb = d.nodes[b].pos;
        float dx = pa.x - pb.x, dy = pa.y - pb.y, dz = pa.z - pb.z;
        total += std::sqrt((double)(dx * dx + dy * dy + dz * dz));
        ++count;
    };
    for (const auto& t : d.triangles) { edge(t.x, t.y); edge(t.y, t.z); edge(t.z, t.x); }
    return count ? (float)(total / count) : 1.0f;
}

std::vector<char> WindingMinorityTris(const CSV3DLoader::CSV3DData& d) {
    const size_t nT = d.triangles.size();
    std::vector<char> minority(nT, 0);
    if (nT == 0) return minority;

    // For each undirected edge, record the triangles using it and which way the
    // directed edge runs in that triangle (dir = true => low->high). Two triangles
    // sharing the edge are consistently wound iff their dir values differ.
    struct Use { int tri; bool dir; };
    std::map<std::pair<unsigned, unsigned>, std::vector<Use>> edgeMap;
    for (size_t t = 0; t < nT; ++t) {
        const unsigned v[3] = { d.triangles[t].x, d.triangles[t].y, d.triangles[t].z };
        for (int k = 0; k < 3; ++k) {
            unsigned a = v[k], b = v[(k + 1) % 3];
            if (a == b) continue;
            bool dir = a < b;
            auto key = dir ? std::make_pair(a, b) : std::make_pair(b, a);
            edgeMap[key].push_back({ (int)t, dir });
        }
    }

    // Triangle adjacency graph: neighbour + whether it must be flipped relative to
    // this triangle to agree (same edge direction in both => one is flipped).
    struct Adj { int tri; bool flip; };
    std::vector<std::vector<Adj>> adj(nT);
    for (const auto& kv : edgeMap) {
        const auto& us = kv.second;
        if (us.size() != 2) continue;                // ignore boundary / non-manifold edges
        bool flip = (us[0].dir == us[1].dir);
        adj[us[0].tri].push_back({ us[1].tri, flip });
        adj[us[1].tri].push_back({ us[0].tri, flip });
    }

    // BFS each connected component, assigning a relative orientation label 0/1;
    // the smaller label-class in the component is the minority (flagged).
    std::vector<signed char> label(nT, -1);
    std::vector<int> stack;
    for (size_t seed = 0; seed < nT; ++seed) {
        if (label[seed] != -1) continue;
        label[seed] = 0;
        stack.clear();
        stack.push_back((int)seed);
        std::vector<int> comp;
        int count[2] = { 0, 0 };
        while (!stack.empty()) {
            int cur = stack.back(); stack.pop_back();
            comp.push_back(cur);
            ++count[label[cur]];
            for (const Adj& a : adj[cur]) {
                signed char want = a.flip ? (signed char)(1 - label[cur]) : label[cur];
                if (label[a.tri] == -1) { label[a.tri] = want; stack.push_back(a.tri); }
            }
        }
        // Majority orientation keeps; the smaller class (ties -> label 1, the set
        // flipped relative to the seed) is painted as the minority.
        signed char minLabel = (count[1] < count[0]) ? 1 : (count[0] < count[1] ? 0 : 1);
        for (int t : comp) if (label[t] == minLabel) minority[t] = 1;
    }
    return minority;
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
int SafeExtractIsosurface3D(double value, SEM_Vec3 axis, double offset_value, double min_offset_value,
                            double target_len_mult, int iterations) {
    __try { return SEM_ExtractIsosurface3D(value, axis, offset_value, min_offset_value,
                                           target_len_mult, iterations); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -100; }
}
int SafeOffsetRemeshInPlaneSurface3D(SEM_Vec3 axis, double offset_value,
                                     const double* xyz, int num_nodes,
                                     const int* tris, int num_tris,
                                     double min_offset_value,
                                     double target_len_mult, int iterations,
                                     SEM_MeshView* out) {
    __try {
        return SEM_OffsetRemeshInPlaneSurface3D(axis, offset_value, xyz, num_nodes,
                                                tris, num_tris, min_offset_value,
                                                target_len_mult, iterations, out);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -100; }
}

std::string SemDetail() {
    const char* e = SEM_GetLastError();
    return (e && *e) ? std::string(" — ") + e : std::string();
}

} // namespace detail
} // namespace SemSessionNS
