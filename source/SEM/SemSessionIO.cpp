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
                                    const std::string& workDir) {
    if (path.empty()) return nullptr;
    Primitive* src = nullptr;
    try {
        src = scene.AddFromCSV3D(path, "", nullptr, nullptr, Colors::BLUE, Colors::RED, false);
        if (!src) { Report(scene, false, "Import failed: could not load CSV3D."); return nullptr; }
        src->semSourcePath = path;
        // Session folder to serialize into; empty => Bind allocates a fresh one.
        src->semWorkDir = workDir;
        scene.stagingEnabled = true;
        scene.SetStaged(src);
        Bind(scene, src);
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
    // dir empty => the SEM core reads from its working dir; pass the chosen
    // directory through so both the core and the shell loader agree on location.
    const char* d = dir.empty() ? nullptr : dir.c_str();
    int rc = (dim == 3) ? SEM_LoadOffsets3D(d) : SEM_LoadOffsets(d);
    if (!CheckRc(scene, false, dim == 3 ? "SEM_LoadOffsets3D" : "SEM_LoadOffsets", rc,
                 { "", "No source loaded", "No offset files found" }))
        return false;

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
    int rc = (dim == 3) ? SEM_LoadMesh3D(path.c_str()) : SEM_LoadMesh(path.c_str());
    if (!CheckRc(scene, false, dim == 3 ? "SEM_LoadMesh3D" : "SEM_LoadMesh", rc,
                 { "", "No source loaded", "Load failed", "Missing #tets section" }))
        return false;

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

    // Offsets: probe the first shell; ImportOffsets reloads the whole set.
    const std::string offShell0 =
        (fs::path(m_workDir) /
         (Stem(m_srcPath) + (dim == 3 ? "_offset3d_0.csv3d" : "_offset_0.csv3d"))).string();
    if (fs::exists(offShell0))
        ImportOffsets(scene, m_workDir);

    // Mesh: a single serialized file. ImportMesh (after ImportOffsets) leaves
    // subdivide+offsets+mesh clean and thermal stale/unsolved.
    const std::string meshPath =
        (fs::path(m_workDir) /
         (Stem(m_srcPath) + (dim == 3 ? "_mesh3d.csv3d" : "_mesh.csv3d"))).string();
    if (fs::exists(meshPath))
        ImportMesh(scene, meshPath);

    // Cache-state sidecar (3D only): exact signed offset distances, clip planes,
    // subdivision level and thermal-BC origin tags the per-stage geometry files
    // cannot carry. Restore it LAST so the tags/distances line up with the loaded
    // shells/mesh (SEM_INTEGRATION.md §4a). Best-effort: a mismatch is reported
    // but does not abort the reload.
    if (dim == 3 && fs::exists(offShell0)) {
        const std::string statePath =
            (fs::path(m_workDir) / (Stem(m_srcPath) + "_state3d.txt")).string();
        if (fs::exists(statePath)) {
            int rc = SEM_LoadState3D(m_workDir.c_str());
            if (CheckRc(scene, false, "SEM_LoadState3D", rc,
                    { "", "No source loaded", "State file missing or unparseable",
                      "State does not match the loaded stages" }))
                // The core now holds the restored clip planes, but the visual
                // plane rectangles are not recreated by the load — rebuild them
                // from the same state file so the planes are shown and editable.
                LoadClipPlanesFromState(scene);
        }
    }

    // Thermal: the mesh nodes carry a non-zero T field (the solver rewrites the
    // file in place) AND the isotherm/isosurface file exists.
    const std::string isoPath =
        (fs::path(m_workDir) /
         (Stem(m_srcPath) + (dim == 3 ? "_isosurface3d.csv3d" : "_isoline.csv3d"))).string();
    if (Alive(scene, m_mesh) && MeshFileHasTField(meshPath) && fs::exists(isoPath)) {
        m_thermalSolved = true;
        ReloadMeshColored(scene);
        DropIsoline(scene);
        const XMFLOAT4 green(0.0f, 1.0f, 0.0f, 1.0f);
        const std::string namePrefix = (dim == 3 ? "isosurface_" : "isoline_");
        m_isoline = scene.AddFromCSV3D(isoPath, namePrefix + Stem(m_srcPath),
                                       AttachParent(), &green, Colors::BLUE, Colors::RED);
        if (dim == 3 && m_isoline) ConfigureSurface3D(m_isoline);
        m_isolinePath = isoPath;
        // Both the solved field and its isosurface were reloaded from disk, so
        // neither stage needs recomputing (ImportMesh had marked thermal stale).
        m_dirty[STAGE_THERMAL] = m_dirty[STAGE_ISOSURFACE] = false;
    }

    snprintf(status, sizeof(status), "Loaded session: %s",
             BaseName(m_workDir).c_str());
}

}
