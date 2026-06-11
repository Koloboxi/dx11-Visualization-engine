#include "../graphics.h"

#include "DockHost.h"
#include "TimeControlWindow.h"
#include "PrimitivesWindow.h"
#include "SEMWindow.h"
#include "LuaGlobalsWindow.h"
#include "SceneAreaWindow.h"
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

    DockHost::Begin(scene, blockMousePick, fpsStr.c_str());

    TimeControlWindow::Draw(scene, blockMousePick);
    PrimitivesWindow::Draw(scene, luaEditor, blockMousePick, blockMouseWheel);
    SEMWindow::Draw(scene, blockMousePick);
    LuaGlobalsWindow::Draw(scene, luaEditor, blockMousePick, blockMouseWheel);
    SceneAreaWindow::Draw(scene, luaEditor, blockMousePick);
    ConsoleWindow::Draw(scene, luaEditor, blockMousePick, blockMouseWheel);

    if (scene.stagingEnabled && ImGui::IsMouseDoubleClicked(0) && !ImGui::GetIO().WantCaptureMouse)
        scene.ClearStaged();

    VectorPick::Process(scene, luaEditor);

    bool navCubeHover = NavCube::Draw(
        ImVec2((float)windowWidth, (float)windowHeight), scene.camera,
        ImGui::GetFrameHeight() + 8.f, scene.rsSolid, scene.rsWireframe, scene.rsNoCull);
    if (navCubeHover) blockMousePick = true;

    scene.blockMousePick  = blockMousePick;
    scene.blockMouseWheel = blockMouseWheel;

    DockHost::End();

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}
