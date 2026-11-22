#include "SemSessionDetail.h"
#include <algorithm>
#include <cmath>
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

// The broken parting line's drawn line and the surface it sweeps.
const XMFLOAT4 kSketchLine(0.95f, 0.35f, 0.10f, 1.0f);
const XMFLOAT4 kSketchSurf(0.95f, 0.55f, 0.25f, 0.22f);

float Dot3(const XMFLOAT3& a, const XMFLOAT3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

}

float PartingShape::Height(const XMFLOAT3& p) const {
    const float r = plane.x * p.x + plane.y * p.y + plane.z * p.z + plane.w;
    if (!profile || pts.size() < 2) return r;
    const float u = Dot3(along, p), h = Dot3(up, p);
    float base = pts.front().y;
    if (u >= pts.back().x) base = pts.back().y;
    else if (u > pts.front().x)
        for (size_t k = 1; k < pts.size(); ++k)
            if (u <= pts[k].x) {
                const float t = (u - pts[k - 1].x) / (pts[k].x - pts[k - 1].x);
                base = pts[k - 1].y + t * (pts[k].y - pts[k - 1].y);
                break;
            }
    return h - base;
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
    m_meshPos.clear(); m_meshT.clear(); m_meshTets.clear(); m_meshEdges.clear();
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
    m_offsetsMs = StageTimeMs(STAGE_OFFSETS);
    m_meshMs = StageTimeMs(STAGE_MESH);
    m_thermalMs = StageTimeMs(STAGE_THERMAL);
    m_isoMs = StageTimeMs(STAGE_ISOSURFACE);
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
                                      int side, const PartingShape& shape, bool skirt) {
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
            const float h = s * shape.Height(n.pos);
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
    m_halfPrim[side] = AddHalfSurface(scene, m_halfData[side],
                                      std::string(HalfTag(side)) + "_" + Stem(m_srcPath),
                                      AttachParent(), side, m_splitShape, m_skirtActive);
    if (SemSession* hs = m_half[side]) {
        hs->m_halfStyled = true;
        hs->m_halfSide   = side;
        hs->m_halfShape  = m_splitShape;
        hs->m_halfSkirt  = m_skirtActive;
    }
}

PartingShape SemSession::CurrentPartingShape() const {
    PartingShape s;
    if (partingKind == PARTING_POLYLINE) {
        XMFLOAT3 along, up, normal;
        SketchAxes(sketchPlane, along, up, normal);
        s.profile = true;
        s.plane   = XMFLOAT4(up.x, up.y, up.z, 0.0f);
        s.along   = along;
        s.up      = up;
        s.pts     = sketchPts;
        if (!s.pts.empty() && s.pts.back().x < s.pts.front().x) std::reverse(s.pts.begin(), s.pts.end());
    } else if (m_splitNode) {
        s.plane = m_splitNode->GetPlane();
    }
    return s;
}

double SemSession::SkirtDepthArg() const {
    return !skirtEnabled ? 0.0 : (skirtDepth > 0.0f ? (double)skirtDepth : -1.0);
}

void SemSession::PushSplitClipPlane(int side) {
    SemSession* hs = m_half[side];
    if (!hs) return;
    // An extended half runs its stages on half + skirt: clipping it at the parting
    // plane would cut the skirt straight off again.
    if (m_skirtActive) {
        hs->Activate();
        SEM_ClearClipPlanes3D();
        Activate();
        return;
    }
    if (!m_splitNode) return;
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
    const bool profile = (partingKind == PARTING_POLYLINE);
    if (profile) {
        if (m_sketchDrawing) EndSketch(scene, true);
        std::string why;
        if (!SketchValid(&why)) { Report(scene, silent, "Parting line: " + why); return false; }
    } else if (!m_splitNode || !Alive(scene, m_splitNode)) {
        Report(scene, silent, "Add a parting plane first.");
        return false;
    }

    Timer splitTimer; splitTimer.Restart();

    // A half hidden altogether - with everything it carries - stays hidden in the
    // new split.
    bool hiddenHalf[2] = { false, false };
    for (int s = 0; s < 2; ++s)
        hiddenHalf[s] = m_halfPrim[s] && Alive(scene, m_halfPrim[s]) && SubtreeHidden(m_halfPrim[s]);

    // Let go of a previous split first: those sessions own the old half contexts,
    // and the core would otherwise be asked to publish into contexts we are about
    // to destroy.
    DropHalves(scene);

    Activate();
    if (profile) {
        XMFLOAT3 along, up, normal;
        SketchAxes(sketchPlane, along, up, normal);
        std::vector<SEM_Vec3> pts;
        for (const XMFLOAT2& q : sketchPts) {
            const XMFLOAT3 p = SketchToWorld(q);
            pts.push_back(SEM_Vec3{ p.x, p.y, p.z });
        }
        if (!CheckRc(scene, silent, "SEM_SetBodyPartingPolyline3D",
                     SEM_SetBodyPartingPolyline3D(SEM_Vec3{ up.x, up.y, up.z },
                                                  SEM_Vec3{ normal.x, normal.y, normal.z },
                                                  pts.data(), (int)pts.size(), (double)splitPlaneTol),
                     { "", "Not a body context", "Fewer than two points", "Bad tolerance",
                       "Draw and sweep directions", "The line doubles back" }))
            return false;
    } else {
        const XMFLOAT4 pl = m_splitNode->GetPlane();     // already unit-normalized
        const SEM_Plane3D sp{ { pl.x, pl.y, pl.z }, pl.w };
        if (!CheckRc(scene, silent, "SEM_SetBodySplitPlane3D",
                     SEM_SetBodySplitPlane3D(sp, (double)splitPlaneTol),
                     { "", "Not a body context", "Zero normal", "Bad tolerance" }))
            return false;
    }
    // Skirted halves are the default mode: set the skirt before the split so the
    // halves come out extended and their stages run unclipped from the start. Off a
    // broken parting line the core always builds one (0 = down to the other half).
    const double skDepth = SkirtDepthArg();
    if (!CheckRc(scene, silent, "SEM_SetBodySkirt3D", SEM_SetBodySkirt3D(skDepth),
                 { "", "Not a body context", "Could not publish a half" }))
        return false;
    m_skirtActive  = profile || (skDepth != 0.0);
    m_splitProfile = profile;
    m_splitShape   = CurrentPartingShape();
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
        if (hiddenHalf[s] && m_halfPrim[s]) scene.SetNodeVisibleCascade(m_halfPrim[s], false);
        m_half[s] = new SemSession();
        m_half[s]->BindExisting(scene, ctx[s], m_halfPrim[s], srcPath, workDir);
        m_half[s]->m_halfStyled = true;
        m_half[s]->m_halfSide   = s;
        m_half[s]->m_halfShape  = m_splitShape;
        m_half[s]->m_halfSkirt  = m_skirtActive;
        PushSplitClipPlane(s);
    }

    if (!m_half[0] || !m_half[1]) { DropHalves(scene); return false; }

    m_bodySplit = true;
    gridStepPreview = 0.0f;
    for (int i = 0; i <= STAGE_ISOSURFACE; ++i) m_dirty[i] = true;
    if (m_splitOnPlaneShown) BuildSplitOnPlane(scene);
    scene.UpdateLight();
    m_splitMs = splitTimer.GetMillisecondsElapsed();
    m_taperCurrent = false;
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
    m_job.skMode = skirtMethod == SEM_SKIRT_COLUMNS ? SEM_SKIRT_COLUMNS
                  : (skirtThermal ? SEM_SKIRT_CONES_THERMAL : SEM_SKIRT_CONES);
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
    // It also dropped the preform, which was glued from the old halves. Off a broken
    // parting line the core builds the skirt whatever the setting.
    m_skirtActive = m_splitProfile || (m_job.skDepth != 0.0);
    DropPreform(scene);
    RefreshHalfPrims(scene);
    m_taperMs = m_job.taperMs;
    m_taperCurrent = true;
    gridStepPreview = 0.0f;

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

void SemSession::ShowPreformData(Scene& scene, const CSV3DLoader::CSV3DData& data) {
    const bool wasVisible = m_preformPrim ? m_preformPrim->visible : true;
    DropPreform(scene);
    m_preformData = data;
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
    m_preformMs = m_job.preformMs;
    // The preform sits right on top of the halves it was glued from, so it is shown
    // on its own; the halves stay a checkbox away.
    for (Primitive* hp : m_halfPrim)
        if (hp && Alive(scene, hp)) scene.SetNodeVisibleCascade(hp, false);
    scene.UpdateLight();
}

void SemSession::FinishBodyPreform(Scene& scene) {
    Activate();
    SEM_MeshView v{};
    if (SEM_GetBodyPreform3D(&v) != 0 || v.num_nodes <= 0) {
        Report(scene, false, "Preform: the core returned no surface" + SemDetail());
        return;
    }
    ShowPreformData(scene, ViewToData(v));
    if (!m_preformPrim) return;
    m_preformPath = SaveNextToSource("_preform", m_preformData);

    double volume = 0.0, land = 0.0;
    int lp = 0, ln = 0, cross = 0, open = 0;
    PreformReport(volume, land, lp, ln, cross, open);
    bool atCrossing = false;
    int seamLoops = 0;
    double seamMin = 0.0, seamMax = 0.0;
    std::string note;
    PreformJoint(atCrossing, seamLoops, seamMin, seamMax, note);
    char joint[96];
    if (atCrossing) snprintf(joint, sizeof(joint), "joined where the halves cross (%+.3g .. %+.3g off the plane)", seamMin, seamMax);
    else            snprintf(joint, sizeof(joint), "joined in the parting plane, lands %.4g", land);
    snprintf(status, sizeof(status),
             "Preform in %.0f ms: volume %.6g, %s%s%s%s.",
             m_job.preformMs, volume, joint,
             open > 0 ? ", NOT closed" : "",
             m_preformPath.empty() ? "" : ", saved ",
             m_preformPath.empty() ? "" : BaseName(m_preformPath).c_str());
}

bool SemSession::PreformJoint(bool& at_crossing, int& seam_loops, double& seam_min, double& seam_max,
                              std::string& note) const {
    at_crossing = false;
    seam_loops = 0;
    seam_min = seam_max = 0.0;
    note.clear();
    if (!IsBody() || !m_bodySplit) return false;
    Activate();
    int at = 0;
    if (SEM_GetBodyPreformJoint3D(&at, &seam_loops, &seam_min, &seam_max) != 0) return false;
    at_crossing = (at != 0);
    note = SEM_GetBodyPreformNote3D();
    return true;
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
    if (!IsBody() || !m_bodySplit || m_splitProfile || !m_splitNode || !Alive(scene, m_splitNode)) return;

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
        h->isoRemesh = isoRemesh;
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
        gridStepPreview = 0.0f;
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

// ---- Broken parting line: the polyline sketch -----------------------------------

void SemSession::SketchAxes(int plane, XMFLOAT3& along, XMFLOAT3& up, XMFLOAT3& normal) {
    switch (plane) {
    case SKETCH_YZ: along = XMFLOAT3(0, 1, 0); up = XMFLOAT3(0, 0, 1); break;
    case SKETCH_XY: along = XMFLOAT3(1, 0, 0); up = XMFLOAT3(0, 1, 0); break;
    default:        along = XMFLOAT3(1, 0, 0); up = XMFLOAT3(0, 0, 1); break;
    }
    // Screen right x screen up: toward the viewer of a view looking straight at it.
    normal = XMFLOAT3(along.y * up.z - along.z * up.y, along.z * up.x - along.x * up.z,
                      along.x * up.y - along.y * up.x);
}

bool SemSession::SourceBox(XMFLOAT3& lo, XMFLOAT3& hi) const {
    if (!m_boxValid) {
        if (!SourceBBox(m_srcPath, m_boxLo, m_boxHi)) m_boxLo = m_boxHi = XMFLOAT3(0, 0, 0);
        m_boxValid = true;
    }
    lo = m_boxLo;
    hi = m_boxHi;
    return m_boxLo.x < m_boxHi.x || m_boxLo.y < m_boxHi.y || m_boxLo.z < m_boxHi.z;
}

XMFLOAT3 SemSession::SketchToWorld(const XMFLOAT2& q) const {
    XMFLOAT3 along, up, normal, lo, hi;
    SketchAxes(sketchPlane, along, up, normal);
    SourceBox(lo, hi);
    const XMFLOAT3 c(0.5f * (lo.x + hi.x), 0.5f * (lo.y + hi.y), 0.5f * (lo.z + hi.z));
    const float n = Dot3(normal, c);
    return XMFLOAT3(along.x * q.x + up.x * q.y + normal.x * n,
                    along.y * q.x + up.y * q.y + normal.y * n,
                    along.z * q.x + up.z * q.y + normal.z * n);
}

XMFLOAT2 SemSession::WorldToSketch(const XMFLOAT3& p) const {
    XMFLOAT3 along, up, normal;
    SketchAxes(sketchPlane, along, up, normal);
    return XMFLOAT2(Dot3(along, p), Dot3(up, p));
}

bool SemSession::SketchValid(std::string* why) const {
    if (sketchPts.size() < 2) {
        if (why) *why = "draw at least two points";
        return false;
    }
    XMFLOAT3 lo, hi;
    SourceBox(lo, hi);
    const float span = (std::max)({ hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1.0f });
    const float eps = 1e-6f * span;
    int dir = 0;
    for (size_t k = 1; k < sketchPts.size(); ++k) {
        const float du = sketchPts[k].x - sketchPts[k - 1].x;
        if (std::abs(du) <= eps) {
            if (why) *why = "two points lie straight above each other (point " + std::to_string(k + 1) + ")";
            return false;
        }
        const int s = (du > 0.0f) ? 1 : -1;
        if (dir != 0 && s != dir) {
            if (why) *why = "the line doubles back at point " + std::to_string(k + 1);
            return false;
        }
        dir = s;
    }
    return true;
}

void SemSession::LookAtSketchPlane(Scene& scene) const {
    XMFLOAT3 along, up, normal, lo, hi;
    SketchAxes(sketchPlane, along, up, normal);
    // The camera's rows: screen right, toward the viewer, screen up (see NavGizmo).
    XMMATRIX m;
    m.r[0] = XMVectorSet(along.x, along.y, along.z, 0.0f);
    m.r[1] = XMVectorSet(normal.x, normal.y, normal.z, 0.0f);
    m.r[2] = XMVectorSet(up.x, up.y, up.z, 0.0f);
    m.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    scene.camera.SetRotation(m);
    if (SourceBox(lo, hi))
        scene.camera.SetPosition(XMFLOAT3(0.5f * (lo.x + hi.x), 0.5f * (lo.y + hi.y), 0.5f * (lo.z + hi.z)));
}

void SemSession::BeginSketch(Scene& scene) {
    if (!IsBody() || AsyncRunning() || m_sketchDrawing) return;
    m_sketchBackup  = sketchPts;
    sketchPts.clear();
    m_sketchDrawing = true;
    LookAtSketchPlane(scene);
    RefreshSketchPrims(scene);
    snprintf(status, sizeof(status), "Drawing the parting line: click its points one way across the view.");
}

void SemSession::EndSketch(Scene& scene, bool keep) {
    if (!m_sketchDrawing) return;
    m_sketchDrawing = false;
    if (!keep) sketchPts = m_sketchBackup;
    m_sketchBackup.clear();
    RefreshSketchPrims(scene);
    snprintf(status, sizeof(status), keep ? "Parting line: %d points." : "Parting line drawing cancelled (%d points).",
             (int)sketchPts.size());
}

bool SemSession::AddSketchPoint(Scene& scene, const XMFLOAT2& q, std::string* why) {
    if (!sketchPts.empty()) {
        XMFLOAT3 lo, hi;
        SourceBox(lo, hi);
        const float span = (std::max)({ hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1.0f });
        const float du = q.x - sketchPts.back().x;
        if (std::abs(du) <= 1e-6f * span) {
            if (why) *why = "a point straight above the last one: the line must keep moving across";
            return false;
        }
        if (sketchPts.size() >= 2) {
            const float d0 = sketchPts[1].x - sketchPts[0].x;
            if ((du > 0.0f) != (d0 > 0.0f)) {
                if (why) *why = "the line must keep going the same way across the view";
                return false;
            }
        }
    }
    sketchPts.push_back(q);
    RefreshSketchPrims(scene);
    return true;
}

void SemSession::UndoSketchPoint(Scene& scene) {
    if (sketchPts.empty()) return;
    sketchPts.pop_back();
    RefreshSketchPrims(scene);
}

void SemSession::ClearSketch(Scene& scene) {
    sketchPts.clear();
    RefreshSketchPrims(scene);
}

void SemSession::SetSketchPlane(Scene& scene, int plane) {
    if (plane == sketchPlane) return;
    sketchPlane = plane;
    // The points were in the old plane's coordinates.
    sketchPts.clear();
    m_sketchBackup.clear();
    if (m_sketchDrawing) LookAtSketchPlane(scene);
    RefreshSketchPrims(scene);
}

void SemSession::SetPartingKind(Scene& scene, int kind) {
    if (kind == partingKind) return;
    if (m_sketchDrawing) EndSketch(scene, false);
    partingKind = kind;
    // The plane's rectangle only means something in the plane mode.
    if (m_splitGroup && Alive(scene, m_splitGroup))
        scene.SetNodeVisibleCascade(m_splitGroup, kind == PARTING_PLANE);
    RefreshSketchPrims(scene);
}

void SemSession::DropSketchPrims(Scene& scene) {
    if (m_sketchGroup && Alive(scene, m_sketchGroup)) scene.RemoveNode(m_sketchGroup);
    m_sketchGroup = nullptr;
}

void SemSession::RefreshSketchPrims(Scene& scene) {
    const bool wasVisible = (m_sketchGroup && Alive(scene, m_sketchGroup)) ? m_sketchGroup->visible : true;
    DropSketchPrims(scene);
    if (!IsBody() || !HasSource() || partingKind != PARTING_POLYLINE || sketchPts.empty()) return;

    XMFLOAT3 along, up, normal, lo, hi;
    SketchAxes(sketchPlane, along, up, normal);
    SourceBox(lo, hi);
    // The source's extent along the line and across the sketch plane.
    float umin = 1e30f, umax = -1e30f, nmin = 1e30f, nmax = -1e30f;
    for (int c = 0; c < 8; ++c) {
        const XMFLOAT3 p((c & 1) ? hi.x : lo.x, (c & 2) ? hi.y : lo.y, (c & 4) ? hi.z : lo.z);
        umin = (std::min)(umin, Dot3(along, p));  umax = (std::max)(umax, Dot3(along, p));
        nmin = (std::min)(nmin, Dot3(normal, p)); nmax = (std::max)(nmax, Dot3(normal, p));
    }
    const float pad = 0.1f * (std::max)({ umax - umin, nmax - nmin, 1e-3f });
    auto at = [&](float u, float h, float n) {
        return XMFLOAT3(along.x * u + up.x * h + normal.x * n, along.y * u + up.y * h + normal.y * n,
                        along.z * u + up.z * h + normal.z * n);
    };

    m_sketchGroup = scene.AddGroupNode("parting_line_" + Stem(m_srcPath), AttachParent());
    std::vector<XMFLOAT3> dots;
    for (const XMFLOAT2& q : sketchPts) dots.push_back(SketchToWorld(q));
    if (dots.size() >= 2) {
        std::vector<XMFLOAT3> poses;
        for (size_t k = 1; k < dots.size(); ++k) { poses.push_back(dots[k - 1]); poses.push_back(dots[k]); }
        std::vector<XMFLOAT4> cols(poses.size(), kSketchLine);
        if (Primitive* line = scene.AddColoredLines(poses, cols, "parting_polyline", m_sketchGroup)) {
            line->SetIlluminationCapability(false);
            StyleLines(line, LINESTYLE_THICK);
        }
    }
    if (Primitive* cloud = scene.AddPointCloud(dots, kSketchLine, "parting_points", m_sketchGroup))
        cloud->SetIlluminationCapability(false);

    // The surface the line sweeps across the body, carried on level past its ends.
    if (SketchValid()) {
        std::vector<XMFLOAT2> prof = sketchPts;
        if (prof.back().x < prof.front().x) std::reverse(prof.begin(), prof.end());
        prof.insert(prof.begin(), XMFLOAT2((std::min)(umin - pad, prof.front().x - pad), prof.front().y));
        prof.push_back(XMFLOAT2((std::max)(umax + pad, prof.back().x + pad), prof.back().y));
        const float n0 = nmin - pad, n1 = nmax + pad;
        std::vector<XMFLOAT3> tris;
        for (size_t k = 1; k < prof.size(); ++k) {
            const XMFLOAT3 a0 = at(prof[k - 1].x, prof[k - 1].y, n0), b0 = at(prof[k].x, prof[k].y, n0);
            const XMFLOAT3 a1 = at(prof[k - 1].x, prof[k - 1].y, n1), b1 = at(prof[k].x, prof[k].y, n1);
            tris.insert(tris.end(), { a0, b0, b1, a0, b1, a1 });
        }
        std::vector<XMFLOAT4> cols(tris.size(), kSketchSurf);
        if (Primitive* surf = scene.AddColoredTriangles(tris, cols, "parting_surface", m_sketchGroup)) {
            surf->SetIlluminationCapability(false);
            surf->SetUseVertexColor(false);
            surf->SetColor(kSketchSurf);
            surf->SetTwoSided(true, kSketchSurf);
            surf->SetAlpha(kSketchSurf.w);
        }
    }
    if (!wasVisible) scene.SetNodeVisibleCascade(m_sketchGroup, false);
    scene.UpdateLight();
}

// ---- Reopening a saved body session -----------------------------------------------

std::string SemSession::RestoreBodyFromCore(Scene& scene) {
    Activate();
    // The parting surface - a profile, or the plane - and the body's own settings.
    SEM_Plane3D pl{};
    double tol = 0.0;
    const bool hasPlane = SEM_GetBodySplitPlane3D(&pl, &tol) == 0;
    if (hasPlane) splitPlaneTol = (float)tol;
    bool profile = false;
    int cnt = 0;
    SEM_Vec3 draw{}, sweep{};
    SEM_GetBodyPartingPolyline3D(&draw, &sweep, nullptr, 0, &cnt);
    if (cnt >= 2) {
        std::vector<SEM_Vec3> pts((size_t)cnt);
        SEM_GetBodyPartingPolyline3D(&draw, &sweep, pts.data(), cnt, &cnt);
        // The sketch plane the line was drawn in: up = the draw direction, swept along
        // the plane's normal.
        for (int k = 0; k < 3 && !profile; ++k) {
            XMFLOAT3 a, u, n;
            SketchAxes(k, a, u, n);
            const double du = draw.x * u.x + draw.y * u.y + draw.z * u.z;
            const double dn = sweep.x * n.x + sweep.y * n.y + sweep.z * n.z;
            if (du > 0.999 && std::abs(dn) > 0.999) { sketchPlane = k; profile = true; }
        }
        if (profile) {
            sketchPts.clear();
            for (const SEM_Vec3& p : pts)
                sketchPts.push_back(WorldToSketch(XMFLOAT3((float)p.x, (float)p.y, (float)p.z)));
            partingKind = PARTING_POLYLINE;
        }
    }
    if (!profile && hasPlane) {
        partingKind = PARTING_PLANE;
        if (ClipPlaneNode* node = AddSplitPlane(scene))
            node->SetPlane(XMFLOAT4((float)pl.n.x, (float)pl.n.y, (float)pl.n.z, (float)pl.d));
    }
    double sd = 0.0, sg = 0.0;
    if (SEM_GetBodySkirtSettings3D(&sd, &sg) == 0) {
        skirtEnabled = (sd != 0.0);
        skirtDepth   = (sd > 0.0) ? (float)sd : 0.0f;
        skirtGrowth  = (float)sg;
        int mode = SEM_SKIRT_COLUMNS;
        if (SEM_GetBodySkirtMode3D(&mode) == 0) {
            skirtMethod = mode == SEM_SKIRT_COLUMNS ? SEM_SKIRT_COLUMNS : SEM_SKIRT_CONES;
            skirtThermal = mode == SEM_SKIRT_CONES_THERMAL;
        }
    }
    const double ta = SEM_GetBodyTaperAngle3D();
    if (ta > 0.0) taperAngle = (float)ta;
    RefreshSketchPrims(scene);

    int ctx[2] = { 0, 0 };
    if (SEM_GetBodyHalfContexts3D(&ctx[0], &ctx[1]) != 0 || ctx[0] <= 0 || ctx[1] <= 0) return std::string();

    // Split: the halves as their contexts hold them, each with the stages it had. A
    // session saved before the body kept its parting plane brings back only the
    // halves: their skirts cannot be told apart then, so they are not tinted.
    const bool knownSurface = profile || hasPlane;
    m_skirtActive  = knownSurface && (profile || (sd != 0.0));
    m_splitProfile = profile;
    m_splitShape   = CurrentPartingShape();
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
        Activate();
        if (!got) continue;
        BuildHalfPrim(scene, s);
        m_half[s] = new SemSession();
        m_half[s]->BindExisting(scene, ctx[s], m_halfPrim[s], srcPath, workDir);
        m_half[s]->m_halfStyled = true;
        m_half[s]->m_halfSide   = s;
        m_half[s]->m_halfShape  = m_splitShape;
        m_half[s]->m_halfSkirt  = m_skirtActive;
        // The core restored the half's own clip plane with it, if it had one.
        m_half[s]->Activate();
        m_half[s]->RestoreStagesFromCore(scene);
        Activate();
    }
    if (!m_half[0] || !m_half[1]) {
        // A half that could not be bound still owns its context.
        for (int s = 0; s < 2; ++s)
            if (!m_half[s] && ctx[s] > 0) SEM_DestroyContext(ctx[s]);
        Activate();
        DropHalves(scene);
        ShowSource(scene, true);
        return "A half could not be restored; split the body again.";
    }
    m_bodySplit = true;
    std::string note;
    if (!knownSurface) {
        m_skirtActive = (sd != 0.0);
        for (SemSession* h : m_half) if (h) h->m_halfSkirt = m_skirtActive;
        note = "Saved by an older build: the halves and their stages are back, not the parting "
               "plane - add one and split again to taper.";
    }
    // The shared stage controls take the knobs the halves last ran with.
    m_half[0]->Activate();
    ApplyPipelineArgs3D();
    Activate();
    for (int i = 0; i <= STAGE_ISOSURFACE; ++i) m_dirty[i] = m_half[0]->m_dirty[i] || m_half[1]->m_dirty[i];
    if (m_splitOnPlaneShown) BuildSplitOnPlane(scene);

    SEM_MeshView pv{};
    if (SEM_GetBodyPreform3D(&pv) == 0 && pv.num_nodes > 0) ShowPreformData(scene, ViewToData(pv));
    scene.UpdateLight();
    return note;
}

}
