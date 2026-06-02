#pragma once
#include "..\graphics\scene\scene.h"
#include "..\graphics\imgui\imgui.h"
#include "..\graphics\imgui\imgui_stdlib.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <cfloat>
#include <cmath>
#include <algorithm>

#define ENABLE_LUA
#ifdef ENABLE_LUA
extern "C" {
#include "..\external\lua\lua.h"
#include "..\external\lua\lualib.h"
#include "..\external\lua\lauxlib.h"
}
#endif

class LuaUpdaterEditor {
public:
    LuaUpdaterEditor()  { Init(); }
    ~LuaUpdaterEditor() { Shutdown(); }

    LuaUpdaterEditor(const LuaUpdaterEditor&) = delete;
    LuaUpdaterEditor& operator=(const LuaUpdaterEditor&) = delete;

    bool DrawConsole(const std::vector<Primitive*>& selected,
                     const std::vector<Primitive*>& allPrimitives,
                     Scene& scene,
                     bool* outBlockWheel = nullptr,
                     bool* inOutOpen = nullptr,
                     ImGuiWindowFlags extraFlags = 0);

    bool DrawGlobals(const std::vector<GlobalSlider>* extraSliders = nullptr,
                     std::vector<Primitive*>* allPrims = nullptr,
                     float currentTime = 0.0f,
                     bool* global_changed = nullptr,
                     bool* outBlockWheel = nullptr,
                     ImGuiWindowFlags extraFlags = 0);

    struct LuaGlobal { std::string name; float value = 0.0f; };
    std::vector<LuaGlobal> globals;

    bool awaitingVectorPick = false;
    Primitive* activeEditorPrim = nullptr;
    const std::vector<GlobalSlider>* sceneSliders = nullptr;
    const std::unordered_map<std::string, std::shared_ptr<float>>* sceneFloatMap = nullptr;

    void OnPrimitiveRemoved(Primitive* p) {
        if (p) p->ClearUpdater();
        if (activeEditorPrim == p) activeEditorPrim = nullptr;
    }

    void ReApplyAll(const std::vector<Primitive*>& allPrimitives) {
#ifdef ENABLE_LUA
        for (Primitive* p : allPrimitives)
            if (!p->luaScript.empty()) {
                std::string err;
                CompileAndApply(p, allPrimitives, p->luaScript, err);
            }
#endif
    }

    void BindToScene(Scene& scene);
    void CompileController(SceneController& ctrl, Scene& scene);
    void DrawControllerWindows(Scene& scene, bool* blockMousePick);
    void DrawControllerScripts(Scene& scene, bool* outBlockWheel);

    nlohmann::json SerializeLuaState();
    void           RestoreLuaState(const nlohmann::json& j);

private:
    void Init();
    void Shutdown();

    Scene* boundScene = nullptr;

    std::string errorMsg;

    struct ConsoleEntry { std::string text; bool isError = false; };
    std::vector<ConsoleEntry> consoleLog;
    char consoleInputBuf[512] = {};

    void ExecuteConsoleCmd(const std::string& raw, Scene& scene);

#ifdef ENABLE_LUA
    lua_State* L = nullptr;

    struct LuaChunk {
        lua_State* L;
        int ref = LUA_NOREF;
        ~LuaChunk() { if (L && ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, ref); }
        LuaChunk(lua_State* l, int r) : L(l), ref(r) {}
        LuaChunk(const LuaChunk&) = delete;
        LuaChunk& operator=(const LuaChunk&) = delete;
    };

    bool CompileAndApply(Primitive* prim, const std::vector<Primitive*>& allPrims,
                         const std::string& script, std::string& outError);

    void PushPrimTable(Primitive& p);
    void ReadPrimTable(int stackIdx, Primitive& p);

    void PushSceneTable(const std::vector<Primitive*>& prims);
    void ReadSceneTableBack(const std::vector<Primitive*>& prims);
    void PushSceneFloats(Scene& sc);
    void ReadSceneFloatsBack(Scene& sc);

    bool CompileChunk(const std::string& code, int& outRef, std::string& outErr);

    void RegisterSceneAPI(Scene& sc);
    void RegisterImGuiAPI(Scene& sc);

    static nlohmann::json LuaValueToJson(lua_State* L, int idx, int depth = 0);
    static void           JsonToLuaValue(lua_State* L, const nlohmann::json& j);
#endif
};
