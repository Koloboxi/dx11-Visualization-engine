#pragma once
#include "../imgui/imgui.h"
#include "../scene/scene.h"
#include "../../scripting/LuaUpdaterEditor.h"
#include "PrimitivesWindow.h"
#include "TimeControlWindow.h"

// The merged "Scene" window. It replaces the former separate "Scene",
// "Time Control", "Lua Globals" and "Primitives" windows, folding each into a
// collapsible section of a single window. In a SEM-session workspace
// (scene.sceneTreeOnly) only the primitive tree is shown; in a scene workspace
// the management / Time Control / Lua Globals sections appear above it.
namespace SceneWindow {

inline void Draw(Scene& scene, LuaUpdaterEditor& lua,
                 bool& blockMousePick, bool& blockMouseWheel) {
    ImGui::Begin("Scene");

    if (!scene.sceneTreeOnly) {
        if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
            PrimitivesWindow::DrawSceneManagementBody(scene, lua);
            // Per-controller windows (demo scenes) draw their own sub-headers.
            lua.DrawControllerWindows(scene, &blockMousePick);
        }
        if (ImGui::CollapsingHeader("Time Control"))
            TimeControlWindow::DrawBody(scene, blockMousePick);
        if (ImGui::CollapsingHeader("Lua Globals")) {
            bool global_changed = false;
            lua.DrawGlobals(&scene.sceneSliders, &scene.primitives, scene.currentTime,
                            &global_changed, &blockMouseWheel, 0, /*embedded=*/true);
            if (global_changed) lua.ReApplyAll(scene.primitives);
        }
    }

    if (ImGui::CollapsingHeader("Primitives", ImGuiTreeNodeFlags_DefaultOpen))
        PrimitivesWindow::DrawTreeBody(scene, lua, blockMousePick, blockMouseWheel);

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
        blockMousePick = true;
    ImGui::End();
}

}
