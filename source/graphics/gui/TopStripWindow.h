#pragma once
#include "GuiIcons.h"
#include "../scene/scene.h"

namespace TopStripWindow {

static bool   s_lightOpen   = false;
static ImVec2 s_lightBtnMin = {};
static ImVec2 s_lightBtnMax = {};

static bool   s_sectionOpen   = false;
static ImVec2 s_sectionBtnMin = {};
static ImVec2 s_sectionBtnMax = {};

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
    ImGui::SameLine();

    GuiIcons::ToggleIconButton("##section", &s_sectionOpen,              btnSz, GuiIcons::SectionIcon);
    s_sectionBtnMin = ImGui::GetItemRectMin();
    s_sectionBtnMax = ImGui::GetItemRectMax();

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

    if (s_sectionOpen) {
        float popW = 240.f;
        float popX = s_sectionBtnMin.x;
        float vpRight = ImGui::GetMainViewport()->WorkPos.x + ImGui::GetMainViewport()->WorkSize.x;
        if (popX + popW > vpRight) popX = vpRight - popW - 4.f;

        ImGui::SetNextWindowPos({popX, s_sectionBtnMax.y + 2.f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({popW, 0.f}, ImGuiCond_Always);
        ImGui::Begin("##sectionpopup", nullptr,
            ImGuiWindowFlags_NoTitleBar       | ImGuiWindowFlags_NoResize     |
            ImGuiWindowFlags_NoMove           | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings  | ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoDocking        | ImGuiWindowFlags_NoCollapse);

        ImGui::Checkbox("Enable section", &scene.section.enabled);
        ImGui::SetItemTooltip("Clip solid meshes against a plane to reveal a\n"
                              "cross-section. Points, lines, grid and axes are\n"
                              "left intact.");

        ImGui::BeginDisabled(!scene.section.enabled);

        ImGui::TextUnformatted("Plane");
        ImGui::RadioButton("YZ", &scene.section.axis, 0); ImGui::SameLine();
        ImGui::RadioButton("XZ", &scene.section.axis, 1); ImGui::SameLine();
        ImGui::RadioButton("XY", &scene.section.axis, 2);
        ImGui::SetItemTooltip("Cutting plane, named by the axes it spans.\n"
                              "YZ cuts along X, XZ along Y, XY along Z.");

        ImGui::SetNextItemWidth(popW - 80.f);
        ImGui::DragFloat("Offset", &scene.section.offset, 0.5f, -100000.f, 100000.f, "%.2f");
        ImGui::SetItemTooltip("Slide the plane along its normal.");

        ImGui::Checkbox("Flip side", &scene.section.flip);
        ImGui::SetItemTooltip("Keep the other half of the geometry.");

        ImGui::EndDisabled();

        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
            blockMousePick = true;

        ImGui::End();
    }
}

}
