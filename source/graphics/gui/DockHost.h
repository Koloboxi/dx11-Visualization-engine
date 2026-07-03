#pragma once
#include "../imgui/imgui.h"
#include "../imgui/imgui_internal.h"
#include "../scene/scene.h"
#include "TopStripWindow.h"

namespace DockHost {

// topInset reserves space at the top of the work area (for the tab strip drawn
// above the dock host / top strip).
inline void Begin(Scene& scene, bool& blockMousePick, const char* fpsText, float topInset = 0.f) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({ vp->WorkPos.x, vp->WorkPos.y + topInset });
    ImGui::SetNextWindowSize({ vp->WorkSize.x, vp->WorkSize.y - topInset });
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_NoTitleBar          | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize            | ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground        | ImGuiWindowFlags_NoDocking  |
        ImGuiWindowFlags_MenuBar             | ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0.f, 0.f));
    ImGui::Begin("##dockhost", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    TopStripWindow::DrawMenuBarContents(scene, blockMousePick, fpsText);

    ImGuiID dockId = ImGui::GetID("##dockspace");
    ImGui::DockSpace(dockId, ImVec2(0.f, 0.f), ImGuiDockNodeFlags_PassthruCentralNode);
}

inline void End() {
    ImGui::End();
}

}
