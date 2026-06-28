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

    int   subMode    = 0;
    int   subN       = 2;
    bool  subEnabled = true;

    int   offsetMode = OFFSET_EVEN;
    float firstGap   = 1.0f;
    int   numOffsets = 8;
    float grading    = 1.2f;
    std::vector<float> gaps = { 25.0f, 25.0f, 25.0f };
    bool  offEnabled = true;

    // meshParam: interior Steiner-point spacing (<0 => source mean edge length).
    // steinerMargin: minimum spacing from offset lines as a multiple of meshParam.
    // meshParamEdgeUnits: meshParam is in source mean-edge-length units, not model units.
    int   meshMethod    = SEM_STEINER_GRID;
    float meshParam     = -1.0f;
    float steinerMargin = 0.45f;
    bool  meshEnabled   = true;
    bool  meshParamEdgeUnits = false;

    // tetMethod: SEM_TET_BAND or SEM_TET_LAYERED. tetParam (<0 => per-method auto):
    // BAND => Steiner grid cell size (a length); LAYERED => use_sdf flag (0/1).
    // tetParamEdgeUnits: BAND cell size in source mean-edge-length units (ignored for LAYERED).
    int   tetMethod    = SEM_TET_LAYERED;
    float tetParam     = 0.0f;
    bool  tetParamEdgeUnits = false;
    // Max tet edge length, in source mean-edge-length units. <= 0 disables the filter.
    float tetMaxEdgeLen = 10.0f;

    bool  thermalEnabled = true;
    bool  isoEnabled     = true;
    float isoValue   = 0.8f;
    // isoAxis = 0: plain extraction (clip planes only). 1/2/3 (X/Y/Z): offset-and-
    // remesh an open iso sheet, shifted along its pseudonormals by isoOffsetValue
    // in multiples of the source's mean edge length (signed, no range limit):
    // positive inward toward the source, negative outward. Ignored when isoAxis = 0.
    int   isoAxis        = 0;
    float isoOffsetValue = 0.0f;
    // Vertex-pruning mode for the offset-remesh (ignored when isoAxis = 0). true =
    // fold cull (drop vertices that fall closer to the iso than |shift|); false =
    // signed-crossing cull (keep folds, drop only vertices that crossed through the
    // iso to the wrong side). Maps to SEM_OffsetRemeshInPlaneSurface3D's cull_by_fold.
    bool  isoCullByFold  = true;
    // When set, the extracted isosurface is drawn with its real (possibly
    // inconsistent) winding and the minority-oriented triangles are painted pure
    // red instead of the uniform green. See SetIsoWinding / WindingMinorityTris.
    bool  isoShowWinding = false;
    // false = "T field" (gradient over the solved temperature); true = "BC" (only
    // Dirichlet boundary nodes tinted, interior grey).
    bool  bcView     = false;
    // Max inward penetration (0..1) for the outermost-offset T=0 BCs. < 0 keeps
    // every outermost node.
    float maxInward  = 0.04f;

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

    char  status[256] = "Ready";

    // SEM_SetRevolution axis selector: 1 = X, 2 = Y, 3 = Z.
    static constexpr int kRevolutionAxisY = 2;

    const std::string& SourcePath() const;
    Primitive*  SourcePrim()  const;
    SceneNode*  OffsetsNode() const;
    Primitive*  MeshPrim()    const;
    Primitive*  IsolinePrim() const;
    // Intermediate iso-extraction stages, available only after an offset-and-remesh
    // (projection) extraction; null otherwise. See ShowSourceIsosurface3D below.
    Primitive*  IsoSourcePrim()     const;
    Primitive*  IsoProjectionPrim() const;
    // Pseudonormal line overlays of the source surface / extracted isosurface,
    // null when not shown. See Show{Source,Iso}Pseudonormals.
    Primitive*  SrcNormalsPrim() const;
    Primitive*  IsoNormalsPrim() const;
    // Offset-points overlay, and whether an offset-axis extraction produced any.
    Primitive*  IsoOffsetPointsPrim() const;
    bool        HasIsoOffsetPoints() const;
    Primitive*  SrcRevSurf()  const;
    Primitive*  IsoRevSurf()  const;
    bool        HasSource()   const;
    int         Dim()         const;
    bool        ThermalSolved() const;
    bool        HasIsolinePath() const;
    // True once an isosurface has been extracted with an offset axis (offset-and-
    // remesh / CDT proj-unproj). Only then do the SEM_Get{Source,Projection}
    // isosurface caches hold a result, so this gates the two Show* toggles below.
    bool        HasIsoProjection() const;

    const std::string& WorkDir() const { return m_workDir; }

    // Session folders group pipeline products per source under %TEMP%/sem/, one
    // folder <stem>_<N> per state. Static so the import UI can enumerate/allocate
    // sessions before a source is bound.
    static std::string SessionRoot();
    static std::vector<std::string> ListSessions(const std::string& srcPath);
    static std::string NewSessionDir(const std::string& srcPath);

    void LoadSessionStages(Scene& scene);

    double MeshParamFactor() const;
    double TetParamFactor() const;

    const Stats& SrcStats()  const;
    const Stats& OffStats()  const;
    const Stats& MeshStats() const;

    void Bind(Scene& scene, Primitive* prim);
    void Unbind();
    void Validate(Scene& scene);

    void MarkStageDirty(Stage st);
    void RecomputeUpTo(Scene& scene, Stage to, bool silent);

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
    // Recreate the visual clip-plane rectangles from the serialized session state
    // (<stem>_state3d.txt). Call after SEM_LoadState3D so the core and the scene
    // agree; without it a reloaded session clips the mesh but shows no planes.
    void LoadClipPlanesFromState(Scene& scene);
    void RemoveClipPlane(Scene& scene, int idx);
    ClipPlaneNode* FindClipPlaneByRect(Primitive* prim) const;

    void ShowClipMirror(Scene& scene, int planeIdx, bool show);
    void RebuildClipMirrors(Scene& scene);
    void DropClipMirrors(Scene& scene);
    bool AnyClipMirror() const;

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

    // Toggle the uneven-winding highlight (isoShowWinding). When an isosurface is
    // already extracted this recolours it in place from the cached display data —
    // no re-extraction; otherwise it just stores the flag for the next extraction.
    void SetIsoWinding(Scene& scene, bool on);

    // Show/hide angle-weighted vertex pseudonormals as short line segments:
    //   source   - computed from the staged source surface file (yellow);
    //   isosurface - computed from the displayed isosurface geometry (cyan).
    // show=false drops the overlay. 3D only; the iso form needs an extracted
    // isosurface.
    bool ShowSourcePseudonormals(Scene& scene, bool show, bool silent);
    bool ShowIsoPseudonormals(Scene& scene, bool show, bool silent);

    // Show/hide the offset isosurface points (SEM_GetIsosurfaceOffsetPoints3D) as a
    // point cloud. Available only after an offset-axis isosurface extraction. 3D
    // only; show=false drops the cloud.
    bool ShowIsoOffsetPoints(Scene& scene, bool show, bool silent);

    // Reverse the winding of the extracted 3D isosurface (turn it inside-out) and
    // reload the displayed primitive. 3D only; requires an extracted isosurface.
    bool FlipIsosurface3D(Scene& scene, bool silent);

    // Show/hide the two intermediate stages of an offset-and-remesh extraction,
    // fetched on demand from the SEM cache (they are not serialized to disk):
    //   ShowSourceIsosurface3D     - the original extracted iso sheet, before the
    //                                offset/remesh stage (SEM_GetSourceIsosurface3D).
    //   ShowIsosurfaceProjection3D - the flat base-plane re-meshed projection with
    //                                its wireframe edges (SEM_GetIsosurfaceProjection3D).
    // Both require HasIsoProjection(); show=false drops the primitive. 3D only.
    bool ShowSourceIsosurface3D(Scene& scene, bool show, bool silent);
    bool ShowIsosurfaceProjection3D(Scene& scene, bool show, bool silent);

    void SetBCView(Scene& scene, bool on);

    // workDir picks the session folder to serialize into; empty => Bind allocates
    // a fresh <stem>_<N>.
    Primitive* ImportSource(Scene& scene, const std::string& path,
                            const std::string& workDir = std::string());

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
    void RecomputeUpToAsync(Scene& scene, Stage to, bool silent);
    void PollAsync(Scene& scene);
    void CancelAsync();
    bool AsyncCancelRequested() const;

private:
    std::string m_srcPath;
    std::string m_meshPath;
    std::string m_isolinePath;
    std::string m_workDir;
    Primitive*  m_srcPrim = nullptr;
    SceneNode*  m_offsets = nullptr;
    Primitive*  m_mesh    = nullptr;
    Primitive*  m_isoline = nullptr;
    // Intermediate offset-and-remesh iso stages (cache-only, never serialized).
    Primitive*  m_isoSource = nullptr;   // pre-offset extracted sheet
    Primitive*  m_isoProj   = nullptr;   // flat re-meshed projection
    // Set when the last extraction ran with an offset axis (offset-and-remesh /
    // CDT proj-unproj), so the two intermediate caches are valid to fetch.
    bool        m_isoProjected = false;
    Primitive*  m_srcRevSurf = nullptr;
    Primitive*  m_isoRevSurf = nullptr;
    // Pseudonormal line overlays (vertex normals drawn as segments).
    Primitive*  m_srcNormals = nullptr;
    Primitive*  m_isoNormals = nullptr;
    // Copy of the geometry currently displayed as the isosurface, kept so the
    // winding highlight can recolour and the pseudonormals can be computed without
    // re-extracting or re-reading a file (handles the offset-and-remesh case too).
    CSV3DLoader::CSV3DData m_isoData;
    // Offset, distance-cleaned isosurface points from SEM_GetIsosurfaceOffsetPoints3D
    // (populated only by an offset-axis extraction); the optional point-cloud overlay.
    std::vector<XMFLOAT3> m_isoOffsetPoints;
    Primitive*  m_isoOffsetPointsPrim = nullptr;
    bool        m_thermalSolved = false;
    Stats m_srcStats, m_offStats, m_meshStats;

    // Stage compute durations in ms; < 0 means "not measured this session".
    double m_offsetsMs = -1.0;
    double m_meshMs    = -1.0;
    double m_thermalMs = -1.0;
    double m_isoMs     = -1.0;

    // Per-stage staleness, indexed by Stage. Bind resets all to true.
    bool        m_dirty[5] = { true, true, true, true, true };

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
        std::atomic<int>   stageKind{ 0 };         // label: 0 offsets, 1 mesh, 2 thermal, 3 isosurface
        std::atomic<int>   progressStage{ 0 };     // index of current stage among the planned ones
        std::atomic<int>   totalStages{ 1 };       // number of progress-weighted stages planned

        // Which heavy stages this run executes (planned on the main thread). The
        // thermal solve and the isosurface extraction are separate progress-
        // reporting stages (runThermal solves, runIso extracts).
        bool   runOffsets = false, runMesh = false, runThermal = false, runIso = false;
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
        double              isoValue = 0.5;
        int                 isoAxis = 0;
        double              isoOffsetValue = 0.0;
        bool                isoCullByFold = true;
        float               maxInward = 1.0f;

        // Deterministic output paths the SEM core writes (computed at launch).
        // expIsoRemesh is the offset-and-remesh product (offset axis only).
        std::string         expOffsets, expMesh, expIso, expIsoRemesh;
        // Worker outputs (read by the main thread after join()).
        std::string         offsetsPath, meshPath, isoPath, error;
        // Isosurface geometry copied off the SEM cache on the worker thread (so the
        // main thread neither re-reads a file nor races the cache lifetime). For an
        // offset-axis run this is the offset-and-remesh result the cache holds only
        // transiently — reloading it from disk was both redundant and broken (the
        // host expected a <stem>_isosurface3d_remesh3d.csv3d the core never writes
        // under that name). isoOffsetPoints holds SEM_GetIsosurfaceOffsetPoints3D
        // (only an offset-axis run produces them; empty otherwise).
        CSV3DLoader::CSV3DData isoDisplayData;
        std::vector<XMFLOAT3>  isoOffsetPoints;
        // Measured durations (ms) of each heavy stage; < 0 when not part of this run.
        double              offsetsMs = -1.0, meshMs = -1.0, thermalMs = -1.0, isoMs = -1.0;

        ~AsyncJob() { if (worker.joinable()) worker.join(); }
    };
    AsyncJob m_job;
    mutable std::atomic<float> m_progressShown{ 0.0f };

    void PipelineWorkerBody();
    void Fail(const std::string& msg);

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
    // Last (normal, d) set pushed to the core, so AutoApplyClipPlanes only
    // re-pushes when a rectangle actually moved.
    std::vector<XMFLOAT4> m_appliedClipPlanes;
    bool ClipPlanesChanged() const;

    void DropOffsets(Scene& scene);
    void DropMesh(Scene& scene);
    void DropIsoline(Scene& scene);
    void DropIsoSource(Scene& scene);
    void DropIsoProjection(Scene& scene);
    void DropSrcNormals(Scene& scene);
    void DropIsoNormals(Scene& scene);
    void DropIsoOffsetPoints(Scene& scene);
    void ReloadMeshColored(Scene& scene);

    // Build the displayed isosurface primitive from `data`, honouring
    // isoShowWinding (per-triangle minority-red colouring) or the plain green
    // surface, named and surface-configured. Does not touch m_isoline/m_isoData.
    Primitive* BuildIsoDisplay(Scene& scene, const CSV3DLoader::CSV3DData& data);
    // Build a line-list primitive of angle-weighted vertex pseudonormals for the
    // surface in `data`, parented under `parent` and drawn in `color`.
    Primitive* BuildPseudonormalLines(Scene& scene, const CSV3DLoader::CSV3DData& data,
                                      const XMFLOAT4& color, const std::string& name,
                                      SceneNode* parent);

    bool FetchMeshData(Scene& scene, bool silent, CSV3DLoader::CSV3DData& out);
    bool FetchIsoData(Scene& scene, bool silent, CSV3DLoader::CSV3DData& out);
    bool FetchSourceIsoData(Scene& scene, bool silent, CSV3DLoader::CSV3DData& out);
    bool FetchIsoProjData(Scene& scene, bool silent, CSV3DLoader::CSV3DData& out);

    bool LoadOffsetShells(Scene& scene, bool silent, const std::string& dir = std::string());
    void CleanupOffsetFiles();

    bool ApplySubdivide(Scene& scene, bool silent);
    bool ApplyOffsets(Scene& scene, bool silent);
    bool ApplyMesh(Scene& scene, bool silent);
};

}
