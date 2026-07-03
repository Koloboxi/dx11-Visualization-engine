#include "SemSessionDetail.h"
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace SemSessionNS {

using namespace detail;

std::string SemSession::OutPath(const char* suffix) const {
    return (fs::path(m_workDir) / (Stem(m_srcPath) + suffix)).string();
}

std::string SemSession::SessionRoot() {
    std::error_code ec;
    auto tmp = fs::temp_directory_path(ec);
    if (ec) return std::string();
    return (tmp / "sem").string();
}

// Parse the trailing _<N> of a session folder name "<stem>_<N>". Returns 0 (not
// a session) unless the name is exactly stem + '_' + digits.
static int SessionIndex(const std::string& folderName, const std::string& stem) {
    const std::string prefix = stem + "_";
    if (folderName.size() <= prefix.size() ||
        folderName.compare(0, prefix.size(), prefix) != 0)
        return 0;
    const std::string num = folderName.substr(prefix.size());
    if (num.empty()) return 0;
    for (char c : num) if (c < '0' || c > '9') return 0;
    try { return std::stoi(num); } catch (...) { return 0; }
}

std::vector<std::string> SemSession::ListSessions(const std::string& srcPath) {
    std::vector<std::string> dirs;
    const std::string root = SessionRoot();
    if (root.empty() || srcPath.empty()) return dirs;
    const std::string stem = Stem(srcPath);

    std::vector<std::pair<int, std::string>> found;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(root, ec)) {
        if (ec) break;
        if (!e.is_directory()) continue;
        int n = SessionIndex(e.path().filename().string(), stem);
        if (n > 0) found.emplace_back(n, e.path().string());
    }
    std::sort(found.begin(), found.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& f : found) dirs.push_back(std::move(f.second));
    return dirs;
}

std::string SemSession::NewSessionDir(const std::string& srcPath) {
    const std::string root = SessionRoot();
    if (root.empty() || srcPath.empty()) return std::string();
    const std::string stem = Stem(srcPath);
    int maxN = 0;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(root, ec)) {
        if (ec) break;
        if (!e.is_directory()) continue;
        maxN = std::max(maxN, SessionIndex(e.path().filename().string(), stem));
    }
    return (fs::path(root) / (stem + "_" + std::to_string(maxN + 1))).string();
}

Primitive* SemSession::ImportSource(Scene& scene, const std::string& path,
                                    const std::string& workDir, bool reload) {
    if (path.empty()) return nullptr;
    Primitive* src = nullptr;
    try {
        src = scene.AddFromCSV3D(path, "", nullptr, nullptr, Colors::BLUE, Colors::RED);
        if (!src) { Report(scene, false, "Import failed: could not load CSV3D."); return nullptr; }
        src->semSourcePath = path;
        // Session folder to serialize into; empty => Bind allocates a fresh one.
        src->semWorkDir = workDir;
        scene.stagingEnabled = true;
        scene.SetStaged(src);
        Bind(scene, src, reload);
        if (dim == 3) {
            ConfigureSurface3D(src);
            src->SetColor(Colors::FRONT_FACE_WHITE);
            src->SetUseVertexColor(false);
            src->SetTwoSided(true, Colors::BACK_FACE_RED);
        } else {
            src->SetColor(Colors::WHITE);
            src->SetUseVertexColor(false);
        }
        scene.UpdateLight();
        // Revolution mode is a 2D-contour feature. For a 2D source, an open
        // half-profile whose endpoints sit on the Y axis is a surface-of-
        // revolution contour — turn the mode on automatically; otherwise off.
        if (dim == 2) SetRevolutionMode(scene, IsOpenContourOnYAxis(path));
        else          { SEM_SetRevolution(0, kRevolutionAxisY); revolutionMode = false; }
    }
    catch (const std::exception& e) { Report(scene, false, std::string("Import failed: ") + e.what()); }
    catch (...)                     { Report(scene, false, "Import failed: unknown exception."); }
    return src;
}

bool SemSession::ImportOffsets(Scene& scene, const std::string& dir) {
    if (!HasSource()) { Report(scene, false, "Import offsets: no staged source."); return false; }
    if (AsyncRunning()) return false;
    // 3D: the offsets are already in the SEM cache (SEM_LoadSession3D restored the
    // whole session in one call); only the 2D pipeline still reloads the core here.
    // Either way we then rebuild the scene from the on-disk shells below.
    if (dim != 3) {
        const char* d = dir.empty() ? nullptr : dir.c_str();
        int rc = SEM_LoadOffsets(d);
        if (!CheckRc(scene, false, "SEM_LoadOffsets", rc,
                     { "", "No source loaded", "No offset files found" }))
            return false;
    }

    DropOffsets(scene);
    if (!LoadOffsetShells(scene, false, dir)) return false;
    if (m_offsets && Alive(scene, m_offsets)) scene.SetNodeVisibleCascade(m_offsets, true);

    // The reloaded offsets (and the subdivide they came from) now populate the
    // cache; recomputing either would clobber it, so mark both clean. The mesh
    // and everything downstream is stale relative to the imported offsets.
    DropMesh(scene);
    m_dirty[STAGE_SUBDIVIDE]  = false;
    m_dirty[STAGE_OFFSETS]    = false;
    m_dirty[STAGE_MESH]       = true;
    m_dirty[STAGE_THERMAL]    = true;
    m_dirty[STAGE_ISOSURFACE] = true;
    scene.UpdateLight();
    snprintf(status, sizeof(status), "Imported offsets.");
    return true;
}

bool SemSession::ImportMesh(Scene& scene, const std::string& path) {
    if (!HasSource()) { Report(scene, false, "Import mesh: no staged source."); return false; }
    if (AsyncRunning()) return false;
    // 3D mesh is already restored into the SEM cache by SEM_LoadSession3D; only 2D
    // reloads the core here. The scene mesh is then built from the file below.
    if (dim != 3) {
        int rc = SEM_LoadMesh(path.c_str());
        if (!CheckRc(scene, false, "SEM_LoadMesh", rc,
                     { "", "No source loaded", "Load failed", "Missing #tets section" }))
            return false;
    }

    DropMesh(scene);
    m_mesh = scene.AddFromCSV3D(path, "mesh_" + Stem(m_srcPath), AttachParent(),
                                nullptr, Colors::CYAN, Colors::YELLOW);
    if (!m_mesh) { Report(scene, false, "Import mesh: file has no drawable geometry."); return false; }
    m_meshPath = path;
    if (dim == 3) ConfigureSurface3D(m_mesh);
    m_meshStats = ComputeStats(path);
    if (m_mesh && Alive(scene, m_mesh)) scene.SetNodeVisibleCascade(m_mesh, true);

    // The reloaded mesh satisfies subdivide + offsets + mesh (BCs are distance-
    // based, so the offsets are not needed to re-solve). Require an explicit
    // thermal solve before an isotherm can be extracted.
    m_dirty[STAGE_SUBDIVIDE] = m_dirty[STAGE_OFFSETS] = m_dirty[STAGE_MESH] = false;
    m_dirty[STAGE_THERMAL]   = m_dirty[STAGE_ISOSURFACE] = true;
    m_thermalSolved = false;
    scene.UpdateLight();
    snprintf(status, sizeof(status), "Imported mesh: %s", BaseName(path).c_str());
    return true;
}

void SemSession::LoadSessionStages(Scene& scene) {
    if (!HasSource() || m_workDir.empty()) return;

    if (dim == 3) { LoadSessionStages3D(scene); return; }

    // ---- 2D pipeline --------------------------------------------------------
    // Offsets: probe the first shell; ImportOffsets reloads the whole set.
    const std::string offShell0 =
        (fs::path(m_workDir) / (Stem(m_srcPath) + "_offset_0.csv3d")).string();
    if (fs::exists(offShell0))
        ImportOffsets(scene, m_workDir);

    // Mesh: a single serialized file. ImportMesh (after ImportOffsets) leaves
    // subdivide+offsets+mesh clean and thermal stale/unsolved.
    const std::string meshPath =
        (fs::path(m_workDir) / (Stem(m_srcPath) + "_mesh.csv3d")).string();
    if (fs::exists(meshPath))
        ImportMesh(scene, meshPath);

    // Thermal: the mesh nodes carry a non-zero T field (the solver rewrites the
    // file in place) AND the isoline file exists.
    const std::string isoPath =
        (fs::path(m_workDir) / (Stem(m_srcPath) + "_isoline.csv3d")).string();
    if (Alive(scene, m_mesh) && MeshFileHasTField(meshPath) && fs::exists(isoPath)) {
        m_thermalSolved = true;
        ReloadMeshColored(scene);
        DropIsoline(scene);
        const XMFLOAT4 green(0.0f, 1.0f, 0.0f, 1.0f);
        m_isoline = scene.AddFromCSV3D(isoPath, "isoline_" + Stem(m_srcPath),
                                       AttachParent(), &green, Colors::BLUE, Colors::RED);
        m_isolinePath = isoPath;
        m_dirty[STAGE_THERMAL] = m_dirty[STAGE_ISOSURFACE] = false;
    }

    snprintf(status, sizeof(status), "Loaded session: %s",
             BaseName(m_workDir).c_str());
}

// 3D reload: a single SEM_LoadSession3D restores the whole SEM cache (source copy,
// offsets, mesh, solved field, iso) and every stage's arguments from the session
// folder; the host then reads the arguments back into the UI and rebuilds the
// scene primitives from the on-disk stage files (ImportOffsets/ImportMesh skip the
// core reload for 3D — see their guards).
void SemSession::LoadSessionStages3D(Scene& scene) {
    int rc = SEM_LoadSession3D(m_workDir.c_str());
    if (!CheckRc(scene, false, "SEM_LoadSession3D", rc,
                 { "", "Bad directory", "Working dir unusable",
                   "Manifest missing", "Manifest parse error",
                   "Version/source mismatch", "Source load failed",
                   "Clip re-snap failed", "Offsets mismatch", "Mesh load failed" })) {
        snprintf(status, sizeof(status), "Session load failed.");
        return;
    }

    // Pull the restored knobs into the UI so the session is editable from where it
    // left off, and learn which stages the cache holds.
    ApplyPipelineArgs3D();
    SEM_PipelineArgs3D a{};
    if (SEM_GetPipelineArgs3D(&a) != 0) a.stages_present = 0;

    // Offsets.
    const std::string offShell0 =
        (fs::path(m_workDir) / (Stem(m_srcPath) + "_offset3d_0.csv3d")).string();
    if ((a.stages_present & SEM_STAGE_OFFSETS) && fs::exists(offShell0))
        ImportOffsets(scene, m_workDir);

    // Mesh.
    const std::string meshPath =
        (fs::path(m_workDir) / (Stem(m_srcPath) + "_mesh3d.csv3d")).string();
    if ((a.stages_present & SEM_STAGE_MESH) && fs::exists(meshPath))
        ImportMesh(scene, meshPath);

    // Clip-plane rectangles from the restored core state (the core already holds
    // the planes; this only recreates the editable visuals).
    LoadClipPlanesFromState(scene);

    // Iso deliverable: the remeshed sheet when the extraction used an offset axis,
    // else the plain extracted iso. Colour the mesh by the solved field first.
    const std::string isoPath =
        (fs::path(m_workDir) / (Stem(m_srcPath) +
            (a.iso_axis != 0 ? "_remesh3d.csv3d" : "_isosurface3d.csv3d"))).string();
    if ((a.stages_present & SEM_STAGE_ISO) && Alive(scene, m_mesh) && fs::exists(isoPath)) {
        if (a.stages_present & SEM_STAGE_THERMAL) {
            m_thermalSolved = true;
            ReloadMeshColored(scene);
            m_dirty[STAGE_THERMAL] = false;
        }
        DropIsoline(scene);
        const XMFLOAT4 green(0.0f, 1.0f, 0.0f, 1.0f);
        m_isoline = scene.AddFromCSV3D(isoPath, "isosurface_" + Stem(m_srcPath),
                                       AttachParent(), &green, Colors::BLUE, Colors::RED);
        if (m_isoline) ConfigureSurface3D(m_isoline);
        m_isolinePath = isoPath;
        m_dirty[STAGE_ISOSURFACE] = false;
    }

    snprintf(status, sizeof(status), "Loaded session: %s",
             BaseName(m_workDir).c_str());
}

// Copy the SEM core's restored 3D pipeline arguments (SEM_GetPipelineArgs3D) back
// into this session's UI fields — the inverse of the forward mapping the async
// driver applies (see SemSessionAsync.cpp). Geometry is already reloaded; this
// only makes the knobs match, so a reopened session can be tweaked and recomputed
// from where it left off. 3D only.
void SemSession::ApplyPipelineArgs3D() {
    if (dim != 3 || !HasSource()) return;
    SEM_PipelineArgs3D a{};
    if (SEM_GetPipelineArgs3D(&a) != 0) return;

    // Subdivide — inverse of ApplySubdivide's n: <0 off (mode 0), 0 adaptive
    // (mode 1), >=1 uniform subN (mode 2).
    subEnabled = true;
    if (a.subdiv_n < 0)       subMode = 0;
    else if (a.subdiv_n == 0) subMode = 1;
    else                    { subMode = 2; subN = a.subdiv_n; }

    // Offsets.
    offsetMode = (a.offset_mode == SEM_OFFSET_GAPS) ? OFFSET_GAPS : OFFSET_EVEN;
    firstGap   = (float)a.first_gap;
    numOffsets = a.num_offsets;
    grading    = (float)a.grading;
    if (a.offset_gap_count > 0) {
        std::vector<double> g(a.offset_gap_count);
        int cnt = 0;
        if (SEM_GetOffsetGaps3D(g.data(), (int)g.size(), &cnt) == 0) {
            const int n = std::min(cnt, (int)g.size());
            gaps.assign(g.begin(), g.begin() + n);
        }
    }

    // Mesh (3D tet). tet_param and tet_max_edge_len both come back in world units,
    // matching tetParam / tetMaxEdgeLen.
    tetMethod         = a.tet_method;
    tetParam          = (float)a.tet_param;
    tetMaxEdgeLen     = (float)a.tet_max_edge_len;

    // Thermal.
    maxInward = a.max_inward;

    // Iso. The core stores min-offset as the absolute clearance c*offset; recover c.
    isoValue       = (float)a.iso_value;
    isoAxis        = a.iso_axis;
    isoOffsetValue = (float)a.iso_offset_value;
    if (a.iso_offset_value > 1e-12 || a.iso_offset_value < -1e-12)
        isoMinOffsetValue = (float)(a.iso_min_offset_value / a.iso_offset_value);
    isoFinalTargetMult = (float)a.iso_target_len_mult;
    isoFinalIters      = a.iso_iterations;
}

}
