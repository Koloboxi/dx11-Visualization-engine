#pragma once
#include "../imgui/imgui.h"
#include "../scene/scene.h"

// A thin strip of workspace tabs, pinned directly below the OS window caption and
// above the dock host's top strip. It holds the always-present "Main menu" tab
// and, once a workspace has been opened, a single workspace tab (max two, for
// now). Selecting a tab switches scene.activeTab.
namespace TabStripWindow {

// Same height as the dock host's top strip (an ImGui menu bar), so the two bars
// stack flush with equal height.
inline float Height() { return ImGui::GetFrameHeight(); }

// Draw one tab-styled button; returns true when clicked. Active tab is drawn in
// the highlighted (header-active) colour.
inline bool TabButton(const char* label, bool active) {
    const ImVec4 act   = ImGui::GetStyleColorVec4(ImGuiCol_TabActive);
    const ImVec4 inact = ImGui::GetStyleColorVec4(ImGuiCol_Tab);
    ImGui::PushStyleColor(ImGuiCol_Button,        active ? act : inact);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_TabHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  act);
    // width 0 => auto-size to the label (workspace tabs carry the source name).
    bool clicked = ImGui::Button(label, { 0.f, ImGui::GetFrameHeight() });
    ImGui::PopStyleColor(3);
    return clicked;
}

inline void Draw(Scene& scene) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize({ vp->WorkSize.x, Height() });
    ImGui::SetNextWindowViewport(vp->ID);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar        | ImGuiWindowFlags_NoResize   |
        ImGuiWindowFlags_NoMove            | ImGuiWindowFlags_NoScrollbar|
        ImGuiWindowFlags_NoSavedSettings   | ImGuiWindowFlags_NoDocking  |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImVec4 barBg = ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg);
    barBg.w = 1.f;  // opaque, so the 3D viewport does not bleed through the tabs
    ImGui::PushStyleColor(ImGuiCol_WindowBg, barBg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    // No vertical padding + no min-size clamp so the window is exactly one
    // frame tall (matching the top strip); otherwise ImGui clamps it to
    // style.WindowMinSize and the extra height overlaps the strip below.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0.f, 0.f));
    ImGui::Begin("##tabstrip", nullptr, flags);
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor();

    if (TabButton("Main menu", scene.activeTab == 0))
        scene.activeTab = 0;

    if (scene.workspaceOpen) {
        ImGui::SameLine();
        if (TabButton(scene.workspaceLabel.c_str(), scene.activeTab == 1))
            scene.activeTab = 1;
    }

    ImGui::End();
}

}
