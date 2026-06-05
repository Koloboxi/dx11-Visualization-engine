#pragma once
#include "../imgui/imgui.h"
#include "../scene/scene.h"
#include "../../scripting/LuaUpdaterEditor.h"

namespace SceneAreaWindow {

inline void Draw(Scene& scene, LuaUpdaterEditor& lua, bool& blockMousePick) {
    ImGui::Begin("Scene");
    lua.DrawControllerWindows(scene, &blockMousePick);
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
        blockMousePick = true;
    ImGui::End();
}

}
