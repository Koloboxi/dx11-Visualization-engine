#pragma once
#include "scene.h"
#include "scene_service.h"
#include "../../external/sem_exports.h"
#include <string>
#include <vector>
#include <initializer_list>
#include <thread>
#include <atomic>

namespace SemSessionNS {

enum Stage { STAGE_SUBDIVIDE = 0, STAGE_OFFSETS = 1, STAGE_MESH = 2, STAGE_THERMAL = 3 };
enum OffsetMode { OFFSET_EVEN = 0, OFFSET_GAPS = 1 };

struct Stats {
    bool valid    = false;
    int  verts    = 0;
    int  edges    = 0;
    int  tris     = 0;
    int  boundary = 0;
};

class SemSession {
public:
    // Pipeline dimension of the staged source, auto-detected on Bind:
    //   2 = contour pipeline (SEM_LoadCSV3D, SEM_*),
    //   3 = triangle-surface pipeline (SEM_LoadSurface3D, SEM_*3D).
    int   dim        = 2;

    int   subMode    = 0;
    int   subN       = 2;
    bool  subEnabled = true;

    int   offsetMode = OFFSET_EVEN;
    float firstGap   = 1.0f;
    int   numOffsets = 8;
    float grading    = 1.2f;
    std::vector<float> gaps = { 25.0f, 25.0f, 25.0f };
    bool  offEnabled = true;

    // 2D mesh tuning — only the free grid CDT mesher remains (SEM_STEINER_GRID).
    // meshParam is the interior Steiner-point spacing (<0 => source avg edge
    // length); steinerMargin is the minimum spacing from offset lines as a
    // multiple of meshParam. meshParamEdgeUnits expresses the spacing in
    // multiples of the source contour's mean edge length instead of model units.
    int   meshMethod    = SEM_STEINER_GRID;
    float meshParam     = -1.0f;
    float steinerMargin = 0.45f;
    bool  meshEnabled   = true;
    bool  meshParamEdgeUnits = false;

    // 3D (surface) mesh tuning via SEM_BuildMesh3D. tetMethod picks the
    // tetrahedralization variant (SEM_TetMethod): SEM_TET_BAND or
    // SEM_TET_LAYERED. tetParam is its primary knob (<0 => per-method auto):
    // BAND => Steiner grid cell size (a length); LAYERED => use_sdf flag (0/1).
    // tetParamEdgeUnits expresses the BAND cell size in multiples of the source
    // surface's mean edge length instead of model units (ignored for LAYERED).
    int   tetMethod    = SEM_TET_LAYERED;
    float tetParam     = 0.0f;
    bool  tetParamEdgeUnits = false;
    // Max tet edge length filter, in multiples of the source surface's mean edge
    // length (see SEM_MeshParams3D::max_edge_len). <= 0 disables the filter.
    float tetMaxEdgeLen = 10.0f;
    // SEM_TET_LAYERED + use_sdf=0 only: carving layer-span (SEM_MeshParams3D::
    // layer_span). A tet is kept when the spread of its vertices' layer indices
    // (max - min) is <= this. <= 0 => 1. Ignored by SEM_TET_BAND and by LAYERED
    // with use_sdf=1.
    int   tetLayerSpan = 1;

    bool  thermalEnabled = true;
    float isoValue   = 0.8f;
    // Mesh colouring after a thermal solve. false = "T field" (blue..red gradient
    // over the solved temperature); true = "BC" (only the Dirichlet boundary
    // nodes are tinted blue for T=0 and red for T=1, every interior node light
    // grey). Toggled live from the SEM window via SetBCView.
    bool  bcView     = false;
    // Max inward penetration (0..1) passed to SEM_SolveThermal3D. Only consulted
    // when useSourceSdf is on.
    float maxInward  = 0.04f;
    // SEM_SolveThermal3D use_source_sdf flag (0/1). 0 = keep every outermost-offset
    // node as a T=0 BC by tag alone (maxInward ignored); 1 = evaluate the source
    // signed distance on those nodes and drop self-intersecting ones via maxInward.
    int   useSourceSdf = 1;

    bool  subAuto     = false;
    bool  offAuto     = false;
    bool  meshAuto    = false;
    bool  thermalAuto = false;

    // --- Clip planes (3D pipeline) ---------------------------------------
    // User-defined clip half-spaces, edited from the SEM window and pushed to the
    // SEM core via SEM_SetClipPlanes3D. Each plane is (xyz = normal, w = d) in
    // nx*x + ny*y + nz*z + d >= 0; the normal points INTO the kept half-space.
    // They are shown on the scene as soft translucent rectangles lying on each
    // plane, sized to the source surface's bounding box: the kept side is soft
    // red, the removed side soft blue. Each plane is a ClipPlaneNode service
    // object whose rectangle is moved/rotated with the orientation transformer;
    // the (normal, d) is read back from that rectangle's transform on Apply.
    std::vector<ClipPlaneNode*> clipPlaneNodes;

    bool  revolutionMode = false;
    int   revSegments    = 48;
    float srcRevAlpha    = 0.8f;
    float isoRevAlpha    = 0.5f;

    float surf3dAlpha    = 1.f;

    // Whether a 3D source surface is a closed manifold enclosing a volume; passed
    // to SEM_LoadSurface3D on Bind (and used for SEM_GetSourceVolume3D).
    bool  srcClosed3D    = true;

    char  status[256] = "Ready";

    // SEM_SetRevolution axis selector: 1 = X, 2 = Y, 3 = Z. Profiles revolve
    // around the Y axis here.
    static constexpr int kRevolutionAxisY = 2;

    const std::string& SourcePath() const;
    Primitive*  SourcePrim()  const;
    SceneNode*  OffsetsNode() const;
    Primitive*  MeshPrim()    const;
    Primitive*  IsolinePrim() const;
    Primitive*  SrcRevSurf()  const;
    Primitive*  IsoRevSurf()  const;
    bool        HasSource()   const;
    int         Dim()         const;
    bool        ThermalSolved() const;
    bool        HasIsolinePath() const;

    // Directory the SEM core serializes pipeline products into: the source's
    // per-session folder (%TEMP%/sem/<stem>_<N>/), set on Bind. Empty until a
    // source is bound.
    const std::string& WorkDir() const { return m_workDir; }

    // --- Session folders -------------------------------------------------
    // Pipeline products are grouped per source under %TEMP%/sem/. Each "session"
    // is a folder <stem>_<N> (N = 1,2,3...) holding one pipeline state's csv3d
    // files. Re-importing a source can either reload an existing session or start
    // a new one. These helpers are static so the import UI can enumerate/allocate
    // sessions before a source is bound.
    static std::string SessionRoot();
    // Existing session folders for the given source path, full paths, sorted by N.
    static std::vector<std::string> ListSessions(const std::string& srcPath);
    // Next free session folder path (<stem>_<maxN+1>); not created here (Bind does).
    static std::string NewSessionDir(const std::string& srcPath);

    // Restore the whole currently-bound session folder: reload any serialized
    // offset shells and mesh found in m_workDir (delegates to ImportOffsets /
    // ImportMesh). Absent stages are skipped; thermal is left unsolved.
    void LoadSessionStages(Scene& scene);

    double MeshParamFactor() const;
    double TetParamFactor() const;

    const Stats& SrcStats()  const;
    const Stats& OffStats()  const;
    const Stats& MeshStats() const;

    void Bind(Scene& scene, Primitive* prim);
    void Unbind();
    void Validate(Scene& scene);

    // Mark a stage's result stale because its own parameters changed. The next
    // Apply (or auto-apply) of this or any later stage will recompute it.
    void MarkStageDirty(Stage st);

    // Bring the pipeline up to (and including) stage 'to', and no further. Each
    // stage is recomputed only when its own parameters changed since it was last
    // applied (m_dirty), or when an upstream stage was just rebuilt this pass and
    // so invalidated its input.
    void RecomputeUpTo(Scene& scene, Stage to, bool silent);

    SceneNode* StagePrim(Stage st) const;
    void SetStageVisible(Scene& scene, Stage st, bool show);
    void ResetStage(Scene& scene, Stage st);

    bool ValidateRevolutionContour(Scene& scene);

    // Enable/disable revolution mode and keep the SEM core in sync via
    // SEM_SetRevolution. When enabling, the staged contour is validated first
    // and the core's own checks (endpoints on axis, no crossing) are surfaced.
    // Returns the resulting revolutionMode state.
    bool SetRevolutionMode(Scene& scene, bool enable);

    void ShowSourceRevolution(Scene& scene, bool show);
    void ShowIsolineRevolution(Scene& scene, bool show);
    void SetSrcRevAlpha(Scene& scene, float a);
    void SetIsoRevAlpha(Scene& scene, float a);

    // Apply the 3D surface opacity to every currently-built 3D pipeline surface.
    void SetSurf3dAlpha(Scene& scene, float a);

    // --- Clip planes -----------------------------------------------------
    // Read the (normal, d) of every ClipPlaneNode and push them to the SEM core
    // (SEM_SetClipPlanes3D). Clipping is applied during meshing, so this
    // invalidates the mesh and everything downstream; mirror copies are refreshed.
    void SetClipPlanes3D(Scene& scene);
    // Re-push the planes to the core whenever the rectangles change (called every
    // frame from the SEM window); no-op while the last-applied set is unchanged.
    void AutoApplyClipPlanes(Scene& scene);
    // Clear every clip plane in the core (SEM_ClearClipPlanes3D), remove every
    // plane node + mirror and invalidate the mesh.
    void ClearClipPlanes3D(Scene& scene);

    // Create a new clip-plane node (default normal +X through the source bbox
    // centre) and its movable rectangle, parented to the source. Remove one by
    // index (drops the node, its rectangle and any mirrors that referenced it).
    ClipPlaneNode* AddClipPlane(Scene& scene);
    void RemoveClipPlane(Scene& scene, int idx);
    // True when prim is the rectangle of one of our clip-plane nodes; returns it.
    ClipPlaneNode* FindClipPlaneByRect(Primitive* prim) const;

    // --- Clip-plane mirror copies ----------------------------------------
    // Toggle the mirror copies for a single clip plane and rebuild. Drop and
    // rebuild every mirror copy from the current plane nodes and the live
    // source/isotherm. With several enabled planes the reflections compose: a
    // later plane also mirrors the copies an earlier plane produced (the
    // reflection orbit), so two planes give the 3 images A, B and B∘A.
    void ShowClipMirror(Scene& scene, int planeIdx, bool show);
    void RebuildClipMirrors(Scene& scene);
    void DropClipMirrors(Scene& scene);
    bool AnyClipMirror() const;

    // --- Stage timing ----------------------------------------------------
    // Wall-clock duration (ms) of the most recent compute of each stage, or < 0
    // when the stage has not been computed this session. Total is the sum of the
    // measured stages; it accumulates as the offsets, mesh and thermal stages are
    // computed in turn. Shown in the SEM window beside each stage header.
    double OffsetsTimeMs() const { return m_offsetsMs; }
    double MeshTimeMs()    const { return m_meshMs; }
    double ThermalTimeMs() const { return m_thermalMs; }
    double TotalTimeMs()   const;

    void DropSrcRev(Scene& scene);
    void DropIsoRev(Scene& scene);

    bool ApplyThermalStage(Scene& scene, bool silent);
    bool ApplyThermal(Scene& scene, bool silent);
    bool ApplyIsoline(Scene& scene, bool silent);

    // Switch the solved mesh between the T-field gradient and the BC view (see
    // bcView). Re-colours the existing mesh in place (no re-solve); a no-op when
    // the value is unchanged or no solved mesh is present.
    void SetBCView(Scene& scene, bool on);

    // Import a .csv3d as the staged pipeline source. workDir picks the session
    // folder to serialize into; empty => Bind allocates a fresh <stem>_<N>.
    Primitive* ImportSource(Scene& scene, const std::string& path,
                            const std::string& workDir = std::string());

    // Reload previously serialized pipeline stages into the SEM cache instead of
    // recomputing them (SEM_LoadOffsets[3D] / SEM_LoadMesh[3D]). The staged source
    // must already be bound (Bind ran SEM_LoadCSV3D / SEM_LoadSurface3D). The
    // reloaded stage and every stage upstream of it are marked clean so a later
    // Apply does not recompute over the imported cache; downstream stages are
    // marked stale. ImportOffsets reads the <stem>_offset[3d]_<i>.csv3d shells
    // from `dir`; ImportMesh reads a single <..>_mesh[3d].csv3d file.
    bool ImportOffsets(Scene& scene, const std::string& dir);
    bool ImportMesh(Scene& scene, const std::string& path);

    void RunFullPipeline(Scene& scene);

    // ======================================================================
    // Asynchronous 3D pipeline.
    //
    // The heavy 3D compute stages — SEM_ComputeOffsets3D, SEM_BuildMesh3D and
    // SEM_SolveThermal3D (plus the quick isosurface extraction) — run on a
    // worker thread so the UI thread stays responsive and can drive a progress
    // bar from SEM_GetProgress(). The worker only ever touches the SEM library
    // (compute; the core auto-serializes to the working dir); it never mutates
    // the Scene. The serialized result files are loaded into the Scene on the
    // main thread by PollAsync(), which the SEM window calls once per frame.
    // ======================================================================

    // True while the worker thread is computing. The window uses this to show
    // the progress bar and disable the controls.
    bool AsyncRunning() const;

    // Short label for the stage currently executing on the worker.
    const char* AsyncStageName() const;

    // Overall progress in [0,1] across the stages actually planned for this run.
    float AsyncProgress() const;

    // Asynchronous counterpart of RecomputeUpTo(). For a 3D source it plans which
    // heavy stages must run (identical dirty / cascade / enabled rules), runs the
    // quick subdivide synchronously, then launches the worker for the planned
    // stages; PollAsync() applies the results. For a 2D source it delegates to
    // the synchronous RecomputeUpTo(). A no-op while a previous run is in flight.
    void RecomputeUpToAsync(Scene& scene, Stage to, bool silent);

    // Called once per frame on the main thread. When the worker has finished,
    // joins it and loads whichever stages it produced into the scene, honoring
    // the per-stage visibility snapshot. No-op while the worker runs or is idle.
    void PollAsync(Scene& scene);

    // Request cancellation of the running job. SEM library calls cannot be
    // interrupted mid-call, so the stage executing right now runs to completion;
    // every stage still queued after it is skipped and the worker stops at the
    // next stage boundary. Stages that already finished keep their results.
    void CancelAsync();
    // True once CancelAsync was requested for the still-running job (the window
    // shows "cancelling" and hides the Cancel button).
    bool AsyncCancelRequested() const;

private:
    std::string m_srcPath;
    std::string m_meshPath;      // last serialized mesh file (for regeneration)
    std::string m_isolinePath;
    std::string m_workDir;       // directory the SEM core serializes into
    Primitive*  m_srcPrim = nullptr;
    SceneNode*  m_offsets = nullptr;   // empty "offsets" group holding the shells
    Primitive*  m_mesh    = nullptr;
    Primitive*  m_isoline = nullptr;
    Primitive*  m_srcRevSurf = nullptr;
    Primitive*  m_isoRevSurf = nullptr;
    bool        m_thermalSolved = false;
    Stats m_srcStats, m_offStats, m_meshStats;

    // Stage compute durations in ms; < 0 means "not measured this session".
    // Reset on Bind (new source); updated by the sync Apply* paths and copied
    // from the worker by PollAsync. See OffsetsTimeMs/MeshTimeMs/ThermalTimeMs.
    double m_offsetsMs = -1.0;
    double m_meshMs    = -1.0;
    double m_thermalMs = -1.0;

    // Per-stage staleness, indexed by Stage. A stage is dirty when its own
    // parameters changed since it was last applied, or when it has never been
    // applied against the currently loaded source (Bind resets all to true).
    bool        m_dirty[4] = { true, true, true, true };

    // ---- Asynchronous 3D pipeline state -----------------------------------
    // Worker thread + cross-thread state. The atomics are the only members the
    // worker and UI thread touch concurrently; the plan/parameter snapshot is
    // written by the launcher before the thread starts and read only by the
    // worker, and the output paths are written by the worker and read only after
    // join().
    struct AsyncJob {
        std::thread        worker;
        std::atomic<bool>  running{ false };       // worker thread alive
        std::atomic<bool>  done{ false };          // worker finished; results pending apply
        std::atomic<bool>  ok{ false };            // worker succeeded
        std::atomic<bool>  cancel{ false };        // UI requested stop; skip queued stages
        std::atomic<bool>  cancelled{ false };     // worker stopped early on that request
        std::atomic<int>   stageKind{ 0 };         // label: 0 offsets,1 mesh,2 thermal,3 isosurface
        std::atomic<int>   progressStage{ 0 };     // index of current stage among the planned ones
        std::atomic<int>   totalStages{ 1 };       // number of progress-weighted stages planned

        // Which heavy stages this run executes (planned on the main thread).
        bool   runOffsets = false, runMesh = false, runThermal = false;
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
        int                 tetLayerSpan = 1;
        double              isoValue = 0.5;
        float               maxInward = 1.0f;
        int                 useSourceSdf = 0;

        // Deterministic output paths the SEM core writes (computed at launch on
        // the main thread, where OutPath/m_srcPath are stable).
        std::string         expOffsets, expMesh, expIso;

        // Worker outputs (read by the main thread after join()). Set from the
        // exp* paths only once the corresponding stage has actually succeeded.
        std::string         offsetsPath, meshPath, isoPath, error;

        // Measured compute durations (ms) of each heavy stage the worker ran;
        // < 0 when the stage was not part of this run. Copied to the session by
        // PollAsync after join().
        double              offsetsMs = -1.0, meshMs = -1.0, thermalMs = -1.0;

        ~AsyncJob() { if (worker.joinable()) worker.join(); }
    };
    AsyncJob m_job;
    mutable std::atomic<float> m_progressShown{ 0.0f };

    // Worker-thread body. Runs the heavy 3D SEM stages; the core serializes each
    // result to the working dir. Touches only the SEM library — never the Scene.
    // On failure it records a message (incl. SEM_GetLastError detail) and stops.
    void PipelineWorkerBody();

    // Record a worker-thread failure. SemDetail() is read immediately so it
    // reflects the call that just failed. Sets done so PollAsync surfaces it.
    void Fail(const std::string& msg);

    // Full path the SEM core writes for a given pipeline product. The core
    // serializes into the working directory using the source file's stem plus a
    // fixed suffix (see sem_exports.cpp / sem_exports3d.cpp), so the caller can
    // reconstruct the path deterministically after a compute/build/extract call.
    std::string OutPath(const char* suffix) const;

    bool Alive(Scene& scene, Primitive* q) const;
    bool Alive(Scene& scene, SceneNode* q) const;
    void Report(Scene& scene, bool silent, const std::string& msg);
    bool CheckRc(Scene& scene, bool silent, const char* call, int rc,
                 std::initializer_list<const char*> errs);
    SceneNode* AttachParent();

    // Every ColoredTriangles surface produced by the 3D pipeline is drawn at the
    // configurable surface opacity. Called right after such a surface is created
    // (only when dim == 3).
    void ConfigureSurface3D(Primitive* p);

    void BuildSourceRevolution(Scene& scene);
    void BuildIsolineRevolution(Scene& scene);

    // Build the movable rectangle (soft red kept side, soft blue removed side) for
    // a clip-plane node, sized to the source surface's bounding box, parented to
    // the node and positioned on the given plane. Remove every plane node + rect.
    void BuildClipPlaneRect(Scene& scene, ClipPlaneNode* node, const XMFLOAT4& plane);
    void DropClipPlaneNodes(Scene& scene);
    // Last (normal, d) set pushed to the core, so AutoApplyClipPlanes only
    // re-pushes when a rectangle actually moved.
    std::vector<XMFLOAT4> m_appliedClipPlanes;
    bool ClipPlanesChanged() const;

    void DropOffsets(Scene& scene);
    void DropMesh(Scene& scene);
    void DropIsoline(Scene& scene);

    // Re-import the current mesh file in place so its nodes recolour by the T
    // field the thermal solver just wrote into it. Preserves visibility, stats,
    // path, the solved flag, and any extracted isotherm (unlike DropMesh).
    void ReloadMeshColored(Scene& scene);

    // Load the per-shell offset files (<stem>_offset[3d]_<i>.csv3d) the SEM core
    // serialized, as primitives under a fresh empty "offsets" group parented to
    // the source contour. Aggregates m_offStats. Returns false if none loaded.
    bool LoadOffsetShells(Scene& scene, bool silent, const std::string& dir = std::string());

    // Delete stale per-shell offset files in the working dir so a run producing
    // fewer shells than a previous one does not reload leftovers.
    void CleanupOffsetFiles();

    bool ApplySubdivide(Scene& scene, bool silent);
    bool ApplyOffsets(Scene& scene, bool silent);
    bool ApplyMesh(Scene& scene, bool silent);
};

}
