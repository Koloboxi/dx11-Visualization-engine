#pragma once
#include "scene.h"
#include "../../external/sem_exports.h"
#include <string>
#include <vector>
#include <initializer_list>

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

    int   subMode    = 1;
    int   subN       = 2;
    bool  subEnabled = true;

    int   offsetMode = OFFSET_EVEN;
    float firstGap   = 1.0f;
    int   numOffsets = 8;
    float grading    = 1.2f;
    std::vector<float> gaps = { 25.0f, 25.0f, 25.0f };
    bool  offEnabled = true;

    int   meshMethod    = SEM_STEINER_MAX_AREA;
    float meshParam     = -1.0f;
    float steinerMargin = 0.45f;
    bool  meshEnabled   = true;
    bool  meshParamEdgeUnits = false;

    // 3D (surface) mesh tuning — TetGen, via SEM_BuildMesh3D. Parallel to the
    // 2D meshMethod/meshParam pair: tetMethod picks the refinement strategy
    // (SEM_TetMethod), tetParam is its primary knob (<0 => per-method auto).
    // tetParamEdgeUnits expresses the volume/length knob in multiples of the
    // source surface's mean edge length instead of model units.
    int   tetMethod    = SEM_TET_MAX_VOL;
    float tetParam     = -1.0f;
    bool  tetParamEdgeUnits = false;

    bool  thermalEnabled = true;
    float isoValue   = 0.5f;

    bool  subAuto     = false;
    bool  offAuto     = false;
    bool  meshAuto    = false;
    bool  thermalAuto = false;

    bool  revolutionMode = false;
    int   revSegments    = 48;
    float srcRevAlpha    = 0.8f;
    float isoRevAlpha    = 0.5f;

    // Opacity applied to every ColoredTriangles surface built by the 3D SEM
    // pipeline (source / offsets / mesh / isosurface). Default 0.5.
    float surf3dAlpha    = 0.5f;

    // When true, every triangle-bearing pipeline primitive is rebuilt as edge
    // wireframe (ColoredLine) instead of a filled ColoredTriangles surface.
    // Toggled from the SEM window; RegenerateGeometry applies it by reloading
    // each primitive from its saved CSV3D file.
    bool  renderTrisAsLines = false;

    char  status[256] = "Ready";

    // SEM_SetRevolution axis selector: 1 = X, 2 = Y, 3 = Z. Profiles revolve
    // around the Y axis here.
    static constexpr int kRevolutionAxisY = 2;

    const std::string& SourcePath() const;
    Primitive*  SourcePrim()  const;
    Primitive*  OffsetsPrim() const;
    Primitive*  MeshPrim()    const;
    Primitive*  IsolinePrim() const;
    Primitive*  SrcRevSurf()  const;
    Primitive*  IsoRevSurf()  const;
    bool        HasSource()   const;
    int         Dim()         const;
    bool        ThermalSolved() const;
    bool        HasIsolinePath() const;

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

    Primitive* StagePrim(Stage st) const;
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

    void DropSrcRev(Scene& scene);
    void DropIsoRev(Scene& scene);

    bool ApplyThermalStage(Scene& scene, bool silent);
    bool ApplyThermal(Scene& scene, bool silent);
    bool ApplyIsoline(Scene& scene, bool silent);

    Primitive* ImportSource(Scene& scene, const std::string& path);
    void RunFullPipeline(Scene& scene);

private:
    std::string m_srcPath;
    std::string m_offsetsPath;   // last serialized offsets file (for regeneration)
    std::string m_meshPath;      // last serialized mesh file (for regeneration)
    std::string m_isolinePath;
    std::string m_workDir;       // directory the SEM core serializes into
    Primitive*  m_srcPrim = nullptr;
    Primitive*  m_offsets = nullptr;
    Primitive*  m_mesh    = nullptr;
    Primitive*  m_isoline = nullptr;
    Primitive*  m_srcRevSurf = nullptr;
    Primitive*  m_isoRevSurf = nullptr;
    bool        m_thermalSolved = false;
    Stats m_srcStats, m_offStats, m_meshStats;

    // Per-stage staleness, indexed by Stage. A stage is dirty when its own
    // parameters changed since it was last applied, or when it has never been
    // applied against the currently loaded source (Bind resets all to true).
    bool        m_dirty[4] = { true, true, true, true };

    // Full path the SEM core writes for a given pipeline product. The core
    // serializes into the working directory using the source file's stem plus a
    // fixed suffix (see sem_exports.cpp / sem_exports3d.cpp), so the caller can
    // reconstruct the path deterministically after a compute/build/extract call.
    std::string OutPath(const char* suffix) const;

    bool Alive(Scene& scene, Primitive* q) const;
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

    void DropOffsets(Scene& scene);
    void DropMesh(Scene& scene);
    void DropIsoline(Scene& scene);

    bool ApplySubdivide(Scene& scene, bool silent);
    bool ApplyOffsets(Scene& scene, bool silent);
    bool ApplyMesh(Scene& scene, bool silent);
};

}
