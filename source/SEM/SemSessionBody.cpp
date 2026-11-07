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
// A skirt is drawn in a lighter shade of its half's colour.
const XMFLOAT4 kHalfPosSkirt(0.74f, 0.85f, 0.96f, 1.0f);
const XMFLOAT4 kHalfNegSkirt(0.98f, 0.88f, 0.70f, 1.0f);
// The glued preform is neither half, so it gets a third tint of its own.
const XMFLOAT4 kPreformFront(0.56f, 0.80f, 0.50f, 1.0f);
const XMFLOAT4 kPreformBack (0.30f, 0.48f, 0.26f, 1.0f);

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
    m_offsets = nullptr; m_mesh = nullptr; m_isoline = nullptr; m_gridBox = nullptr;
    m_meshPos.clear(); m_meshT.clear(); m_meshEdges.clear();
    m_isoProj = nullptr; m_isoProjected = false;
    m_srcNormals = nullptr;
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
    DropPreform(scene);
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

Primitive* SemSession::AddHalfSurface(Scene& scene, CSV3DLoader::CSV3DData data,
                                      const std::string& name, SceneNode* parent,
                                      int side, const XMFLOAT4& plane, bool skirt) {
    const XMFLOAT4& front = (side == 0) ? kHalfPosFront : kHalfNegFront;
    const XMFLOAT4& back  = (side == 0) ? kHalfPosBack  : kHalfNegBack;
    const XMFLOAT4& tint  = (side == 0) ? kHalfPosSkirt : kHalfNegSkirt;
    bool shaded = false;
    if (skirt && !data.nodes.empty()) {
        // T = 1 on the far side of the parting plane (the skirt), 0 on the half.
        XMFLOAT3 lo = data.nodes[0].pos, hi = lo;
        for (const auto& n : data.nodes) {
            lo.x = (std::min)(lo.x, n.pos.x); hi.x = (std::max)(hi.x, n.pos.x);
            lo.y = (std::min)(lo.y, n.pos.y); hi.y = (std::max)(hi.y, n.pos.y);
            lo.z = (std::min)(lo.z, n.pos.z); hi.z = (std::max)(hi.z, n.pos.z);
        }
        const float dx = hi.x - lo.x, dy = hi.y - lo.y, dz = hi.z - lo.z;
        const float tol = 1e-5f * std::sqrt(dx * dx + dy * dy + dz * dz);
        const float s   = (side == 0) ? 1.0f : -1.0f;
        for (auto& n : data.nodes) {
            const float h = s * (plane.x * n.pos.x + plane.y * n.pos.y + plane.z * n.pos.z + plane.w);
            n.T = (h < -tol) ? 1.0f : 0.0f;
            shaded = shaded || (n.T > 0.5f);
        }
    }
    Primitive* p = shaded
        ? scene.AddFromCSV3DData(data, name, parent, nullptr, front, tint)
        : scene.AddFromCSV3DData(data, name, parent, nullptr, Colors::BLUE, Colors::RED);
    if (!p) return nullptr;
    ConfigureSurface3D(p);
    p->SetColor(front);
    p->SetUseVertexColor(shaded);
    p->SetTwoSided(true, back);
    return p;
}

void SemSession::BuildHalfPrim(Scene& scene, int side) {
    const XMFLOAT4 plane = m_splitNode ? m_splitNode->GetPlane() : XMFLOAT4(0.0f, 0.0f, 1.0f, 0.0f);
    m_halfPrim[side] = AddHalfSurface(scene, m_halfData[side],
                                      std::string(HalfTag(side)) + "_" + Stem(m_srcPath),
                                      AttachParent(), side, plane, m_skirtActive);
    if (SemSession* hs = m_half[side]) {
        hs->m_halfStyled = true;
        hs->m_halfSide   = side;
        hs->m_halfPlane  = plane;
        hs->m_halfSkirt  = m_skirtActive;
    }
}

double SemSession::SkirtDepthArg() const {
    return !skirtEnabled ? 0.0 : (skirtDepth > 0.0f ? (double)skirtDepth : -1.0);
}

void SemSession::PushSplitClipPlane(int side) {
    SemSession* hs = m_half[side];
    if (!hs || !m_splitNode) return;
    // An extended half runs its stages on half + skirt: clipping it at the parting
    // plane would cut the skirt straight off again.
    if (m_skirtActive) {
        hs->Activate();
        SEM_ClearClipPlanes3D();
        Activate();
        return;
    }
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
    // Skirted halves are the default mode: set the skirt before the split so the
    // halves come out extended and their stages run unclipped from the start.
    const double skDepth = SkirtDepthArg();
    if (!CheckRc(scene, silent, "SEM_SetBodySkirt3D", SEM_SetBodySkirt3D(skDepth),
                 { "", "Not a body context", "Could not publish a half" }))
        return false;
    m_skirtActive = (skDepth != 0.0);
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
        m_half[s]->m_halfStyled = true;
        m_half[s]->m_halfSide   = s;
        m_half[s]->m_halfPlane  = m_splitNode->GetPlane();
        m_half[s]->m_halfSkirt  = m_skirtActive;
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

    // The taper is the body's own heavy stage - the push over both halves plus
    // their remesh sweeps - so it runs on the worker and reports progress there,
    // like every other 3D stage. PollAsync picks the result up.
    if (m_job.worker.joinable()) m_job.worker.join();
    m_job.runOffsets = m_job.runMesh = m_job.runThermal = m_job.runIso = false;
    m_job.runStandalone = m_job.runTaper = m_job.runRemesh = false;
    m_job.runBodyTaper  = true;
    m_job.runBodyPreform = false;
    m_job.tpAngle       = (double)taperAngle;
    m_job.tpRemeshIters = taperRemeshIters;
    // The skirt goes in with the taper: 0 = none, < 0 = down to the other half.
    m_job.skDepth  = SkirtDepthArg();
    m_job.skGrowth = (double)skirtGrowth;
    m_job.offsetsPath.clear();
    m_job.meshPath.clear();
    m_job.isoPath.clear();
    m_job.error.clear();
    m_job.offsetsMs = m_job.meshMs = m_job.thermalMs = m_job.isoMs = -1.0;
    m_job.taperMs = -1.0;
    m_job.totalStages.store(1);
    m_job.stageKind.store(7);
    m_job.progressStage.store(0);
    m_job.ok.store(false);
    m_job.done.store(false);
    m_job.cancel.store(false);
    m_job.cancelled.store(false);
    m_progressShown.store(0.0f);
    m_job.running.store(true);
    snprintf(status, sizeof(status), m_job.skDepth != 0.0 ? "Tapering and extending both halves..."
                                                          : "Tapering both halves...");
    m_job.worker = std::thread(&SemSession::PipelineWorkerBody, this);
    return true;
}

void SemSession::FinishBodyTaper(Scene& scene) {
    Activate();
    // The core re-tapered from the pristine cut, built the skirts on top and
    // reloaded both half contexts, so every stage those halves had built is gone.
    // Replace their primitives with the new geometry (half + skirt) and reset their
    // stage state; the skirt decides whether they are still clipped at the plane.
    // It also dropped the preform, which was glued from the old halves.
    m_skirtActive = (m_job.skDepth != 0.0);
    DropPreform(scene);
    RefreshHalfPrims(scene);

    int moved = 0, bad = 0;
    double worst = 0.0, growth = 0.0;
    TaperReport(+1, moved, bad, worst, growth);
    int n = snprintf(status, sizeof(status),
                     "Taper %.2f deg in %.0f ms: moved %d, %d facets short, min draft %.2f deg, "
                     "outline +%.4f.",
                     taperAngle, m_job.taperMs, moved, bad, worst, growth);
    double d = 0.0, rp = 0.0, rn = 0.0;
    if (m_skirtActive && n > 0 && n < (int)sizeof(status) && SkirtState(d, rp, rn))
        snprintf(status + n, sizeof(status) - n, " Skirt down %.4g / %.4g.", rp, rn);
}

void SemSession::RefreshHalfPrims(Scene& scene) {
    DropSplitOnPlane(scene);
    for (int s = 0; s < 2; ++s) {
        // Read what the half's context holds - half + skirt when the skirt is on -
        // so the primitive shows exactly the surface its stages will run on.
        SemSession* hs = m_half[s];
        if (!hs) continue;
        hs->Activate();
        SEM_MeshView v{};
        const bool got = (SEM_GetSourceSurface3D(&v) == 0 && v.num_nodes > 0);
        if (got) m_halfData[s] = ViewToData(v);
        Activate();
        if (!got) continue;

        const bool wasVisible = m_halfPrim[s] ? m_halfPrim[s]->visible : true;
        hs->RebindHalf(scene, nullptr);
        if (m_halfPrim[s] && Alive(scene, m_halfPrim[s])) scene.RemovePrimitive(m_halfPrim[s]);
        m_halfPrim[s] = nullptr;
        BuildHalfPrim(scene, s);
        if (m_halfPrim[s]) m_halfPrim[s]->visible = wasVisible;
        hs->RebindHalf(scene, m_halfPrim[s]);
        // Republishing reloaded the half's source, which cleared its clip planes.
        PushSplitClipPlane(s);
    }
    for (int i = 0; i <= STAGE_ISOSURFACE; ++i) m_dirty[i] = true;
    if (m_splitOnPlaneShown) BuildSplitOnPlane(scene);
    scene.UpdateLight();
}

bool SemSession::SkirtState(double& depth, double& reached_pos, double& reached_neg) const {
    depth = reached_pos = reached_neg = 0.0;
    if (!IsBody()) return false;
    Activate();
    return SEM_GetBodySkirt3D(&depth, &reached_pos, &reached_neg) == 0;
}

bool SemSession::HalvesHaveIsosurfaces() const {
    for (const SemSession* h : m_half)
        if (!h || !h->IsolinePrim()) return false;
    return true;
}

bool SemSession::ApplyPreform(Scene& scene, bool silent) {
    if (!IsBody() || !m_bodySplit) {
        Report(scene, silent, "The preform needs a split body.");
        return false;
    }
    if (AsyncRunning()) return false;
    if (preformSource == PREFORM_FROM_ISOSURFACES && !HalvesHaveIsosurfaces()) {
        Report(scene, silent, "Extract the isosurface on both halves first.");
        return false;
    }

    if (m_job.worker.joinable()) m_job.worker.join();
    m_job.runOffsets = m_job.runMesh = m_job.runThermal = m_job.runIso = false;
    m_job.runStandalone = m_job.runTaper = m_job.runRemesh = false;
    m_job.runBodyTaper   = false;
    m_job.runBodyPreform = true;
    m_job.pfSource = (preformSource == PREFORM_FROM_ISOSURFACES) ? SEM_PREFORM_FROM_ISOSURFACES
                                                                 : SEM_PREFORM_FROM_HALVES;
    m_job.offsetsPath.clear();
    m_job.meshPath.clear();
    m_job.isoPath.clear();
    m_job.error.clear();
    m_job.offsetsMs = m_job.meshMs = m_job.thermalMs = m_job.isoMs = -1.0;
    m_job.preformMs = -1.0;
    m_job.totalStages.store(1);
    m_job.stageKind.store(8);
    m_job.progressStage.store(0);
    m_job.ok.store(false);
    m_job.done.store(false);
    m_job.cancel.store(false);
    m_job.cancelled.store(false);
    m_progressShown.store(0.0f);
    m_job.running.store(true);
    snprintf(status, sizeof(status), "Gluing the halves into a preform...");
    m_job.worker = std::thread(&SemSession::PipelineWorkerBody, this);
    return true;
}

void SemSession::DropPreform(Scene& scene) {
    if (m_preformPrim && Alive(scene, m_preformPrim)) scene.RemovePrimitive(m_preformPrim);
    m_preformPrim = nullptr;
    m_preformData = CSV3DLoader::CSV3DData();
}

void SemSession::FinishBodyPreform(Scene& scene) {
    Activate();
    SEM_MeshView v{};
    if (SEM_GetBodyPreform3D(&v) != 0 || v.num_nodes <= 0) {
        Report(scene, false, "Preform: the core returned no surface" + SemDetail());
        return;
    }
    const bool wasVisible = m_preformPrim ? m_preformPrim->visible : true;
    DropPreform(scene);
    m_preformData = ViewToData(v);
    Primitive* p = scene.AddFromCSV3DData(m_preformData, "preform_" + Stem(m_srcPath),
                                          AttachParent(), nullptr,
                                          Colors::BLUE, Colors::RED);
    if (!p) return;
    ConfigureSurface3D(p);
    p->SetColor(kPreformFront);
    p->SetUseVertexColor(false);
    p->SetTwoSided(true, kPreformBack);
    p->visible = wasVisible;
    m_preformPrim = p;
    // The preform sits right on top of the halves it was glued from, so a fresh
    // one is shown on its own; the halves stay a checkbox away.
    for (Primitive* hp : m_halfPrim)
        if (hp && Alive(scene, hp)) scene.SetNodeVisibleCascade(hp, false);
    m_preformPath = SaveNextToSource("_preform", m_preformData);
    scene.UpdateLight();

    double volume = 0.0, land = 0.0;
    int lp = 0, ln = 0, cross = 0, open = 0;
    PreformReport(volume, land, lp, ln, cross, open);
    snprintf(status, sizeof(status),
             "Preform in %.0f ms: volume %.6g, lands %.4g%s%s%s.",
             m_job.preformMs, volume, land,
             open > 0 ? ", NOT closed" : "",
             m_preformPath.empty() ? "" : ", saved ",
             m_preformPath.empty() ? "" : BaseName(m_preformPath).c_str());
}

bool SemSession::PreformReport(double& volume, double& land_area, int& loops_pos, int& loops_neg,
                               int& crossings, int& open_edges) const {
    volume = land_area = 0.0;
    loops_pos = loops_neg = crossings = open_edges = 0;
    if (!IsBody() || !m_bodySplit) return false;
    Activate();
    return SEM_GetBodyPreformReport3D(&volume, &land_area, &loops_pos, &loops_neg,
                                      &crossings, &open_edges) == 0;
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

bool SemSession::AdaptiveRemeshReport(int& crease_edges, int& corners,
                                      double& min_edge, double& max_edge) const {
    crease_edges = corners = 0;
    min_edge = max_edge = 0.0;
    Activate();
    return SEM_GetLastAdaptiveRemeshReport3D(&crease_edges, &corners, &min_edge, &max_edge) == 0;
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
        h->tetMaxEdgeLen = tetMaxEdgeLen; h->tetMaxEdgeAuto = tetMaxEdgeAuto;
        h->meshEnabled = meshEnabled;
        h->thermalEnabled = thermalEnabled; h->maxInward = maxInward; h->bcView = bcView;
        h->innerEnabled = innerEnabled; h->innerFirstGap = innerFirstGap;
        h->innerNum = innerNum; h->innerGrading = innerGrading;
        h->gridBoxScale = gridBoxScale; h->gridStep = gridStep;
        h->bcFrom = bcFrom; h->bcTo = bcTo; h->isoShift = isoShift;
        h->isoEnabled = isoEnabled; h->isoValue = isoValue;
        h->isoShowNonManifold = isoShowNonManifold;
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
