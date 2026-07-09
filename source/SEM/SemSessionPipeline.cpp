#include "SemSessionDetail.h"
#include <filesystem>

namespace fs = std::filesystem;

namespace SemSessionNS {

using namespace detail;

// Synchronous pipeline driver — the 2D path only. 3D is driven asynchronously on
// a worker thread (RecomputeUpToAsync / PipelineWorkerBody), which the UI always
// calls; that entry point redirects 2D here. The ApplyOffsets/Mesh/Thermal/Isoline
// helpers below are therefore 2D-only (their former dim==3 branches duplicated the
// worker and were dead). The guard makes the 2D-only contract explicit.
void SemSession::RecomputeUpTo(Scene& scene, Stage to, bool silent) {
    if (!HasSource() || dim == 3) return;
    bool ran = false;

    if (m_dirty[STAGE_SUBDIVIDE]) {
        if (!ApplySubdivide(scene, silent)) return;
        m_dirty[STAGE_SUBDIVIDE] = false;
        ran = true;
    }

    // Visibility rule for a cascaded run: only the stage whose Apply was pressed
    // (== `to`) is shown; the prerequisite stages this call had to (re)build to
    // reach it are created hidden. The user reveals them with the tree eye.
    if (to >= STAGE_OFFSETS && (m_dirty[STAGE_OFFSETS] || ran)) {
        if (offEnabled || (m_offsets && Alive(scene, m_offsets))) {
            if (!ApplyOffsets(scene, silent)) return;
            if (m_offsets && Alive(scene, m_offsets)) scene.SetNodeVisibleCascade(m_offsets, to == STAGE_OFFSETS);
            ran = true;
        }
        m_dirty[STAGE_OFFSETS] = false;
    }

    if (to >= STAGE_MESH && (m_dirty[STAGE_MESH] || ran)) {
        if (meshEnabled || (m_mesh && Alive(scene, m_mesh))) {
            if (!ApplyMesh(scene, silent)) return;
            if (m_mesh && Alive(scene, m_mesh)) scene.SetNodeVisibleCascade(m_mesh, to == STAGE_MESH);
            ran = true;
        }
        m_dirty[STAGE_MESH] = false;
    }

    // Thermal solve: heat-conduction solve + recolour the band mesh in place. No
    // isosurface here — that is the separate STAGE_ISOSURFACE below.
    if (to >= STAGE_THERMAL && (m_dirty[STAGE_THERMAL] || ran)) {
        if (thermalEnabled || m_thermalSolved) {
            if (!ApplyThermal(scene, silent)) return;
            // The thermal product is the recoloured mesh: show it only when
            // thermal is the pressed stage, hide it when it ran as a prerequisite.
            if (m_mesh && Alive(scene, m_mesh)) scene.SetNodeVisibleCascade(m_mesh, to == STAGE_THERMAL);
            ran = true;
        }
        m_dirty[STAGE_THERMAL] = false;
    }

    // Isosurface: extract the iso sheet (with its offset/remesh parameters) from
    // the already-solved field.
    if (to >= STAGE_ISOSURFACE && (m_dirty[STAGE_ISOSURFACE] || ran)) {
        if (isoEnabled || (m_isoline && Alive(scene, m_isoline))) {
            if (ApplyIsoline(scene, silent) && m_isoline && Alive(scene, m_isoline))
                scene.SetNodeVisibleCascade(m_isoline, to == STAGE_ISOSURFACE);
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
    // the isotherm.
    Timer t; t.Restart();
    int rc = SafeSolveThermal();
    const double ms = t.GetMillisecondsElapsed();
    if (rc == -100) {
        Report(scene, silent, "Thermal solver crashed (access violation caught).");
        return false;
    }
    if (!CheckRc(scene, silent, "SEM_SolveThermal", rc,
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
    // 2D only: the 3D isosurface (with its offset-and-remesh) is extracted on the
    // worker thread (PipelineWorkerBody) — see RecomputeUpTo's note.
    bool revWasShown = Alive(scene, m_isoRevSurf) && m_isoRevSurf->visible;
    try {
        double v = isoValue;
        if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0;
        Timer t; t.Restart();
        int rc = SafeExtractIsoline(v);
        if (rc == -100) {
            Report(scene, silent, "Isoline extraction crashed (access violation caught).");
            return false;
        }
        if (!CheckRc(scene, silent, "SEM_ExtractIsoline", rc,
                     { "", "No source loaded", "No mesh built", "Invalid value", "Extraction failed" }))
            return false;

        CSV3DLoader::CSV3DData data;
        if (!FetchIsoData(scene, silent, data)) return false;
        m_isoMs = t.GetMillisecondsElapsed();
        std::string p = OutPath("_isoline.csv3d");
        DropIsoline(scene);
        m_isoData = data;
        m_isoline = BuildIsoDisplay(scene, m_isoData);
        m_isolinePath = p;
        if (revWasShown && m_isoline) BuildIsolineRevolution(scene);
        snprintf(status, sizeof(status), "Isoline T=%.3f: %s", v, BaseName(p).c_str());
        return true;
    }
    catch (const std::exception& e) { Report(scene, silent, std::string("Isoline exception: ") + e.what()); }
    catch (...)                     { Report(scene, silent, "Isoline: unknown exception."); }
    return false;
}

Primitive* SemSession::BuildIsoDisplay(Scene& scene, const CSV3DLoader::CSV3DData& data) {
    const std::string name = (dim == 3 ? "isosurface_" : "isoline_") + Stem(m_srcPath);
    const XMFLOAT4 green = Colors::GREEN;
    Primitive* prim = nullptr;

    if (dim == 3 && (isoShowWinding || isoShowNonManifold) && !data.triangles.empty()) {
        // Per-triangle defect colouring. Non-manifold triangles (edges shared by 3+
        // faces, or bowtie vertices — the artefacts vertex welding leaves) are painted
        // bright magenta; minority-winding triangles pure red; the rest green. The
        // triangles are emitted with their real winding (ColoredTriangles no longer
        // re-orients), so the minority genuinely reads as red. Non-manifold takes
        // priority over winding when both highlights are on.
        std::vector<char> minority = isoShowWinding    ? WindingMinorityTris(data) : std::vector<char>();
        std::vector<char> nonman   = isoShowNonManifold ? NonManifoldTris(data)     : std::vector<char>();
        const size_t nN = data.nodes.size();
        std::vector<XMFLOAT3> poses;
        std::vector<XMFLOAT4> cols;
        poses.reserve(data.triangles.size() * 3);
        cols.reserve(data.triangles.size() * 3);
        for (size_t i = 0; i < data.triangles.size(); ++i) {
            const auto& t = data.triangles[i];
            if (t.x >= nN || t.y >= nN || t.z >= nN) continue;
            const XMFLOAT4& c = (!nonman.empty()   && nonman[i])   ? Colors::MAGENTA
                              : (!minority.empty() && minority[i]) ? Colors::RED
                                                                   : green;
            poses.push_back(data.nodes[t.x].pos); cols.push_back(c);
            poses.push_back(data.nodes[t.y].pos); cols.push_back(c);
            poses.push_back(data.nodes[t.z].pos); cols.push_back(c);
        }
        prim = scene.AddColoredTriangles(poses, cols, name, AttachParent());
    } else {
        // The SEM extractor already orients the isosurface consistently, so its
        // winding is drawn as-is (there is no orientation fix-up pass any more —
        // it used to mis-flip regions across sharp creases and light them from the
        // back, the spurious dark-green patch). Use "Flip isosurface" if the whole
        // sheet faces inward.
        prim = scene.AddFromCSV3DData(data, name, AttachParent(), &green, Colors::BLUE, Colors::RED);
    }
    if (prim && dim == 3) ConfigureSurface3D(prim);
    return prim;
}

Primitive* SemSession::BuildPseudonormalLines(Scene& scene, const CSV3DLoader::CSV3DData& data,
                                              const XMFLOAT4& color, const std::string& name,
                                              SceneNode* parent) {
    return BuildPseudonormalLines(scene, data, VertexPseudonormals(data), color, name, parent);
}

Primitive* SemSession::BuildPseudonormalLines(Scene& scene, const CSV3DLoader::CSV3DData& data,
                                              const std::vector<XMFLOAT3>& normalsIn,
                                              const XMFLOAT4& color, const std::string& name,
                                              SceneNode* parent) {
    if (data.nodes.empty() || data.triangles.empty()) return nullptr;
    // Fall back to geometry-derived pseudonormals when none were supplied (or the
    // supplied set does not match the vertex count).
    std::vector<XMFLOAT3> normals = (normalsIn.size() == data.nodes.size())
                                  ? normalsIn : VertexPseudonormals(data);
    const float len = MeanTriEdgeLen(data) * 0.8f;

    // One explicit edge per vertex (base -> base + n*len); the standard loader
    // turns the edge section into a thickened line-list primitive.
    CSV3DLoader::CSV3DData seg;
    seg.nodes.reserve(data.nodes.size() * 2);
    seg.edges.reserve(data.nodes.size());
    for (size_t i = 0; i < data.nodes.size(); ++i) {
        const XMFLOAT3& p = data.nodes[i].pos;
        const XMFLOAT3& nrm = normals[i];
        if (nrm.x == 0.0f && nrm.y == 0.0f && nrm.z == 0.0f) continue;   // unreferenced vertex
        unsigned a = (unsigned)seg.nodes.size();
        CSV3DLoader::Node n0{}; n0.pos = p;
        CSV3DLoader::Node n1{}; n1.pos = XMFLOAT3(p.x + nrm.x * len, p.y + nrm.y * len, p.z + nrm.z * len);
        seg.nodes.push_back(n0);
        seg.nodes.push_back(n1);
        seg.edges.push_back({ a, a + 1 });
    }
    if (seg.edges.empty()) return nullptr;
    Primitive* lines = scene.AddFromCSV3DData(seg, name, parent, &color);
    // Normals are the third-thinnest tier (thinner than the on-plane lines, thicker
    // than the band-mesh edges).
    StyleLines(lines, LINESTYLE_THIN);
    return lines;
}

void SemSession::RebuildIsoDisplayInPlace(Scene& scene) {
    // Nothing displayed yet (or 2D): the caller's flag simply takes effect at the
    // next extraction.
    if (dim != 3 || !Alive(scene, m_isoline) || m_isoData.triangles.empty()) return;

    // Recolour in place from the cached display geometry: rebuild only the iso
    // primitive (and its dependent overlays), preserving visibility and the
    // revolution / clip-mirror surfaces — no re-extraction.
    const bool vis = m_isoline->visible;
    const bool revWasShown  = Alive(scene, m_isoRevSurf) && m_isoRevSurf->visible;
    const bool normalsShown = m_isoNormals != nullptr;

    DropIsoNormals(scene);
    DropIsoRev(scene);
    scene.RemovePrimitive(m_isoline);
    m_isoline = BuildIsoDisplay(scene, m_isoData);
    if (m_isoline) {
        if (Alive(scene, m_isoline)) scene.SetNodeVisibleCascade(m_isoline, vis);
        if (normalsShown) ShowIsoPseudonormals(scene, true, true);
        if (revWasShown)  BuildIsolineRevolution(scene);
    }
    if (AnyClipMirror()) RebuildClipMirrors(scene);
    scene.UpdateLight();
}

void SemSession::SetIsoWinding(Scene& scene, bool on) {
    if (isoShowWinding == on) return;
    isoShowWinding = on;
    RebuildIsoDisplayInPlace(scene);
}

void SemSession::SetIsoNonManifold(Scene& scene, bool on) {
    if (isoShowNonManifold == on) return;
    isoShowNonManifold = on;
    RebuildIsoDisplayInPlace(scene);
}

bool SemSession::ShowSourcePseudonormals(Scene& scene, bool show, bool silent) {
    if (!show) { DropSrcNormals(scene); scene.UpdateLight(); return true; }
    if (!HasSource()) { Report(scene, silent, "Stage a source first."); return false; }
    if (dim != 3) { Report(scene, silent, "Pseudonormals apply to a 3D surface only."); return false; }
    // Take the active source surface and its vertex pseudonormals straight from the
    // SEM core (the geometry as the pipeline sees it — subdivided and clip-snapped),
    // so the drawn normals match the ones the core computes. Read both off the same
    // view before any other SEM_* call (the pointers are cache-owned, see SEM_MeshView).
    SEM_MeshView v{};
    if (SEM_GetSourceSurface3D(&v) != 0 || v.num_nodes <= 0 || v.num_tris <= 0) {
        Report(scene, silent, "Source has no triangle surface.");
        return false;
    }
    CSV3DLoader::CSV3DData data = ViewToData(v);
    std::vector<XMFLOAT3> normals = ViewNormals(v);
    DropSrcNormals(scene);
    m_srcNormals = BuildPseudonormalLines(scene, data, normals, Colors::YELLOW,
                                          "src_normals_" + Stem(m_srcPath), AttachParent());
    if (!m_srcNormals) { Report(scene, silent, "Source pseudonormals: build failed."); return false; }
    scene.UpdateLight();
    snprintf(status, sizeof(status), "Source pseudonormals shown.");
    return true;
}

bool SemSession::ShowIsoPseudonormals(Scene& scene, bool show, bool silent) {
    if (!show) { DropIsoNormals(scene); scene.UpdateLight(); return true; }
    if (dim != 3) { Report(scene, silent, "Pseudonormals apply to the 3D isosurface only."); return false; }
    if (!Alive(scene, m_isoline) || m_isoData.triangles.empty()) {
        Report(scene, silent, "Extract the isosurface first.");
        return false;
    }
    DropIsoNormals(scene);
    m_isoNormals = BuildPseudonormalLines(scene, m_isoData, Colors::CYAN,
                                          "iso_normals_" + Stem(m_srcPath), m_isoline);
    if (!m_isoNormals) { Report(scene, silent, "Isosurface pseudonormals: build failed."); return false; }
    scene.UpdateLight();
    snprintf(status, sizeof(status), "Isosurface pseudonormals shown.");
    return true;
}

bool SemSession::ShowSourceIsoPseudonormals(Scene& scene, bool show, bool silent) {
    if (!show) { DropIsoSrcNormals(scene); scene.UpdateLight(); return true; }
    if (dim != 3) { Report(scene, silent, "Pseudonormals apply to the 3D isosurface only."); return false; }
    if (!m_isoProjected) {
        Report(scene, silent, "Extract the isosurface with an offset axis first.");
        return false;
    }
    // The pre-offset source isosurface (the orange sheet) lives only in the SEM
    // cache; fetch it on demand so the overlay shows even when the sheet itself is
    // hidden. Its pseudonormals (the core's, the directions the remesh shifted each
    // vertex along) come straight off the same view — read before any other SEM_* call.
    SEM_MeshView v{};
    if (SEM_GetSourceIsosurface3D(&v) != 0) {
        Report(scene, silent, "SEM_GetSourceIsosurface3D failed" + SemDetail());
        return false;
    }
    CSV3DLoader::CSV3DData data = ViewToData(v);
    std::vector<XMFLOAT3> normals = ViewNormals(v);
    DropIsoSrcNormals(scene);
    const XMFLOAT4 orange(1.0f, 0.5f, 0.0f, 1.0f);
    m_isoSrcNormals = BuildPseudonormalLines(scene, data, normals, orange,
                                             "isosrc_normals_" + Stem(m_srcPath), AttachParent());
    if (!m_isoSrcNormals) { Report(scene, silent, "Source isosurface pseudonormals: build failed."); return false; }
    scene.UpdateLight();
    snprintf(status, sizeof(status), "Source isosurface pseudonormals shown.");
    return true;
}

bool SemSession::ShowIsosurfaceLoops3D(Scene& scene, bool show, bool silent) {
    if (!show) { DropIsoLoops(scene); scene.UpdateLight(); return true; }
    if (dim != 3) { Report(scene, silent, "Isosurface loops are 3D only."); return false; }
    if (!m_isoProjected) {
        Report(scene, silent, "Extract the isosurface with an offset axis first.");
        return false;
    }
    // The loops are the closed boundary loops of the source isosurface used as the
    // offset-remesh's CDT constraints: coords are the source-iso vertices, edges the
    // per-loop wireframe (each loop closed last->first). Both live only in the SEM
    // cache; ViewToData copies them out before any other SEM_* call invalidates them.
    SEM_MeshView v{};
    if (SEM_GetIsosurfaceLoops3D(&v) != 0) {
        Report(scene, silent, "SEM_GetIsosurfaceLoops3D failed" + SemDetail());
        return false;
    }
    CSV3DLoader::CSV3DData data = ViewToData(v);
    if (data.edges.empty()) { Report(scene, silent, "No isosurface loops to show."); return false; }
    DropIsoLoops(scene);
    const XMFLOAT4 magenta = Colors::MAGENTA;
    m_isoLoops = scene.AddFromCSV3DData(data, "isoloops_" + Stem(m_srcPath),
                                        AttachParent(), &magenta);
    if (!m_isoLoops) { Report(scene, silent, "Isosurface loops: build failed."); return false; }
    // Thickest tier of the SEM line overlays.
    StyleLines(m_isoLoops, LINESTYLE_THICK);
    scene.UpdateLight();
    snprintf(status, sizeof(status), "Isosurface loops shown.");
    return true;
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
                                    nullptr, Colors::BLUE, Colors::RED, bcView, true);
    if (!m_mesh) { Report(scene, true, "Recolour mesh: reload failed."); return; }
    m_meshPath  = path;
    m_meshStats = keepStats;
    StyleLines(m_mesh, LINESTYLE_HAIRLINE);
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
    // Drop the pseudonormal overlay first — it is parented under m_isoline, so
    // removing the isosurface would otherwise destroy it and dangle the pointer.
    DropIsoNormals(scene);
    if (Alive(scene, m_isoline)) scene.RemovePrimitive(m_isoline);
    m_isoline = nullptr;
    m_isoData = CSV3DLoader::CSV3DData();
    DropIsoRev(scene);
    m_isolinePath.clear();
    // A new (or cleared) extraction invalidates the intermediate offset/remesh
    // stages and their caches, so drop their primitives (and the source-iso
    // pseudonormal overlay computed from them) and the gate flag too.
    DropIsoSource(scene);
    DropIsoProjection(scene);
    DropIsoSrcNormals(scene);
    DropIsoLoops(scene);
    m_isoProjected = false;
}

void SemSession::DropSrcNormals(Scene& scene) {
    if (Alive(scene, m_srcNormals)) scene.RemovePrimitive(m_srcNormals);
    m_srcNormals = nullptr;
}

void SemSession::DropIsoNormals(Scene& scene) {
    if (Alive(scene, m_isoNormals)) scene.RemovePrimitive(m_isoNormals);
    m_isoNormals = nullptr;
}

void SemSession::DropIsoSrcNormals(Scene& scene) {
    if (Alive(scene, m_isoSrcNormals)) scene.RemovePrimitive(m_isoSrcNormals);
    m_isoSrcNormals = nullptr;
}

void SemSession::DropIsoLoops(Scene& scene) {
    if (Alive(scene, m_isoLoops)) scene.RemovePrimitive(m_isoLoops);
    m_isoLoops = nullptr;
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
            rc = SEM_ComputeOffsetsAt(g.data(), (int)g.size());
        } else {
            rc = SEM_ComputeOffsets(firstGap, numOffsets, grading);
        }
        const double ms = t.GetMillisecondsElapsed();
        if (!CheckRc(scene, silent, "SEM_ComputeOffsets", rc,
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
        double param = (double)meshParam;
        SEM_MeshParams params{ meshMethod, param, (double)steinerMargin };
        Timer t; t.Restart();
        int rc = SafeBuildMesh(&params);
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

        CSV3DLoader::CSV3DData data;
        if (!FetchMeshData(scene, silent, data)) return false;
        std::string p = OutPath("_mesh.csv3d");
        DropMesh(scene);
        m_mesh = scene.AddFromCSV3DData(data, "mesh_" + Stem(m_srcPath), AttachParent(), nullptr, Colors::CYAN, Colors::YELLOW);
        StyleLines(m_mesh, LINESTYLE_HAIRLINE);
        m_meshPath = p;
        m_meshStats = ComputeStatsData(data);
        snprintf(status, sizeof(status), "Mesh: %s", BaseName(p).c_str());
        return true;
    }
    catch (const std::exception& e) { Report(scene, silent, std::string("Mesh exception: ") + e.what()); }
    catch (...)                     { Report(scene, silent, "Mesh: unknown exception."); }
    return false;
}

}
