#include "SemSessionDetail.h"

namespace SemSessionNS {

using namespace detail;

bool SemSession::AsyncRunning() const { return m_job.running.load(); }

const char* SemSession::AsyncStageName() const {
    switch (m_job.stageKind.load()) {
        case 0:  return "Computing offset shells";
        case 1:  return "Building tetrahedral mesh";
        case 2:  return "Solving thermal field";
        default: return "Extracting isosurface";
    }
}

float SemSession::AsyncProgress() const {
    if (!m_job.running.load()) return 0.0f;
    int total = m_job.totalStages.load();
    if (total < 1) total = 1;
    // The library exposes a single progress value (0..1) for the call currently
    // running (offsets, mesh, thermal solve and isosurface extraction each report
    // their own); it resets to 0 between calls. Fold it into the per-stage band
    // and clamp monotonic.
    float p = SEM_GetProgress();
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    float overall = ((float)m_job.progressStage.load() + p) / (float)total;
    float prev = m_progressShown.load();
    if (overall < prev) overall = prev;
    m_progressShown.store(overall);
    return overall;
}

void SemSession::RecomputeUpToAsync(Scene& scene, Stage to, bool silent) {
    if (!HasSource()) return;
    if (dim != 3) { RecomputeUpTo(scene, to, silent); return; }
    if (m_job.running.load()) return;
    if (m_job.worker.joinable()) m_job.worker.join();

    bool ran = false;

    // Subdivide runs synchronously: it is quick, reports no progress and only
    // updates the SEM cache (no scene work). It must precede the offsets.
    if (m_dirty[STAGE_SUBDIVIDE]) {
        if (!ApplySubdivide(scene, silent)) return;
        m_dirty[STAGE_SUBDIVIDE] = false;
        ran = true;
    }

    // Plan the heavy stages exactly as RecomputeUpTo would execute them. The
    // thermal solve and the isosurface extraction are now distinct stages: each
    // is a separate progress-reporting SEM call run on the worker.
    bool runOff = false, runMesh = false, runTherm = false, runIso = false;
    if (to >= STAGE_OFFSETS && (m_dirty[STAGE_OFFSETS] || ran)) {
        if (offEnabled || (m_offsets && Alive(scene, m_offsets))) { runOff = true; ran = true; }
        m_dirty[STAGE_OFFSETS] = false;
    }
    if (to >= STAGE_MESH && (m_dirty[STAGE_MESH] || ran)) {
        if (meshEnabled || (m_mesh && Alive(scene, m_mesh))) { runMesh = true; ran = true; }
        m_dirty[STAGE_MESH] = false;
    }
    if (to >= STAGE_THERMAL && (m_dirty[STAGE_THERMAL] || ran)) {
        if (thermalEnabled || m_thermalSolved) { runTherm = true; ran = true; }
        m_dirty[STAGE_THERMAL] = false;
    }
    if (to >= STAGE_ISOSURFACE && (m_dirty[STAGE_ISOSURFACE] || ran)) {
        if (isoEnabled || (m_isoline && Alive(scene, m_isoline))) { runIso = true; ran = true; }
        m_dirty[STAGE_ISOSURFACE] = false;
    }

    // Rebuilding a stage invalidates everything after 'to' we did not touch.
    if (ran)
        for (int s = (int)to + 1; s <= STAGE_ISOSURFACE; ++s) m_dirty[s] = true;

    // No heavy work (e.g. just a subdivide, or nothing dirty): no worker,
    // no progress bar — just refresh and return.
    if (!runOff && !runMesh && !runTherm && !runIso) { scene.UpdateLight(); return; }

    // Preconditions the worker cannot check itself (it has no Scene access):
    // any solve/extract run needs a mesh already built and present, and an iso
    // extraction that does not also solve needs the field already solved.
    if ((runTherm || runIso) && !runMesh && !(m_mesh && Alive(scene, m_mesh))) {
        Report(scene, silent, "Build the mesh first.");
        return;
    }
    if (runIso && !runTherm && !m_thermalSolved) {
        Report(scene, silent, "Solve thermal first.");
        return;
    }

    // Clear stale per-shell offset files before the worker recomputes, so a run
    // producing fewer shells does not leave leftovers for the loader to pick up.
    if (runOff) CleanupOffsetFiles();

    // Snapshot the plan, parameters and visibility intent for the worker /
    // PollAsync (mutable session state must not be read once running).
    m_job.runOffsets  = runOff;
    m_job.runMesh     = runMesh;
    m_job.runThermal  = runTherm;
    m_job.runIso      = runIso;
    m_job.offVisible  = offEnabled;
    m_job.meshVisible = meshEnabled;
    m_job.isoVisible  = isoEnabled;
    m_job.totalStages.store((int)runOff + (int)runMesh + (int)runTherm + (int)runIso);

    m_job.offsetMode  = offsetMode;
    m_job.firstGap    = firstGap;
    m_job.numOffsets  = numOffsets;
    m_job.grading     = grading;
    m_job.gaps.assign(gaps.begin(), gaps.end());
    m_job.tetMethod   = tetMethod;
    {
        double param = (double)tetParam;
        if (tetParamEdgeUnits && tetParam > 0.0f && tetMethod == SEM_TET_BAND)
            param *= TetParamFactor();
        m_job.tetParam = param;
    }
    m_job.tetMaxEdgeLen = (double)tetMaxEdgeLen;
    m_job.isoValue = isoValue;
    m_job.isoAxis = isoAxis;
    m_job.isoOffsetValue = (double)isoOffsetValue;
    m_job.maxInward = maxInward;

    // Deterministic output paths the SEM core writes during each compute call.
    m_job.expOffsets = OutPath("_offsets3d.csv3d");
    m_job.expMesh    = OutPath("_mesh3d.csv3d");
    m_job.expIso     = OutPath("_isosurface3d.csv3d");
    m_job.expIsoRemesh = OutPath("_isosurface3d_remesh3d.csv3d");

    m_job.offsetsPath.clear();
    m_job.meshPath.clear();
    m_job.isoPath.clear();
    m_job.error.clear();
    m_job.offsetsMs = m_job.meshMs = m_job.thermalMs = m_job.isoMs = -1.0;
    m_job.stageKind.store(runOff ? 0 : runMesh ? 1 : runTherm ? 2 : 3);
    m_job.progressStage.store(0);
    m_job.ok.store(false);
    m_job.done.store(false);
    m_job.cancel.store(false);
    m_job.cancelled.store(false);
    m_progressShown.store(0.0f);
    m_job.running.store(true);

    snprintf(status, sizeof(status), "Computing...");
    m_job.worker = std::thread(&SemSession::PipelineWorkerBody, this);
}

void SemSession::PollAsync(Scene& scene) {
    if (!m_job.running.load() || !m_job.done.load()) return;
    if (m_job.worker.joinable()) m_job.worker.join();
    m_job.running.store(false);
    m_job.done.store(false);

    const bool cancelled = m_job.cancelled.load();
    // A genuine failure aborts; a cancellation still applies whatever stages
    // finished before the stop (their paths are set, the skipped ones are empty).
    if (!m_job.ok.load() && !cancelled) {
        Report(scene, false, m_job.error.empty() ? "3D pipeline failed." : m_job.error);
        return;
    }

    // Record the measured durations of whichever stages this run computed; stages
    // that did not run keep their previous time so the total accumulates.
    if (m_job.offsetsMs >= 0.0) m_offsetsMs = m_job.offsetsMs;
    if (m_job.meshMs    >= 0.0) m_meshMs    = m_job.meshMs;
    if (m_job.thermalMs >= 0.0) m_thermalMs = m_job.thermalMs;
    if (m_job.isoMs     >= 0.0) m_isoMs     = m_job.isoMs;

    // Did the thermal solve actually complete this run? (Planned but skipped by a
    // cancellation leaves thermalMs < 0, so don't trust runThermal alone.)
    const bool solvedThisRun = (m_job.thermalMs >= 0.0);

    // Offset shells.
    if (!m_job.offsetsPath.empty()) {
        DropOffsets(scene);
        LoadOffsetShells(scene, true);
        if (m_offsets && Alive(scene, m_offsets)) scene.SetNodeVisibleCascade(m_offsets, m_job.offVisible);
    }

    // Tetrahedral mesh. Built from the cache-resident mesh (SEM_GetMesh3D) the
    // worker left behind, not the serialized file. The worker ran offsets→mesh→
    // (thermal) in order and is now joined, so the cache holds the final mesh —
    // with the solved T when this run also solved.
    if (!m_job.meshPath.empty()) {
        CSV3DLoader::CSV3DData data;
        if (FetchMeshData(scene, true, data)) {
            DropMesh(scene);
            // When this run also solved the thermal field the cached mesh already
            // holds the solved T, so colour it like the sync solve (blue..red,
            // honouring the BC view). An unsolved mesh keeps cyan..yellow.
            const bool solved = solvedThisRun;
            m_mesh = scene.AddFromCSV3DData(data, "mesh_" + Stem(m_srcPath),
                                            AttachParent(), nullptr,
                                            solved ? Colors::BLUE : Colors::CYAN,
                                            solved ? Colors::RED  : Colors::YELLOW,
                                            true, solved && bcView, /*registerColorSets*/ solved);
            m_meshPath = m_job.meshPath;
            ConfigureSurface3D(m_mesh);
            m_meshStats = ComputeStatsData(data);
            if (solvedThisRun) m_thermalSolved = true;
            if (m_mesh && Alive(scene, m_mesh)) scene.SetNodeVisibleCascade(m_mesh, m_job.meshVisible);
        }
    }
    else if (solvedThisRun) {
        // Thermal-only run: the mesh was not rebuilt above, but SEM_SolveThermal3D
        // overwrote its file with the solved T. Re-import in place so the existing
        // mesh recolours by the new field.
        m_thermalSolved = true;
        ReloadMeshColored(scene);
    }

    // Isosurface. A plain extraction comes from the cache (SEM_GetIsosurface3D);
    // an offset-axis run produced a standalone offset-and-remesh file, which the
    // cache does not hold, so reload that displayed surface from disk.
    if (!m_job.isoPath.empty()) {
        const XMFLOAT4 green(0.0f, 1.0f, 0.0f, 1.0f);
        bool built = false;
        if (m_job.isoAxis != 0) {
            DropIsoline(scene);
            m_isoline = scene.AddFromCSV3D(m_job.isoPath, "isosurface_" + Stem(m_srcPath),
                                           AttachParent(), &green, Colors::BLUE, Colors::RED);
            built = (m_isoline != nullptr);
        } else {
            CSV3DLoader::CSV3DData data;
            if (FetchIsoData(scene, true, data)) {
                DropIsoline(scene);
                m_isoline = scene.AddFromCSV3DData(data, "isosurface_" + Stem(m_srcPath),
                                                   AttachParent(), &green, Colors::BLUE, Colors::RED);
                built = (m_isoline != nullptr);
            }
        }
        if (built) {
            ConfigureSurface3D(m_isoline);
            m_isolinePath = m_job.isoPath;
            // An offset axis means the standalone offset-and-remesh step ran, so the
            // intermediate-stage caches (source iso, projection) are valid.
            m_isoProjected = (m_job.isoAxis != 0);
            if (m_isoline && Alive(scene, m_isoline)) scene.SetNodeVisibleCascade(m_isoline, m_job.isoVisible);
        }
    }

    // A planned stage's dirty flag was cleared up-front by RecomputeUpToAsync;
    // when cancellation skipped it (no output produced) restore that flag so the
    // next Apply recomputes it instead of trusting a stage that never ran.
    if (cancelled) {
        if (m_job.runOffsets && m_job.offsetsPath.empty()) m_dirty[STAGE_OFFSETS]    = true;
        if (m_job.runMesh    && m_job.meshPath.empty())    m_dirty[STAGE_MESH]       = true;
        if (m_job.runThermal && m_job.thermalMs < 0.0)     m_dirty[STAGE_THERMAL]    = true;
        if (m_job.runIso     && m_job.isoPath.empty())     m_dirty[STAGE_ISOSURFACE] = true;
    }

    if (AnyClipMirror()) RebuildClipMirrors(scene);
    scene.UpdateLight();
    snprintf(status, sizeof(status), cancelled ? "Cancelled." : "Done.");
}

void SemSession::CancelAsync() {
    if (m_job.running.load()) m_job.cancel.store(true);
}

bool SemSession::AsyncCancelRequested() const {
    return m_job.running.load() && m_job.cancel.load();
}

void SemSession::PipelineWorkerBody() {
    int idx = 0;   // index of the current stage among the planned ones

    // Stop here if cancellation was requested before this stage starts. The SEM
    // call already in flight cannot be interrupted, so we can only break between
    // stages: every queued stage is skipped, finished ones keep their results.
    // Returns true when it stopped.
    auto stopIfCancelled = [&]() -> bool {
        if (!m_job.cancel.load()) return false;
        m_job.cancelled.store(true);
        m_job.ok.store(false);
        m_job.done.store(true);
        return true;
    };

    // Offset shells — SEM_ComputeOffsets3D / SEM_ComputeOffsetsAt3D.
    if (m_job.runOffsets) {
        if (stopIfCancelled()) return;
        m_job.stageKind.store(0);
        m_job.progressStage.store(idx);
        Timer t; t.Restart();
        int rc = (m_job.offsetMode == OFFSET_GAPS && !m_job.gaps.empty())
               ? SEM_ComputeOffsetsAt3D(m_job.gaps.data(), (int)m_job.gaps.size())
               : SEM_ComputeOffsets3D(m_job.firstGap, m_job.numOffsets, m_job.grading);
        m_job.offsetsMs = t.GetMillisecondsElapsed();
        if (rc != 0) return Fail("SEM_ComputeOffsets3D failed (" + std::to_string(rc) + ")");
        m_job.offsetsPath = m_job.expOffsets;
        ++idx;
    }

    // Tetrahedral band mesh — SEM_BuildMesh3D.
    if (m_job.runMesh) {
        if (stopIfCancelled()) return;
        m_job.stageKind.store(1);
        m_job.progressStage.store(idx);
        SEM_MeshParams3D params3d{ m_job.tetMethod, m_job.tetParam, m_job.tetMaxEdgeLen };
        Timer t; t.Restart();
        int rc = SafeBuildMesh3D(&params3d);
        m_job.meshMs = t.GetMillisecondsElapsed();
        if (rc == -100) return Fail("TetGen DLL crashed (access violation caught). "
                                    "Try a larger volume or a looser quality bound.");
        if (rc != 0) return Fail("SEM_BuildMesh3D failed (" + std::to_string(rc) + ")");
        m_job.meshPath = m_job.expMesh;
        ++idx;
    }

    // Steady-state thermal solve — SEM_SolveThermal3D (its own progress stage).
    if (m_job.runThermal) {
        if (stopIfCancelled()) return;
        m_job.stageKind.store(2);
        m_job.progressStage.store(idx);
        Timer t; t.Restart();
        int rc = SafeSolveThermal3D(m_job.maxInward);
        m_job.thermalMs = t.GetMillisecondsElapsed();
        if (rc == -100) return Fail("Thermal solver crashed (access violation caught).");
        if (rc != 0) return Fail("SEM_SolveThermal3D failed (" + std::to_string(rc) + ")");
        ++idx;
    }

    // Isosurface extraction — SEM_ExtractIsosurface3D. Now a long-running call
    // that reports its own progress, so it is a separate progress-weighted stage.
    if (m_job.runIso) {
        if (stopIfCancelled()) return;
        m_job.stageKind.store(3);
        m_job.progressStage.store(idx);
        double v = m_job.isoValue;
        if (v < 0.0) v = 0.0;
        if (v > 1.0) v = 1.0;
        Timer t; t.Restart();
        int rc = SafeExtractIsosurface3D(v);
        if (rc == -100) { m_job.isoMs = t.GetMillisecondsElapsed();
                          return Fail("Isosurface extraction crashed (access violation caught)."); }
        if (rc != 0) { m_job.isoMs = t.GetMillisecondsElapsed();
                       return Fail("SEM_ExtractIsosurface3D failed (" + std::to_string(rc) + ")"); }
        m_job.isoPath = m_job.expIso;
        // Offset-and-remesh is now a standalone step that takes the extracted iso
        // sheet as raw arrays (SEM_OffsetRemeshInPlaneSurface3D), fed straight from
        // the SEM cache (coords/tris from SEM_GetIsosurface3D). It writes its own
        // <stem>_isosurface3d_remesh3d.csv3d, which becomes the displayed surface.
        // The returned view is ignored here; PollAsync reloads from that file on the
        // main thread.
        if (m_job.isoAxis != 0) {
            SEM_MeshView src{};
            int grc = SEM_GetIsosurface3D(&src);
            if (grc != 0) {
                m_job.isoMs = t.GetMillisecondsElapsed();
                return Fail("SEM_GetIsosurface3D failed (" + std::to_string(grc) + ")");
            }
            SEM_MeshView rv{};
            int rrc = SafeOffsetRemeshInPlaneSurface3D(m_job.isoAxis, m_job.isoOffsetValue,
                                                       src.coords, src.num_nodes,
                                                       src.tris, src.num_tris, &rv);
            m_job.isoMs = t.GetMillisecondsElapsed();
            if (rrc == -100) return Fail("Isosurface offset/remesh crashed (access violation caught).");
            if (rrc != 0) return Fail("SEM_OffsetRemeshInPlaneSurface3D failed (" + std::to_string(rrc) + ")");
            m_job.isoPath = m_job.expIsoRemesh;
        } else {
            m_job.isoMs = t.GetMillisecondsElapsed();
        }
        ++idx;
    }

    m_job.ok.store(true);
    m_job.done.store(true);
}

void SemSession::Fail(const std::string& msg) {
    m_job.error = msg + SemDetail();
    m_job.ok.store(false);
    m_job.done.store(true);
}

}
