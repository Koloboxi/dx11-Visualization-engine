#include "../graphics.h"

#include "DockHost.h"
#include "TabStripWindow.h"
#include "SceneWindow.h"
#include "MainMenuWindow.h"
#include "SEMWindow.h"
#include "TransformWindow.h"
#include "ConsoleWindow.h"
#include "VectorPick.h"

#include <format>
#include <string>

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

    luaEditor.sceneFloatMap = &scene.sceneFloats;
    if (!scene.luaReApplyCallback)
        luaEditor.BindToScene(scene);

    bool blockMousePick  = false;
    bool blockMouseWheel = false;

    // Tab strip below the OS caption: "Main menu" plus (once opened) the single
    // workspace tab.
    const float tabH = TabStripWindow::Height();
    TabStripWindow::Draw(scene);

    // Main-menu tab: it covers the 3D viewport, so the mouse must not reach it
    // and no workspace windows draw.
    if (scene.activeTab == 0) {
        MainMenuWindow::Draw(scene, luaEditor, tabH);
        scene.blockMousePick  = true;
        scene.blockMouseWheel = true;
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        return;
    }

    DockHost::Begin(scene, blockMousePick, fpsStr.c_str(), tabH);

    SEMWindow::Draw(scene, blockMousePick);
    SceneWindow::Draw(scene, luaEditor, blockMousePick, blockMouseWheel);
    TransformWindow::Draw(scene, blockMousePick);
    ConsoleWindow::Draw(scene, luaEditor, blockMousePick, blockMouseWheel);

    if (scene.stagingEnabled && ImGui::IsMouseDoubleClicked(0) && !ImGui::GetIO().WantCaptureMouse)
        scene.ClearStaged();

    VectorPick::Process(scene, luaEditor);

    bool navCubeHover = NavCube::Draw(
        ImVec2((float)windowWidth, (float)windowHeight), scene.camera);
    if (navCubeHover) blockMousePick = true;

    // Block camera zoom whenever the wheel is over any ImGui window/popup, the
    // same way blockMousePick guards clicks. The dock host's central node is a
    // pass-through, so WantCaptureMouse stays false over the bare 3D viewport.
    if (ImGui::GetIO().WantCaptureMouse) blockMouseWheel = true;

    scene.blockMousePick  = blockMousePick;
    scene.blockMouseWheel = blockMouseWheel;

    DockHost::End();

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}
