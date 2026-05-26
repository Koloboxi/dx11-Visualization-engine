#include "graphics.h"

#include "gui/LayoutState.h"
#include "gui/GuiIcons.h"
#include "gui/TopStripWindow.h"
#include "gui/TimeControlWindow.h"
#include "gui/PrimitivesWindow.h"

static LayoutState s_layout;

static const char* s_activeSplitter = nullptr;

static void DrawSplitters(LayoutState& lay, float wW, float wH, bool& blockMousePick) {
    float conY  = wH - lay.consoleH;
    float panH  = conY - LayoutState::STRIP_H;
    ImVec2 mp   = ImGui::GetMousePos();
    bool anyAct = ImGui::IsAnyItemActive();

    auto* fdl = ImGui::GetForegroundDrawList();
    ImU32 sc  = IM_COL32(55, 62, 82, 200);

    fdl->AddLine({lay.panelW, LayoutState::STRIP_H}, {lay.panelW, wH},    sc, 1.f);
    fdl->AddLine({lay.colW,   LayoutState::STRIP_H}, {lay.colW,   conY},   sc, 1.f);
    fdl->AddLine({0,          conY},                  {lay.panelW, conY},   sc, 1.f);
    fdl->AddLine({0,    LayoutState::STRIP_H + lay.timeH}, {lay.colW,  LayoutState::STRIP_H + lay.timeH}, sc, 1.f);
    fdl->AddLine({lay.colW, LayoutState::STRIP_H + lay.luaH}, {lay.panelW, LayoutState::STRIP_H + lay.luaH}, sc, 1.f);

    float tol = 5.f;

    auto vCheck = [&](const char* id, float* val, float y0, float y1, float vmin, float vmax) {
        bool over = (s_activeSplitter == id) || (!anyAct && s_activeSplitter == nullptr &&
                    mp.x >= *val-tol && mp.x <= *val+tol && mp.y >= y0 && mp.y <= y1);
        if (over || s_activeSplitter == id) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (over && ImGui::IsMouseClicked(0)) s_activeSplitter = id;
        if (s_activeSplitter == id) {
            blockMousePick = true;
            if (ImGui::IsMouseDown(0)) {
                *val += ImGui::GetIO().MouseDelta.x;
                if (*val < vmin) *val = vmin;
                if (*val > vmax) *val = vmax;
            } else s_activeSplitter = nullptr;
        }
    };

    auto hCheck = [&](const char* id, float* val, float x0, float x1, float vmin, float vmax) {
        bool over = (s_activeSplitter == id) || (!anyAct && s_activeSplitter == nullptr &&
                    mp.y >= *val-tol && mp.y <= *val+tol && mp.x >= x0 && mp.x <= x1);
        if (over || s_activeSplitter == id) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (over && ImGui::IsMouseClicked(0)) s_activeSplitter = id;
        if (s_activeSplitter == id) {
            blockMousePick = true;
            if (ImGui::IsMouseDown(0)) {
                *val += ImGui::GetIO().MouseDelta.y;
                if (*val < vmin) *val = vmin;
                if (*val > vmax) *val = vmax;
            } else s_activeSplitter = nullptr;
        }
    };

    vCheck("##sp_panel", &lay.panelW, LayoutState::STRIP_H, wH,  200.f, wW - 200.f);
    vCheck("##sp_col",   &lay.colW,   LayoutState::STRIP_H, conY, 80.f,  lay.panelW - 80.f);

    float mConY = conY;
    hCheck("##sp_con", &mConY, 0.f, lay.panelW, LayoutState::STRIP_H + 100.f, wH - 60.f);
    lay.consoleH = wH - mConY;

    float timeAbsY = LayoutState::STRIP_H + lay.timeH;
    hCheck("##sp_time", &timeAbsY, 0.f, lay.colW, LayoutState::STRIP_H + 60.f, conY - 60.f);
    lay.timeH = timeAbsY - LayoutState::STRIP_H;

    float luaAbsY = LayoutState::STRIP_H + lay.luaH;
    hCheck("##sp_lua", &luaAbsY, lay.colW, lay.panelW, LayoutState::STRIP_H + 60.f, conY - 60.f);
    lay.luaH = luaAbsY - LayoutState::STRIP_H;
}

void Graphics::Gui()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    static int fpsCounter = 0;
    static std::string fpsStr;
    ++fpsCounter;
    if (fpsTimer.GetMillisecondsElapsed() >= 1000) {
        fpsTimer.Restart();
        fpsStr = std::format("{}", fpsCounter);
        fpsCounter = 0;
    }
    ImGui::GetBackgroundDrawList()->AddText(ImVec2(0, 0), ImColor(0, 255, 0), fpsStr.c_str());

    float wW = (float)windowWidth;
    float wH = (float)windowHeight;
    s_layout.Validate(wW, wH);

    float panelH = wH - LayoutState::STRIP_H - s_layout.consoleH;
    float conY   = wH - s_layout.consoleH;

    bool blockMousePick  = false;
    bool blockMouseWheel = false;

    ImGuiWindowFlags fixedFlags =
        ImGuiWindowFlags_NoMove        | ImGuiWindowFlags_NoResize    |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoCollapse;

    // 1. Time Control
    TimeControlWindow::Draw(s_layout, wH, scene, blockMousePick, fixedFlags);

    // 2. Primitives + Scenes
    PrimitivesWindow::Draw(s_layout, wH, scene, luaEditor, blockMousePick, blockMouseWheel, fixedFlags);

    // 3. Lua Globals
    {
        float luaH = s_layout.luaH > 0.f ? s_layout.luaH : panelH * .33f;
        ImGui::SetNextWindowPos({s_layout.colW, LayoutState::STRIP_H}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({s_layout.panelW - s_layout.colW, luaH}, ImGuiCond_Always);
        bool global_changed = false;
        if (luaEditor.DrawGlobals(&scene.sceneSliders, &scene.primitives, scene.currentTime,
                                  &global_changed, &blockMouseWheel, fixedFlags))
            blockMousePick = true;
        if (global_changed) luaEditor.ReApplyAll(scene.primitives);
    }

    // 4. Scene windows area (right column bottom)
    {
        float luaH  = s_layout.luaH > 0.f ? s_layout.luaH : panelH * .33f;
        float swY   = LayoutState::STRIP_H + luaH;
        float swH   = panelH - luaH;
        float swW   = s_layout.panelW - s_layout.colW;
        ImGui::SetNextWindowPos({s_layout.colW, swY}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({swW, swH}, ImGuiCond_Always);
        ImGui::Begin("##scenearea", nullptr, fixedFlags | ImGuiWindowFlags_NoTitleBar);
        for (SceneWindow& w : scene.sceneWindows) {
            if (!w.open || !w.drawContent) continue;
            ImGui::PushID(w.id.c_str());
            if (ImGui::CollapsingHeader(w.title.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                w.drawContent(w);
            ImGui::PopID();
        }
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
            blockMousePick = true;
        ImGui::End();
    }

    // 5. Console (always visible at bottom of panel)
    {
        std::vector<Primitive*> selPrims;
        for (Primitive* p : scene.primitives) if (p->selected) selPrims.push_back(p);

        ImGui::SetNextWindowPos({0.f, conY}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({s_layout.panelW, s_layout.consoleH}, ImGuiCond_Always);
        if (luaEditor.DrawConsole(selPrims, scene.primitives, scene, &blockMouseWheel, nullptr, fixedFlags))
            blockMousePick = true;
    }

    // Pick mode vector insertion
    if (luaEditor.awaitingVectorPick)
        scene.pickModeActive = true;

    if (luaEditor.awaitingVectorPick && scene.pickedPrimId != 0) {
        Primitive* from = luaEditor.activeEditorPrim;
        UINT to_id = scene.pickedPrimId;
        if (from) {
            int toIdx = -1; std::string tname;
            for (int k = 0; k < (int)scene.primitives.size(); k++) {
                if (scene.primitives[k]->id == to_id) {
                    toIdx = k + 1;
                    tname = scene.primitives[k]->name.empty()
                        ? ("p" + std::to_string(to_id)) : scene.primitives[k]->name;
                    break;
                }
            }
            if (toIdx > 0) {
                std::string idx  = std::to_string(toIdx);
                std::string snip = "local vec_to_" + tname +
                    " = {x=scene[" + idx + "].x-p.x" +
                    ", y=scene[" + idx + "].y-p.y" +
                    ", z=scene[" + idx + "].z-p.z}\n";
                from->luaScript.insert(0, snip);
            }
        }
        luaEditor.awaitingVectorPick = false;
        scene.pickedPrimId = 0;
    }

    // 6. Top strip (drawn late to sit above panel content)
    TopStripWindow::Draw(wW, scene, blockMousePick);

    // 7. Splitter overlay (drawn last so it can override cursor)
    DrawSplitters(s_layout, wW, wH, blockMousePick);

    // 8. NavCube
    bool navCubeHover = NavCube::Draw(
        ImVec2(wW, wH), scene.camera,
        LayoutState::STRIP_H, scene.rsSolid, scene.rsWireframe);
    if (navCubeHover) blockMousePick = true;

    scene.blockMousePick  = blockMousePick;
    scene.blockMouseWheel = blockMouseWheel;

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}
