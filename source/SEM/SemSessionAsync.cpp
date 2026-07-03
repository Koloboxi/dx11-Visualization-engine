#include "SemSessionDetail.h"

namespace SemSessionNS {

using namespace detail;

bool SemSession::AsyncRunning() const { return m_job.running.load(); }

const char* SemSession::AsyncStageName() const {
    switch (m_job.stageKind.load()) {
        case 0:  return "Computing offset shells";
        case 1:  return "Building tetrahedral mesh";
        case 2:  return "Solving thermal field";
        case 4:  return "Offset-remeshing surface";
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
        // The cached source geometry changed; recreate the displayed source so it
        // matches what the offsets/mesh will be built from.
        RebuildSourcePrim(scene);
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
    m_job.runStandalone     = false;   // only ApplyStandaloneOffsetRemeshAsync sets this
    // Only the stage whose Apply was pressed (== `to`) is shown; the prerequisite
    // stages this run had to compute to reach it are produced hidden (the user
    // reveals them with the tree eye). The thermal solve's product is the mesh,
    // so the mesh is visible when either Mesh or Thermal is the pressed stage.
    m_job.offVisible  = (to == STAGE_OFFSETS);
    m_job.meshVisible = (to == STAGE_MESH || to == STAGE_THERMAL);
    m_job.isoVisible  = (to == STAGE_ISOSURFACE);
    m_job.totalStages.store((int)runOff + (int)runMesh + (int)runTherm + (int)runIso);

    m_job.offsetMode  = offsetMode;
    m_job.firstGap    = firstGap;
    m_job.numOffsets  = numOffsets;
    m_job.grading     = grading;
    m_job.gaps.assign(gaps.begin(), gaps.end());
    m_job.tetMethod   = tetMethod;
    m_job.tetParam    = (double)tetParam;
    m_job.tetMaxEdgeLen = (double)tetMaxEdgeLen;
    m_job.isoValue = isoValue;
    m_job.isoAxis = isoAxis;
    m_job.isoOffsetValue = (double)isoOffsetValue;
    // isoMinOffsetValue is stored as the fraction c in [0,1]; the SEM core wants the
    // absolute clearance (min_offset_value), from which it recomputes c = min/offset.
    m_job.isoMinOffsetValue = (double)isoMinOffsetValue * (double)isoOffsetValue;
    // Final isotropic remesh knobs — now part of the isosurface stage (folded into
    // SEM_ExtractIsosurface3D), run after the offset-remesh when an offset axis is set.
    m_job.isoFinalTargetMult = (double)isoFinalTargetMult;
    m_job.isoFinalIters      = isoFinalIters;
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

void SemSession::ApplyStandaloneOffsetRemeshAsync(Scene& scene, bool silent) {
    if (!HasSource()) { Report(scene, silent, "Import a source surface first."); return; }
    if (dim != 3)     { Report(scene, silent, "Standalone offset-remesh is 3D only."); return; }
    if (m_job.running.load()) return;
    if (m_job.worker.joinable()) m_job.worker.join();

    int axis = soAxis;
    if (axis < 1) axis = 1; if (axis > 3) axis = 3;

    // Copy the active source surface (subdivided / clip-snapped) out of the SEM core
    // NOW, on the main thread: the standalone call reads the same cache's clip planes
    // and re-applies them to the result, so its input geometry must be an independent
    // copy handed in as raw arrays.
    SEM_MeshView src{};
    if (SEM_GetSourceSurface3D(&src) != 0 || src.num_nodes <= 0 || src.num_tris <= 0) {
        Report(scene, silent, "Source has no triangle surface" + SemDetail());
        return;
    }
    m_job.soXyz.assign(src.coords, src.coords + 3 * (size_t)src.num_nodes);
    m_job.soTris.assign(src.tris,  src.tris  + 3 * (size_t)src.num_tris);
    m_job.soAxis       = axis;
    m_job.soOffset     = (double)soOffset;
    // soMinOffset is stored as the fraction c in [0,1]; SEM_OffsetRemeshInPlaneSurface3D
    // wants the absolute clearance (min_offset_value = c * offset).
    m_job.soMinOffset  = (double)soMinOffset * (double)soOffset;
    m_job.soTargetMult = (double)soTargetMult;
    m_job.soIters      = soIters;

    m_job.runOffsets = m_job.runMesh = m_job.runThermal = m_job.runIso = false;
    m_job.runStandalone     = true;
    m_job.isoVisible = true;
    m_job.totalStages.store(1);
    m_job.expIsoRemesh = OutPath("_isosurface3d_remesh3d.csv3d");

    m_job.offsetsPath.clear();
    m_job.meshPath.clear();
    m_job.isoPath.clear();
    m_job.error.clear();
    m_job.offsetsMs = m_job.meshMs = m_job.thermalMs = m_job.isoMs = -1.0;
    m_job.stageKind.store(4);
    m_job.progressStage.store(0);
    m_job.ok.store(false);
    m_job.done.store(false);
    m_job.cancel.store(false);
    m_job.cancelled.store(false);
    m_progressShown.store(0.0f);
    m_job.running.store(true);

    snprintf(status, sizeof(status), "Offset-remeshing...");
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
                                            solved && bcView, /*registerColorSets*/ solved);
            m_meshPath = m_job.meshPath;
            StyleLines(m_mesh, LINESTYLE_HAIRLINE);
            ConfigureSurface3D(m_mesh);
            m_meshStats = ComputeStatsData(data);
            if (solvedThisRun) m_thermalSolved = true;
            if (m_mesh && Alive(scene, m_mesh)) scene.SetNodeVisibleCascade(m_mesh, m_job.meshVisible);
        }
    }
    else if (solvedThisRun) {
        // Thermal-only run: the mesh was not rebuilt above, but SEM_SolveThermal3D
        // overwrote its file with the solved T. Re-import in place so the existing
        // mesh recolours by the new field, then apply this run's visibility intent
        // (shown when Thermal was the pressed stage, hidden as a prerequisite).
        m_thermalSolved = true;
        ReloadMeshColored(scene);
        if (m_mesh && Alive(scene, m_mesh)) scene.SetNodeVisibleCascade(m_mesh, m_job.meshVisible);
    }

    // Isosurface. A plain extraction is the extracted iso (SEM_GetSourceIsosurface3D);
    // an offset-axis run is the offset-and-remesh product (SEM_GetIsosurface3D).
    if (!m_job.isoPath.empty()) {
        // The worker already marshalled the displayed isosurface off the cache
        // (m_job.isoDisplayData) — covering both a plain extraction and the offset-
        // and-remesh product — so there is no file to re-read here. Route it through
        // BuildIsoDisplay so the uneven-winding highlight applies.
        DropIsoline(scene);
        m_isoData = m_job.isoDisplayData;
        m_isoline = BuildIsoDisplay(scene, m_isoData);
        if (m_isoline) {
            if (m_job.runStandalone) {
                // The standalone result is not serialized under a stem, but its flat
                // projection cache (iso_flat) IS populated, so the projection overlay
                // is available (unlike a plain pipeline extraction, gate it on).
                m_isolinePath.clear();
                m_isoProjected = true;
            } else {
                m_isolinePath = m_job.isoPath;
                // An offset axis means the offset-and-remesh step ran, so the
                // intermediate-stage caches (source iso, projection) are valid.
                m_isoProjected = (m_job.isoAxis != 0);
            }
            if (Alive(scene, m_isoline)) scene.SetNodeVisibleCascade(m_isoline, m_job.isoVisible);
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
    // The on-plane overlay also covers the source/final isosurfaces, so rebuild it
    // once a pipeline run has (re)extracted them.
    if (m_clipOnPlaneShown) RefreshClipOnPlane(scene);
    // The offset/isosurface/remesh clip-change logs are regenerated by a pipeline
    // run; rebuild whichever category overlays are shown.
    RefreshClipChanges(scene);
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
        // Extract-and-remesh in one call. axis == 0 extracts only; axis 1/2/3 also runs
        // the folded-in offset-and-remesh (was SEM_OffsetRemeshIsosurface3D) AND the final
        // isotropic remesh (was SEM_RemeshIsosurface3D) as part of this stage, reading the
        // result back through SEM_GetIsosurface3D. When only the final-remesh knobs changed
        // since the last extract, the core takes its fast path and re-remeshes the cached
        // base without re-extracting.
        int rc = SafeExtractIsosurface3D(v, m_job.isoAxis, m_job.isoOffsetValue,
                                         m_job.isoMinOffsetValue,
                                         m_job.isoFinalTargetMult, m_job.isoFinalIters);
        m_job.isoMs = t.GetMillisecondsElapsed();
        if (rc == -100) return Fail("Isosurface extraction crashed (access violation caught).");
        if (rc != 0) return Fail("SEM_ExtractIsosurface3D failed (" + std::to_string(rc) + ")");
        m_job.isoPath = m_job.expIso;

        // Copy the result straight off the SEM cache here on the worker thread.
        // SEM_MeshView pointers live in the process-global cache and stay valid only
        // until the next mutating SEM call, so we marshal them out now (ViewToData)
        // instead of having the main thread re-read a serialized file later — that
        // disk round-trip was redundant and, for the offset-and-remesh product, also
        // broken: the host looked for a <stem>_isosurface3d_remesh3d.csv3d the core
        // never writes under that name (it writes surface_remesh3d.csv3d).
        if (m_job.isoAxis != 0) {
            // The offset-and-remesh ran as part of the extract; its remeshed result is
            // cached and read back through SEM_GetIsosurface3D.
            SEM_MeshView rv{};
            int grc = SEM_GetIsosurface3D(&rv);
            if (grc != 0) return Fail("SEM_GetIsosurface3D failed (" + std::to_string(grc) + ")");
            m_job.isoDisplayData = ViewToData(rv);            // copy before any further SEM call
            m_job.isoPath = m_job.expIsoRemesh;
        } else {
            // Plain extraction: the displayed surface is the extracted iso itself
            // (no remesh ran, so SEM_GetIsosurface3D is empty — read the source iso).
            SEM_MeshView iv{};
            int grc = SEM_GetSourceIsosurface3D(&iv);
            if (grc != 0) return Fail("SEM_GetSourceIsosurface3D failed (" + std::to_string(grc) + ")");
            m_job.isoDisplayData = ViewToData(iv);
        }
        ++idx;
    }

    // Standalone offset-and-remesh of an imported surface passed in as raw arrays
    // (SEM_OffsetRemeshInPlaneSurface3D). Self-contained: it reads no pipeline cache
    // except the clip planes and publishes the flat projection (SEM_GetIsosurface-
    // Projection3D). Its own progress fills the single stage band.
    if (m_job.runStandalone) {
        if (stopIfCancelled()) return;
        m_job.stageKind.store(4);
        m_job.progressStage.store(idx);
        Timer t; t.Restart();
        SEM_MeshView out{};
        int rc = SafeOffsetRemeshInPlaneSurface3D(
                     m_job.soAxis, m_job.soOffset,
                     m_job.soXyz.data(), (int)(m_job.soXyz.size() / 3),
                     m_job.soTris.data(), (int)(m_job.soTris.size() / 3),
                     m_job.soMinOffset, m_job.soTargetMult, m_job.soIters, &out);
        m_job.isoMs = t.GetMillisecondsElapsed();
        if (rc == -100) return Fail("Standalone offset-remesh crashed (access violation caught).");
        if (rc != 0) return Fail("SEM_OffsetRemeshInPlaneSurface3D failed (" + std::to_string(rc) + ")");
        m_job.isoDisplayData = ViewToData(out);           // copy before any further SEM call
        m_job.isoPath = m_job.expIsoRemesh;               // non-empty: an iso to display
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
