#include "LuaUpdaterEditor.h"

void LuaUpdaterEditor::CompileController(SceneController& ctrl, Scene& sc)
{
#ifdef ENABLE_LUA
    ctrl.compiledTick   = nullptr;
    ctrl.compiledReset  = nullptr;
    ctrl.compiledFns.clear();

    struct CompiledEntry { std::string name; std::shared_ptr<LuaChunk> chunk; };
    std::vector<CompiledEntry> compiled;

    for (auto& s : ctrl.scripts) {
        if (s.code.empty()) continue;
        int ref; std::string err;
        if (!CompileChunk(s.code, ref, err)) continue;
        compiled.push_back({s.name, std::make_shared<LuaChunk>(L, ref)});
    }

    lua_State* LS = L;
    auto* primsPtr = &sc.primitives;

    auto makeTickFn = [this, LS, primsPtr](std::shared_ptr<LuaChunk> chunk)
        -> std::function<void(Scene&,float,float,bool)>
    {
        return [this, LS, chunk, primsPtr](Scene& scene, float t, float dt, bool paused) {
            lua_pushnumber(LS,t);  lua_setglobal(LS,"t");
            lua_pushnumber(LS,dt); lua_setglobal(LS,"dt");
            lua_pushboolean(LS,(int)paused); lua_setglobal(LS,"paused");
            PushSceneTable(*primsPtr);
            PushSceneFloats(scene);
            lua_rawgeti(LS, LUA_REGISTRYINDEX, chunk->ref);
            if (lua_pcall(LS,0,0,0)!=LUA_OK) { lua_pop(LS,1); return; }
            ReadSceneTableBack(*primsPtr);
            ReadSceneFloatsBack(scene);
        };
    };

    auto makeResetFn = [this, LS, primsPtr](std::shared_ptr<LuaChunk> chunk)
        -> std::function<void(Scene&)>
    {
        return [this, LS, chunk, primsPtr](Scene& scene) {
            lua_pushnumber(LS, 0.f); lua_setglobal(LS,"t");
            lua_pushnumber(LS, 0.f); lua_setglobal(LS,"dt");
            lua_pushboolean(LS, 1);  lua_setglobal(LS,"paused");
            PushSceneTable(*primsPtr);
            PushSceneFloats(scene);
            lua_rawgeti(LS, LUA_REGISTRYINDEX, chunk->ref);
            if (lua_pcall(LS,0,0,0)!=LUA_OK) { lua_pop(LS,1); return; }
            ReadSceneTableBack(*primsPtr);
            ReadSceneFloatsBack(scene);
        };
    };

    auto makeGenericFn = [this, LS, primsPtr](std::shared_ptr<LuaChunk> chunk)
        -> std::function<void(Scene&)>
    {
        return [this, LS, chunk, primsPtr](Scene& scene) {
            PushSceneTable(*primsPtr);
            PushSceneFloats(scene);
            lua_rawgeti(LS, LUA_REGISTRYINDEX, chunk->ref);
            if (lua_pcall(LS,0,0,0)!=LUA_OK) { lua_pop(LS,1); return; }
            ReadSceneTableBack(*primsPtr);
            ReadSceneFloatsBack(scene);
        };
    };

    for (auto& ce : compiled) {
        if (ce.name == "tick")
            ctrl.compiledTick = makeTickFn(ce.chunk);
        else if (ce.name == "reset")
            ctrl.compiledReset = makeResetFn(ce.chunk);
        else
            ctrl.compiledFns[ce.name] = makeGenericFn(ce.chunk);
    }
#endif
}


void LuaUpdaterEditor::DrawControllerWindows(Scene& sc, bool* blockMousePick)
{
#ifdef ENABLE_LUA
    if (!sc.controller) return;
    for (auto& wd : sc.controller->windowDefs) {
        const std::string* drawScript = sc.controller->GetScriptCode("draw_" + wd.id);
        if (!drawScript) continue;
        ImGui::PushID(wd.id.c_str());
        if (ImGui::CollapsingHeader(wd.title.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            lua_pushnumber(L,sc.currentTime); lua_setglobal(L,"t");
            lua_pushboolean(L,(int)sc.timePaused); lua_setglobal(L,"paused");
            PushSceneTable(sc.primitives);
            PushSceneFloats(sc);
            if (luaL_loadstring(L, drawScript->c_str())==LUA_OK)
                if (lua_pcall(L,0,0,0)!=LUA_OK) lua_pop(L,1);
            ReadSceneTableBack(sc.primitives);
            ReadSceneFloatsBack(sc);
        }
        ImGui::PopID();
    }
    (void)blockMousePick;
#endif
}


void LuaUpdaterEditor::DrawControllerScripts(Scene& scene, bool* outBlockWheel)
{
#ifdef ENABLE_LUA
    SceneController* ctrl = scene.controller;
    if (!ctrl) return;

    ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1.f), "[controller]");
    ImGui::SameLine();
    ImGui::TextDisabled("tick / reset run automatically; draw_<id> feed the scene windows");

    if (ImGui::BeginTabBar("##ctrlscripts")) {
        for (auto& s : ctrl->scripts) {
            if (ImGui::BeginTabItem(s.name.c_str())) {
                float scriptH = -ImGui::GetFrameHeightWithSpacing() - 4;
                ImGui::InputTextMultiline(("##code_" + s.name).c_str(), &s.code,
                    ImVec2(-1, scriptH), ImGuiInputTextFlags_AllowTabInput);
                if (outBlockWheel && ImGui::IsItemHovered()) *outBlockWheel = true;
                if (ImGui::Button("Apply")) {
                    CompileController(*ctrl, scene);
                    scene.sceneTick  = ctrl->compiledTick;
                    scene.sceneReset = ctrl->compiledReset;
                }
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
#endif
}
