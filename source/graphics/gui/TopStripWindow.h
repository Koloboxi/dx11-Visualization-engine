#pragma once
#include "GuiIcons.h"
#include "../scene/scene.h"

namespace TopStripWindow {

static bool   s_lightOpen   = false;
static ImVec2 s_lightBtnMin = {};
static ImVec2 s_lightBtnMax = {};

inline void DrawMenuBarContents(Scene& scene, bool& blockMousePick, const char* fpsText = nullptr) {
    if (!ImGui::BeginMenuBar()) return;

    float btnSz = ImGui::GetFrameHeight() - 2.f;

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
        float avail = ImGui::GetContentRegionAvail().x;
        if (avail > tw + 10.f) ImGui::SameLine(0.f, avail - tw - 10.f);
        else ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", buf);
    }

    ImGui::EndMenuBar();

    if (s_lightOpen) {
        float popW = 220.f;
        float popX = s_lightBtnMin.x;
        float vpRight = ImGui::GetMainViewport()->WorkPos.x + ImGui::GetMainViewport()->WorkSize.x;
        if (popX + popW > vpRight) popX = vpRight - popW - 4.f;

        ImGui::SetNextWindowPos({popX, s_lightBtnMax.y + 2.f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({popW, 0.f}, ImGuiCond_Always);
        ImGui::Begin("##lightpopup", nullptr,
            ImGuiWindowFlags_NoTitleBar       | ImGuiWindowFlags_NoResize     |
            ImGuiWindowFlags_NoMove           | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings  | ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoDocking        | ImGuiWindowFlags_NoCollapse);

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

}
