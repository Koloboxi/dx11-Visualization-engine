#pragma once
#include "../imgui/imgui.h"
#include "../scene/scene.h"
#include "../../scripting/LuaUpdaterEditor.h"
#include "../../SEM/SemSession.h"
#include "SEMWindow.h"
#include "PrimitivesWindow.h"
#include <filesystem>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <shellapi.h>

// Full-screen start menu shown while scene.activeTab == 0. It offers two entry
// points: a SEM session (import a new CSV3D source, or reopen one of the saved
// sessions from %TEMP%/sem) and a scene (empty or one of the four demo scenes).
// Picking either opens the single workspace tab. Only one workspace may be open
// at a time (parallel workspaces are not implemented yet).
namespace MainMenuWindow {

// A unique source stem and the session folders saved under it (%TEMP%/sem).
struct StemGroup { std::string stem; std::vector<std::string> dirs; };

inline bool AllDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    return true;
}

inline std::string BaseName(const std::string& p) {
    auto s = p.find_last_of("\\/");
    return s != std::string::npos ? p.substr(s + 1) : p;
}

// Group every "<stem>_<N>" session folder under %TEMP%/sem by its source stem.
inline std::vector<StemGroup> EnumerateSessions() {
    namespace fs = std::filesystem;
    std::vector<StemGroup> groups;
    const std::string root = SemSessionNS::SemSession::SessionRoot();
    if (root.empty()) return groups;

    std::map<std::string, std::vector<std::pair<int, std::string>>> byStem;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(root, ec)) {
        if (ec) break;
        if (!e.is_directory()) continue;
        const std::string name = e.path().filename().string();
        auto us = name.find_last_of('_');
        if (us == std::string::npos || us == 0) continue;
        const std::string stem = name.substr(0, us);
        const std::string num  = name.substr(us + 1);
        if (!AllDigits(num)) continue;
        byStem[stem].emplace_back(std::stoi(num), e.path().string());
    }
    for (auto& kv : byStem) {
        std::sort(kv.second.begin(), kv.second.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        StemGroup g;
        g.stem = kv.first;
        for (auto& p : kv.second) g.dirs.push_back(p.second);
        groups.push_back(std::move(g));
    }
    return groups;
}

// Legacy fallback: recover the source CSV3D by name from the exe-relative Data
// tree. New sessions keep their own copy of the source in the session folder (see
// OpenSemSession), so this is only used for sessions saved before that. Empty if
// not found.
inline std::string ResolveSourcePath(const std::string& stem) {
    namespace fs = std::filesystem;
    const std::string dataDir = PrimitivesWindow::GetExeDir() + "\\Data";
    const std::string target  = stem + ".csv3d";
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(dataDir, ec)) {
        if (ec) break;
        if (e.is_regular_file() && e.path().filename().string() == target)
            return e.path().string();
    }
    return {};
}

// Send a folder to the Recycle Bin (FOF_ALLOWUNDO). Narrow paths match the rest
// of the SEM path handling. pFrom must be double-null terminated.
inline void RecycleToBin(const std::string& path) {
    std::string buf = path;
    buf.push_back('\0');
    buf.push_back('\0');
    SHFILEOPSTRUCTA op{};
    op.wFunc  = FO_DELETE;
    op.pFrom  = buf.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    SHFileOperationA(&op);
}

// Tree-style multi-select click: shift = range from last, ctrl = toggle, plain =
// replace. Updates sel / last in place.
inline void SelectClick(std::set<int>& sel, int& last, int i) {
    const bool ctrl  = ImGui::GetIO().KeyCtrl;
    const bool shift = ImGui::GetIO().KeyShift;
    if (shift && last >= 0) {
        sel.clear();
        int lo = std::min(i, last), hi = std::max(i, last);
        for (int j = lo; j <= hi; ++j) sel.insert(j);
    } else if (ctrl) {
        if (sel.count(i)) sel.erase(i); else sel.insert(i);
        last = i;
    } else {
        sel.clear(); sel.insert(i); last = i;
    }
}

inline void OpenSemSession(Scene& scene, const std::string& stem, const std::string& dir) {
    namespace fs = std::filesystem;
    // The session folder keeps a copy of the source (SEM writes it on import), so a
    // reopen is self-contained; fall back to the Data tree for legacy sessions.
    std::string src = (fs::path(dir) / (stem + ".csv3d")).string();
    if (!fs::exists(src)) src = ResolveSourcePath(stem);
    if (src.empty()) return;  // no in-folder copy and none in Data — cannot reopen
    auto& S = SEMWindow::Session();
    S.ImportSource(scene, src, dir, /*reload=*/true);
    S.LoadSessionStages(scene);
    scene.workspaceOpen  = true;
    scene.activeTab      = 1;
    scene.sceneTreeOnly  = true;
    scene.workspaceLabel = stem;
}

// topInset: height of the tab strip drawn above this menu.
inline void Draw(Scene& scene, LuaUpdaterEditor& lua, float topInset) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos ({ vp->WorkPos.x, vp->WorkPos.y + topInset });
    ImGui::SetNextWindowSize({ vp->WorkSize.x, vp->WorkSize.y - topInset });
    ImGui::SetNextWindowViewport(vp->ID);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar        | ImGuiWindowFlags_NoResize   |
        ImGuiWindowFlags_NoMove            | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoSavedSettings   | ImGuiWindowFlags_NoDocking;

    const ImU32 bg = scene.lightTheme ? IM_COL32(232, 234, 238, 255)
                                      : IM_COL32(28, 30, 34, 255);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, bg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##mainmenu", nullptr, flags);
    ImGui::PopStyleVar(2);

    // page: 0 = root (SEM / Scenes), 1 = SEM entry, 2 = Scenes entry.
    static int page = 0;
    static int selStem = -1;                 // source whose sessions are shown
    static std::vector<StemGroup> groups;
    static std::set<int> stemSel, sessSel;   // multi-select highlight per list
    static int stemLast = -1, sessLast = -1; // shift-anchor per list
    static int focusedList = -1;             // 0 = stems, 1 = sessions (for Delete)
    static bool parallelError = false;

    // Opening a second workspace is not supported yet — surface an error instead.
    auto guardOpen = [&]() -> bool {
        if (scene.workspaceOpen) { parallelError = true; return false; }
        return true;
    };
    auto refresh = [&](const std::string& keepStem) {
        groups = EnumerateSessions();
        stemSel.clear(); sessSel.clear();
        stemLast = sessLast = -1;
        selStem = -1;
        if (!keepStem.empty())
            for (int i = 0; i < (int)groups.size(); ++i)
                if (groups[i].stem == keepStem) { selStem = i; break; }
    };

    const float btnSz = 170.f;
    const float listW = 260.f;
    const float listH = 360.f;
    const float gap   = 28.f;

    // Center a horizontal row of columns (given widths) in the window; returns
    // each column's top-left, all sharing the vertical band of height rowH.
    auto layoutRow = [&](const std::vector<float>& widths, float rowH) {
        float total = 0.f;
        for (float w : widths) total += w;
        if (!widths.empty()) total += gap * (widths.size() - 1);
        const ImVec2 win = ImGui::GetWindowSize();
        float x = (win.x - total) * 0.5f;
        const float yTop = (win.y - rowH) * 0.5f;
        std::vector<ImVec2> pos;
        for (float w : widths) { pos.push_back({x, yTop}); x += w + gap; }
        return pos;
    };
    auto squareBtn = [&](const char* id, ImVec2 topLeft, float rowH) {
        ImGui::SetCursorPos({topLeft.x, topLeft.y + (rowH - btnSz) * 0.5f});
        return ImGui::Button(id, {btnSz, btnSz});
    };

    if (page == 0) {
        auto pos = layoutRow({btnSz, btnSz}, btnSz);
        if (squareBtn("SEM", pos[0], btnSz)) {
            page = 1; refresh("");
        }
        if (squareBtn("Scenes", pos[1], btnSz)) {
            page = 2;
        }
    } else if (page == 1) {
        const bool showSessions = (selStem >= 0 && selStem < (int)groups.size());
        std::vector<float> widths = { btnSz, listW };
        if (showSessions) widths.push_back(listW);
        auto pos = layoutRow(widths, listH);

        // "Import CSV3D..." — same behaviour as the SEM window button; the SEM
        // window (shown once the workspace tab opens) drives any existing-session
        // choice modal.
        if (squareBtn("Import\nCSV3D...", pos[0], listH) && guardOpen()) {
            std::string p = SEMWindow::BrowseCsv3dFile();
            if (!p.empty()) {
                scene.workspaceOpen  = true;
                scene.activeTab      = 1;
                scene.sceneTreeOnly  = true;
                scene.workspaceLabel = PrimitivesWindow::FileStem(p);
                SEMWindow::BeginSourceImport(scene, SEMWindow::Session(), p);
            }
        }

        // Saved sources (unique CSV3D stems). Click selects; double-click opens
        // the session list; Delete recycles every session of the selected sources.
        ImGui::SetCursorPos(pos[1]);
        ImGui::BeginChild("##stems", {listW, listH}, true);
        if (ImGui::IsWindowFocused()) focusedList = 0;
        ImGui::TextDisabled("Saved sources");
        ImGui::Separator();
        for (int i = 0; i < (int)groups.size(); ++i) {
            if (ImGui::Selectable(groups[i].stem.c_str(), stemSel.count(i) > 0,
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                if (ImGui::IsMouseDoubleClicked(0)) {
                    selStem = i; sessSel.clear(); sessLast = -1;
                    stemSel = { i }; stemLast = i;
                } else {
                    SelectClick(stemSel, stemLast, i);
                }
            }
        }
        if (groups.empty()) ImGui::TextDisabled("(none)");
        ImGui::EndChild();

        // Concrete sessions of the selected source. Click selects; double-click
        // opens one; Delete recycles the selected sessions.
        if (showSessions) {
            ImGui::SetCursorPos(pos[2]);
            ImGui::BeginChild("##sessions", {listW, listH}, true);
            if (ImGui::IsWindowFocused()) focusedList = 1;
            ImGui::TextDisabled("Sessions");
            ImGui::Separator();
            const auto& dirs = groups[selStem].dirs;
            for (int i = 0; i < (int)dirs.size(); ++i) {
                if (ImGui::Selectable(BaseName(dirs[i]).c_str(), sessSel.count(i) > 0,
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        if (guardOpen()) { OpenSemSession(scene, groups[selStem].stem, dirs[i]); page = 0; }
                    } else {
                        SelectClick(sessSel, sessLast, i);
                    }
                }
            }
            ImGui::EndChild();
        }

        // Delete acts on whichever list is focused.
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !ImGui::GetIO().WantTextInput) {
            if (focusedList == 0 && !stemSel.empty()) {
                std::string keep;  // whole source removed => forget the open column
                for (int i : stemSel)
                    if (i >= 0 && i < (int)groups.size())
                        for (const std::string& d : groups[i].dirs) RecycleToBin(d);
                refresh(keep);
            } else if (focusedList == 1 && showSessions && !sessSel.empty()) {
                const std::string keep = groups[selStem].stem;
                const auto& dirs = groups[selStem].dirs;
                for (int i : sessSel)
                    if (i >= 0 && i < (int)dirs.size()) RecycleToBin(dirs[i]);
                refresh(keep);
            }
        }
    } else { // page == 2: Scenes
        auto pos = layoutRow({ btnSz, listW }, listH);

        if (squareBtn("+", pos[0], listH)) {
            if (guardOpen()) {
                PrimitivesWindow::ClearAndLoad(scene, lua, [&] { scene.ClearScene(); });
                scene.workspaceOpen  = true;
                scene.activeTab      = 1;
                scene.sceneTreeOnly  = false;
                scene.workspaceLabel = "Scene";
                page = 0;
            }
        }

        ImGui::SetCursorPos(pos[1]);
        ImGui::BeginChild("##demos", {listW, listH}, true);
        ImGui::TextDisabled("Demo scenes");
        ImGui::Separator();
        static const char* demoNames[] = { "Newton", "PH Demo", "GR Demo", "Ideal Gas" };
        for (int i = 0; i < 4; ++i) {
            if (ImGui::Selectable(demoNames[i], false,
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                if (ImGui::IsMouseDoubleClicked(0) && guardOpen()) {
                    PrimitivesWindow::ClearAndLoad(scene, lua, [&] {
                        if (i == 0)      scene.LoadNewtonDemo();
                        else if (i == 1) scene.LoadPersistentHomologyScene();
                        else if (i == 2) scene.LoadGRScene();
                        else             scene.LoadIdealGasScene();
                        lua.ReApplyAll(scene.primitives);
                    });
                    scene.workspaceOpen  = true;
                    scene.activeTab      = 1;
                    scene.sceneTreeOnly  = false;
                    scene.workspaceLabel = demoNames[i];
                    page = 0;
                }
            }
        }
        ImGui::EndChild();
    }

    // Parallel-workspace error modal.
    if (parallelError) { ImGui::OpenPopup("Not supported##mm"); parallelError = false; }
    if (ImGui::BeginPopupModal("Not supported##mm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("A workspace is already open.\n"
                           "Parallel workspaces are not implemented yet — close the current\n"
                           "one first (not yet supported).");
        ImGui::Spacing();
        if (ImGui::Button("OK", {120, 0})) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::End();
    ImGui::PopStyleColor();
}

}
