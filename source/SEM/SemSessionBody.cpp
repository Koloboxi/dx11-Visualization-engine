#include "SemSessionDetail.h"
#include <functional>

namespace SemSessionNS {

using namespace detail;

namespace {

// The two halves get distinct tints so the split is readable at a glance; the
// closed source keeps the neutral white it had.
const XMFLOAT4 kHalfPosFront(0.42f, 0.66f, 0.90f, 1.0f);   // cool: positive side
const XMFLOAT4 kHalfPosBack (0.22f, 0.36f, 0.56f, 1.0f);
const XMFLOAT4 kHalfNegFront(0.93f, 0.72f, 0.38f, 1.0f);   // warm: negative side
const XMFLOAT4 kHalfNegBack (0.60f, 0.42f, 0.16f, 1.0f);

const char* kSplitOnPlanePrefix = "split_on_plane_";

const char* HalfTag(int side) { return (side == 0) ? "half_pos" : "half_neg"; }

}

SemSession::~SemSession() {
    // Releasing a half destroys its SEM context; the scene primitives are the
    // scene's to free, so only our own sessions are deleted here.
    for (int i = 0; i < 2; ++i) {
        if (!m_half[i]) continue;
        m_half[i]->Release();
        delete m_half[i];
        m_half[i] = nullptr;
    }
}

Primitive*  SemSession::HalfPrim(int side)    const { return m_halfPrim[(side >= 0) ? 0 : 1]; }
SemSession* SemSession::HalfSession(int side) const { return m_half[(side >= 0) ? 0 : 1]; }

double SemSession::BodyVolume() const {
    if (!IsBody()) return -1.0;
    Activate();
    return SEM_GetBodyVolume3D();
}

void SemSession::BindExisting(Scene& scene, int ctx, Primitive* prim,
                              const std::string& srcPath, const std::string& workDir) {
    (void)scene;
    m_ctx       = ctx;
    m_srcPrim   = prim;
    m_srcPath   = srcPath;
    m_workDir   = workDir;
    dim         = 3;
    sessionMode = SESSION_PIPELINE;
    m_isHalf    = true;
    m_offsets = nullptr; m_mesh = nullptr; m_isoline = nullptr;
    m_isoSource = nullptr; m_isoProj = nullptr; m_isoProjected = false;
    m_srcNormals = nullptr; m_isoNormals = nullptr; m_isoSrcNormals = nullptr;
    m_isoLoops = nullptr;
    m_meshPath.clear(); m_isolinePath.clear();
    m_thermalSolved = false;
    m_offStats = m_meshStats = Stats();
    m_offsetsMs = m_meshMs = m_thermalMs = m_isoMs = -1.0;
    for (int i = 0; i <= STAGE_ISOSURFACE; ++i) m_dirty[i] = true;
}

void SemSession::RebindHalf(Scene& scene, Primitive* prim) {
    DropIsoline(scene);
    DropMesh(scene);
    DropOffsets(scene);
    DropSrcNormals(scene);
    m_srcPrim = prim;
    m_thermalSolved = false;
    m_offStats = m_meshStats = Stats();
    m_offsetsMs = m_meshMs = m_thermalMs = m_isoMs = -1.0;
    for (int i = 0; i <= STAGE_ISOSURFACE; ++i) m_dirty[i] = true;
}

ClipPlaneNode* SemSession::AddSplitPlane(Scene& scene) {
    if (!UsesPartingPlane() || !HasSource()) {
        Report(scene, false, "This mode has no parting plane.");
        return nullptr;
    }
    if (m_splitNode && Alive(scene, m_splitNode)) return m_splitNode;

    if (!m_splitGroup || !Alive(scene, m_splitGroup))
        m_splitGroup = scene.AddGroupNode("split_plane", AttachParent());

    ClipPlaneNode* node = new ClipPlaneNode();
    node->name = "split_plane";
    m_splitGroup->AddChild(node);
    // Default: horizontal through the source's mid-height, which is the usual
    // starting guess for a parting plane.
    XMFLOAT3 lo, hi;
    float d0 = 0.0f;
    if (SourceBBox(m_srcPath, lo, hi)) d0 = -0.5f * (lo.z + hi.z);
    BuildClipPlaneRect(scene, node, XMFLOAT4(0.0f, 0.0f, 1.0f, d0));
    m_splitNode = node;
    snprintf(status, sizeof(status), "Added a parting plane.");
    return node;
}

void SemSession::RemoveSplitPlane(Scene& scene) {
    if (m_splitNode && Alive(scene, m_splitNode)) scene.RemoveNode(m_splitNode);
    m_splitNode = nullptr;
    if (m_splitGroup && Alive(scene, m_splitGroup)) scene.RemoveNode(m_splitGroup);
    m_splitGroup = nullptr;
}

void SemSession::DropHalves(Scene& scene) {
    DropSplitOnPlane(scene);
    for (int i = 0; i < 2; ++i) {
        if (m_half[i]) {
            m_half[i]->Release();       // destroys that half's SEM context
            delete m_half[i];
            m_half[i] = nullptr;
        }
        if (m_halfPrim[i] && Alive(scene, m_halfPrim[i])) scene.RemovePrimitive(m_halfPrim[i]);
        m_halfPrim[i] = nullptr;
        m_halfData[i] = CSV3DLoader::CSV3DData();
    }
    m_bodySplit = false;
    m_bodyPhase = 0;
}

void SemSession::BuildHalfPrim(Scene& scene, int side) {
    const XMFLOAT4& front = (side == 0) ? kHalfPosFront : kHalfNegFront;
    const XMFLOAT4& back  = (side == 0) ? kHalfPosBack  : kHalfNegBack;
    Primitive* p = scene.AddFromCSV3DData(m_halfData[side],
                                          std::string(HalfTag(side)) + "_" + Stem(m_srcPath),
                                          AttachParent(), nullptr,
                                          Colors::BLUE, Colors::RED);
    if (!p) return;
    ConfigureSurface3D(p);
    p->SetColor(front);
    p->SetUseVertexColor(false);
    p->SetTwoSided(true, back);
    m_halfPrim[side] = p;
}

void SemSession::PushSplitClipPlane(int side) {
    SemSession* hs = m_half[side];
    if (!hs || !m_splitNode) return;
    const XMFLOAT4 pl = m_splitNode->GetPlane();        // unit normal, kept side = +n
    const float    s  = (side == 0) ? 1.0f : -1.0f;     // half 1 keeps the other side
    const SEM_Plane3D cp{ { s * pl.x, s * pl.y, s * pl.z }, s * pl.w };
    hs->Activate();
    const int rc = SEM_SetClipPlanes3D(&cp, 1, (double)splitPlaneTol);
    Activate();
    if (rc != 0)
        snprintf(status, sizeof(status), "Clip plane on the %s half failed (%d).",
                 (side == 0) ? "+" : "-", rc);
}

bool SemSession::ApplySplit(Scene& scene, bool silent) {
    if (!IsBody())    { Report(scene, silent, "Split needs a closed (body) source."); return false; }
    if (!HasSource()) return false;
    if (AsyncRunning()) return false;
    if (!m_splitNode || !Alive(scene, m_splitNode)) {
        Report(scene, silent, "Add a parting plane first.");
        return false;
    }

    // Let go of a previous split first: those sessions own the old half contexts,
    // and the core would otherwise be asked to publish into contexts we are about
    // to destroy.
    DropHalves(scene);

    Activate();
    const XMFLOAT4 pl = m_splitNode->GetPlane();     // already unit-normalized
    const SEM_Plane3D sp{ { pl.x, pl.y, pl.z }, pl.w };
    if (!CheckRc(scene, silent, "SEM_SetBodySplitPlane3D",
                 SEM_SetBodySplitPlane3D(sp, (double)splitPlaneTol),
                 { "", "Not a body context", "Zero normal", "Bad tolerance" }))
        return false;
    if (!CheckRc(scene, silent, "SEM_SplitBody3D", SEM_SplitBody3D(),
                 { "", "Not a body context", "No parting plane", "No source",
                   "The plane misses the body", "Could not publish a half" }))
        return false;

    int ctx[2] = { 0, 0 };
    if (SEM_GetBodyHalfContexts3D(&ctx[0], &ctx[1]) != 0 || ctx[0] <= 0 || ctx[1] <= 0) {
        Report(scene, silent, "Split: the core returned no half contexts" + SemDetail());
        return false;
    }

    // The closed body stays in the tree as the parent of both halves, just hidden.
    ShowSource(scene, false);

    for (int s = 0; s < 2; ++s) {
        if (SEM_SetActiveContext(ctx[s]) != 0) continue;
        const char* wd  = SEM_GetWorkingDir();
        const char* src = SEM_GetSourcePath3D();
        const std::string workDir = wd  ? wd  : "";
        const std::string srcPath = src ? src : "";
        SEM_MeshView v{};
        const bool got = (SEM_GetSourceSurface3D(&v) == 0 && v.num_nodes > 0);
        if (got) m_halfData[s] = ViewToData(v);
        Activate();                                  // back to the body context
        if (!got) {
            Report(scene, silent, std::string("Split: half ") + HalfTag(s) + " is empty");
            continue;
        }
        BuildHalfPrim(scene, s);
        m_half[s] = new SemSession();
        m_half[s]->BindExisting(scene, ctx[s], m_halfPrim[s], srcPath, workDir);
        PushSplitClipPlane(s);
    }

    if (!m_half[0] || !m_half[1]) { DropHalves(scene); return false; }

    m_bodySplit = true;
    for (int i = 0; i <= STAGE_ISOSURFACE; ++i) m_dirty[i] = true;
    if (m_splitOnPlaneShown) BuildSplitOnPlane(scene);
    scene.UpdateLight();
    snprintf(status, sizeof(status), "Split: %d + %d triangles.",
             (int)m_halfData[0].triangles.size(), (int)m_halfData[1].triangles.size());
    return true;
}

bool SemSession::ApplyTaper(Scene& scene, bool silent) {
    if (!IsBody() || !m_bodySplit) {
        Report(scene, silent, "Taper needs a split body.");
        return false;
    }
    if (AsyncRunning()) return false;

    Activate();
    const int rc = SEM_ApplyBodyTaper3D((double)taperAngle, taperRemeshIters);
    if (rc == SEM_RC_CANCELLED) {
        snprintf(status, sizeof(status), "Taper cancelled.");
        return false;
    }
    if (!CheckRc(scene, silent, "SEM_ApplyBodyTaper3D", rc,
                 { "", "Not a body context", "Not split yet",
                   "Positive half failed", "Negative half failed",
                   "Could not publish a half" }))
        return false;

    // The core re-tapered from the pristine cut and reloaded both half contexts,
    // so every stage those halves had built is gone. Replace their primitives with
    // the new geometry and reset their stage state.
    DropSplitOnPlane(scene);
    for (int s = 0; s < 2; ++s) {
        SEM_MeshView v{};
        if (SEM_GetBodyHalf3D((s == 0) ? +1 : -1, &v) != 0 || v.num_nodes <= 0) continue;
        m_halfData[s] = ViewToData(v);

        const bool wasVisible = m_halfPrim[s] ? m_halfPrim[s]->visible : true;
        if (m_half[s]) m_half[s]->RebindHalf(scene, nullptr);
        if (m_halfPrim[s] && Alive(scene, m_halfPrim[s])) scene.RemovePrimitive(m_halfPrim[s]);
        m_halfPrim[s] = nullptr;
        BuildHalfPrim(scene, s);
        if (m_halfPrim[s]) m_halfPrim[s]->visible = wasVisible;
        if (m_half[s]) m_half[s]->RebindHalf(scene, m_halfPrim[s]);
        // Republishing reloaded the half's source, which cleared its clip planes.
        PushSplitClipPlane(s);
    }
    for (int i = 0; i <= STAGE_ISOSURFACE; ++i) m_dirty[i] = true;
    if (m_splitOnPlaneShown) BuildSplitOnPlane(scene);
    scene.UpdateLight();

    int moved = 0, bad = 0;
    double worst = 0.0, growth = 0.0;
    TaperReport(+1, moved, bad, worst, growth);
    snprintf(status, sizeof(status),
             "Taper %.2f deg: moved %d, %d facets short, min draft %.2f deg, outline +%.4f.",
             taperAngle, moved, bad, worst, growth);
    return true;
}

bool SemSession::TaperReport(int side, int& verts_moved, int& facets_violating,
                             double& min_draft_deg, double& contour_growth) const {
    verts_moved = facets_violating = 0;
    min_draft_deg = contour_growth = 0.0;
    if (!IsBody() || !m_bodySplit) return false;
    Activate();
    return SEM_GetBodyTaperReport3D(side, &verts_moved, &facets_violating,
                                    &min_draft_deg, &contour_growth) == 0;
}

bool SemSession::StandaloneTaperReport(int& verts_moved, int& facets_violating,
                                      double& min_draft_deg, double& contour_growth) const {
    verts_moved = facets_violating = 0;
    min_draft_deg = contour_growth = 0.0;
    Activate();
    return SEM_GetLastTaperReport3D(&verts_moved, &facets_violating,
                                    &min_draft_deg, &contour_growth) == 0;
}

bool SemSession::MeasureDraftNow(int side, double angle_deg,
                                 int& facets_violating, double& min_draft_deg) const {
    facets_violating = 0;
    min_draft_deg    = 0.0;
    if (!IsBody() || !m_bodySplit) return false;
    Activate();
    return SEM_MeasureBodyDraft3D(side, angle_deg, &facets_violating, &min_draft_deg) == 0;
}

void SemSession::DropSplitOnPlane(Scene& scene) {
    if (m_splitOnPlaneGroup && Alive(scene, m_splitOnPlaneGroup))
        scene.RemoveNode(m_splitOnPlaneGroup);
    m_splitOnPlaneGroup = nullptr;
}

void SemSession::BuildSplitOnPlane(Scene& scene) {
    DropSplitOnPlane(scene);
    if (!IsBody() || !m_bodySplit || !m_splitNode || !Alive(scene, m_splitNode)) return;

    Activate();
    double avg = SEM_GetSurfaceAvgEdgeLen3D();
    if (avg <= 0.0) avg = 1.0;
    const float tol  = (float)(1e-4 * avg);
    const float lift = (float)(2e-3 * avg);
    const std::vector<XMFLOAT4> planes{ m_splitNode->GetPlane() };

    SceneNode* grp = scene.AddGroupNode(kSplitOnPlanePrefix + Stem(m_srcPath), AttachParent());
    m_splitOnPlaneGroup = grp;

    bool any = false;
    for (int s = 0; s < 2; ++s) {
        if (m_halfData[s].nodes.empty()) continue;
        any |= BuildOnPlaneOverlay(scene, grp, m_halfData[s], planes, tol, lift,
                                   (s == 0) ? kHalfPosFront : kHalfNegFront,
                                   HalfTag(s));
    }
    if (!any) DropSplitOnPlane(scene);
    scene.UpdateLight();
}

void SemSession::ShowSplitOnPlane(Scene& scene, bool show) {
    m_splitOnPlaneShown = show;
    if (show) BuildSplitOnPlane(scene);
    else      DropSplitOnPlane(scene);
}

void SemSession::PushStageParams() {
    for (SemSession* h : m_half) {
        if (!h) continue;
        h->subMode = subMode; h->subN = subN; h->subEnabled = subEnabled;
        h->offsetMode = offsetMode; h->firstGap = firstGap;
        h->numOffsets = numOffsets; h->grading = grading; h->gaps = gaps;
        h->offEnabled = offEnabled;
        h->tetMethod = tetMethod; h->tetParam = tetParam;
        h->tetMaxEdgeLen = tetMaxEdgeLen; h->meshEnabled = meshEnabled;
        h->thermalEnabled = thermalEnabled; h->maxInward = maxInward; h->bcView = bcView;
        h->isoEnabled = isoEnabled; h->isoValue = isoValue; h->isoAxisN = isoAxisN;
        h->isoOffsetValue = isoOffsetValue; h->isoMinOffsetValue = isoMinOffsetValue;
        h->isoFinalTargetMult = isoFinalTargetMult; h->isoFinalIters = isoFinalIters;
        h->isoShowWinding = isoShowWinding; h->isoShowNonManifold = isoShowNonManifold;
        h->surf3dAlpha = surf3dAlpha;
        // The stage knobs are edited on this session, so this is also where the
        // staleness the UI recorded here reaches the halves.
        for (int i = 0; i <= STAGE_ISOSURFACE; ++i)
            if (m_dirty[i]) h->m_dirty[i] = true;
    }
}

void SemSession::StartBodyHalf(Scene& scene, int side) {
    SemSession* h = m_half[side];
    if (!h) { m_bodyPhase = 0; return; }
    h->Activate();
    h->RecomputeUpToAsync(scene, m_bodyStage, m_bodySilent);
    Activate();
}

void SemSession::RecomputeBodyUpToAsync(Scene& scene, Stage to, bool silent) {
    if (!IsBody()) return;
    if (!m_bodySplit) { Report(scene, silent, "Split the body first."); return; }
    if (AsyncRunning()) return;

    PushStageParams();
    m_bodyStage  = to;
    m_bodySilent = silent;
    m_bodyPhase  = 1;
    // Mirror the single-session bookkeeping: the stages up to `to` are no longer
    // stale here, and everything after it is.
    for (int i = 0; i <= (int)to; ++i) m_dirty[i] = false;
    for (int i = (int)to + 1; i <= STAGE_ISOSURFACE; ++i) m_dirty[i] = true;

    snprintf(status, sizeof(status), "Both halves: computing...");
    StartBodyHalf(scene, 0);
}

void SemSession::PollBodyStage(Scene& scene) {
    if (!IsBody()) return;

    for (int i = 0; i < 2; ++i) {
        if (!m_half[i]) continue;
        m_half[i]->Activate();
        m_half[i]->PollAsync(scene);
        // A subdivide makes the half replace its own source primitive, so follow
        // the new pointer instead of keeping the destroyed one.
        m_halfPrim[i] = m_half[i]->SourcePrim();
    }
    Activate();

    if (m_bodyPhase == 1) {
        if (m_half[0] && m_half[0]->AsyncRunning()) return;
        m_bodyPhase = 2;
        StartBodyHalf(scene, 1);
        if (!m_half[1] || !m_half[1]->AsyncRunning()) {
            m_bodyPhase = 0;
            snprintf(status, sizeof(status), "Both halves: done.");
        }
    } else if (m_bodyPhase == 2) {
        if (m_half[1] && m_half[1]->AsyncRunning()) return;
        m_bodyPhase = 0;
        snprintf(status, sizeof(status), "Both halves: done.");
    }
}

const SemSession* SemSession::BodyRunningHalf() const {
    for (const SemSession* h : m_half)
        if (h && h->AsyncRunning()) return h;
    return nullptr;
}

}
