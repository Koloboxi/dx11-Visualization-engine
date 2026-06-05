#pragma once
#include "../scene/scene.h"
#include "../../scripting/LuaUpdaterEditor.h"

namespace LuaGlobalsWindow {

inline void Draw(Scene& scene, LuaUpdaterEditor& lua, bool& blockMousePick, bool& blockMouseWheel) {
    bool global_changed = false;
    if (lua.DrawGlobals(&scene.sceneSliders, &scene.primitives, scene.currentTime,
                        &global_changed, &blockMouseWheel, 0))
        blockMousePick = true;
    if (global_changed) lua.ReApplyAll(scene.primitives);
}

}
