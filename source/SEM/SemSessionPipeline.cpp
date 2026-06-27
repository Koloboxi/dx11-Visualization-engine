#include "SemSessionDetail.h"
#include <filesystem>

namespace fs = std::filesystem;

namespace SemSessionNS {

using namespace detail;

void SemSession::RecomputeUpTo(Scene& scene, Stage to, bool silent) {
    if (!HasSource()) return;
    bool ran = false;

    if (m_dirty[STAGE_SUBDIVIDE]) {
        if (!ApplySubdivide(scene, silent)) return;
        m_dirty[STAGE_SUBDIVIDE] = false;
        ran = true;
    }

    if (to >= STAGE_OFFSETS && (m_dirty[STAGE_OFFSETS] || ran)) {
        if (offEnabled || (m_offsets && Alive(scene, m_offsets))) {
            if (!ApplyOffsets(scene, silent)) return;
            if (m_offsets && Alive(scene, m_offsets)) scene.SetNodeVisibleCascade(m_offsets, offEnabled);
            ran = true;
        }
        m_dirty[STAGE_OFFSETS] = false;
    }

    if (to >= STAGE_MESH && (m_dirty[STAGE_MESH] || ran)) {
        if (meshEnabled || (m_mesh && Alive(scene, m_mesh))) {
            if (!ApplyMesh(scene, silent)) return;
            if (m_mesh && Alive(scene, m_mesh)) scene.SetNodeVisibleCascade(m_mesh, meshEnabled);
            ran = true;
        }
        m_dirty[STAGE_MESH] = false;
    }

    // Thermal solve: heat-conduction solve + recolour the band mesh in place. No
    // isosurface here — that is the separate STAGE_ISOSURFACE below.
    if (to >= STAGE_THERMAL && (m_dirty[STAGE_THERMAL] || ran)) {
        if (thermalEnabled || m_thermalSolved) {
            if (!ApplyThermal(scene, silent)) return;
            ran = true;
        }
        m_dirty[STAGE_THERMAL] = false;
    }

    // Isosurface: extract the iso sheet (with its offset/remesh parameters) from
    // the already-solved field.
    if (to >= STAGE_ISOSURFACE && (m_dirty[STAGE_ISOSURFACE] || ran)) {
        if (isoEnabled || (m_isoline && Alive(scene, m_isoline))) {
            if (ApplyIsoline(scene, silent) && m_isoline && Alive(scene, m_isoline))
                scene.SetNodeVisibleCascade(m_isoline, isoEnabled);
            ran = true;
        }
        m_dirty[STAGE_ISOSURFACE] = false;
    }

    // Rebuilding a stage invalidates everything after 'to' that we did not
    // touch — mark it stale so its own Apply rebuilds it on demand.
    if (ran)
        for (int s = (int)to + 1; s <= STAGE_ISOSURFACE; ++s) m_dirty[s] = true;

    scene.UpdateLight();
}

SceneNode* SemSession::StagePrim(Stage st) const {
    // The thermal solve has no primitive of its own — its product is the recoloured
    // band mesh, so it shares the mesh node. The isosurface is the iso sheet.
    return st == STAGE_OFFSETS    ? m_offsets
         : st == STAGE_MESH       ? static_cast<SceneNode*>(m_mesh)
         : st == STAGE_THERMAL    ? static_cast<SceneNode*>(m_mesh)
         : st == STAGE_ISOSURFACE ? static_cast<SceneNode*>(m_isoline)
         : nullptr;
}

void SemSession::SetStageVisible(Scene& scene, Stage st, bool show) {
    SceneNode* p = StagePrim(st);
    if (show && !(p && Alive(scene, p))) {
        RecomputeUpTo(scene, st, false);
        p = StagePrim(st);
    }
    if (p && Alive(scene, p)) scene.SetNodeVisibleCascade(p, show);
}

void SemSession::ResetStage(Scene& scene, Stage st) {
    if (st <= STAGE_OFFSETS)         { DropOffsets(scene); DropMesh(scene);
                                       m_dirty[STAGE_OFFSETS] = m_dirty[STAGE_MESH] =
                                       m_dirty[STAGE_THERMAL] = m_dirty[STAGE_ISOSURFACE] = true; }
    else if (st == STAGE_MESH)       { DropMesh(scene);
                                       m_dirty[STAGE_MESH] = m_dirty[STAGE_THERMAL] =
                                       m_dirty[STAGE_ISOSURFACE] = true; }
    else if (st == STAGE_THERMAL)    { DropIsoline(scene); m_thermalSolved = false; m_isoMs = -1.0;
                                       m_dirty[STAGE_THERMAL] = m_dirty[STAGE_ISOSURFACE] = true; }
    else if (st == STAGE_ISOSURFACE) { DropIsoline(scene);
                                       m_dirty[STAGE_ISOSURFACE] = true; }
    snprintf(status, sizeof(status), "Reset stage.");
}

bool SemSession::ApplyThermal(Scene& scene, bool silent) {
    if (!HasSource()) return false;
    if (!Alive(scene, m_mesh)) { Report(scene, silent, "Build the mesh first."); return false; }
    // SEM_SolveThermal overwrites each cached mesh node's T with the steady-state
    // temperature in place AND rewrites the serialized mesh file. Re-import below
    // so the displayed mesh recolours by the solved field; the same field feeds
    // the isotherm/isosurface.
    Timer t; t.Restart();
    int rc = (dim == 3) ? SafeSolveThermal3D(maxInward) : SafeSolveThermal();
    const double ms = t.GetMillisecondsElapsed();
    if (rc == -100) {
        Report(scene, silent, "Thermal solver crashed (access violation caught).");
        return false;
    }
    if (!CheckRc(scene, silent, dim == 3 ? "SEM_SolveThermal3D" : "SEM_SolveThermal", rc,
                 { "", "No source loaded", "No mesh built", "No offsets",
                   "", "No boundary nodes", "Solve failed" }))
        return false;
    m_thermalMs = ms;
    m_thermalSolved = true;
    ReloadMeshColored(scene);
    snprintf(status, sizeof(status), "Thermal solved.");
    return true;
}

bool SemSession::ApplyIsoline(Scene& scene, bool silent) {
    if (!HasSource()) return false;
    if (!Alive(scene, m_mesh) || !m_thermalSolved) {
        Report(scene, silent, "Solve thermal first."); return false;
    }
    bool revWasShown = Alive(scene, m_isoRevSurf) && m_isoRevSurf->visible;
    try {
        double v = isoValue;
        if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0;
        Timer t; t.Restart();
        int rc = (dim == 3) ? SafeExtractIsosurface3D(v)
                            : SafeExtractIsoline(v);
        if (rc == -100) {
            Report(scene, silent, (dim == 3 ? "Isosurface" : "Isoline")
                   + std::string(" extraction crashed (access violation caught)."));
            return false;
        }
        if (!CheckRc(scene, silent, dim == 3 ? "SEM_ExtractIsosurface3D" : "SEM_ExtractIsoline", rc,
                     { "", "No source loaded", "No mesh built", "Invalid value", "Extraction failed" }))
            return false;

        // The offset-and-remesh is now a standalone step (SEM_OffsetRemeshInPlaneSurface3D)
        // that operates on the just-written iso sheet file. When an offset axis is
        // chosen, run it and display its re-meshed result; otherwise display the plain
        // extracted sheet straight from the cache.
        CSV3DLoader::CSV3DData data;
        std::string p = OutPath(dim == 3 ? "_isosurface3d.csv3d" : "_isoline.csv3d");
        const bool doRemesh = (dim == 3 && isoAxis != 0);
        if (doRemesh) {
            // The remesh entry point now takes the open sheet as raw arrays. Feed it
            // the just-extracted iso surface straight from the SEM cache (coords/tris
            // from SEM_GetIsosurface3D); the cache stays valid until the remesh call
            // consumes it.
            SEM_MeshView src{};
            int grc = SEM_GetIsosurface3D(&src);
            if (grc != 0) {
                Report(scene, silent, "SEM_GetIsosurface3D failed (" + std::to_string(grc) + ")" + SemDetail());
                return false;
            }
            SEM_MeshView rv{};
            int rrc = SafeOffsetRemeshInPlaneSurface3D(isoAxis, (double)isoOffsetValue,
                                                       src.coords, src.num_nodes,
                                                       src.tris, src.num_tris, &rv);
            if (rrc == -100) {
                Report(scene, silent, "Isosurface offset/remesh crashed (access violation caught).");
                return false;
            }
            if (!CheckRc(scene, silent, "SEM_OffsetRemeshInPlaneSurface3D", rrc, { "" }))
                return false;
            data = ViewToData(rv);
            p = OutPath("_isosurface3d_remesh3d.csv3d");
        } else {
            if (!FetchIsoData(scene, silent, data)) return false;
        }
        m_isoMs = t.GetMillisecondsElapsed();
        DropIsoline(scene);
        const XMFLOAT4 green(0.0f, 1.0f, 0.0f, 1.0f);
        const std::string namePrefix = (dim == 3 ? "isosurface_" : "isoline_");
        m_isoline = scene.AddFromCSV3DData(data, namePrefix + Stem(m_srcPath), AttachParent(), &green, Colors::BLUE, Colors::RED);
        if (dim == 3) ConfigureSurface3D(m_isoline);
        m_isolinePath = p;
        // The offset-and-remesh step filled the intermediate-stage caches (source
        // sheet + projection), so they are valid to fetch now.
        m_isoProjected = doRemesh;
        if (revWasShown && m_isoline) BuildIsolineRevolution(scene);
        if (AnyClipMirror()) RebuildClipMirrors(scene);
        snprintf(status, sizeof(status), "%s T=%.3f: %s",
                 dim == 3 ? "Isosurface" : "Isoline", v, BaseName(p).c_str());
        return true;
    }
    catch (const std::exception& e) { Report(scene, silent, std::string("Isoline exception: ") + e.what()); }
    catch (...)                     { Report(scene, silent, "Isoline: unknown exception."); }
    return false;
}

bool SemSession::FlipIsosurface3D(Scene& scene, bool silent) {
    if (!HasSource()) return false;
    if (dim != 3) { Report(scene, silent, "Flip applies to the 3D isosurface only."); return false; }
    if (!Alive(scene, m_isoline)) { Report(scene, silent, "Extract the isosurface first."); return false; }
    bool revWasShown = Alive(scene, m_isoRevSurf) && m_isoRevSurf->visible;
    try {
        int rc = SafeFlipIsosurface3D();
        if (rc == -100) {
            Report(scene, silent, "Isosurface flip crashed (access violation caught).");
            return false;
        }
        if (!CheckRc(scene, silent, "SEM_FlipIsosurface3D", rc,
                     { "", "No isosurface extracted" }))
            return false;

        CSV3DLoader::CSV3DData data;
        if (!FetchIsoData(scene, silent, data)) return false;
        std::string p = OutPath("_isosurface3d.csv3d");
        DropIsoline(scene);
        const XMFLOAT4 green(0.0f, 1.0f, 0.0f, 1.0f);
        m_isoline = scene.AddFromCSV3DData(data, "isosurface_" + Stem(m_srcPath), AttachParent(), &green, Colors::BLUE, Colors::RED);
        ConfigureSurface3D(m_isoline);
        m_isolinePath = p;
        if (revWasShown && m_isoline) BuildIsolineRevolution(scene);
        if (AnyClipMirror()) RebuildClipMirrors(scene);
        snprintf(status, sizeof(status), "Isosurface flipped: %s", BaseName(p).c_str());
        return true;
    }
    catch (const std::exception& e) { Report(scene, silent, std::string("Flip exception: ") + e.what()); }
    catch (...)                     { Report(scene, silent, "Flip: unknown exception."); }
    return false;
}

void SemSession::SetBCView(Scene& scene, bool on) {
    if (bcView == on) return;
    bcView = on;
    // Only a solved mesh carries a meaningful T field. Switch the pre-built colour
    // set in place; fall back to a full recolour reload if the sets are absent.
    if (m_thermalSolved && Alive(scene, m_mesh)) {
        if (!m_mesh->ActivateColorSet(on ? "bc" : "tfield"))
            ReloadMeshColored(scene);
    }
}

void SemSession::DropOffsets(Scene& scene) {
    if (Alive(scene, m_offsets)) scene.RemoveNode(m_offsets);
    m_offsets = nullptr; m_offStats = Stats();
}

// Called BEFORE a compute (never after) so a run producing fewer shells than a
// previous one does not reload leftovers — the per-shell loader probes indices
// 0.. until a file is missing.
void SemSession::CleanupOffsetFiles() {
    if (m_workDir.empty() || m_srcPath.empty()) return;
    const std::string stem  = Stem(m_srcPath);
    const std::string pfx2d = stem + "_offset_";
    const std::string pfx3d = stem + "_offset3d_";
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(m_workDir, ec)) {
        if (ec) break;
        if (!e.is_regular_file()) continue;
        const std::string fn = e.path().filename().string();
        if (fn.rfind(pfx2d, 0) == 0 || fn.rfind(pfx3d, 0) == 0)
            fs::remove(e.path(), ec);
    }
}

bool SemSession::LoadOffsetShells(Scene& scene, bool silent, const std::string& dir) {
    m_offsets = scene.AddGroupNode("offsets", m_srcPrim);
    const char* prefix = (dim == 3) ? "_offset3d_" : "_offset_";
    const std::string base = dir.empty() ? m_workDir : dir;

    Stats agg;
    int loaded = 0;
    for (int i = 0; ; ++i) {
        std::string suffix = std::string(prefix) + std::to_string(i) + ".csv3d";
        std::string p = (fs::path(base) / (Stem(m_srcPath) + suffix)).string();
        if (!fs::exists(p)) break;

        Primitive* shell = scene.AddFromCSV3D(p, "offset_" + std::to_string(i),
                                              m_offsets, nullptr, Colors::CYAN, Colors::YELLOW);
        if (!shell) continue;
        if (dim == 3) {
            ConfigureSurface3D(shell);
            shell->SetColor(Colors::FRONT_FACE_WHITE);
            shell->SetUseVertexColor(false);
            shell->SetTwoSided(true, Colors::BACK_FACE_RED);
            shell->visible = false;
        } else {
            shell->SetColor(Colors::WHITE);
            shell->SetUseVertexColor(false);
        }

        Stats s = ComputeStats(p);
        agg.valid = true;
        agg.verts += s.verts; agg.edges += s.edges;
        agg.tris  += s.tris;  agg.boundary += s.boundary;
        ++loaded;
    }

    if (loaded == 0) {
        scene.RemoveNode(m_offsets);
        m_offsets = nullptr; m_offStats = Stats();
        Report(scene, silent, "Offsets: no shell files produced.");
        return false;
    }
    m_offStats = agg;
    snprintf(status, sizeof(status), "Offsets: %d shells.", loaded);
    return true;
}

void SemSession::DropMesh(Scene& scene) {
    if (Alive(scene, m_mesh)) scene.RemovePrimitive(m_mesh);
    m_mesh = nullptr; m_meshStats = Stats(); m_meshPath.clear();
    m_thermalSolved = false;
    DropIsoline(scene);
}

void SemSession::ReloadMeshColored(Scene& scene) {
    if (m_meshPath.empty() || !Alive(scene, m_mesh)) return;
    // Snapshot what a focused reload must preserve; unlike DropMesh this neither
    // clears m_meshPath/m_thermalSolved nor drops the isotherm.
    const std::string path = m_meshPath;
    const bool wasVisible = m_mesh->visible;
    const Stats keepStats = m_meshStats;

    // Rebuild from the cache-resident mesh (the solver wrote the steady-state T
    // into each node in place), not the disk file.
    CSV3DLoader::CSV3DData data;
    if (!FetchMeshData(scene, true, data)) return;

    scene.RemovePrimitive(m_mesh);
    // Register both colour sets ("tfield"/"bc") so the BC/T toggle can switch the
    // mesh in place afterwards without reloading the file (see SetBCView).
    m_mesh = scene.AddFromCSV3DData(data, "mesh_" + Stem(m_srcPath), AttachParent(),
                                    nullptr, Colors::BLUE, Colors::RED, true, bcView, true);
    if (!m_mesh) { Report(scene, true, "Recolour mesh: reload failed."); return; }
    m_meshPath  = path;
    m_meshStats = keepStats;
    if (dim == 3) ConfigureSurface3D(m_mesh);
    if (Alive(scene, m_mesh)) scene.SetNodeVisibleCascade(m_mesh, wasVisible);
    scene.UpdateLight();
}

bool SemSession::FetchMeshData(Scene& scene, bool silent, CSV3DLoader::CSV3DData& out) {
    SEM_MeshView v{};
    int rc = (dim == 3) ? SEM_GetMesh3D(&v) : SEM_GetMesh(&v);
    if (rc != 0) {
        Report(scene, silent, std::string(dim == 3 ? "SEM_GetMesh3D" : "SEM_GetMesh")
               + " failed (" + std::to_string(rc) + ")" + SemDetail());
        return false;
    }
    out = ViewToData(v);
    return true;
}

bool SemSession::FetchIsoData(Scene& scene, bool silent, CSV3DLoader::CSV3DData& out) {
    SEM_MeshView v{};
    int rc = (dim == 3) ? SEM_GetIsosurface3D(&v) : SEM_GetIsoline(&v);
    if (rc != 0) {
        Report(scene, silent, std::string(dim == 3 ? "SEM_GetIsosurface3D" : "SEM_GetIsoline")
               + " failed (" + std::to_string(rc) + ")" + SemDetail());
        return false;
    }
    out = ViewToData(v);
    return true;
}

void SemSession::DropIsoline(Scene& scene) {
    if (Alive(scene, m_isoline)) scene.RemovePrimitive(m_isoline);
    m_isoline = nullptr;
    DropIsoRev(scene);
    m_isolinePath.clear();
    // A new (or cleared) extraction invalidates the intermediate offset/remesh
    // stages and their caches, so drop their primitives and the gate flag too.
    DropIsoSource(scene);
    DropIsoProjection(scene);
    m_isoProjected = false;
}

void SemSession::DropIsoSource(Scene& scene) {
    if (Alive(scene, m_isoSource)) scene.RemovePrimitive(m_isoSource);
    m_isoSource = nullptr;
}

void SemSession::DropIsoProjection(Scene& scene) {
    if (Alive(scene, m_isoProj)) scene.RemovePrimitive(m_isoProj);
    m_isoProj = nullptr;
}

// SEM_GetSourceIsosurface3D / SEM_GetIsosurfaceProjection3D return the two
// intermediate stages of an offset-and-remesh extraction directly from the cache
// (negative if no projection extraction has run). The view's arrays are valid
// only until the next SEM_* call, so ViewToData copies everything out.
bool SemSession::FetchSourceIsoData(Scene& scene, bool silent, CSV3DLoader::CSV3DData& out) {
    SEM_MeshView v{};
    int rc = SEM_GetSourceIsosurface3D(&v);
    if (rc != 0) {
        Report(scene, silent, "SEM_GetSourceIsosurface3D failed (" + std::to_string(rc) + ")" + SemDetail());
        return false;
    }
    out = ViewToData(v);
    return true;
}

bool SemSession::FetchIsoProjData(Scene& scene, bool silent, CSV3DLoader::CSV3DData& out) {
    SEM_MeshView v{};
    int rc = SEM_GetIsosurfaceProjection3D(&v);
    if (rc != 0) {
        Report(scene, silent, "SEM_GetIsosurfaceProjection3D failed (" + std::to_string(rc) + ")" + SemDetail());
        return false;
    }
    out = ViewToData(v);
    return true;
}

bool SemSession::ShowSourceIsosurface3D(Scene& scene, bool show, bool silent) {
    if (!show) { DropIsoSource(scene); scene.UpdateLight(); return true; }
    if (dim != 3) { Report(scene, silent, "Source isosurface is 3D only."); return false; }
    if (!m_isoProjected) {
        Report(scene, silent, "Extract the isosurface with an offset axis first.");
        return false;
    }
    CSV3DLoader::CSV3DData data;
    if (!FetchSourceIsoData(scene, silent, data)) return false;
    DropIsoSource(scene);
    const XMFLOAT4 orange(1.0f, 0.5f, 0.0f, 1.0f);
    m_isoSource = scene.AddFromCSV3DData(data, "isosource_" + Stem(m_srcPath),
                                         AttachParent(), &orange, Colors::BLUE, Colors::RED);
    if (!m_isoSource) { Report(scene, silent, "Source isosurface: build failed."); return false; }
    ConfigureSurface3D(m_isoSource);
    scene.UpdateLight();
    return true;
}

bool SemSession::ShowIsosurfaceProjection3D(Scene& scene, bool show, bool silent) {
    if (!show) { DropIsoProjection(scene); scene.UpdateLight(); return true; }
    if (dim != 3) { Report(scene, silent, "Isosurface projection is 3D only."); return false; }
    if (!m_isoProjected) {
        Report(scene, silent, "Extract the isosurface with an offset axis first.");
        return false;
    }
    CSV3DLoader::CSV3DData data;
    if (!FetchIsoProjData(scene, silent, data)) return false;
    DropIsoProjection(scene);
    const XMFLOAT4 magenta = Colors::MAGENTA;
    m_isoProj = scene.AddFromCSV3DData(data, "isoproj_" + Stem(m_srcPath),
                                       AttachParent(), &magenta, Colors::BLUE, Colors::RED);
    if (!m_isoProj) { Report(scene, silent, "Isosurface projection: build failed."); return false; }
    ConfigureSurface3D(m_isoProj);
    scene.UpdateLight();
    return true;
}

bool SemSession::ApplySubdivide(Scene& scene, bool silent) {
    try {
        int n = !subEnabled ? -1
              : (subMode == 0) ? -1
              : (subMode == 1) ? 0
              : (subN < 1 ? 1 : subN);
        if (dim == 3) {
            int rc = SEM_SubdivideSurface3D(n);
            return CheckRc(scene, silent, "SEM_SubdivideSurface3D", rc,
                           { "", "No surface loaded", "Invalid argument" });
        }
        int rc = SEM_SubdivideContour(n);
        return CheckRc(scene, silent, "SEM_SubdivideContour", rc,
                       { "", "No source loaded", "Invalid argument" });
    }
    catch (const std::exception& e) { Report(scene, silent, std::string("Subdivide exception: ") + e.what()); }
    catch (...)                     { Report(scene, silent, "Subdivide: unknown exception."); }
    return false;
}

bool SemSession::ApplyOffsets(Scene& scene, bool silent) {
    try {
        DropOffsets(scene);
        CleanupOffsetFiles();
        int rc;
        Timer t; t.Restart();
        if (offsetMode == OFFSET_GAPS) {
            if (gaps.empty()) { Report(scene, silent, "Add at least one gap."); return false; }
            std::vector<double> g(gaps.begin(), gaps.end());
            rc = (dim == 3) ? SEM_ComputeOffsetsAt3D(g.data(), (int)g.size())
                            : SEM_ComputeOffsetsAt(g.data(), (int)g.size());
        } else {
            rc = (dim == 3) ? SEM_ComputeOffsets3D(firstGap, numOffsets, grading)
                            : SEM_ComputeOffsets(firstGap, numOffsets, grading);
        }
        const double ms = t.GetMillisecondsElapsed();
        if (!CheckRc(scene, silent, dim == 3 ? "SEM_ComputeOffsets3D" : "SEM_ComputeOffsets", rc,
                     { "", "No source loaded", "Invalid parameters" }))
            return false;
        m_offsetsMs = ms;

        return LoadOffsetShells(scene, silent);
    }
    catch (const std::exception& e) { Report(scene, silent, std::string("Offsets exception: ") + e.what()); }
    catch (...)                     { Report(scene, silent, "Offsets: unknown exception."); }
    return false;
}

bool SemSession::ApplyMesh(Scene& scene, bool silent) {
    try {
        int rc;
        if (dim == 3) {
            double param = (double)tetParam;
            if (tetParamEdgeUnits && tetParam > 0.0f && tetMethod == SEM_TET_BAND)
                param *= TetParamFactor();
            SEM_MeshParams3D params3d{ tetMethod, param, (double)tetMaxEdgeLen };
            Timer t; t.Restart();
            rc = SafeBuildMesh3D(&params3d);
            const double ms = t.GetMillisecondsElapsed();
            if (rc == -100) {
                Report(scene, silent, "TetGen DLL crashed (access violation caught). "
                                      "Try a larger volume or a looser quality bound.");
                return false;
            }
            if (!CheckRc(scene, silent, "SEM_BuildMesh3D", rc,
                         { "", "No surface loaded", "Compute offsets first",
                           "", "Tetrahedralization failed", "Invalid method" }))
                return false;
            m_meshMs = ms;
        } else {
            double param = (double)meshParam;
            if (meshParamEdgeUnits && meshParam > 0.0f && meshMethod == SEM_STEINER_GRID)
                param *= MeshParamFactor();
            SEM_MeshParams params{ meshMethod, param, (double)steinerMargin };
            Timer t; t.Restart();
            rc = SafeBuildMesh(&params);
            const double ms = t.GetMillisecondsElapsed();
            if (rc == -100) {
                Report(scene, silent, "Mesher DLL crashed (access violation caught). "
                                      "Try another Steiner method or a larger parameter.");
                return false;
            }
            if (!CheckRc(scene, silent, "SEM_BuildMesh", rc,
                         { "", "No source loaded", "Compute offsets first",
                           "Not enough valid lines", "Triangulation failed", "Invalid method" }))
                return false;
            m_meshMs = ms;
        }

        CSV3DLoader::CSV3DData data;
        if (!FetchMeshData(scene, silent, data)) return false;
        std::string p = OutPath(dim == 3 ? "_mesh3d.csv3d" : "_mesh.csv3d");
        DropMesh(scene);
        m_mesh = scene.AddFromCSV3DData(data, "mesh_" + Stem(m_srcPath), AttachParent(), nullptr, Colors::CYAN, Colors::YELLOW);
        m_meshPath = p;
        if (dim == 3) ConfigureSurface3D(m_mesh);
        m_meshStats = ComputeStatsData(data);
        snprintf(status, sizeof(status), "Mesh: %s", BaseName(p).c_str());
        return true;
    }
    catch (const std::exception& e) { Report(scene, silent, std::string("Mesh exception: ") + e.what()); }
    catch (...)                     { Report(scene, silent, "Mesh: unknown exception."); }
    return false;
}

}
