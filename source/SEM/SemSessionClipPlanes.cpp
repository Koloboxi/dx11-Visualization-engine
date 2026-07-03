#include "SemSessionDetail.h"
#include <cmath>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <set>
#include <vector>

namespace fs = std::filesystem;

namespace SemSessionNS {

using namespace detail;

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
    int rc = SEM_SetClipPlanes3D(flat.empty() ? nullptr : flat.data(), count, clipPlaneTol);
    if (!CheckRc(scene, false, "SEM_SetClipPlanes3D", rc,
                 { "", "No surface loaded", "Invalid planes" }))
        return;

    // Clipping is applied during SEM_BuildMesh3D, so the cached mesh (and the
    // thermal field/isosurface downstream) is now stale.
    DropMesh(scene);
    m_dirty[STAGE_MESH] = m_dirty[STAGE_THERMAL] = m_dirty[STAGE_ISOSURFACE] = true;
    if (AnyClipMirror()) RebuildClipMirrors(scene);
    // The core re-snapped the source onto the planes; request a rebuild of the
    // displayed source (deferred by the caller until no plane is being dragged).
    m_srcRebuildPending = true;
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
    // No planes => no clip-change records; drop every category overlay (and its
    // shown flag) so a stale one does not linger.
    for (int c = 0; c < CLIPCHG_COUNT; ++c) { DropClipChanges(scene, c); m_clipChangesShown[c] = false; }
    DropMesh(scene);
    m_dirty[STAGE_MESH] = m_dirty[STAGE_THERMAL] = m_dirty[STAGE_ISOSURFACE] = true;
    m_appliedClipPlanes.clear();
    // The pristine source is restored in the core; rebuild the displayed one (which
    // also refreshes the on-plane overlay — now empty, no planes) to drop snapping.
    RebuildSourcePrim(scene);
    scene.UpdateLight();
    snprintf(status, sizeof(status), "Clip planes cleared.");
}

void SemSession::LoadClipPlanesFromState(Scene& scene) {
    if (dim != 3) return;
    DropClipPlaneNodes(scene);

    const std::string statePath =
        (fs::path(m_workDir) / (Stem(m_srcPath) + "_state3d.txt")).string();
    std::ifstream in(statePath);
    if (!in) return;

    // Find the "#clip_planes;<N>" marker, then read 4*N numeric values. The DLL
    // writes each plane on its own line with the four components ';'-separated
    // (e.g. "nx;ny;nz;d"), so split on ';' as well as whitespace: this restores
    // the planes whether the writer packs 4 per line or one value per line.
    std::vector<XMFLOAT4> planes;
    std::string line;
    const std::string key = "#clip_planes;";
    while (std::getline(in, line)) {
        if (line.compare(0, key.size(), key) != 0) continue;
        int n = 0;
        try { n = std::stoi(line.substr(key.size())); } catch (...) { return; }
        std::vector<double> v;
        v.reserve((size_t)n * 4);
        std::string vline;
        while ((int)v.size() < n * 4 && std::getline(in, vline)) {
            if (!vline.empty() && vline[0] == '#') break;   // next section started
            std::replace(vline.begin(), vline.end(), ';', ' ');
            std::istringstream ss(vline);
            double x;
            while ((int)v.size() < n * 4 && (ss >> x)) v.push_back(x);
        }
        if ((int)v.size() < n * 4) return;   // truncated / unexpected layout
        for (int i = 0; i < n; ++i)
            planes.push_back(XMFLOAT4((float)v[i * 4 + 0], (float)v[i * 4 + 1],
                                      (float)v[i * 4 + 2], (float)v[i * 4 + 3]));
        break;
    }
    if (planes.empty()) return;

    for (const XMFLOAT4& pl : planes) {
        ClipPlaneNode* node = new ClipPlaneNode();
        node->name = "clip_plane_" + std::to_string(clipPlaneNodes.size());
        ClipGroup(scene)->AddChild(node);
        BuildClipPlaneRect(scene, node, pl);
        clipPlaneNodes.push_back(node);
    }

    // The core already holds these planes (SEM_LoadState3D ran first), so record
    // them as the applied set — using each node's reconstructed (normalized)
    // plane, exactly as ClipPlanesChanged compares — so AutoApplyClipPlanes does
    // not re-push and dirty the freshly loaded mesh.
    m_appliedClipPlanes.clear();
    m_appliedClipPlanes.reserve(clipPlaneNodes.size());
    for (ClipPlaneNode* node : clipPlaneNodes)
        if (node) m_appliedClipPlanes.push_back(node->GetPlane());
    if (AnyClipMirror()) RebuildClipMirrors(scene);
}

// The shared grouping node that owns every clip-plane node. Created lazily under
// the source so the planes form one collapsible subtree; re-created if the
// previous one was torn down (source rebind, tree delete).
SceneNode* SemSession::ClipGroup(Scene& scene) {
    if (m_clipGroup && Alive(scene, m_clipGroup)) return m_clipGroup;
    m_clipGroup = scene.AddGroupNode("clip_planes", AttachParent());
    return m_clipGroup;
}

ClipPlaneNode* SemSession::AddClipPlane(Scene& scene) {
    if (dim != 3 || !HasSource()) { Report(scene, false, "Clip planes apply to the 3D pipeline only."); return nullptr; }

    // Default plane through the origin (d = 0), axis-aligned by index: the first
    // three planes get normals +X, +Y, +Z respectively (there are never more).
    const int idx = (int)clipPlaneNodes.size();
    const XMFLOAT3 n0 = (idx == 1) ? XMFLOAT3(0, 1, 0)
                      : (idx == 2) ? XMFLOAT3(0, 0, 1)
                                   : XMFLOAT3(1, 0, 0);
    const float d0 = 0.0f;

    ClipPlaneNode* node = new ClipPlaneNode();
    node->name = "clip_plane_" + std::to_string(clipPlaneNodes.size());
    ClipGroup(scene)->AddChild(node);

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
    // Removing the grouping node destroys its whole subtree (every plane node and
    // its rectangle), so the per-node loop is only the fallback when no group
    // exists yet. Clear our dangling references afterwards.
    if (m_clipGroup && Alive(scene, m_clipGroup)) scene.RemoveNode(m_clipGroup);
    else for (ClipPlaneNode* node : clipPlaneNodes)
        if (node && Alive(scene, node)) scene.RemoveNode(node);
    m_clipGroup = nullptr;
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

static const char* kOnPlanePrefix = "sem_onplane_";

// Per-surface overlay colours: source surface white, source isosurface orange,
// final isosurface cyan (matching the orange/cyan pseudonormal conventions used
// elsewhere for those two sheets).
static const XMFLOAT4 kOnPlaneSrcColor    = Colors::WHITE;
static const XMFLOAT4 kOnPlaneIsoSrcColor = XMFLOAT4(1.0f, 0.55f, 0.0f, 1.0f);
static const XMFLOAT4 kOnPlaneIsoFinColor = Colors::CYAN;

void SemSession::DropClipOnPlane(Scene& scene) {
    if (m_onPlaneGroup && Alive(scene, m_onPlaneGroup)) scene.RemoveNode(m_onPlaneGroup);
    m_onPlaneGroup = nullptr;
}

// Add one surface's on-plane overlay under `grp`. The test is strict: the core has
// already snapped on-plane geometry exactly onto each plane (the source via
// SEM_SetClipPlanes3D's on_plane_rel_tol, the isosurfaces by being cut flush), so a
// vertex counts as "on a plane" when its signed distance is within only `tol`
// (a small float-rounding band). A triangle whose three vertices all lie on a
// SINGLE plane is drawn as a filled face (lifted off the surface by `lift` along
// that plane's normal to avoid z-fighting); an edge with both endpoints on one
// plane that is not part of such a triangle is drawn as a line; an on-plane vertex
// covered by neither is drawn as a point.
bool SemSession::BuildOnPlaneOverlay(Scene& scene, SceneNode* grp,
                                     const CSV3DLoader::CSV3DData& data,
                                     const std::vector<XMFLOAT4>& planes,
                                     float tol, float lift, const XMFLOAT4& color,
                                     const char* tag) {
    const size_t n = data.nodes.size();
    if (n == 0 || data.triangles.empty() || planes.empty()) return false;

    // Per-plane on-plane flags for every vertex.
    std::vector<std::vector<char>> on(planes.size(), std::vector<char>(n, 0));
    std::vector<char> onAny(n, 0);
    for (size_t p = 0; p < planes.size(); ++p) {
        const XMFLOAT4& pl = planes[p];
        for (size_t i = 0; i < n; ++i) {
            const XMFLOAT3& q = data.nodes[i].pos;
            float dist = pl.x * q.x + pl.y * q.y + pl.z * q.z + pl.w;
            if (std::fabs(dist) <= tol) { on[p][i] = 1; onAny[i] = 1; }
        }
    }

    std::vector<char> covered(n, 0);

    // Planar triangles: all three vertices on a single plane. Each is emitted as an
    // explicit triangle soup lifted off the surface along that plane's normal, and
    // marks its vertices and its three edges as covered so they are not redrawn.
    std::vector<XMFLOAT3> tposes;
    std::vector<XMFLOAT4> tcols;
    std::set<std::pair<unsigned, unsigned>> triEdges;
    auto markEdge = [&](unsigned a, unsigned b) {
        triEdges.insert(a < b ? std::make_pair(a, b) : std::make_pair(b, a));
    };
    for (const auto& t : data.triangles) {
        if (t.x >= n || t.y >= n || t.z >= n) continue;
        for (size_t p = 0; p < planes.size(); ++p) {
            if (!(on[p][t.x] && on[p][t.y] && on[p][t.z])) continue;
            const XMFLOAT4& pl = planes[p];
            for (unsigned idx : { t.x, t.y, t.z }) {
                const XMFLOAT3& q = data.nodes[idx].pos;
                tposes.push_back(XMFLOAT3(q.x + pl.x * lift, q.y + pl.y * lift, q.z + pl.z * lift));
                tcols.push_back(color);
                covered[idx] = 1;
            }
            markEdge(t.x, t.y); markEdge(t.y, t.z); markEdge(t.z, t.x);
            break;
        }
    }

    // On-plane edges from the triangle topology (undirected, deduped): drawn when
    // both endpoints lie on one plane and the edge is not already part of a filled
    // planar triangle.
    std::set<std::pair<unsigned, unsigned>> drawn;
    auto consider = [&](unsigned a, unsigned b) {
        if (a == b || a >= n || b >= n) return;
        auto key = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
        if (triEdges.count(key)) return;
        for (size_t p = 0; p < planes.size(); ++p)
            if (on[p][a] && on[p][b]) { drawn.insert(key); return; }
    };
    for (const auto& t : data.triangles) { consider(t.x, t.y); consider(t.y, t.z); consider(t.z, t.x); }
    for (const auto& e : drawn) { covered[e.first] = 1; covered[e.second] = 1; }

    // Isolated on-plane vertices covered by neither a triangle nor an edge.
    std::vector<XMFLOAT3> pts;
    for (size_t i = 0; i < n; ++i)
        if (onAny[i] && !covered[i]) pts.push_back(data.nodes[i].pos);

    if (tposes.empty() && drawn.empty() && pts.empty()) return false;

    if (!tposes.empty()) {
        Primitive* faces = scene.AddColoredTriangles(tposes, tcols,
                                                     std::string(kOnPlanePrefix) + tag + "_tris", grp);
        if (faces) { faces->SetIlluminationCapability(false); faces->SetUseVertexColor(false);
                     faces->SetColor(color); }
    }
    if (!drawn.empty()) {
        CSV3DLoader::CSV3DData seg;
        seg.nodes = data.nodes;                   // positions referenced by index
        seg.edges.assign(drawn.begin(), drawn.end());
        Primitive* lines = scene.AddFromCSV3DData(seg, std::string(kOnPlanePrefix) + tag + "_lines",
                                                  grp, &color);
        if (lines) { lines->SetIlluminationCapability(false); lines->SetUseVertexColor(false);
                     lines->SetColor(color); StyleLines(lines, LINESTYLE_MEDIUM); }
    }
    if (!pts.empty()) {
        Primitive* cloud = scene.AddPointCloud(pts, color, std::string(kOnPlanePrefix) + tag + "_points", grp);
        if (cloud) cloud->SetIlluminationCapability(false);
    }
    return true;
}

// Build the on-plane overlay for the three pipeline surfaces at once: the source
// surface, the source isosurface and the final isosurface. Each is fetched from
// the core in turn (every view is copied out before the next SEM_* call), tested
// against the live clip planes and drawn in its own colour. All share one group
// node so the single "Show geometry on planes" checkbox toggles them together.
void SemSession::BuildClipOnPlane(Scene& scene) {
    if (dim != 3 || AsyncRunning()) return;

    std::vector<XMFLOAT4> planes;
    planes.reserve(clipPlaneNodes.size());
    for (ClipPlaneNode* node : clipPlaneNodes)
        if (node) planes.push_back(node->GetPlane());     // GetPlane is unit-normalized
    if (planes.empty()) return;

    double avg = SEM_GetSurfaceAvgEdgeLen3D();
    if (avg <= 0.0) avg = 1.0;
    // Strict on-plane test: only absorb the float-rounding noise from the core's
    // exact snap (geometry is double there, the view hands it back as float).
    const float tol  = (float)(1e-4 * avg);
    const float lift = (float)(2e-3 * avg);       // lift filled tris clear of the surface

    SceneNode* grp = scene.AddGroupNode(kOnPlanePrefix + Stem(m_srcPath), AttachParent());
    m_onPlaneGroup = grp;
    bool any = false;

    SEM_MeshView v{};
    if (Alive(scene, m_srcPrim) && SEM_GetSourceSurface3D(&v) == 0 && v.num_nodes > 0)
        any |= BuildOnPlaneOverlay(scene, grp, ViewToData(v), planes, tol, lift,
                                   kOnPlaneSrcColor, "src");

    if (sessionMode == SESSION_STANDALONE_REMESH) {
        // Standalone offset-remesh: its result lives only in the host copy (m_isoData);
        // the pipeline iso caches (SEM_GetSourceIsosurface3D / SEM_GetIsosurface3D) are
        // empty in this mode. Draw the result's on-plane geometry in the final-iso colour.
        if (!m_isoData.nodes.empty() && !m_isoData.triangles.empty())
            any |= BuildOnPlaneOverlay(scene, grp, m_isoData, planes, tol, lift,
                                       kOnPlaneIsoFinColor, "isofin");
    } else {
        SEM_MeshView vi{};
        if (SEM_GetSourceIsosurface3D(&vi) == 0 && vi.num_nodes > 0)
            any |= BuildOnPlaneOverlay(scene, grp, ViewToData(vi), planes, tol, lift,
                                       kOnPlaneIsoSrcColor, "isosrc");

        SEM_MeshView vf{};
        if (SEM_GetIsosurface3D(&vf) == 0 && vf.num_nodes > 0)
            any |= BuildOnPlaneOverlay(scene, grp, ViewToData(vf), planes, tol, lift,
                                       kOnPlaneIsoFinColor, "isofin");
    }

    if (!any) { DropClipOnPlane(scene); }         // nothing lay on any plane
}

void SemSession::RefreshClipOnPlane(Scene& scene) {
    if (!m_clipOnPlaneShown) return;
    DropClipOnPlane(scene);
    BuildClipOnPlane(scene);
    scene.UpdateLight();
}

void SemSession::ShowClipOnPlane(Scene& scene, bool show) {
    m_clipOnPlaneShown = show;
    DropClipOnPlane(scene);
    if (show) BuildClipOnPlane(scene);
    scene.UpdateLight();
}

static const char* kClipChangePrefix = "sem_clipchg_";

// Snap displacements drawn as yellow segments (original -> snapped), removed
// geometry as a translucent red surface (matching the soft-red "kept" / soft-blue
// "removed" convention of the plane rectangles, but opaque enough to read as a solid
// clipped-away sheet).
static const XMFLOAT4 kClipChgMoveColor   = Colors::YELLOW;
static const XMFLOAT4 kClipChgRemoveColor = XMFLOAT4(0.90f, 0.30f, 0.30f, 0.45f);

// Map a record's "<stage>/<op>" label to its window-section category: "offset<i>/.."
// -> Offsets, "isosurface/.." and "remesh/.." -> Isosurface, everything else
// ("source/..") -> Source surface.
static int ClipChangeCategoryOf(const char* stage) {
    if (!stage) return CLIPCHG_SOURCE;
    std::string s(stage);
    if (s.rfind("offset", 0) == 0)     return CLIPCHG_OFFSETS;
    if (s.rfind("isosurface", 0) == 0) return CLIPCHG_ISO;
    if (s.rfind("remesh", 0) == 0)     return CLIPCHG_ISO;
    return CLIPCHG_SOURCE;
}

void SemSession::BuildClipChanges(Scene& scene, int category) {
    if (dim != 3 || AsyncRunning()) return;
    if (category < 0 || category >= CLIPCHG_COUNT) return;

    const SEM_ClipChangeView* cc = nullptr;
    int n = 0;
    if (SEM_GetClipChanges3D(&cc, &n) != 0 || !cc || n <= 0) return;

    // Copy everything out of the cache-owned views first: the view pointers are only
    // valid until the next SEM_* call, and the records of this category are gathered
    // before any scene primitive is built.
    std::vector<XMFLOAT3> segNodes;                          // pairs: orig, snapped, ...
    std::vector<std::pair<unsigned, unsigned>> segEdges;
    std::vector<CSV3DLoader::CSV3DData> removedMeshes;
    for (int i = 0; i < n; ++i) {
        if (ClipChangeCategoryOf(cc[i].stage) != category) continue;
        if (cc[i].moved && cc[i].num_moved > 0) {
            for (int k = 0; k < cc[i].num_moved; ++k) {
                const double* m = cc[i].moved + 6 * k;
                unsigned base = (unsigned)segNodes.size();
                segNodes.push_back(XMFLOAT3((float)m[0], (float)m[1], (float)m[2]));
                segNodes.push_back(XMFLOAT3((float)m[3], (float)m[4], (float)m[5]));
                segEdges.push_back({ base, base + 1 });
            }
        }
        if (cc[i].removed.num_nodes > 0 &&
            (cc[i].removed.num_tris > 0 || cc[i].removed.num_edges > 0))
            removedMeshes.push_back(ViewToData(cc[i].removed));
    }
    if (segEdges.empty() && removedMeshes.empty()) return;

    SceneNode* grp = scene.AddGroupNode(
        std::string(kClipChangePrefix) + std::to_string(category) + "_" + Stem(m_srcPath),
        AttachParent());
    m_clipChangeGroup[category] = grp;

    if (!segEdges.empty()) {
        CSV3DLoader::CSV3DData seg;
        seg.nodes.reserve(segNodes.size());
        for (const XMFLOAT3& p : segNodes) { CSV3DLoader::Node nd{}; nd.pos = p; seg.nodes.push_back(nd); }
        seg.edges = std::move(segEdges);
        Primitive* lines = scene.AddFromCSV3DData(seg, std::string(kClipChangePrefix) + "move",
                                                  grp, &kClipChgMoveColor);
        if (lines) { lines->SetIlluminationCapability(false); lines->SetUseVertexColor(false);
                     lines->SetColor(kClipChgMoveColor); StyleLines(lines, LINESTYLE_MEDIUM); }
        // Mark where the snapped vertices landed with a point cloud (every odd node).
        std::vector<XMFLOAT3> pts;
        pts.reserve(segNodes.size() / 2);
        for (size_t i = 1; i < segNodes.size(); i += 2) pts.push_back(segNodes[i]);
        Primitive* cloud = scene.AddPointCloud(pts, kClipChgMoveColor,
                                               std::string(kClipChangePrefix) + "movepts", grp);
        if (cloud) cloud->SetIlluminationCapability(false);
    }

    int ri = 0;
    for (const CSV3DLoader::CSV3DData& rm : removedMeshes) {
        Primitive* surf = scene.AddFromCSV3DData(rm, std::string(kClipChangePrefix) + "rem" +
                                                 std::to_string(ri++), grp, &kClipChgRemoveColor);
        if (surf) { surf->SetUseVertexColor(false); surf->SetColor(kClipChgRemoveColor);
                    surf->SetTwoSided(true, kClipChgRemoveColor); surf->SetAlpha(kClipChgRemoveColor.w); }
    }
}

void SemSession::DropClipChanges(Scene& scene, int category) {
    if (category < 0 || category >= CLIPCHG_COUNT) return;
    if (m_clipChangeGroup[category] && Alive(scene, m_clipChangeGroup[category]))
        scene.RemoveNode(m_clipChangeGroup[category]);
    m_clipChangeGroup[category] = nullptr;
}

void SemSession::ShowClipChanges(Scene& scene, int category, bool show) {
    if (category < 0 || category >= CLIPCHG_COUNT) return;
    m_clipChangesShown[category] = show;
    DropClipChanges(scene, category);
    if (show) BuildClipChanges(scene, category);
    scene.UpdateLight();
}

void SemSession::RefreshClipChanges(Scene& scene) {
    for (int c = 0; c < CLIPCHG_COUNT; ++c) {
        if (!m_clipChangesShown[c]) continue;
        DropClipChanges(scene, c);
        BuildClipChanges(scene, c);
    }
    scene.UpdateLight();
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

    Primitive* rect = scene.AddColoredTriangles(poses, cols, node->name + "_rect", node);
    if (!rect) return;
    rect->SetIlluminationCapability(false);
    rect->SetUseVertexColor(false);
    rect->SetColor(softRed);
    rect->SetTwoSided(true, softBlue);
    node->rect = rect;
    node->SetPlane(plane);
}

}
