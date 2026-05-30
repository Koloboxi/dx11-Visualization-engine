#pragma once
#include "LayoutState.h"
#include "GuiIcons.h"
#include "../scene/scene.h"

namespace TopStripWindow {

static bool   s_lightOpen   = false;
static ImVec2 s_lightBtnMin = {};
static ImVec2 s_lightBtnMax = {};

inline void Draw(float windowW, Scene& scene, bool& blockMousePick, const char* fpsText = nullptr) {
    constexpr float SH = LayoutState::STRIP_H;
    float btnSz = SH - 8.f;

    ImGui::SetNextWindowPos({0.f, 0.f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({windowW, SH}, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {4.f, 4.f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   {3.f, 3.f});
    ImGui::Begin("##topstrip", nullptr,
        ImGuiWindowFlags_NoTitleBar    | ImGuiWindowFlags_NoResize        |
        ImGuiWindowFlags_NoMove        | ImGuiWindowFlags_NoScrollbar     |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleVar(2);

    GuiIcons::ToggleIconButton("##outline", &scene.outlineThroughObjets, btnSz, GuiIcons::OutlineIcon);
    ImGui::SameLine();
    GuiIcons::ToggleIconButton("##sgrid",   &scene.showGrid,             btnSz, GuiIcons::GridIcon);
    ImGui::SameLine();
    GuiIcons::ToggleIconButton("##saxes",   &scene.showAxes,             btnSz, GuiIcons::AxesIcon);
    ImGui::SameLine();

    bool prevSmooth = scene.smoothShade;
    GuiIcons::ToggleIconButton("##smooth",  &scene.smoothShade,          btnSz, GuiIcons::SmoothSphereIcon);
    if (scene.smoothShade != prevSmooth) scene.UpdateLight();
    ImGui::SameLine();

    GuiIcons::ToggleIconButton("##light",   &s_lightOpen,                btnSz, GuiIcons::LightbulbIcon);
    s_lightBtnMin = ImGui::GetItemRectMin();
    s_lightBtnMax = ImGui::GetItemRectMax();

    if (fpsText && fpsText[0]) {
        char buf[32];
        snprintf(buf, sizeof(buf), "FPS: %s", fpsText);
        float tw = ImGui::CalcTextSize(buf).x;
        ImGui::SameLine(windowW - tw - 10.f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", buf);
    }

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
        blockMousePick = true;

    ImGui::End();

    if (s_lightOpen) {
        float popW = 220.f;
        float popX = s_lightBtnMin.x;
        if (popX + popW > windowW) popX = windowW - popW - 4.f;

        ImGui::SetNextWindowPos({popX, s_lightBtnMax.y + 2.f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({popW, 0.f}, ImGuiCond_Always);
        ImGui::Begin("##lightpopup", nullptr,
            ImGuiWindowFlags_NoTitleBar       | ImGuiWindowFlags_NoResize     |
            ImGuiWindowFlags_NoMove           | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings  | ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoCollapse);

        bool changed = false;
        changed |= ImGui::DragFloat("Ambient",   &scene.ambient,   0.05f, 0.f, 1.f,  "%.2f");
        changed |= ImGui::DragFloat("Intensity", &scene.intensity, 0.05f, 0.f, 1.f,  "%.2f");
        changed |= ImGui::DragFloat("Shininess", &scene.shininess, 0.1f,  0.f, 100.f,"%.1f");
        if (changed) scene.UpdateLight();

        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
            blockMousePick = true;

        ImGui::End();
    }
}

} // namespace TopStripWindow
