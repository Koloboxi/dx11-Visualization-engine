#include "SemSessionDetail.h"
#include "../utils/errorLogger.h"
#include <functional>

namespace SemSessionNS {

using namespace detail;

const std::string& SemSession::SourcePath() const { return m_srcPath; }
Primitive*  SemSession::SourcePrim()  const { return m_srcPrim; }
SceneNode*  SemSession::OffsetsNode() const { return m_offsets; }
Primitive*  SemSession::MeshPrim()    const { return m_mesh; }
Primitive*  SemSession::IsolinePrim() const { return m_isoline; }
Primitive*  SemSession::IsoSourcePrim()     const { return m_isoSource; }
Primitive*  SemSession::IsoProjectionPrim() const { return m_isoProj; }
Primitive*  SemSession::SrcNormalsPrim() const { return m_srcNormals; }
Primitive*  SemSession::IsoNormalsPrim() const { return m_isoNormals; }
Primitive*  SemSession::IsoSrcNormalsPrim() const { return m_isoSrcNormals; }
Primitive*  SemSession::IsoLoopsPrim() const { return m_isoLoops; }
Primitive*  SemSession::SrcRevSurf()  const { return m_srcRevSurf; }
Primitive*  SemSession::IsoRevSurf()  const { return m_isoRevSurf; }
void SemSession::StyleLines(Primitive* p, int style) {
    if (!p) return;
    std::function<void(SceneNode*)> rec = [&](SceneNode* n) {
        if (n->IsPrimitive()) {
            Primitive* q = static_cast<Primitive*>(n);
            if (q->GetDimension() == 1) q->lineStyle = style;
        }
        for (SceneNode* ch : n->children) rec(ch);
    };
    rec(p);
}

bool        SemSession::HasSource()   const { return m_srcPrim != nullptr && !m_srcPath.empty(); }
int         SemSession::Dim()         const { return dim; }
bool        SemSession::ThermalSolved() const { return m_thermalSolved; }
bool        SemSession::HasIsolinePath() const { return !m_isolinePath.empty(); }
bool        SemSession::HasIsoProjection() const { return m_isoProjected; }

double SemSession::TotalTimeMs() const {
    double t = 0.0;
    if (m_offsetsMs >= 0.0) t += m_offsetsMs;
    if (m_meshMs    >= 0.0) t += m_meshMs;
    if (m_thermalMs >= 0.0) t += m_thermalMs;
    if (m_isoMs     >= 0.0) t += m_isoMs;
    return t;
}

const Stats& SemSession::SrcStats()  const { return m_srcStats; }
const Stats& SemSession::OffStats()  const { return m_offStats; }
const Stats& SemSession::MeshStats() const { return m_meshStats; }

void SemSession::Bind(Scene& scene, Primitive* prim, bool reload) {
    if (prim == m_srcPrim) return;
    // The previous source's pseudonormal overlay belonged to its subtree; drop it
    // before rebinding so it does not linger orphaned under the old source.
    DropSrcNormals(scene);
    m_srcPrim   = prim;
    m_srcPath   = prim ? prim->semSourcePath : std::string();
    m_offsets   = nullptr;
    m_mesh      = nullptr;
    m_isoline   = nullptr;
    m_isoNormals = nullptr;
    m_isoSrcNormals = nullptr;
    m_isoLoops = nullptr;
    m_isoData   = CSV3DLoader::CSV3DData();
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
    // The on-plane overlay belonged to the previous source's subtree; abandon the
    // reference (the staging change tears the old source down) and reset its state.
    m_onPlaneGroup = nullptr;
    m_clipOnPlaneShown = false;
    for (int c = 0; c < CLIPCHG_COUNT; ++c) { m_clipChangeGroup[c] = nullptr; m_clipChangesShown[c] = false; }
    m_srcRebuildPending = false;
    m_offsetsMs = m_meshMs = m_thermalMs = m_isoMs = -1.0;
    // A fresh source clears the SEM core cache, so every stage must be recomputed.
    m_dirty[0] = m_dirty[1] = m_dirty[2] = m_dirty[3] = m_dirty[4] = true;
    if (!m_srcPath.empty()) {
        // Point the SEM core at this source's session folder so OutPath can
        // reconstruct the deterministic paths it writes. The folder is chosen at
        // import (prim->semWorkDir); allocate a fresh session when none was set.
        m_workDir = prim ? prim->semWorkDir : std::string();
        if (m_workDir.empty()) {
            m_workDir = NewSessionDir(m_srcPath);
            if (prim) prim->semWorkDir = m_workDir;
        }
        std::error_code ec;
        std::filesystem::create_directories(m_workDir, ec);
        SEM_SetWorkingDir(m_workDir.c_str());
        dim = DetectSemDim(m_srcPath);
        // Reopening a saved 3D session: SEM_LoadSession3D (called next via
        // LoadSessionStages) reloads the source into the core itself and reads
        // session3d.txt. Calling SEM_LoadSurface3D here would rewrite that manifest
        // as a bare source and wipe the saved stages before the reload can read it,
        // so skip it and let the session reload drive the core. 2D has no session
        // reload, so it still loads the core source here.
        if (!(reload && dim == 3)) {
            int rc = (dim == 3) ? SEM_LoadSurface3D(m_srcPath.c_str())
                                : SEM_LoadCSV3D(m_srcPath.c_str());
            if (rc != 0)
                Report(scene, false, (dim == 3 ? "SEM_LoadSurface3D failed ("
                                               : "SEM_LoadCSV3D failed (")
                                    + std::to_string(rc) + ")" + SemDetail());
        }
    }
    m_srcStats = ComputeStats(m_srcPath);
    if (HasSource()) snprintf(status, sizeof(status), "Staged: %s", BaseName(m_srcPath).c_str());
    else             snprintf(status, sizeof(status), "Ready");
}

void SemSession::Unbind() {
    m_srcPrim = nullptr; m_srcPath.clear();
    m_offsets = nullptr; m_mesh = nullptr; m_isoline = nullptr;
    m_isoSource = nullptr; m_isoProj = nullptr; m_isoProjected = false;
    m_srcNormals = nullptr; m_isoNormals = nullptr; m_isoSrcNormals = nullptr;
    m_isoLoops = nullptr;
    m_isoData = CSV3DLoader::CSV3DData();
    m_srcRevSurf = nullptr; m_isoRevSurf = nullptr;
    m_meshPath.clear(); m_isolinePath.clear();
    m_thermalSolved = false;
    // The plane nodes (and their grouping node) were children of the (now-gone)
    // source subtree; just drop our dangling references — the scene already
    // destroyed the nodes.
    clipPlaneNodes.clear();
    m_clipGroup = nullptr;
    m_onPlaneGroup = nullptr;
    m_clipOnPlaneShown = false;
    for (int c = 0; c < CLIPCHG_COUNT; ++c) { m_clipChangeGroup[c] = nullptr; m_clipChangesShown[c] = false; }
    m_srcRebuildPending = false;
    m_srcStats = m_offStats = m_meshStats = Stats();
    snprintf(status, sizeof(status), "Ready");
}

void SemSession::Validate(Scene& scene) {
    if (m_srcPrim && !Alive(scene, m_srcPrim)) { Unbind(); return; }
    if (m_offsets && !Alive(scene, m_offsets)) { m_offsets = nullptr; m_offStats = Stats(); }
    if (m_mesh    && !Alive(scene, m_mesh))    { m_mesh    = nullptr; m_meshStats = Stats(); m_thermalSolved = false; }
    if (m_isoline && !Alive(scene, m_isoline)) { m_isoline = nullptr; }
    if (m_isoSource && !Alive(scene, m_isoSource)) m_isoSource = nullptr;
    if (m_isoProj    && !Alive(scene, m_isoProj))    m_isoProj    = nullptr;
    if (m_srcNormals && !Alive(scene, m_srcNormals)) m_srcNormals = nullptr;
    if (m_isoNormals && !Alive(scene, m_isoNormals)) m_isoNormals = nullptr;
    if (m_isoSrcNormals && !Alive(scene, m_isoSrcNormals)) m_isoSrcNormals = nullptr;
    if (m_isoLoops && !Alive(scene, m_isoLoops)) m_isoLoops = nullptr;
    if (m_srcRevSurf && !Alive(scene, m_srcRevSurf)) m_srcRevSurf = nullptr;
    if (m_isoRevSurf && !Alive(scene, m_isoRevSurf)) m_isoRevSurf = nullptr;
    // Drop plane nodes whose subtree was removed externally (e.g. tree delete).
    for (auto it = clipPlaneNodes.begin(); it != clipPlaneNodes.end(); ) {
        if (*it && !Alive(scene, *it)) it = clipPlaneNodes.erase(it);
        else ++it;
    }
    if (m_clipGroup && !Alive(scene, m_clipGroup)) m_clipGroup = nullptr;
    if (m_onPlaneGroup && !Alive(scene, m_onPlaneGroup)) { m_onPlaneGroup = nullptr; m_clipOnPlaneShown = false; }
    for (int c = 0; c < CLIPCHG_COUNT; ++c)
        if (m_clipChangeGroup[c] && !Alive(scene, m_clipChangeGroup[c])) {
            m_clipChangeGroup[c] = nullptr; m_clipChangesShown[c] = false;
        }
}

void SemSession::MarkStageDirty(Stage st) {
    if (st >= STAGE_SUBDIVIDE && st <= STAGE_ISOSURFACE) m_dirty[st] = true;
}

void SemSession::SetSurf3dAlpha(Scene& scene, float a) {
    surf3dAlpha = a;
    for (Primitive* p : { m_srcPrim, m_mesh, m_isoline, m_isoSource, m_isoProj })
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

void SemSession::ShowSource(Scene& scene, bool show) {
    if (!Alive(scene, m_srcPrim)) return;
    // Toggle only the source surface itself, not the pipeline products parented
    // under it. Its own wireframe edges ("(edges)") are part of the source, so
    // flip them with it; every other direct child keeps its own visibility.
    m_srcPrim->visible = show;
    for (SceneNode* ch : m_srcPrim->children)
        if (ch && ch->IsPrimitive() && ch->name.find("(edges)") != std::string::npos)
            ch->visible = show;
}

SceneNode* SemSession::AttachParent() { return m_srcPrim ? static_cast<SceneNode*>(m_srcPrim) : nullptr; }

void SemSession::ConfigureSurface3D(Primitive* p) {
    if (!p) return;
    p->SetAlpha(surf3dAlpha);
}

void SemSession::RebuildSourcePrim(Scene& scene) {
    m_srcRebuildPending = false;
    if (dim != 3 || AsyncRunning() || !Alive(scene, m_srcPrim)) return;

    SEM_MeshView v{};
    if (SEM_GetSourceSurface3D(&v) != 0 || v.num_nodes <= 0) return;
    CSV3DLoader::CSV3DData data = ViewToData(v);
    if (data.nodes.empty() || data.triangles.empty()) return;

    Primitive* old   = m_srcPrim;
    SceneNode* parent = old->parent;
    const bool wasVisible = old->visible;

    // Detach the children that must survive the swap (clip planes, offsets, mesh,
    // overlays, ...) — everything except the source's own "(edges)" wireframe,
    // which the rebuild regenerates — so RemovePrimitive does not destroy them.
    std::vector<SceneNode*> keep;
    for (SceneNode* ch : old->children)
        if (!(ch && ch->IsPrimitive() && ch->name.find("(edges)") != std::string::npos))
            keep.push_back(ch);
    for (SceneNode* ch : keep) old->RemoveChild(ch);

    Primitive* fresh = scene.AddFromCSV3DData(data, old->name, parent, nullptr, Colors::BLUE, Colors::RED);
    if (!fresh) {                                  // rebuild failed: undo the detach
        for (SceneNode* ch : keep) old->AddChild(ch);
        return;
    }
    for (SceneNode* ch : keep) fresh->AddChild(ch);

    // Match the source surface's render configuration (see ImportSource).
    ConfigureSurface3D(fresh);
    fresh->SetColor(Colors::FRONT_FACE_WHITE);
    fresh->SetUseVertexColor(false);
    fresh->SetTwoSided(true, Colors::BACK_FACE_RED);
    fresh->visible = wasVisible;
    for (SceneNode* ch : fresh->children)
        if (ch && ch->IsPrimitive() && ch->name.find("(edges)") != std::string::npos)
            ch->visible = wasVisible;
    fresh->semSourcePath = m_srcPath;
    fresh->semWorkDir    = m_workDir;

    // RemovePrimitive destroys the old source (and its leftover wireframe), clears
    // the staging pointer if it was staged, and resets the gizmo target.
    scene.RemovePrimitive(old);
    m_srcPrim = fresh;
    scene.SetStaged(fresh);

    // Restore the gizmo selection: kept primitives keep their `selected` flag, so
    // re-target whatever is still flagged (the destroyed source/edges drop out).
    std::vector<Primitive*> sel;
    for (Primitive* p : scene.primitives) if (p->selected) sel.push_back(p);
    scene.orientationTransformer.SetTargetObjects(sel);

    m_srcStats = ComputeStatsData(data);
    if (AnyClipMirror())   RebuildClipMirrors(scene);
    if (m_clipOnPlaneShown) RefreshClipOnPlane(scene);
    RefreshClipChanges(scene);
    scene.UpdateLight();
}

}
