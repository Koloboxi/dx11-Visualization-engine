#pragma once
#include "../graphics/scene/scene.h"
#include "../graphics/scene/scene_service.h"
#include "../external/sem_exports.h"
#include "../loaders/CSV3DLoader.h"
#include <string>
#include <vector>
#include <initializer_list>
#include <thread>
#include <atomic>

namespace SemSessionNS {

enum Stage { STAGE_SUBDIVIDE = 0, STAGE_OFFSETS = 1, STAGE_MESH = 2, STAGE_THERMAL = 3,
             STAGE_ISOSURFACE = 4 };
enum OffsetMode { OFFSET_EVEN = 0, OFFSET_GAPS = 1 };

// Grouping of the SEM_GetClipChanges3D records by the pipeline stage that produced
// them. Only the source's are shown (Source surface section):
//   CLIPCHG_SOURCE  - the "source/..." records
enum ClipChangeCategory { CLIPCHG_SOURCE = 0, CLIPCHG_COUNT = 1 };

struct Stats {
    bool valid    = false;
    int  verts    = 0;
    int  edges    = 0;
    int  tris     = 0;
    int  boundary = 0;
};

class SemSession {
public:
    // 2 = contour pipeline (SEM_LoadCSV3D, SEM_*); 3 = triangle-surface pipeline
    // (SEM_LoadSurface3D, SEM_*3D). Auto-detected on Bind.
    int   dim        = 2;

    // Session mode (3D only): SESSION_PIPELINE runs the full load -> offsets -> mesh ->
    // thermal -> isosurface chain; SESSION_OFFSET_REMESH skips all of that and only
    // offset-and-remeshes the imported surface directly via the standalone
    // SEM_OffsetRemeshInPlaneSurface3D, with clip planes still editable. The pipeline
    // stage sections are hidden in the standalone mode (and vice versa).
    // SESSION_BODY is picked automatically when the imported surface is closed: the
    // SEM core makes that a body context (SEM_CTX_BODY), and this session then
    // drives the body stages — parting plane, split, taper — plus the ordinary
    // pipeline run on BOTH halves at once. See the body section further down.
    // The three standalone modes all work on the imported open surface directly
    // and leave the pipeline cache alone; they differ only in what they do to it.
    //   SESSION_OFFSET_REMESH - shift along the pseudonormals, then re-mesh over a
    //                           base plane (SEM_OffsetRemeshInPlaneSurface3D).
    //   SESSION_TAPER         - add draft about a parting plane the surface's
    //                           boundary lies in (SEM_TaperInPlaneSurface3D).
    //   SESSION_REMESH        - plain isotropic remesh, shape preserved
    //                           (SEM_RemeshStandaloneSurface3D).
    enum SessionMode { SESSION_PIPELINE = 0, SESSION_OFFSET_REMESH = 1,
                       SESSION_BODY = 2, SESSION_TAPER = 3, SESSION_REMESH = 4 };
    int   sessionMode = SESSION_PIPELINE;
    // Standalone offset-remesh parameters (SESSION_OFFSET_REMESH). soAxisN is the
    // projection-plane normal (any direction; need not be unit — (0,0,1) reproduces the
    // former Z base plane); soOffset the pseudonormal shift as a distance in world units
    // (positive inward), soMinOffset the clearance cull as a fraction of the
    // offset (c in [0,1]; the absolute clearance handed to SEM is c * soOffset); the
    // final isotropic cleanup runs soIters sweeps toward soTargetMult * the sheet's
    // own average edge length (soTargetMult <= 0 = median; soIters <= 0 skips it).
    // These map to SEM_OffsetRemeshInPlaneSurface3D's arguments.
    XMFLOAT3 soAxisN  = { 0.0f, 0.0f, 1.0f };
    float soOffset    = 0.001f;
    float soMinOffset = 0.99f;
    float soTargetMult = 1.0f;
    int   soIters     = 3;

    int   subMode    = 0;
    int   subN       = 2;
    bool  subEnabled = true;

    int   offsetMode = OFFSET_EVEN;
    float firstGap   = 1.0f;
    int   numOffsets = 8;
    float grading    = 1.2f;
    std::vector<float> gaps = { 25.0f, 25.0f, 25.0f };
    bool  offEnabled = true;

    // meshParam: interior Steiner-point spacing in world units (<0 => source mean edge length).
    // steinerMargin: minimum spacing from offset lines as a multiple of meshParam.
    int   meshMethod    = SEM_STEINER_GRID;
    float meshParam     = -1.0f;
    float steinerMargin = 0.45f;
    bool  meshEnabled   = true;

    // tetMethod: the 3D mesh is the grid domain (SEM_TET_GRID); the band methods
    // (SEM_TET_BAND / SEM_TET_LAYERED) only come back with older sessions, until the
    // mesh is rebuilt. tetParam (<0 => per-method auto): BAND => Steiner grid cell
    // size (a length in world units); LAYERED => use_sdf flag (0/1).
    int   tetMethod    = SEM_TET_GRID;
    float tetParam     = 1.0f;
    // Max tet edge length, in world units. <= 0 disables the filter. While
    // tetMaxEdgeAuto is set it follows the offsets: sqrt(2) times the gap between
    // the last two shells (AutoTetMaxEdge), re-read whenever the mesh is built.
    float tetMaxEdgeLen  = 10.0f;
    bool  tetMaxEdgeAuto = true;
    // sqrt(2) * the gap between the outermost shell and the one inside it, from the
    // offset settings as they stand (0 when they define no shell).
    float AutoTetMaxEdge() const;

    bool  thermalEnabled = true;
    bool  isoEnabled     = true;
    // 3D: the smoothing - the level of the field the isosurface is taken at, which
    // the shift brings back onto the From level. 2D: the isoline value.
    float isoValue   = 0.05f;
    // When set, triangles of the displayed standalone result that touch a
    // non-manifold edge or bowtie vertex are painted bright magenta over the uniform
    // green — the topology defects vertex welding can leave in an offset-remeshed
    // surface. See SetIsoNonManifold / NonManifoldTris.
    bool  isoShowNonManifold = false;
    // How the domain mesh is drawn. true = "BC": only the nodes the BC levels hold
    // fixed and the edges between them, blue at T = 0 and red at T = 1; false = the
    // solved T field over every edge. Until the field is solved for the BC levels as
    // they stand, the BCs are drawn either way.
    bool  bcView     = false;
    // Max inward penetration (0..1) for the outermost-offset T=0 BCs. < 0 keeps
    // every outermost node.
    float maxInward  = 0.04f;
    // Offsets into the material (SEM_ComputeInnerOffsets3D), graded like the outer
    // ones; the grid embeds them as further levels.
    bool  innerEnabled  = false;
    float innerFirstGap = 1.0f;
    int   innerNum      = 2;
    float innerGrading  = 1.0f;
    // The grid domain (SEM_TET_GRID): the box as multiples of the part's extents
    // along x, y, z, and the grid step in world units (0 = auto).
    XMFLOAT3 gridBoxScale = { 1.5f, 1.5f, 1.5f };
    float    gridStep     = 0.0f;
    // Thermal BC levels (SEM_SetThermalBC3D): the field runs from 0 on bcFrom to 1
    // on bcTo. Level 0 is the source, k > 0 the k-th outer offset, k < 0 the k-th
    // inner one; SEM_LEVEL_OUTERMOST / INNERMOST follow the extreme levels.
    int   bcFrom = 0;
    int   bcTo   = SEM_LEVEL_OUTERMOST;
    // Shift the field back along its gradient before the isosurface is taken
    // (SEM_SetIsoShift3D), so isoValue sets the smoothing, not the size.
    bool  isoShift = true;

    bool  subAuto     = false;
    bool  offAuto     = false;
    bool  meshAuto    = false;
    bool  thermalAuto = false;

    // Each plane is (xyz = normal, w = d) in nx*x + ny*y + nz*z + d >= 0; the
    // normal points into the kept half-space. Pushed to the core via SEM_SetClipPlanes3D.
    std::vector<ClipPlaneNode*> clipPlaneNodes;

    bool  revolutionMode = false;
    int   revSegments    = 48;
    float srcRevAlpha    = 0.8f;
    float isoRevAlpha    = 0.5f;

    float surf3dAlpha    = 1.f;

    // On-plane snap tolerance handed to SEM_SetClipPlanes3D (its on_plane_rel_tol),
    // as a fraction of the source surface's mean edge length: vertices within this
    // band of a clip plane are snapped exactly onto it before cutting. Default 0.01;
    // 0 means exact (no snapping).
    float clipPlaneTol = 0.01f;

    char  status[256] = "Ready";

    // SEM_SetRevolution axis selector: 1 = X, 2 = Y, 3 = Z.
    static constexpr int kRevolutionAxisY = 2;

    SemSession() = default;
    ~SemSession();
    SemSession(const SemSession&)            = delete;
    SemSession& operator=(const SemSession&) = delete;

    // ======================================================================
    // Body session (SESSION_BODY): the imported surface is closed, so the SEM
    // core made its context a body context. This ONE session drives three
    // things and never appears as more than one setup in the workspace:
    //   - the body itself (its volume, the parting plane, the split);
    //   - the two open halves the split produced, each the source of its own
    //     SEM context, shown as children of the closed source primitive (which
    //     is just hidden, not removed, once the split succeeds);
    //   - the ordinary pipeline stages, whose parameters are shared and whose
    //     Apply runs BOTH halves — one after the other, because the library's
    //     progress and cancel state is process-global (see PollBodyStage).
    // ======================================================================
    float splitPlaneTol = 0.01f;    // on_plane_rel_tol handed to the split
    float taperAngle    = 7.0f;     // degrees from the draw direction
    // Isotropic remesh sweeps the core runs over each tapered surface right after
    // the push, with the parting plane as a constraint. The push stretches the
    // triangles it moves, so this is what leaves the draft cone evenly meshed;
    // 0 skips it and keeps the taper's own triangulation.
    int   taperRemeshIters = 2;

    // The skirt: both halves extended past the parting plane by their own draft
    // walls. On by default: the split already extends them (straight down, there is
    // no draft yet) and the taper rebuilds the skirt at its angle in the same Apply
    // (SEM_ApplyBodyTaperSkirt3D).
    // skirtDepth <= 0 extends each down to the other half's height. skirtGrowth
    // thins it out away from the parting line: 0 keeps evenly spaced rings with a
    // vertex per outline vertex, larger values space the rings ever wider and drop
    // ring vertices to match (the outline's corners always stay).
    bool  skirtEnabled = true;
    float skirtDepth   = 0.0f;
    float skirtGrowth  = 0.3f;

    // The preform (stage after the isosurface): the two halves glued back into one
    // closed body on their own geometry, joined by flat lands in the parting plane.
    // Which halves it takes:
    enum PreformSource  { PREFORM_FROM_HALVES = 0, PREFORM_FROM_ISOSURFACES = 1 };
    int    preformSource   = PREFORM_FROM_ISOSURFACES;
    // SESSION_REMESH knobs: target edge length as a multiple of the surface's own
    // average (<= 0 uses the median), and the sweep count.
    float rmTargetMult  = 1.0f;
    int   rmIters       = 3;
    // Adaptive instead of plain (SEM_RemeshAdaptiveStandaloneSurface3D): the target
    // length above becomes the one for flat regions; curved regions get edges down
    // to rmMinMult (same unit), short enough that a facet sags from the input by at
    // most rmMaxDevMult, and edges turning by more than rmFeatureDeg are kept sharp.
    // It needs more sweeps than the plain remesh, hence its own count.
    bool  rmAdaptive      = false;
    float rmMinMult       = 0.3f;
    float rmMaxDevMult    = 0.05f;
    float rmFeatureDeg    = 40.0f;
    int   rmAdaptiveIters = 8;

    // The parting plane is shared by the body split and the two standalone modes
    // that need one, so the same add/select/remove controls serve all of them.
    bool UsesPartingPlane() const {
        return IsBody() || sessionMode == SESSION_TAPER || sessionMode == SESSION_REMESH;
    }

    // A body session additionally picks WHICH of its two jobs the window drives:
    // the split-and-stages chain, or a plain remesh of the closed surface. It is
    // a sub-mode rather than a sessionMode value, because the session stays a
    // body either way (its halves, its context kind and its polling depend on it).
    enum BodyMode { BODYMODE_SPLIT = 0, BODYMODE_REMESH = 1 };
    int bodyMode = BODYMODE_SPLIT;

    bool IsBody()    const { return sessionMode == SESSION_BODY; }
    bool BodyRemeshMode() const { return IsBody() && bodyMode == BODYMODE_REMESH; }
    bool BodySplit() const { return m_bodySplit; }
    // Enclosed volume of the closed source, or < 0 when unavailable.
    double BodyVolume() const;

    ClipPlaneNode* SplitPlaneNode() const { return m_splitNode; }
    ClipPlaneNode* AddSplitPlane(Scene& scene);
    void           RemoveSplitPlane(Scene& scene);

    // side >= 0 is the half in the plane's positive half-space.
    Primitive*  HalfPrim(int side) const;
    SemSession* HalfSession(int side) const;

    // Push the plane to the core and split. Hides the closed source, builds a
    // primitive per half under it (each in its own colour) and binds a hidden
    // session to each half's context. Re-splitting replaces both.
    bool ApplySplit(Scene& scene, bool silent);

    // Taper both halves to `taperAngle` and extend them by the skirt setting above,
    // in one core call. The core always tapers the pristine cut, so calling this
    // again with another angle rebuilds the halves rather than stacking a second
    // draft on them; the halves' pipeline stages are dropped because their source
    // geometry changed.
    bool ApplyTaper(Scene& scene, bool silent);
    bool TaperReport(int side, int& verts_moved, int& facets_violating,
                     double& min_draft_deg, double& contour_growth) const;
    // The per-facet draft check run right now against `angle_deg`, changing
    // nothing — so the same number can be read before a taper and after it.
    bool MeasureDraftNow(int side, double angle_deg,
                         int& facets_violating, double& min_draft_deg) const;

    // Whether the halves the core holds carry a skirt (set by the last split or
    // taper). With one, the halves' offsets and distances run on the extended
    // surfaces, while their domains still end on the parting plane.
    bool SkirtActive() const { return m_skirtActive; }
    // The core's setting and how deep each half's skirt reached.
    bool SkirtState(double& depth, double& reached_pos, double& reached_neg) const;
    // The skirt setting above as the core takes it: 0 = none, < 0 = down to the
    // other half's height.
    double SkirtDepthArg() const;

    // Glue the two halves into the preform (SEM_BuildBodyPreform3D) on the worker.
    // The result is shown as its own primitive under the body and written next to
    // the source as <stem>_preform.csv3d.
    bool ApplyPreform(Scene& scene, bool silent);
    // True when both halves have an extracted isosurface to glue.
    bool HalvesHaveIsosurfaces() const;
    // What the last preform came out as (SEM_GetBodyPreformReport3D). False when
    // there is none.
    bool PreformReport(double& volume, double& land_area, int& loops_pos, int& loops_neg,
                       int& crossings, int& open_edges) const;
    Primitive*         PreformPrim() const { return m_preformPrim; }
    const std::string& PreformPath() const { return m_preformPath; }

    // Overlay of the geometry of both halves that lies exactly on the parting
    // plane — the cut outline and any face the plane snapped flat.
    void ShowSplitOnPlane(Scene& scene, bool show);
    bool SplitOnPlaneShown() const { return m_splitOnPlaneShown; }

    // One pipeline stage on both halves, driven from this session. Phase 0 = idle,
    // 1 = the positive half is running, 2 = the negative one. PollBodyStage (called
    // once per frame by the workspace) applies each half's results and starts the
    // second one when the first finishes.
    void RecomputeBodyUpToAsync(Scene& scene, Stage to, bool silent);
    void PollBodyStage(Scene& scene);
    int  BodyPhase() const { return m_bodyPhase; }
    const SemSession* BodyRunningHalf() const;

    const std::string& SourcePath() const;
    Primitive*  SourcePrim()  const;
    SceneNode*  OffsetsNode() const;
    Primitive*  MeshPrim()    const;
    // The grid box of the domain as grey lines (3D), null without a domain.
    Primitive*  GridBoxPrim() const;
    // Whether a domain mesh is built (a body asks its positive half). Its primitive
    // can be absent while it is: the BC view of a domain with no fixed node draws
    // nothing.
    bool        MeshBuilt()   const;
    Primitive*  IsolinePrim() const;
    // The flat projection of a standalone offset-remesh (SESSION_OFFSET_REMESH);
    // null otherwise. See ShowIsosurfaceProjection3D below.
    Primitive*  IsoProjectionPrim() const;
    // Pseudonormal line overlay of the source surface, null when not shown.
    Primitive*  SrcNormalsPrim() const;
    Primitive*  SrcRevSurf()  const;
    Primitive*  IsoRevSurf()  const;
    bool        HasSource()   const;
    int         Dim()         const;
    bool        ThermalSolved() const;
    bool        HasIsolinePath() const;
    // True once a standalone offset-remesh has produced its flat projection
    // (SEM_GetIsosurfaceProjection3D), which gates ShowIsosurfaceProjection3D.
    bool        HasIsoProjection() const;

    const std::string& WorkDir() const { return m_workDir; }

    // --- SEM pipeline context ---------------------------------------------
    // Each session owns one SEM context (SEM_CreateContext): an independent copy
    // of the library's whole cache. That is what lets several setups be open at
    // once — switching between them only changes which context is active, so no
    // stage is ever dropped or recomputed. Allocated by Bind on the first real
    // source; 0 until then.
    int  Context() const { return m_ctx; }
    // Make this session's context current on the calling thread. Called before
    // anything that talks to the SEM library (per frame for the active session,
    // around PollAsync for every other one, and first thing on the worker thread).
    void Activate() const;
    // Give the context back to the library; the session must not be used against
    // SEM afterwards. Called when the setup is closed.
    void Release();

    // Session folders group pipeline products per source under %TEMP%/sem/, one
    // folder <stem>_<N> per state. Static so the import UI can enumerate/allocate
    // sessions before a source is bound.
    static std::string SessionRoot();
    static std::vector<std::string> ListSessions(const std::string& srcPath);
    static std::string NewSessionDir(const std::string& srcPath);

    void LoadSessionStages(Scene& scene);

    const Stats& SrcStats()  const;
    const Stats& OffStats()  const;
    const Stats& MeshStats() const;

    void Bind(Scene& scene, Primitive* prim, bool reload = false);
    void Unbind();
    void Validate(Scene& scene);

    // Bind this session to a SEM context someone else created and already loaded —
    // a body half (SEM_SplitBody3D). No SEM_LoadSurface3D is issued, so that
    // context's cache survives. `prim` is the half's scene primitive; its
    // semSourcePath is deliberately left empty so the workspace never mistakes a
    // half for a top-level setup.
    void BindExisting(Scene& scene, int ctx, Primitive* prim,
                      const std::string& srcPath, const std::string& workDir);
    // A half whose source geometry was replaced (by a taper): drop everything built
    // from the old geometry, mark every stage stale and point at the new primitive.
    void RebindHalf(Scene& scene, Primitive* prim);

    void MarkStageDirty(Stage st);
    void RecomputeUpTo(Scene& scene, Stage to, bool silent);

    // The levels the grid embeds, as signed distances from the deepest inner offset
    // to the outermost outer one (level k is [k + numInner]); empty without offsets.
    // A body asks its halves. Kept from the last read while a worker runs.
    const std::vector<double>& OffsetLevels(int& numInner);
    // A change of the BC levels: they go to the core at once and the domain is
    // redrawn by them (a body does both halves); the field is re-solved by the next
    // isosurface Apply.
    void ApplyThermalBC(Scene& scene);
    // The gap the shift scales - the distance between the BC levels - and the box
    // and step of the grid; false while there is none or a worker runs.
    bool IsoShiftGap(double& gap);
    bool GridInfo(XMFLOAT3& lo, XMFLOAT3& hi, double& step);

    SceneNode* StagePrim(Stage st) const;
    void ResetStage(Scene& scene, Stage st);

    bool ValidateRevolutionContour(Scene& scene);
    bool SetRevolutionMode(Scene& scene, bool enable);

    void ShowSourceRevolution(Scene& scene, bool show);
    void ShowIsolineRevolution(Scene& scene, bool show);
    void SetSrcRevAlpha(Scene& scene, float a);
    void SetIsoRevAlpha(Scene& scene, float a);

    void SetSurf3dAlpha(Scene& scene, float a);

    // Show/hide ONLY the source surface primitive (and its own "(edges)"
    // wireframe child), without cascading to the pipeline products parented
    // under it (offsets, mesh, isosurface, ...). Rendering is per-primitive, so
    // toggling the source's own `visible` flag leaves the children untouched.
    void ShowSource(Scene& scene, bool show);

    // --- Clip planes -----------------------------------------------------
    void SetClipPlanes3D(Scene& scene);
    void AutoApplyClipPlanes(Scene& scene);
    void ClearClipPlanes3D(Scene& scene);

    ClipPlaneNode* AddClipPlane(Scene& scene);
    // Recreate the visual clip-plane rectangles from the core's active clip planes
    // (SEM_GetClipPlanes3D). Call after SEM_LoadSession3D so the core and the scene
    // agree; without it a reloaded session clips the mesh but shows no planes.
    void LoadClipPlanesFromState(Scene& scene);
    void RemoveClipPlane(Scene& scene, int idx);
    ClipPlaneNode* FindClipPlaneByRect(Primitive* prim) const;

    void ShowClipMirror(Scene& scene, int planeIdx, bool show);
    void RebuildClipMirrors(Scene& scene);
    void DropClipMirrors(Scene& scene);
    bool AnyClipMirror() const;

    // Recreate the displayed source-surface primitive from the SEM core's current
    // active source (SEM_GetSourceSurface3D) — the geometry as the pipeline sees
    // it, i.e. subdivided and clip-snapped. The replacement keeps the source's
    // children (clip planes, offsets, mesh, ...), staging and gizmo selection.
    // 3D only; a no-op while a background compute is running. Called after a 3D
    // subdivide and, deferred to when no plane is being dragged, after a clip-plane
    // change (see SrcRebuildPending / AutoApplyClipPlanes).
    void RebuildSourcePrim(Scene& scene);
    bool SrcRebuildPending() const { return m_srcRebuildPending; }

    // Overlay of the vertices/edges/triangles that lie on the clip planes (a
    // vertex is "on a plane" when it lies on it exactly — the core has already
    // snapped on-plane geometry, so the test is strict). For every surface a
    // triangle with all three vertices on a single plane is drawn as a filled
    // face; an edge with both endpoints on the same plane (not part of such a
    // triangle) is drawn as a line; an on-plane vertex covered by neither is drawn
    // as a point. Built for three surfaces at once, each in its own colour: the
    // source surface (white), the source isosurface (orange) and the final
    // isosurface (cyan). RefreshClipOnPlane rebuilds the overlay in place when it
    // is shown (after a plane/tol/source/pipeline change). 3D only.
    void ShowClipOnPlane(Scene& scene, bool show);
    void RefreshClipOnPlane(Scene& scene);
    bool ClipOnPlaneShown() const { return m_clipOnPlaneShown; }

    // Overlay of the clip-plane geometry changes the core logged for one pipeline
    // stage category (SEM_GetClipChanges3D). For every record whose stage belongs to
    // `category` the snap displacements are drawn as short yellow segments (original
    // -> snapped vertex) and the removed geometry (the clipped-away sheet / dropped
    // coplanar caps) as a translucent red surface. Each category has its own checkbox
    // in the matching SEM window section. RefreshClipChanges rebuilds whichever
    // categories are shown (after a plane/pipeline change). 3D only.
    void ShowClipChanges(Scene& scene, int category, bool show);
    void RefreshClipChanges(Scene& scene);
    bool ClipChangesShown(int category) const {
        return category >= 0 && category < CLIPCHG_COUNT && m_clipChangesShown[category];
    }

    // Wall-clock duration (ms) of the most recent compute of each stage, or < 0
    // when the stage has not been computed this session.
    double OffsetsTimeMs() const { return m_offsetsMs; }
    double MeshTimeMs()    const { return m_meshMs; }
    double ThermalTimeMs() const { return m_thermalMs; }
    double IsoTimeMs()     const { return m_isoMs; }
    double TotalTimeMs()   const;

    void DropSrcRev(Scene& scene);
    void DropIsoRev(Scene& scene);

    bool ApplyThermal(Scene& scene, bool silent);
    bool ApplyIsoline(Scene& scene, bool silent);

    // Toggle the non-manifold highlight (isoShowNonManifold). Recolours the already
    // displayed sheet in place from the cached display data (no re-extraction) when
    // one exists; otherwise just stores the flag for the next run.
    void SetIsoNonManifold(Scene& scene, bool on);

    // Show/hide the SEM core's pseudonormals of the active source surface
    // (SEM_GetSourceSurface3D) as short yellow line segments. 3D only.
    bool ShowSourcePseudonormals(Scene& scene, bool show, bool silent);

    // Show/hide the flat base-plane re-meshed projection (with its wireframe edges)
    // of the last standalone offset-remesh (SEM_GetIsosurfaceProjection3D). Requires
    // HasIsoProjection(); show=false drops the primitive. 3D only.
    bool ShowIsosurfaceProjection3D(Scene& scene, bool show, bool silent);

    // BC or T-field drawing of the domain (bcView). A body sets both halves.
    void SetBCView(Scene& scene, bool on);

    // workDir picks the session folder to serialize into; empty => Bind allocates
    // a fresh <stem>_<N>. reload=true means the caller will follow up with
    // LoadSessionStages (SEM_LoadSession3D restores the core), so the destructive
    // SEM_LoadSurface3D in Bind is skipped for 3D — see Bind.
    Primitive* ImportSource(Scene& scene, const std::string& path,
                            const std::string& workDir = std::string(),
                            bool reload = false);

    bool ImportOffsets(Scene& scene, const std::string& dir);
    bool ImportMesh(Scene& scene, const std::string& path);

    // ======================================================================
    // Asynchronous 3D pipeline. The heavy 3D stages run on a worker thread so the
    // UI stays responsive; the worker only touches the SEM library (never the
    // Scene). PollAsync() loads the serialized results into the Scene on the main
    // thread, once per frame.
    // ======================================================================
    bool AsyncRunning() const;
    const char* AsyncStageName() const;
    float AsyncProgress() const;
    // Finer breakdown of the SEM call in flight: the phase running right now inside
    // the current stage (SEM_GetProgressStage, e.g. "Carving band tets") and that
    // phase's own [0,1] fraction (SEM_GetProgressStageFraction). AsyncSubStageName
    // returns nullptr/"" when the library reports no sub-stage. Meaningful only
    // while AsyncRunning().
    const char* AsyncSubStageName() const;
    float AsyncSubStageProgress() const;
    void RecomputeUpToAsync(Scene& scene, Stage to, bool silent);
    // SESSION_OFFSET_REMESH driver: offset-and-remesh the imported source surface
    // directly via the standalone SEM_OffsetRemeshInPlaneSurface3D (soAxisN/soOffset/
    // soMinOffset/soTargetMult/soIters), applying the current clip planes, on the worker
    // thread (the SEM call reports progress) like the pipeline. Displays the result in
    // the isosurface slot; does NOT touch the offsets/mesh/thermal caches, but its flat
    // projection becomes available (HasIsoProjection). 3D only. See PollAsync.
    void ApplyStandaloneOffsetRemeshAsync(Scene& scene, bool silent);
    // SESSION_TAPER / SESSION_REMESH drivers, same shape as the one above: the
    // imported surface is copied out of the core on the main thread and handed to
    // the worker as raw arrays, and the result is displayed in the isosurface
    // slot. Both need the parting plane; the remesh uses it only as a constraint
    // unless it also has to cut. 3D only.
    void ApplyStandaloneTaperAsync(Scene& scene, bool silent);
    void ApplyStandaloneRemeshAsync(Scene& scene, bool silent);
    // What the last SESSION_TAPER run did, same fields as the body taper's
    // report. False before one has run.
    bool StandaloneTaperReport(int& verts_moved, int& facets_violating,
                               double& min_draft_deg, double& contour_growth) const;
    // What the last adaptive remesh kept and produced: the input's sharp edges and
    // corners, and the result's edge-length range. False before one has run.
    bool AdaptiveRemeshReport(int& crease_edges, int& corners,
                              double& min_edge, double& max_edge) const;
    // Where the last standalone result was written: "<source folder>/<stem>_<mode>
    // .csv3d", or the session folder when the source folder refuses the write.
    // Nothing else serializes a standalone run, so that file IS the product.
    // Empty until one finishes; cleared when the next one starts.
    const std::string& ResultPath() const { return m_resultPath; }
    // Open Explorer on the result folder with the file selected. False when there
    // is no result, or it has since been deleted.
    bool RevealResult() const;
    // The same for any file.
    static bool RevealFile(const std::string& path);
    // Write the standalone result currently in m_isoData next to the source as
    // "<stem><tag>.csv3d". Returns the path written, empty when it could not be.
    std::string SaveStandaloneResult(const char* tag);
    // The same for any surface: next to the source, else the session folder.
    std::string SaveNextToSource(const char* tag, const CSV3DLoader::CSV3DData& data);
    void PollAsync(Scene& scene);
    void CancelAsync();
    bool AsyncCancelRequested() const;

private:
    int         m_ctx = 0;
    std::string m_srcPath;
    std::string m_meshPath;
    std::string m_isolinePath;
    std::string m_resultPath;
    std::string m_workDir;
    Primitive*  m_srcPrim = nullptr;
    SceneNode*  m_offsets = nullptr;
    Primitive*  m_mesh    = nullptr;
    Primitive*  m_isoline = nullptr;
    // The flat projection of a standalone offset-remesh (cache-only, never serialized),
    // and whether the cache holds one.
    Primitive*  m_isoProj   = nullptr;
    bool        m_isoProjected = false;
    Primitive*  m_srcRevSurf = nullptr;
    Primitive*  m_isoRevSurf = nullptr;
    // Pseudonormal line overlay of the source (vertex normals drawn as segments).
    Primitive*  m_srcNormals = nullptr;
    // Copy of the geometry currently displayed as the isosurface, kept so the
    // non-manifold highlight can recolour it without re-extracting.
    CSV3DLoader::CSV3DData m_isoData;
    // The 3D domain mesh as last read from the core, kept so its drawing can be
    // rebuilt (BC / T field, new BC levels) without reading it again: the node
    // positions and solved T, and the unique edges of its tets (a << 32 | b).
    std::vector<XMFLOAT3>           m_meshPos;
    std::vector<float>              m_meshT;
    std::vector<unsigned long long> m_meshEdges;
    Primitive*                      m_gridBox = nullptr;
    bool        m_thermalSolved = false;
    Stats m_srcStats, m_offStats, m_meshStats;

    // Set when a clip-plane change baked new snapped source geometry but a plane is
    // still being dragged/edited; the source primitive is rebuilt once the edit
    // settles (DrawClipPlanesSection / AutoApplyClipPlanes), so the gizmo is not
    // yanked mid-drag.
    bool        m_srcRebuildPending = false;

    // "Geometry on clip planes" overlay. One group node owns, per surface (source
    // surface / source isosurface / final isosurface), up to a filled-triangle
    // child (planar tris), a line-list child (on-plane edges) and a point-cloud
    // child (isolated on-plane verts).
    bool        m_clipOnPlaneShown = false;
    SceneNode*  m_onPlaneGroup = nullptr;

    // Per-category clip-change overlay: one group node owns the snap-displacement
    // lines and removed-geometry surfaces logged for that stage category.
    bool        m_clipChangesShown[CLIPCHG_COUNT] = { false };
    SceneNode*  m_clipChangeGroup[CLIPCHG_COUNT]  = { nullptr };

    // Stage compute durations in ms; < 0 means "not measured this session".
    double m_offsetsMs = -1.0;
    double m_meshMs    = -1.0;
    double m_thermalMs = -1.0;
    double m_isoMs     = -1.0;

    // Per-stage staleness, indexed by Stage. Bind resets all to true.
    bool        m_dirty[5] = { true, true, true, true, true };

    // ---- Body session state (SESSION_BODY only) ---------------------------
    // Set on the two hidden sessions a body owns. A half is NOT a setup: its
    // primitive must never carry semSourcePath/semContext and must never be
    // staged, or the workspace would adopt it as a top-level setup and reload its
    // source into a fresh context, wiping the cache it just computed into.
    bool           m_isHalf    = false;
    bool           m_bodySplit = false;
    bool           m_skirtActive = false;  // the core extends the halves
    // The 3D offsets have no primitives: they are levels of the grid in the core.
    bool                m_offsetsReady = false;
    std::vector<double> m_levels;            // last OffsetLevels, while a worker runs
    int                 m_levelsInner  = 0;
    // A half's own look, kept so that rebuilding its primitive (after a subdivide)
    // gives back the same tint: which side it is, the parting plane (normal toward
    // the positive side), and whether its skirt is tinted apart.
    bool           m_halfStyled = false;
    int            m_halfSide   = 0;
    XMFLOAT4       m_halfPlane  = XMFLOAT4(0.0f, 0.0f, 1.0f, 0.0f);
    bool           m_halfSkirt  = false;
    ClipPlaneNode* m_splitNode  = nullptr;   // the parting plane's visual rectangle
    SceneNode*     m_splitGroup = nullptr;
    Primitive*     m_halfPrim[2] = { nullptr, nullptr };   // 0 = positive side
    SemSession*    m_half[2]     = { nullptr, nullptr };   // owned; destroyed with us
    CSV3DLoader::CSV3DData m_halfData[2];
    bool           m_splitOnPlaneShown = false;
    SceneNode*     m_splitOnPlaneGroup = nullptr;
    int            m_bodyPhase  = 0;
    Stage          m_bodyStage  = STAGE_OFFSETS;
    bool           m_bodySilent = false;

    void DropHalves(Scene& scene);
    void BuildHalfPrim(Scene& scene, int side);
    // A half's surface primitive in that half's colour. With `skirt`, the nodes on
    // the far side of the parting plane are tinted lighter, so the extension reads
    // apart from the half itself.
    Primitive* AddHalfSurface(Scene& scene, CSV3DLoader::CSV3DData data, const std::string& name,
                              SceneNode* parent, int side, const XMFLOAT4& plane, bool skirt);
    // Re-read both halves from their contexts (what their pipelines actually run
    // on) and rebuild their primitives and stage state.
    void RefreshHalfPrims(Scene& scene);
    // Give one half's context the parting plane as its clip plane, oriented so the
    // kept half-space is that half's own side, when it carries no skirt: its source
    // and offsets are then cut flush at the plane too. (Its domain and isosurface
    // end on the plane either way - the core sets that up for every half.)
    // Re-applied after a taper, because republishing a half reloads its source and
    // clears its clip planes.
    void PushSplitClipPlane(int side);
    void StartBodyHalf(Scene& scene, int side);
    // Copy the shared stage knobs (and the staleness flags the UI set here) into
    // both halves before a run.
    void PushStageParams();
    void BuildSplitOnPlane(Scene& scene);
    void DropSplitOnPlane(Scene& scene);
    // Rebuild both half primitives from the freshly tapered geometry. Called from
    // PollAsync once the body-taper worker is done, never from the worker itself:
    // it touches the Scene.
    void FinishBodyTaper(Scene& scene);
    // Show the preform the worker just built (main thread, after the join).
    void FinishBodyPreform(Scene& scene);
    void DropPreform(Scene& scene);
    Primitive*             m_preformPrim = nullptr;
    CSV3DLoader::CSV3DData m_preformData;
    std::string            m_preformPath;

    // ---- Asynchronous 3D pipeline state -----------------------------------
    // The atomics are the only members the worker and UI thread touch
    // concurrently; the plan/parameter snapshot is written before the thread
    // starts and read only by the worker, the output paths only after join().
    struct AsyncJob {
        std::thread        worker;
        std::atomic<bool>  running{ false };
        std::atomic<bool>  done{ false };          // worker finished; results pending apply
        std::atomic<bool>  ok{ false };
        std::atomic<bool>  cancel{ false };        // UI requested stop; skip queued stages
        std::atomic<bool>  cancelled{ false };     // worker stopped early on that request
        std::atomic<int>   stageKind{ 0 };         // label: 0 offsets, 1 mesh, 2 thermal, 3 isosurface, ... 8 preform
        std::atomic<int>   progressStage{ 0 };     // index of current stage among the planned ones
        std::atomic<int>   totalStages{ 1 };       // number of progress-weighted stages planned

        // Which heavy stages this run executes (planned on the main thread). The
        // thermal solve and the isosurface extraction are separate progress-
        // reporting stages (runThermal solves, runIso extracts).
        bool   runOffsets = false, runMesh = false, runThermal = false, runIso = false;
        // SESSION_OFFSET_REMESH: offset-and-remesh a surface passed in as raw arrays
        // (SEM_OffsetRemeshInPlaneSurface3D). Set only by ApplyStandaloneOffsetRemeshAsync,
        // never alongside the run* stages above. soXyz/soTris are the input geometry copy.
        bool   runStandalone = false;
        // The other two standalone modes; never set alongside each other.
        bool   runTaper  = false;
        bool   runRemesh = false;
        // A body session's own stage: SEM_ApplyBodyTaper3D over both halves (and
        // their post-taper remesh sweeps). It runs on this worker rather than on
        // the halves', because the core drives both halves inside one call.
        bool   runBodyTaper = false;
        double taperMs = -1.0;
        // The body's other own stage: SEM_BuildBodyPreform3D, gluing the halves.
        bool   runBodyPreform = false;
        int    pfSource = 0;
        double preformMs = -1.0;
        SEM_Plane3D         tpPlane = { { 0, 0, 1 }, 0.0 };
        double              tpTol = 0.01, tpAngle = 7.0;
        int                 tpRemeshIters = 2;
        // The body taper's skirt, as SEM_ApplyBodyTaperSkirt3D takes it.
        double              skDepth = 0.0, skGrowth = 0.0;
        double              rmTargetMult = 1.0;
        int                 rmIters = 3;
        bool                rmAdaptive = false;
        double              rmMinMult = 0.3, rmMaxDevMult = 0.05, rmFeatureDeg = 40.0;
        SEM_Vec3            soAxisN = { 0.0, 0.0, 1.0 };
        double              soOffset = 0.0, soMinOffset = 0.0, soTargetMult = 1.0;
        int                 soIters = 3;
        std::vector<double> soXyz;
        std::vector<int>    soTris;
        // Visibility to apply to each produced primitive (snapshot of *Enabled).
        bool   offVisible = false, meshVisible = false, isoVisible = false;

        // Parameter snapshot (written before launch, read only by the worker).
        int                 offsetMode = OFFSET_EVEN;
        double              firstGap = 1.0, grading = 1.2;
        int                 numOffsets = 8;
        std::vector<double> gaps;
        int                 tetMethod = SEM_TET_BAND;
        double              tetParam = -1.0;
        double              tetMaxEdgeLen = 0.0;
        double              isoValue = 0.05;
        float               maxInward = 1.0f;
        bool                innerEnabled = false;
        double              innerFirstGap = 1.0, innerGrading = 1.0;
        int                 innerNum = 0;
        SEM_Vec3            gridBoxScale = { 1.5, 1.5, 1.5 };
        double              gridStep = 0.0;
        int                 bcFrom = 0, bcTo = SEM_LEVEL_OUTERMOST;
        bool                isoShift = true;

        // Deterministic output paths the SEM core writes (computed at launch).
        // expIsoRemesh tags a standalone result.
        std::string         expOffsets, expMesh, expIso, expIsoRemesh;
        // Worker outputs (read by the main thread after join()).
        std::string         offsetsPath, meshPath, isoPath, error;
        // Isosurface geometry copied off the SEM cache on the worker thread (so the
        // main thread neither re-reads a file nor races the cache lifetime).
        CSV3DLoader::CSV3DData isoDisplayData;
        // Manifold-repair counts of a standalone offset-remesh result (SEM_GetLast-
        // ManifoldRepair3D). -1 = not queried / not a standalone run.
        int                 nmEdges = -1, nmBowties = -1, nmVertsAdded = -1, nmTrisRemoved = -1;
        // Interior holes closed on that same result (SEM_GetLastHoleFill3D). -1 = not
        // queried / not a standalone run.
        int                 nmHolesFilled = -1;
        // Measured durations (ms) of each heavy stage; < 0 when not part of this run.
        double              offsetsMs = -1.0, meshMs = -1.0, thermalMs = -1.0, isoMs = -1.0;

        ~AsyncJob() { if (worker.joinable()) worker.join(); }
    };
    AsyncJob m_job;
    mutable std::atomic<float> m_progressShown{ 0.0f };

    void PipelineWorkerBody();
    void Fail(const std::string& msg);
    // Shared setup for the three standalone drivers, and the parting plane read
    // off its visual rectangle in the form the core takes.
    bool BeginStandaloneJob(Scene& scene, bool silent, int stageKind);
    bool PartingPlaneForJob(SEM_Plane3D& out) const;

    // Full path the SEM core writes for a pipeline product: working dir + source
    // stem + a fixed suffix.
    std::string OutPath(const char* suffix) const;

    bool Alive(Scene& scene, Primitive* q) const;
    bool Alive(Scene& scene, SceneNode* q) const;
    void Report(Scene& scene, bool silent, const std::string& msg);
    bool CheckRc(Scene& scene, bool silent, const char* call, int rc,
                 std::initializer_list<const char*> errs);
    SceneNode* AttachParent();

    void ConfigureSurface3D(Primitive* p);

    void BuildSourceRevolution(Scene& scene);
    void BuildIsolineRevolution(Scene& scene);

    // Single grouping node that owns every clip-plane node, so the planes form
    // one collapsible subtree under the source instead of N siblings. Created
    // lazily by ClipGroup() and torn down with DropClipPlaneNodes.
    SceneNode* m_clipGroup = nullptr;
    SceneNode* ClipGroup(Scene& scene);

    void BuildClipPlaneRect(Scene& scene, ClipPlaneNode* node, const XMFLOAT4& plane);
    void DropClipPlaneNodes(Scene& scene);

    void DropClipOnPlane(Scene& scene);
    void BuildClipOnPlane(Scene& scene);
    // Build/drop the clip-change overlay of one stage category. BuildClipChanges
    // fetches SEM_GetClipChanges3D, keeps the records whose stage maps to `category`
    // (ClipChangeCategoryOf), and draws their snap segments and removed geometry.
    void BuildClipChanges(Scene& scene, int category);
    void DropClipChanges(Scene& scene, int category);
    // Add one surface's on-plane overlay (filled planar triangles, on-plane edges
    // and isolated on-plane points, all in `color`) under `grp`. `tol` is the
    // strict on-plane band; `lift` nudges the filled triangles off the surface
    // along the plane normal to avoid z-fighting. `tag` disambiguates the child
    // primitive names. Returns true if anything was added.
    bool BuildOnPlaneOverlay(Scene& scene, SceneNode* grp,
                             const CSV3DLoader::CSV3DData& data,
                             const std::vector<XMFLOAT4>& planes,
                             float tol, float lift, const XMFLOAT4& color, const char* tag);
    // Last (normal, d) set pushed to the core, so AutoApplyClipPlanes only
    // re-pushes when a rectangle actually moved.
    std::vector<XMFLOAT4> m_appliedClipPlanes;
    bool ClipPlanesChanged() const;

    void DropOffsets(Scene& scene);
    void DropMesh(Scene& scene);
    void DropIsoline(Scene& scene);
    void DropIsoProjection(Scene& scene);
    void DropSrcNormals(Scene& scene);
    void ReloadMeshColored(Scene& scene);
    // 3D domain drawing. TakeMeshData keeps the positions and T of a freshly read
    // mesh (and its edges when `topology` is set); RebuildMeshDisplay redraws it in
    // the current mode (bcView, or the BCs while the field is stale), keeping the
    // primitive's visibility; RebuildGridBox redraws the grid box.
    void TakeMeshData(const CSV3DLoader::CSV3DData& data, bool topology);
    void RebuildMeshDisplay(Scene& scene);
    void RebuildGridBox(Scene& scene);

    // Assign line style `style` (a Scene LineStyleId) to every thick-line (dim==1)
    // primitive in p's subtree — used to give the SEM overlays their thickness tier
    // (iso loops thickest, on-plane lines, normals, mesh edges thinnest).
    static void StyleLines(Primitive* p, int style);

    // Build the displayed isosurface primitive from `data`, honouring
    // isoShowNonManifold (bright-magenta defect colouring) or the plain green
    // surface, named and surface-configured. Does not touch m_isoline/m_isoData.
    Primitive* BuildIsoDisplay(Scene& scene, const CSV3DLoader::CSV3DData& data);
    // Recolour the already displayed iso sheet in place from m_isoData (no
    // re-extraction), preserving visibility and dependent overlays.
    void RebuildIsoDisplayInPlace(Scene& scene);
    // Build a line-list primitive of vertex normals for the surface in `data`,
    // parented under `parent` and drawn in `color`. The explicit-normals overload
    // draws the supplied per-vertex normals (e.g. the SEM core's, via ViewNormals);
    // the other computes angle-weighted pseudonormals from the geometry.
    Primitive* BuildPseudonormalLines(Scene& scene, const CSV3DLoader::CSV3DData& data,
                                      const std::vector<XMFLOAT3>& normals,
                                      const XMFLOAT4& color, const std::string& name,
                                      SceneNode* parent);
    Primitive* BuildPseudonormalLines(Scene& scene, const CSV3DLoader::CSV3DData& data,
                                      const XMFLOAT4& color, const std::string& name,
                                      SceneNode* parent);

    bool FetchMeshData(Scene& scene, bool silent, CSV3DLoader::CSV3DData& out);
    bool FetchIsoData(Scene& scene, bool silent, CSV3DLoader::CSV3DData& out);
    bool FetchIsoProjData(Scene& scene, bool silent, CSV3DLoader::CSV3DData& out);

    bool LoadOffsetShells(Scene& scene, bool silent, const std::string& dir = std::string());
    void CleanupOffsetFiles();

    // 3D session reload (SEM_LoadSession3D + scene rebuild) and the inverse mapping
    // of the restored SEM pipeline arguments back into this session's UI fields.
    void LoadSessionStages3D(Scene& scene);
    void ApplyPipelineArgs3D();

    bool ApplySubdivide(Scene& scene, bool silent);
    bool ApplyOffsets(Scene& scene, bool silent);
    bool ApplyMesh(Scene& scene, bool silent);
};

}
