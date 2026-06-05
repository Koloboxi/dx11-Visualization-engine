#pragma once
#include "../scene/scene.h"
#include "../../scripting/LuaUpdaterEditor.h"
#include <string>

namespace VectorPick {

inline void Process(Scene& scene, LuaUpdaterEditor& lua) {
    if (lua.awaitingVectorPick)
        scene.pickModeActive = true;

    if (lua.awaitingVectorPick && scene.pickedPrimId != 0) {
        Primitive* from = lua.activeEditorPrim;
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
        lua.awaitingVectorPick = false;
        scene.pickedPrimId = 0;
    }
}

}
