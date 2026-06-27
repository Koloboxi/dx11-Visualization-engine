#include "SemSessionDetail.h"
#include <cmath>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <algorithm>

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
    int rc = SEM_SetClipPlanes3D(flat.empty() ? nullptr : flat.data(), count);
    if (!CheckRc(scene, false, "SEM_SetClipPlanes3D", rc,
                 { "", "No surface loaded", "Invalid planes" }))
        return;

    // Clipping is applied during SEM_BuildMesh3D, so the cached mesh (and the
    // thermal field/isosurface downstream) is now stale.
    DropMesh(scene);
    m_dirty[STAGE_MESH] = m_dirty[STAGE_THERMAL] = m_dirty[STAGE_ISOSURFACE] = true;
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
    m_dirty[STAGE_MESH] = m_dirty[STAGE_THERMAL] = m_dirty[STAGE_ISOSURFACE] = true;
    scene.UpdateLight();
    m_appliedClipPlanes.clear();
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
        AttachParent() ? AttachParent()->AddChild(node) : scene.root.AddChild(node);
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

}
