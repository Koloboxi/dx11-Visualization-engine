#pragma once
// Small ImGui control panel docked under the 3D nav gizmo (NavGizmo, drawn by
// the scene in the top-right corner). The orientation cube itself is now the 3D
// gizmo; this panel only carries the camera position/scale resets and the zoom
// drag. Render-mode toggles and the iso/dim presets moved to the top strip.
#include "..\imgui\imgui.h"
#include "..\camera\Camera.h"
#include "NavGizmo.h"
#include <algorithm>
#include <cmath>

class NavCube {
public:
    // Returns true when the panel is hovered (so the caller can block picking).
    static bool Draw(ImVec2 windowSize, Camera& camera) {
        float panelX = windowSize.x - NavGizmo::RIGHT_MARGIN - NavGizmo::VIEW_PX;
        float panelY = NavGizmo::TOP_MARGIN + NavGizmo::VIEW_PX + 4.f;

        ImGui::SetNextWindowPos(ImVec2(panelX, panelY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(NavGizmo::VIEW_PX, 0.f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.60f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.f, 4.f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(4.f, 3.f));
        ImGui::Begin("##navcube_panel", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::PopStyleVar(2);

        float btnW = (NavGizmo::VIEW_PX - 12.f) * 0.5f;

        if (ImGui::Button("pos", ImVec2(btnW, 0))) camera.SetPosition(XMFLOAT3(0.f, 0.f, 0.f));
        ImGui::SetItemTooltip("Recenter the camera target at the origin.");
        ImGui::SameLine();
        if (ImGui::Button("scale", ImVec2(btnW, 0))) camera.SetScale(1.f);
        ImGui::SetItemTooltip("Reset the zoom to 1.0.");

        float scale = camera.GetScale();
        float logS  = std::log10f(scale);
        ImGui::SetNextItemWidth(NavGizmo::VIEW_PX - 8.f);
        if (ImGui::DragFloat("##zoom", &logS, 0.02f, -4.f, 5.f, "zoom 1e%.2f", ImGuiSliderFlags_NoRoundToFormat))
            camera.SetScale(std::powf(10.f, logS));
        ImGui::SetItemTooltip("Drag to zoom (log scale).");

        bool panelHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        ImGui::End();

        return panelHovered;
    }
};
