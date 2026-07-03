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

static bool   s_thickOpen     = false;
static ImVec2 s_thickBtnMin   = {};
static ImVec2 s_thickBtnMax   = {};

static bool   s_projOpen      = false;
static ImVec2 s_projBtnMin    = {};
static ImVec2 s_projBtnMax    = {};

// Close a dropdown when the user clicks outside both its body and its owning
// toggle button. `winHovered` must be sampled while the popup window is current
// (before End()); this call itself must run after End() so the button-rect test
// is not clipped by the popup window.
inline void CloseDropdownOnClickOutside(bool& open, bool winHovered,
                                        const ImVec2& btnMin, const ImVec2& btnMax) {
    if (open && ImGui::IsMouseClicked(0) && !winHovered &&
        !ImGui::IsMouseHoveringRect(btnMin, btnMax, false))
        open = false;
}

// Keep at most one top-strip dropdown open: opening `self` closes the rest.
inline void CloseOtherDropdowns(bool& self) {
    bool* all[] = { &s_lightOpen, &s_sectionOpen, &s_thickOpen, &s_projOpen };
    for (bool* p : all) if (p != &self) *p = false;
}

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

    if (GuiIcons::ToggleIconButton("##light",   &s_lightOpen,            btnSz, GuiIcons::LightbulbIcon) && s_lightOpen)
        CloseOtherDropdowns(s_lightOpen);
    s_lightBtnMin = ImGui::GetItemRectMin();
    s_lightBtnMax = ImGui::GetItemRectMax();
    ImGui::SameLine();

    if (GuiIcons::ToggleIconButton("##section", &s_sectionOpen,          btnSz, GuiIcons::SectionIcon) && s_sectionOpen)
        CloseOtherDropdowns(s_sectionOpen);
    s_sectionBtnMin = ImGui::GetItemRectMin();
    s_sectionBtnMax = ImGui::GetItemRectMax();
    ImGui::SameLine();

    if (GuiIcons::ToggleIconButton("##thickness", &s_thickOpen,          btnSz, GuiIcons::LineWeightIcon) && s_thickOpen)
        CloseOtherDropdowns(s_thickOpen);
    s_thickBtnMin = ImGui::GetItemRectMin();
    s_thickBtnMax = ImGui::GetItemRectMax();
    ImGui::SameLine();

    if (GuiIcons::ToggleIconButton("##projparams", &s_projOpen,          btnSz, GuiIcons::ProjectionIcon) && s_projOpen)
        CloseOtherDropdowns(s_projOpen);
    s_projBtnMin = ImGui::GetItemRectMin();
    s_projBtnMax = ImGui::GetItemRectMax();
    ImGui::SameLine();

    // Render modes moved here from the nav-cube panel.
    GuiIcons::ToggleIconButton("##solid",  &scene.rsSolid,     btnSz, GuiIcons::SolidCube);
    ImGui::SetItemTooltip("Solid (filled) rendering.");
    ImGui::SameLine();
    GuiIcons::ToggleIconButton("##wire",   &scene.rsWireframe, btnSz, GuiIcons::WireframeCube);
    ImGui::SetItemTooltip("Wireframe rendering.");
    ImGui::SameLine();
    GuiIcons::ToggleIconButton("##nocull", &scene.rsNoCull,    btnSz, GuiIcons::NoCullIcon);
    ImGui::SetItemTooltip("Two-sided faces (disable back-face culling).");
    ImGui::SameLine();
    if (GuiIcons::ToggleIconButton("##theme", &scene.lightTheme, btnSz, GuiIcons::ThemeIcon)) {
        if (scene.lightTheme) ImGui::StyleColorsLight();
        else                  ImGui::StyleColorsDark();
    }
    ImGui::SetItemTooltip("Toggle light / dark UI theme.");

    if (fpsText && fpsText[0]) {
        char buf[32];
        snprintf(buf, sizeof(buf), "FPS: %s", fpsText);
        float tw = ImGui::CalcTextSize(buf).x;
        float avail = ImGui::GetContentRegionAvail().x;
        if (avail > tw + 10.f) ImGui::SameLine(0.f, avail - tw - 10.f);
        else ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImVec4 col = scene.lightTheme ? ImVec4(0.0f, 0.0f, 0.0f, 1.0f) : ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
        ImGui::TextColored(col, "%s", buf);
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

        bool winHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (winHovered) blockMousePick = true;

        ImGui::End();
        CloseDropdownOnClickOutside(s_lightOpen, winHovered, s_lightBtnMin, s_lightBtnMax);
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

        bool winHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (winHovered) blockMousePick = true;

        ImGui::End();
        CloseDropdownOnClickOutside(s_sectionOpen, winHovered, s_sectionBtnMin, s_sectionBtnMax);
    }

    if (s_thickOpen) {
        float popW = 250.f;
        float popX = s_thickBtnMin.x;
        float vpRight = ImGui::GetMainViewport()->WorkPos.x + ImGui::GetMainViewport()->WorkSize.x;
        if (popX + popW > vpRight) popX = vpRight - popW - 4.f;

        ImGui::SetNextWindowPos({popX, s_thickBtnMax.y + 2.f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({popW, 0.f}, ImGuiCond_Always);
        ImGui::Begin("##thickpopup", nullptr,
            ImGuiWindowFlags_NoTitleBar       | ImGuiWindowFlags_NoResize     |
            ImGuiWindowFlags_NoMove           | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings  | ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoDocking        | ImGuiWindowFlags_NoCollapse);

        ImGui::TextUnformatted("Line styles");
        ImGui::TextDisabled("Width of thickened lines, in clip-space half-width\n"
                            "units. Each primitive picks a style in its Transform\n"
                            "window; style 0 is the default (grid, trajectories).");
        ImGui::Separator();

        // One editable width per named style. Edits apply live to every line using
        // that style on the next frame.
        for (int i = 0; i < (int)scene.lineStyles.size(); ++i) {
            ImGui::PushID(i);
            ImGui::SetNextItemWidth(popW - 100.f);
            ImGui::DragFloat(scene.lineStyles[i].name.c_str(), &scene.lineStyles[i].thickness,
                             0.0002f, 0.0001f, 0.05f, "%.4f");
            if (scene.lineStyles[i].thickness < 0.0001f) scene.lineStyles[i].thickness = 0.0001f;
            ImGui::PopID();
        }

        bool winHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (winHovered) blockMousePick = true;

        ImGui::End();
        CloseDropdownOnClickOutside(s_thickOpen, winHovered, s_thickBtnMin, s_thickBtnMax);
    }

    if (s_projOpen) {
        float popW = 220.f;
        float popX = s_projBtnMin.x;
        float vpRight = ImGui::GetMainViewport()->WorkPos.x + ImGui::GetMainViewport()->WorkSize.x;
        if (popX + popW > vpRight) popX = vpRight - popW - 4.f;

        ImGui::SetNextWindowPos({popX, s_projBtnMax.y + 2.f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({popW, 0.f}, ImGuiCond_Always);
        ImGui::Begin("##projpopup", nullptr,
            ImGuiWindowFlags_NoTitleBar       | ImGuiWindowFlags_NoResize     |
            ImGuiWindowFlags_NoMove           | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings  | ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoDocking        | ImGuiWindowFlags_NoCollapse);

        ImGui::TextUnformatted("Standard projections (iso/dim)");
        ImGui::TextDisabled("Vertical axis & direction the nav-cube\nball points up.");
        ImGui::TextUnformatted("Up axis");
        ImGui::RadioButton("X", &scene.projUpAxis, 0); ImGui::SameLine();
        ImGui::RadioButton("Y", &scene.projUpAxis, 1); ImGui::SameLine();
        ImGui::RadioButton("Z", &scene.projUpAxis, 2);
        ImGui::TextUnformatted("Direction");
        ImGui::RadioButton("+", &scene.projUpSign,  1); ImGui::SameLine();
        ImGui::RadioButton("-", &scene.projUpSign, -1);

        bool winHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (winHovered) blockMousePick = true;

        ImGui::End();
        CloseDropdownOnClickOutside(s_projOpen, winHovered, s_projBtnMin, s_projBtnMax);
    }
}

}
