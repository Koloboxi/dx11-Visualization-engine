#pragma once
#include "LayoutState.h"
#include "../scene/scene.h"

namespace TimeControlWindow {

inline void Draw(const LayoutState& lay, float windowH, Scene& scene,
                 bool& blockMousePick, ImGuiWindowFlags extraFlags) {
    float panelH   = windowH - LayoutState::STRIP_H - lay.consoleH;
    float timeH    = lay.timeH > 0.f ? lay.timeH : panelH * .33f;

    ImGui::SetNextWindowPos({0.f, LayoutState::STRIP_H}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({lay.colW, timeH}, ImGuiCond_Always);
    ImGui::Begin("Time Control", nullptr, extraFlags);

    ImGui::Text("t = %.3f", scene.currentTime);
    if (scene.timePaused) {
        if (ImGui::Button("Play "))  scene.timePaused = false;
    } else {
        if (ImGui::Button("Pause")) scene.timePaused = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) scene.ResetTime();

    ImGui::DragFloat("Speed",    &scene.timeSpeed, 0.01f, 0.0f, 20.0f, "%.2f");
    ImGui::DragFloat("Max time", &scene.timeMax,   0.5f,  0.0f, 10000.f,
        scene.timeMax < 0.01f ? "unlimited" : "%.1f s");
    ImGui::Checkbox("Loop", &scene.timeLoop);

    ImGui::Separator();
    ImGui::Text("Trajectories");
    ImGui::Checkbox("Show##traj", &scene.showTrajectories);
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear##traj")) scene.ClearTrajectories();
    ImGui::SetNextItemWidth(lay.colW - 100.f);
    ImGui::DragInt("Max len##traj", &scene.trajectoryMaxLen, 10, 0, 1000000,
        scene.trajectoryMaxLen <= 0 ? "unlimited" : "%d pts");
    if (scene.trajectoryMaxLen < 0) scene.trajectoryMaxLen = 0;

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
        blockMousePick = true;
    ImGui::End();
}

}
