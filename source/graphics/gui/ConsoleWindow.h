#pragma once
#include "../scene/scene.h"
#include "../../scripting/LuaUpdaterEditor.h"
#include <vector>

namespace ConsoleWindow {

inline void Draw(Scene& scene, LuaUpdaterEditor& lua, bool& blockMousePick, bool& blockMouseWheel) {
    std::vector<Primitive*> selPrims;
    for (Primitive* p : scene.primitives) if (p->selected) selPrims.push_back(p);

    if (lua.DrawConsole(selPrims, scene.primitives, scene, &blockMouseWheel, nullptr, 0))
        blockMousePick = true;
}

}
