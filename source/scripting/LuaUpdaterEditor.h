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

inline void LuaUpdaterEditor::ExecuteConsoleCmd(const std::string& raw, Scene& scene)
{
    auto log = [&](const std::string& s, bool err = false) {
        consoleLog.push_back({ s, err });
    };

    std::istringstream ss(raw);
    std::string cmd;
    if (!(ss >> cmd)) return;

    auto parseColor = [&](std::vector<std::string>& toks, int offset, XMFLOAT4& col) {
        if ((int)toks.size() >= offset + 4) {
            col.x = std::stof(toks[offset]);
            col.y = std::stof(toks[offset + 1]);
            col.z = std::stof(toks[offset + 2]);
            col.w = std::stof(toks[offset + 3]);
        }
    };

    std::vector<std::string> args;
    { std::string t; while (ss >> t) args.push_back(t); }

    try {
        if (cmd == "help") {
            std::string sub = args.empty() ? "" : args[0];
            if (sub.empty()) {
                log("Commands: help, add, del");
                log("  help add                  - list add subcommands");
                log("  add <type> [args]         - add a primitive");
                log("  del id <id>               - delete by id");
                log("  del name <name>           - delete by name");
            } else if (sub == "add") {
                log("add point <x> <y> <z> [r g b a]");
                log("add sphere <radius> <x> <y> <z> <subdivs> [r g b a]");
                log("add arc3d <arcR> <tubeR> <deg> <cx> <cy> <cz> <divs> [r g b a]");
                log("add arrow3d <shaftR> <headR> <headLen> <fx> <fy> <fz> <tx> <ty> <tz> <sides> [r g b a]");
                log("Colors default to 0.6 0.6 0.9 1.0 if not provided.");
            } else {
                log("Unknown help topic. Try: help, help add", true);
            }
        }
        else if (cmd == "add") {
            if (args.empty()) { log("add: missing type. Try 'help add'.", true); return; }
            XMFLOAT4 col{ 0.6f, 0.6f, 0.9f, 1.0f };
            std::string type = args[0];
            if (type == "point") {
                if (args.size() < 4) { log("add point: usage: add point <x> <y> <z> [r g b a]", true); return; }
                float x = std::stof(args[1]), y = std::stof(args[2]), z = std::stof(args[3]);
                parseColor(args, 4, col);
                scene.AddPoint({ x,y,z }, col);
                log("Added point at " + args[1] + " " + args[2] + " " + args[3]);
            }
            else if (type == "sphere") {
                if (args.size() < 6) { log("add sphere: usage: add sphere <radius> <x> <y> <z> <subdivs> [r g b a]", true); return; }
                float r = std::stof(args[1]);
                float x = std::stof(args[2]), y = std::stof(args[3]), z = std::stof(args[4]);
                int subdivs = std::stoi(args[5]);
                parseColor(args, 6, col);
                scene.AddSphere(r, { x,y,z }, (UINT)std::max(0, subdivs), col);
                log("Added sphere r=" + args[1]);
            }
            else if (type == "arc3d") {
                if (args.size() < 8) { log("add arc3d: usage: add arc3d <arcR> <tubeR> <deg> <cx> <cy> <cz> <divs> [r g b a]", true); return; }
                float aR = std::stof(args[1]), tR = std::stof(args[2]), deg = std::stof(args[3]);
                float cx = std::stof(args[4]), cy = std::stof(args[5]), cz = std::stof(args[6]);
                int divs = std::stoi(args[7]);
                parseColor(args, 8, col);
                scene.AddArc3d(aR, tR, deg, { cx,cy,cz }, (UINT)std::max(3, divs), col);
                log("Added arc3d");
            }
            else if (type == "arrow3d") {
                if (args.size() < 11) { log("add arrow3d: usage: add arrow3d <shaftR> <headR> <headLen> <fx> <fy> <fz> <tx> <ty> <tz> <sides> [r g b a]", true); return; }
                float shR = std::stof(args[1]), heR = std::stof(args[2]), heL = std::stof(args[3]);
                float fx = std::stof(args[4]), fy = std::stof(args[5]), fz = std::stof(args[6]);
                float tx = std::stof(args[7]), ty = std::stof(args[8]), tz = std::stof(args[9]);
                int sides = std::stoi(args[10]);
                parseColor(args, 11, col);
                scene.AddArrow3d(shR, heR, heL, { fx,fy,fz }, { tx,ty,tz }, (UINT)std::max(3, sides), col);
                log("Added arrow3d");
            }
            else {
                log("Unknown primitive type '" + type + "'. Try 'help add'.", true);
            }
        }
        else if (cmd == "del") {
            if (args.empty()) { log("del: missing mode. Use 'del id <id>' or 'del name <name>'.", true); return; }
            std::string mode = args[0];
            if (mode == "id") {
                if (args.size() < 2) { log("del id: missing id", true); return; }
                UINT id = (UINT)std::stoul(args[1]);
                Primitive* found = nullptr;
                for (Primitive* p : scene.primitives) if (p->id == id) { found = p; break; }
                if (!found) { log("del id: id " + args[1] + " not found", true); return; }
                if (found == activeEditorPrim) activeEditorPrim = nullptr;
                found->ClearUpdater();
                scene.RemovePrimitive(found);
                log("Deleted id " + args[1]);
            }
            else if (mode == "name") {
                if (args.size() < 2) { log("del name: missing name", true); return; }
                std::string name = args[1];
                std::vector<Primitive*> toDelete;
                for (Primitive* p : scene.primitives) if (p->name == name) toDelete.push_back(p);
                if (toDelete.empty()) { log("del name: '" + name + "' not found", true); return; }
                for (Primitive* p : toDelete) {
                    if (p == activeEditorPrim) activeEditorPrim = nullptr;
                    p->ClearUpdater();
                    scene.RemovePrimitive(p);
                }
                log("Deleted " + std::to_string(toDelete.size()) + " primitive(s) named '" + name + "'");
            }
            else {
                log("del: unknown mode '" + mode + "'. Use id or name.", true);
            }
        }
        else {
            log("Unknown command: '" + cmd + "'. Type 'help' for commands.", true);
        }
    }
    catch (const std::exception& e) {
        log(std::string("Parse error: ") + e.what(), true);
    }
}

inline bool LuaUpdaterEditor::DrawConsole(const std::vector<Primitive*>& selected,
                                           const std::vector<Primitive*>& allPrimitives,
                                           Scene& scene,
                                           bool* outBlockWheel,
                                           bool* inOutOpen,
                                           ImGuiWindowFlags extraFlags)
{
    bool localOpen = true;
    bool* openPtr = inOutOpen ? inOutOpen : &localOpen;
    ImGui::Begin("Console", openPtr, extraFlags);
    bool blockPick = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    if (scene.controllerSelected && scene.controller) {
        DrawControllerScripts(scene, outBlockWheel);
    }
    else if (selected.empty()) {
        float logH = -ImGui::GetFrameHeightWithSpacing() * 2 - 8;
        if (ImGui::BeginChild("##conlog", ImVec2(0, logH), true)) {
            if (outBlockWheel && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
                *outBlockWheel = true;
            for (const auto& entry : consoleLog) {
                if (entry.isError)
                    ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "%s", entry.text.c_str());
                else
                    ImGui::TextUnformatted(entry.text.c_str());
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        bool run = false;
        ImGui::SetNextItemWidth(-70);
        if (ImGui::InputText("##cin", consoleInputBuf, sizeof(consoleInputBuf),
            ImGuiInputTextFlags_EnterReturnsTrue))
            run = true;
        ImGui::SameLine();
        if (ImGui::Button("Run", ImVec2(60, 0))) run = true;

        if (run && consoleInputBuf[0] != '\0') {
            consoleLog.push_back({ std::string("> ") + consoleInputBuf, false });
            ExecuteConsoleCmd(std::string(consoleInputBuf), scene);
            consoleInputBuf[0] = '\0';
        }
    }
    else {
        if (ImGui::BeginTabBar("##primtabs")) {
            for (Primitive* prim : selected) {
                std::string tabLabel = prim->name.empty()
                    ? ("id " + std::to_string(prim->id))
                    : prim->name;

                if (ImGui::BeginTabItem(tabLabel.c_str())) {
                    activeEditorPrim = prim;

#ifndef ENABLE_LUA
                    ImGui::TextColored(ImVec4(1.f, 0.8f, 0.2f, 1.f), "Lua disabled.");
                    ImGui::InputTextMultiline("##script", &prim->luaScript,
                        ImVec2(-1, -ImGui::GetFrameHeightWithSpacing() - 4));
                    ImGui::BeginDisabled(); ImGui::Button("Apply"); ImGui::EndDisabled();
#else
                    if (prim->HasUpdater())
                        ImGui::TextColored(ImVec4(0.3f, 1.f, 0.4f, 1.f), "[active]");
                    else
                        ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.f), "[no updater]");

                    ImGui::SameLine();
                    if (ImGui::SmallButton("Clear##lua")) {
                        prim->ClearUpdater();
                        prim->luaScript.clear();
                        errorMsg.clear();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Pick vec")) {
                        awaitingVectorPick = true;
                        activeEditorPrim = prim;
                    }
                    if (awaitingVectorPick && activeEditorPrim == prim) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(1.f, 0.8f, 0.1f, 1.f), "click a primitive...");
                    }
                    ImGui::Separator();
                    ImGui::TextDisabled("p.{x,y,z,scale,rx,ry,rz,rw,r,g,b,a}  vx,vy,vz(ro)  scene[i]  t  dt");

                    float scriptH = -ImGui::GetFrameHeightWithSpacing() - 4;
                    ImGui::InputTextMultiline("##script", &prim->luaScript,
                        ImVec2(-1, scriptH),
                        ImGuiInputTextFlags_AllowTabInput);
                    if (outBlockWheel && ImGui::IsItemHovered())
                        *outBlockWheel = true;

                    if (ImGui::Button("Apply")) {
                        errorMsg.clear();
                        if (!CompileAndApply(prim, allPrimitives, prim->luaScript, errorMsg))
                            errorMsg = "Error: " + errorMsg;
                    }
                    if (!errorMsg.empty()) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(1.f, 0.3f, 0.3f, 1.f), "%s", errorMsg.c_str());
                    }
#endif
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
    }

    ImGui::End();
    return blockPick;
}

inline bool LuaUpdaterEditor::DrawGlobals(const std::vector<GlobalSlider>* extraSliders,
                                           std::vector<Primitive*>* allPrims,
                                           float currentTime,
                                           bool* global_changed,
                                           bool* outBlockWheel,
                                           ImGuiWindowFlags extraFlags) {
    ImGui::Begin("Lua Globals", nullptr, extraFlags);
    bool blockPick = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    if (extraSliders && !extraSliders->empty()) {
        for (const auto& slider : *extraSliders) {
            if (!slider.valuePtr) continue;
            ImGui::TextUnformatted(slider.label.c_str());
            ImGui::SameLine();
            ImGui::SetNextItemWidth(130);
            float dragSpeed = (slider.max - slider.min) * 0.005f;
            if (dragSpeed <= 0.f) dragSpeed = 0.1f;
            ImGui::PushID(slider.label.c_str());
            // DragFloat (not SliderFloat) so values can be typed via Ctrl+click; clamps to [min,max]
            ImGui::DragFloat("##s", slider.valuePtr, dragSpeed, slider.min, slider.max, "%.2f");
            ImGui::PopID();
        }
        ImGui::Separator();
    }

    float childH = -ImGui::GetFrameHeightWithSpacing() - 4;
    if (ImGui::BeginChild("##glist", ImVec2(0, childH))) {
        if (outBlockWheel && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
            *outBlockWheel = true;
        bool anyChanged = false;
        for (int i = 0; i < (int)globals.size(); i++) {
            ImGui::PushID(i);
            ImGui::SetNextItemWidth(130);
            ImGui::InputText("##n", &globals[i].name);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            if (ImGui::DragFloat("##v", &globals[i].value, 0.1f, 0, 0, "%.4g"))
                anyChanged = true;
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) {
                globals.erase(globals.begin() + i);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        if (global_changed) *global_changed = anyChanged && allPrims;
    }
    ImGui::EndChild();

    if (ImGui::Button("+ Add")) globals.push_back({});
    ImGui::End();
    return blockPick;
}

inline void LuaUpdaterEditor::Init() {
#ifdef ENABLE_LUA
    L = luaL_newstate();
    luaL_openlibs(L);
    luaL_dostring(L, R"(
function euler(x,y,z)
  local cx,sx=math.cos(x/2),math.sin(x/2)
  local cy,sy=math.cos(y/2),math.sin(y/2)
  local cz,sz=math.cos(z/2),math.sin(z/2)
  return sx*cy*cz-cx*sy*sz, cx*sy*cz+sx*cy*sz, cx*cy*sz-sx*sy*cz, cx*cy*cz+sx*sy*sz
end
function gauss(mu, sigma)
  local u1=math.random(); if u1<1e-10 then u1=1e-10 end
  local z=math.sqrt(-2*math.log(u1))*math.cos(2*math.pi*math.random())
  return mu + sigma*z
end
function hsv_to_rgb(h,s,v)
  local i=math.floor(h*6); local f=h*6-i; local p=v*(1-s)
  local q=v*(1-f*s); local t=v*(1-(1-f)*s)
  local r,g,b
  local m=i%6
  if m==0 then r,g,b=v,t,p elseif m==1 then r,g,b=q,v,p
  elseif m==2 then r,g,b=p,v,t elseif m==3 then r,g,b=p,q,v
  elseif m==4 then r,g,b=t,p,v else r,g,b=v,p,q end
  return r,g,b
end
math.pi = math.pi or 3.14159265358979
)");
#endif
}

inline void LuaUpdaterEditor::Shutdown() {
#ifdef ENABLE_LUA
    if (L) { lua_close(L); L = nullptr; }
#endif
}

#ifdef ENABLE_LUA

inline void LuaUpdaterEditor::PushPrimTable(Primitive& p) {
    XMFLOAT3 pos = p.GetPosition();
    XMFLOAT4 rot = p.GetRotation();
    XMFLOAT4 col = p.GetColor();
    lua_newtable(L);
    auto setNum = [&](const char* k, float v) {
        lua_pushnumber(L, v); lua_setfield(L, -2, k);
    };
    setNum("x", pos.x); setNum("y", pos.y); setNum("z", pos.z);
    setNum("scale", p.GetScale());
    setNum("rx", rot.x); setNum("ry", rot.y); setNum("rz", rot.z); setNum("rw", rot.w);
    setNum("r", col.x); setNum("g", col.y); setNum("b", col.z); setNum("a", col.w);
    setNum("vx", p.velocity.x); setNum("vy", p.velocity.y); setNum("vz", p.velocity.z);
}

inline void LuaUpdaterEditor::ReadPrimTable(int idx, Primitive& p) {
    auto getNum = [&](const char* k) -> float {
        lua_getfield(L, idx, k);
        float v = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);
        return v;
    };
    p.SetPosition({ getNum("x"), getNum("y"), getNum("z") });
    p.SetScale(getNum("scale"));
    float rx = getNum("rx"), ry = getNum("ry"), rz = getNum("rz"), rw = getNum("rw");
    float lenSq = rx*rx + ry*ry + rz*rz + rw*rw;
    if (lenSq > 0.01f)
        p.SetRotation(XMFLOAT4(rx, ry, rz, rw));
    p.SetColor({ getNum("r"), getNum("g"), getNum("b"), getNum("a") });
}

inline bool LuaUpdaterEditor::CompileAndApply(
    Primitive* prim, const std::vector<Primitive*>& allPrims,
    const std::string& script, std::string& outError)
{
    prim->ClearUpdater();

    if (script.empty()) return true;

    if (luaL_loadstring(L, script.c_str()) != LUA_OK) {
        outError = lua_tostring(L, -1);
        lua_pop(L, 1);
        return false;
    }

    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    auto chunk = std::make_shared<LuaChunk>(L, ref);

    lua_State* LS = L;
    const std::vector<Primitive*>* primsPtr = &allPrims;

    prim->SetUpdater([this, LS, chunk, primsPtr](Primitive& self, float t, float dt) {
        lua_pushnumber(LS, t);  lua_setglobal(LS, "t");
        lua_pushnumber(LS, dt); lua_setglobal(LS, "dt");

        PushPrimTable(self);
        lua_setglobal(LS, "p");

        lua_newtable(LS);
        for (int i = 0; i < (int)primsPtr->size(); ++i) {
            Primitive* o = (*primsPtr)[i];
            lua_newtable(LS);
            XMFLOAT3 op = o->GetPosition();
            lua_pushnumber(LS, op.x);          lua_setfield(LS, -2, "x");
            lua_pushnumber(LS, op.y);          lua_setfield(LS, -2, "y");
            lua_pushnumber(LS, op.z);          lua_setfield(LS, -2, "z");
            lua_pushnumber(LS, o->GetScale()); lua_setfield(LS, -2, "scale");
            lua_pushnumber(LS, o->velocity.x); lua_setfield(LS, -2, "vx");
            lua_pushnumber(LS, o->velocity.y); lua_setfield(LS, -2, "vy");
            lua_pushnumber(LS, o->velocity.z); lua_setfield(LS, -2, "vz");
            lua_seti(LS, -2, i + 1);
        }
        lua_setglobal(LS, "scene");

        for (auto& g : this->globals)
            if (!g.name.empty()) {
                lua_pushnumber(LS, (lua_Number)g.value);
                lua_setglobal(LS, g.name.c_str());
            }

        if (this->sceneSliders)
            for (const auto& sl : *this->sceneSliders)
                if (!sl.luaGlobalName.empty() && sl.valuePtr) {
                    lua_pushnumber(LS, (lua_Number)*sl.valuePtr);
                    lua_setglobal(LS, sl.luaGlobalName.c_str());
                }

        if (this->sceneFloatMap)
            for (const auto& [k, v] : *this->sceneFloatMap)
                if (v) {
                    lua_pushnumber(LS, (lua_Number)*v);
                    lua_setglobal(LS, k.c_str());
                }

        lua_rawgeti(LS, LUA_REGISTRYINDEX, chunk->ref);
        if (lua_pcall(LS, 0, 0, 0) != LUA_OK) {
            lua_pop(LS, 1); return;
        }

        lua_getglobal(LS, "p");
        if (lua_istable(LS, -1)) ReadPrimTable(-1, self);
        lua_pop(LS, 1);

        int selfIdx = -1;
        for (int i = 0; i < (int)primsPtr->size(); i++)
            if ((*primsPtr)[i] == &self) { selfIdx = i; break; }

        lua_getglobal(LS, "scene");
        if (lua_istable(LS, -1)) {
            for (int i = 0; i < (int)primsPtr->size(); i++) {
                if (i == selfIdx) continue;
                lua_geti(LS, -1, i + 1);
                if (lua_istable(LS, -1)) {
                    auto gN = [&](const char* k) -> float {
                        lua_getfield(LS, -1, k);
                        float v = (float)lua_tonumber(LS, -1);
                        lua_pop(LS, 1); return v;
                    };
                    Primitive* other = (*primsPtr)[i];
                    XMFLOAT3 op = other->GetPosition();
                    float nx = gN("x"), ny = gN("y"), nz = gN("z");
                    if (nx != op.x || ny != op.y || nz != op.z)
                        other->SetPosition({nx, ny, nz});
                    float ns = gN("scale");
                    if (ns != other->GetScale()) other->SetScale(ns);
                    other->velocity = {gN("vx"), gN("vy"), gN("vz")};
                }
                lua_pop(LS, 1);
            }
        }
        lua_pop(LS, 1);
    });

    return true;
}

// ─── Helper: push/read full scene[] table ──────────────────────────────────

inline void LuaUpdaterEditor::PushSceneTable(const std::vector<Primitive*>& prims)
{
#ifdef ENABLE_LUA
    lua_newtable(L);
    for (int i = 0; i < (int)prims.size(); ++i) {
        Primitive& p = *prims[i];
        XMFLOAT3 pos = p.GetPosition();
        XMFLOAT4 rot = p.GetRotation();
        XMFLOAT4 col = p.GetColor();
        lua_newtable(L);
        auto sN = [&](const char* k, float v){ lua_pushnumber(L,v); lua_setfield(L,-2,k); };
        auto sS = [&](const char* k, const char* v){ lua_pushstring(L,v); lua_setfield(L,-2,k); };
        sN("x", pos.x); sN("y", pos.y); sN("z", pos.z);
        sN("scale", p.GetScale());
        sN("rx", rot.x); sN("ry", rot.y); sN("rz", rot.z); sN("rw", rot.w);
        sN("r", col.x); sN("g", col.y); sN("b", col.z); sN("a", col.w);
        sN("vx", p.velocity.x); sN("vy", p.velocity.y); sN("vz", p.velocity.z);
        sS("name", p.name.c_str());
        lua_seti(L, -2, i + 1);
    }
    lua_setglobal(L, "scene");
#endif
}

inline void LuaUpdaterEditor::ReadSceneTableBack(const std::vector<Primitive*>& prims)
{
#ifdef ENABLE_LUA
    lua_getglobal(L, "scene");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }
    for (int i = 0; i < (int)prims.size(); ++i) {
        lua_geti(L, -1, i + 1);
        if (lua_istable(L, -1)) {
            Primitive& p = *prims[i];
            auto gN = [&](const char* k) -> float {
                lua_getfield(L, -1, k); float v=(float)lua_tonumber(L,-1); lua_pop(L,1); return v;
            };
            p.SetPosition({gN("x"), gN("y"), gN("z")});
            p.SetScale(gN("scale"));
            float rx=gN("rx"),ry=gN("ry"),rz=gN("rz"),rw=gN("rw");
            float lsq=rx*rx+ry*ry+rz*rz+rw*rw;
            if (lsq > 0.01f) p.SetRotation({rx,ry,rz,rw});
            p.SetColor({gN("r"),gN("g"),gN("b"),gN("a")});
            p.velocity = {gN("vx"),gN("vy"),gN("vz")};
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
#endif
}

inline void LuaUpdaterEditor::PushSceneFloats(Scene& sc)
{
#ifdef ENABLE_LUA
    for (auto& [k,v] : sc.sceneFloats)
        if (v) { lua_pushnumber(L,(lua_Number)*v); lua_setglobal(L,k.c_str()); }
    for (auto& g : globals)
        if (!g.name.empty()) { lua_pushnumber(L,(lua_Number)g.value); lua_setglobal(L,g.name.c_str()); }
    if (sceneSliders)
        for (auto& sl : *sceneSliders)
            if (!sl.luaGlobalName.empty() && sl.valuePtr)
                { lua_pushnumber(L,(lua_Number)*sl.valuePtr); lua_setglobal(L,sl.luaGlobalName.c_str()); }
#endif
}

inline void LuaUpdaterEditor::ReadSceneFloatsBack(Scene& sc)
{
#ifdef ENABLE_LUA
    for (auto& [k,v] : sc.sceneFloats) {
        if (!v) continue;
        lua_getglobal(L, k.c_str());
        if (lua_isnumber(L,-1)) *v = (float)lua_tonumber(L,-1);
        lua_pop(L,1);
    }
#endif
}

inline bool LuaUpdaterEditor::CompileChunk(const std::string& code, int& outRef, std::string& outErr)
{
#ifdef ENABLE_LUA
    if (code.empty()) { outRef = LUA_NOREF; return true; }
    if (luaL_loadstring(L, code.c_str()) != LUA_OK) {
        outErr = lua_tostring(L,-1); lua_pop(L,1); outRef = LUA_NOREF; return false;
    }
    outRef = luaL_ref(L, LUA_REGISTRYINDEX);
    return true;
#else
    outRef = LUA_NOREF; return true;
#endif
}

// ─── Scene Lua API ──────────────────────────────────────────────────────────

#ifdef ENABLE_LUA
static int lua_SceneAddPoint(lua_State* L) {
    Scene* sc = (Scene*)lua_touserdata(L, lua_upvalueindex(1));
    float x=(float)luaL_optnumber(L,1,0), y=(float)luaL_optnumber(L,2,0), z=(float)luaL_optnumber(L,3,0);
    float r=(float)luaL_optnumber(L,4,.6f),g=(float)luaL_optnumber(L,5,.6f),
          b=(float)luaL_optnumber(L,6,.9f),a=(float)luaL_optnumber(L,7,1.f);
    sc->AddPoint({x,y,z},{r,g,b,a});
    lua_pushinteger(L, sc->primitives.back()->id);
    return 1;
}
static int lua_SceneAddSphere(lua_State* L) {
    Scene* sc = (Scene*)lua_touserdata(L, lua_upvalueindex(1));
    float rad=(float)luaL_optnumber(L,1,50);
    float x=(float)luaL_optnumber(L,2,0),y=(float)luaL_optnumber(L,3,0),z=(float)luaL_optnumber(L,4,0);
    int divs=(int)luaL_optinteger(L,5,2);
    float r=(float)luaL_optnumber(L,6,.6f),g=(float)luaL_optnumber(L,7,.6f),
          b=(float)luaL_optnumber(L,8,.9f),a=(float)luaL_optnumber(L,9,1.f);
    sc->AddSphere(rad,{x,y,z},(UINT)divs,{r,g,b,a});
    lua_pushinteger(L, sc->primitives.back()->id);
    return 1;
}
static int lua_SceneAddCubeWireframe(lua_State* L) {
    Scene* sc = (Scene*)lua_touserdata(L, lua_upvalueindex(1));
    float hs=(float)luaL_optnumber(L,1,150);
    float x=(float)luaL_optnumber(L,2,0),y=(float)luaL_optnumber(L,3,0),z=(float)luaL_optnumber(L,4,0);
    float r=(float)luaL_optnumber(L,5,.4f),g=(float)luaL_optnumber(L,6,.5f),
          b=(float)luaL_optnumber(L,7,.6f),a=(float)luaL_optnumber(L,8,.9f);
    sc->AddCubeWireframe(hs,{x,y,z},{r,g,b,a});
    lua_pushinteger(L, sc->primitives.back()->id);
    return 1;
}
static int lua_SceneRemove(lua_State* L) {
    Scene* sc = (Scene*)lua_touserdata(L, lua_upvalueindex(1));
    UINT id = (UINT)luaL_checkinteger(L,1);
    for (Primitive* p : sc->primitives) if (p->id == id) { sc->RemovePrimitive(p); break; }
    return 0;
}
static int lua_SceneRemoveByName(lua_State* L) {
    Scene* sc = (Scene*)lua_touserdata(L, lua_upvalueindex(1));
    std::string nm = luaL_checkstring(L,1);
    std::vector<Primitive*> del;
    for (Primitive* p : sc->primitives) if (p->name == nm) del.push_back(p);
    for (Primitive* p : del) sc->RemovePrimitive(p);
    return 0;
}
static int lua_SceneRemoveByPrefix(lua_State* L) {
    Scene* sc = (Scene*)lua_touserdata(L, lua_upvalueindex(1));
    std::string pfx = luaL_checkstring(L,1);
    sc->RemovePrimitivesByPrefix(pfx);
    return 0;
}
static int lua_SceneSetName(lua_State* L) {
    Scene* sc = (Scene*)lua_touserdata(L, lua_upvalueindex(1));
    UINT id = (UINT)luaL_checkinteger(L,1);
    const char* nm = luaL_checkstring(L,2);
    for (Primitive* p : sc->primitives) if (p->id == id) { p->name = nm; break; }
    return 0;
}
static int lua_SceneSetParent(lua_State* L) {
    Scene* sc = (Scene*)lua_touserdata(L, lua_upvalueindex(1));
    UINT childId  = (UINT)luaL_checkinteger(L,1);
    UINT parentId = (UINT)luaL_checkinteger(L,2);
    Primitive* child = nullptr; Primitive* parent = nullptr;
    for (Primitive* p : sc->primitives) { if (p->id==childId) child=p; if (p->id==parentId) parent=p; }
    if (!child || !parent || child == parent) return 0;
    if (child->parent) child->parent->RemoveChild(child); // removes from root or old parent
    parent->AddChild(child);
    return 0;
}
static int lua_SceneSetPaused(lua_State* L) {
    Scene* sc = (Scene*)lua_touserdata(L, lua_upvalueindex(1));
    sc->timePaused = lua_toboolean(L,1) != 0;
    return 0;
}
static int lua_SceneSetTime(lua_State* L) {
    Scene* sc = (Scene*)lua_touserdata(L, lua_upvalueindex(1));
    sc->currentTime = (float)luaL_checknumber(L,1);
    return 0;
}
static int lua_SceneSetTimeMax(lua_State* L) {
    Scene* sc = (Scene*)lua_touserdata(L, lua_upvalueindex(1));
    sc->timeMax = (float)luaL_checknumber(L,1);
    return 0;
}
static int lua_SceneUpdateLight(lua_State* L) {
    Scene* sc = (Scene*)lua_touserdata(L, lua_upvalueindex(1));
    sc->UpdateLight();
    return 0;
}
static int lua_SceneGetLoop(lua_State* L) {
    Scene* sc = (Scene*)lua_touserdata(L, lua_upvalueindex(1));
    lua_pushboolean(L, sc->timeLoop ? 1 : 0);
    return 1;
}

// ─── Persistent-homology numeric helpers (hybrid: heavy math stays in C++) ────
static std::vector<XMFLOAT3> ph_readCloud(lua_State* L, int idx) {
    std::vector<XMFLOAT3> pts;
    if (!lua_istable(L, idx)) return pts;
    int n = (int)lua_rawlen(L, idx);
    pts.reserve(n);
    for (int i = 1; i <= n; ++i) {
        lua_geti(L, idx, i);
        if (lua_istable(L, -1)) {
            lua_geti(L,-1,1); float x=(float)lua_tonumber(L,-1); lua_pop(L,1);
            lua_geti(L,-1,2); float y=(float)lua_tonumber(L,-1); lua_pop(L,1);
            lua_geti(L,-1,3); float z=(float)lua_tonumber(L,-1); lua_pop(L,1);
            pts.push_back({x,y,z});
        }
        lua_pop(L,1);
    }
    return pts;
}
// scene_ph_topology(cloud, radius) -> beta0, beta1, edges, triangles  (Vietoris-Rips)
static int lua_ScenePhTopology(lua_State* L) {
    std::vector<XMFLOAT3> pts = ph_readCloud(L, 1);
    float r = (float)luaL_optnumber(L, 2, 0.0);
    int N = (int)pts.size();
    int beta0=0, beta1=0, edges=0, tris=0;
    if (N > 0) {
        std::vector<int> parent(N);
        for (int i=0;i<N;i++) parent[i]=i;
        auto find = [&](int x)->int { while(parent[x]!=x){parent[x]=parent[parent[x]];x=parent[x];} return x; };
        const float r2 = 4.f*r*r;
        for (int i=0;i<N;i++) for (int j=i+1;j<N;j++) {
            float dx=pts[i].x-pts[j].x, dy=pts[i].y-pts[j].y, dz=pts[i].z-pts[j].z;
            if (dx*dx+dy*dy+dz*dz <= r2) { int pi=find(i),pj=find(j); if(pi!=pj)parent[pi]=pj; edges++; }
        }
        for (int i=0;i<N;i++) for (int j=i+1;j<N;j++) for (int k=j+1;k<N;k++) {
            auto d2=[&](int a,int b){float dx=pts[a].x-pts[b].x,dy=pts[a].y-pts[b].y,dz=pts[a].z-pts[b].z;return dx*dx+dy*dy+dz*dz;};
            if (d2(i,j)<=r2 && d2(i,k)<=r2 && d2(j,k)<=r2) tris++;
        }
        for (int i=0;i<N;i++) if (find(i)==i) beta0++;
        beta1 = edges - N + beta0; if (beta1<0) beta1=0;
    }
    lua_pushinteger(L,beta0); lua_pushinteger(L,beta1);
    lua_pushinteger(L,edges); lua_pushinteger(L,tris);
    return 4;
}
// scene_ph_coverage(cloud) -> coverage radius (half the longest MST edge)
static int lua_ScenePhCoverage(lua_State* L) {
    std::vector<XMFLOAT3> pts = ph_readCloud(L, 1);
    int N = (int)pts.size();
    float maxEdge = 0.f;
    if (N >= 2) {
        std::vector<float> key(N, FLT_MAX);
        std::vector<char>  inMST(N, 0);
        key[0] = 0.f;
        for (int it=0; it<N; ++it) {
            int u=-1; for (int i=0;i<N;i++) if(!inMST[i] && (u<0||key[i]<key[u])) u=i;
            inMST[u]=1; if (key[u]<FLT_MAX) maxEdge = std::max(maxEdge, key[u]);
            for (int v=0;v<N;v++){ if(inMST[v])continue; float dx=pts[u].x-pts[v].x,dy=pts[u].y-pts[v].y,dz=pts[u].z-pts[v].z; float d=sqrtf(dx*dx+dy*dy+dz*dz); if(d<key[v])key[v]=d; }
        }
    }
    lua_pushnumber(L, maxEdge/2.f);
    return 1;
}
static int lua_SceneCallAction(lua_State* L) {
    Scene* sc = (Scene*)lua_touserdata(L, lua_upvalueindex(1));
    const char* fn = luaL_checkstring(L,1);
    if (sc->controller) {
        auto it = sc->controller->compiledFns.find(fn);
        if (it != sc->controller->compiledFns.end()) it->second(*sc);
    }
    return 0;
}

// ─── ImGui Lua API ──────────────────────────────────────────────────────────
static int lua_ImguiText(lua_State* L) {
    const char* s = luaL_checkstring(L,1); ImGui::TextUnformatted(s); return 0;
}
static int lua_ImguiTextColored(lua_State* L) {
    float r=(float)luaL_checknumber(L,1),g=(float)luaL_checknumber(L,2),
          b=(float)luaL_checknumber(L,3),a=(float)luaL_checknumber(L,4);
    const char* s=luaL_checkstring(L,5);
    ImGui::TextColored({r,g,b,a},"%s",s); return 0;
}
static int lua_ImguiSeparator(lua_State* L) { ImGui::Separator(); return 0; }
static int lua_ImguiSameLine(lua_State* L)  { ImGui::SameLine();  return 0; }
static int lua_ImguiDragFloat(lua_State* L) {
    Scene* sc = (Scene*)lua_touserdata(L, lua_upvalueindex(1));
    const char* key   = luaL_checkstring(L,1);
    const char* label = luaL_checkstring(L,2);
    float step = (float)luaL_optnumber(L,3,0.1f);
    float mn   = (float)luaL_optnumber(L,4,0.f);
    float mx   = (float)luaL_optnumber(L,5,1.f);
    const char* fmt = luaL_optstring(L,6,"%.3f");

    auto it = sc->sceneFloats.find(key);
    if (it == sc->sceneFloats.end()) {
        sc->sceneFloats[key] = std::make_shared<float>(0.f);
        it = sc->sceneFloats.find(key);
    }
    float val = *it->second;
    ImGui::SetNextItemWidth(80);
    bool changed = ImGui::DragFloat(label, &val, step, mn, mx, fmt);
    if (changed) {
        *it->second = val;
        // keep the Lua global in sync so the surrounding ReadSceneFloatsBack won't revert it
        lua_pushnumber(L, val); lua_setglobal(L, key);
    }
    lua_pushboolean(L, changed);
    lua_pushnumber(L, val);
    return 2;
}
static int lua_ImguiDragInt(lua_State* L) {
    Scene* sc = (Scene*)lua_touserdata(L, lua_upvalueindex(1));
    const char* key   = luaL_checkstring(L,1);
    const char* label = luaL_checkstring(L,2);
    float step = (float)luaL_optnumber(L,3,1.f);
    int mn     = (int)luaL_optinteger(L,4,0);
    int mx     = (int)luaL_optinteger(L,5,1000);

    int cur = 0;
    if (sc->sceneData.contains(key)) cur = sc->sceneData[key].get<int>();
    ImGui::SetNextItemWidth(80);
    bool changed = ImGui::DragInt(label, &cur, step, mn, mx);
    if (changed) {
        sc->sceneData[key] = cur;
        auto it = sc->sceneFloats.find(key);
        if (it != sc->sceneFloats.end()) *it->second = (float)cur;
        lua_pushnumber(L, (lua_Number)cur); lua_setglobal(L, key);
    }

    lua_pushboolean(L, changed);
    lua_pushinteger(L, cur);
    return 2;
}
static int lua_ImguiButton(lua_State* L) {
    Scene* sc = (Scene*)lua_touserdata(L, lua_upvalueindex(1));
    const char* label  = luaL_checkstring(L,1);
    const char* action = luaL_optstring(L,2,"");
    bool clicked = ImGui::Button(label, {-1,0});
    if (clicked && sc->controller && action[0] != '\0') {
        auto it = sc->controller->compiledFns.find(action);
        if (it != sc->controller->compiledFns.end()) it->second(*sc);
    }
    lua_pushboolean(L, clicked);
    return 1;
}
static int lua_ImguiPlotLines(lua_State* L) {
    lua_State* LS = L;
    const char* label   = luaL_checkstring(L,1);
    const char* tblName = luaL_checkstring(L,2);
    float height = (float)luaL_optnumber(L,3,60.f);

    lua_getglobal(LS, tblName);
    if (!lua_istable(LS,-1)) { lua_pop(LS,1); return 0; }
    int n = (int)lua_rawlen(LS,-1);
    std::vector<float> buf(n);
    for (int i=0;i<n;i++) {
        lua_geti(LS,-1,i+1);
        buf[i]=(float)lua_tonumber(LS,-1);
        lua_pop(LS,1);
    }
    lua_pop(LS,1);
    if (!buf.empty()) {
        float mn=*std::min_element(buf.begin(),buf.end());
        float mx=*std::max_element(buf.begin(),buf.end());
        if (mx-mn<1e-6f){mn-=1.f;mx+=1.f;}
        char ov[32]; snprintf(ov,sizeof(ov),"%.4g",buf.back());
        ImGui::PlotLines(label,buf.data(),n,0,ov,mn*0.95f,mx*1.05f,{-1.f,height});
    }
    return 0;
}
static int lua_ImguiCombo(lua_State* L) {
    Scene* sc = (Scene*)lua_touserdata(L, lua_upvalueindex(1));
    const char* key   = luaL_checkstring(L,1);
    const char* label = luaL_checkstring(L,2);
    if (!lua_istable(L,3)) return 0;

    int cur = 0;
    if (sc->sceneData.contains(key)) cur = sc->sceneData[key].get<int>();
    else if (sc->sceneFloats.count(key)) cur = (int)*sc->sceneFloats[key];
    int n = (int)lua_rawlen(L,3);
    std::vector<std::string> opts(n);
    std::vector<const char*> ptrs(n);
    for (int i=0;i<n;i++){
        lua_geti(L,3,i+1);
        opts[i]=lua_tostring(L,-1);
        ptrs[i]=opts[i].c_str();
        lua_pop(L,1);
    }
    ImGui::SetNextItemWidth(130);
    bool changed = ImGui::Combo(label,&cur,ptrs.data(),n);
    if (changed) {
        sc->sceneData[key]=cur;
        auto it = sc->sceneFloats.find(key);
        if (it != sc->sceneFloats.end()) *it->second = (float)cur;
        lua_pushnumber(L, (lua_Number)cur); lua_setglobal(L, key);
    }
    lua_pushboolean(L,changed);
    lua_pushinteger(L,cur);
    return 2;
}
static int lua_ImguiProgress(lua_State* L) {
    float frac = (float)luaL_checknumber(L,1);
    const char* overlay = luaL_optstring(L,2,nullptr);
    ImGui::ProgressBar(frac, ImVec2(-1.f, 0.f), overlay);
    return 0;
}
#endif

inline void LuaUpdaterEditor::RegisterSceneAPI(Scene& sc)
{
#ifdef ENABLE_LUA
    lua_pushlightuserdata(L, &sc);
    lua_pushcclosure(L, lua_SceneAddPoint,        1); lua_setglobal(L,"scene_add_point");
    lua_pushlightuserdata(L, &sc);
    lua_pushcclosure(L, lua_SceneAddSphere,       1); lua_setglobal(L,"scene_add_sphere");
    lua_pushlightuserdata(L, &sc);
    lua_pushcclosure(L, lua_SceneAddCubeWireframe,1); lua_setglobal(L,"scene_add_cube_wireframe");
    lua_pushlightuserdata(L, &sc);
    lua_pushcclosure(L, lua_SceneRemove,          1); lua_setglobal(L,"scene_remove");
    lua_pushlightuserdata(L, &sc);
    lua_pushcclosure(L, lua_SceneRemoveByName,    1); lua_setglobal(L,"scene_remove_by_name");
    lua_pushlightuserdata(L, &sc);
    lua_pushcclosure(L, lua_SceneRemoveByPrefix,  1); lua_setglobal(L,"scene_remove_by_prefix");
    lua_pushlightuserdata(L, &sc);
    lua_pushcclosure(L, lua_SceneSetName,         1); lua_setglobal(L,"scene_set_name");
    lua_pushlightuserdata(L, &sc);
    lua_pushcclosure(L, lua_SceneSetParent,       1); lua_setglobal(L,"scene_set_parent");
    lua_pushlightuserdata(L, &sc);
    lua_pushcclosure(L, lua_SceneSetPaused,       1); lua_setglobal(L,"scene_set_paused");
    lua_pushlightuserdata(L, &sc);
    lua_pushcclosure(L, lua_SceneSetTime,         1); lua_setglobal(L,"scene_set_time");
    lua_pushlightuserdata(L, &sc);
    lua_pushcclosure(L, lua_SceneSetTimeMax,      1); lua_setglobal(L,"scene_set_time_max");
    lua_pushlightuserdata(L, &sc);
    lua_pushcclosure(L, lua_SceneUpdateLight,     1); lua_setglobal(L,"scene_update_light");
    lua_pushlightuserdata(L, &sc);
    lua_pushcclosure(L, lua_SceneGetLoop,         1); lua_setglobal(L,"scene_get_loop");
    lua_pushlightuserdata(L, &sc);
    lua_pushcclosure(L, lua_ScenePhTopology,      1); lua_setglobal(L,"scene_ph_topology");
    lua_pushlightuserdata(L, &sc);
    lua_pushcclosure(L, lua_ScenePhCoverage,      1); lua_setglobal(L,"scene_ph_coverage");
    lua_pushlightuserdata(L, &sc);
    lua_pushcclosure(L, lua_SceneCallAction,      1); lua_setglobal(L,"scene_call_action");
#endif
}

inline void LuaUpdaterEditor::RegisterImGuiAPI(Scene& sc)
{
#ifdef ENABLE_LUA
    lua_pushcfunction(L, lua_ImguiText);        lua_setglobal(L,"imgui_text");
    lua_pushcfunction(L, lua_ImguiTextColored); lua_setglobal(L,"imgui_text_colored");
    lua_pushcfunction(L, lua_ImguiSeparator);   lua_setglobal(L,"imgui_separator");
    lua_pushcfunction(L, lua_ImguiSameLine);    lua_setglobal(L,"imgui_same_line");
    lua_pushlightuserdata(L,&sc);
    lua_pushcclosure(L, lua_ImguiDragFloat, 1); lua_setglobal(L,"imgui_drag_float");
    lua_pushlightuserdata(L,&sc);
    lua_pushcclosure(L, lua_ImguiDragInt,   1); lua_setglobal(L,"imgui_drag_int");
    lua_pushlightuserdata(L,&sc);
    lua_pushcclosure(L, lua_ImguiButton,    1); lua_setglobal(L,"imgui_button");
    lua_pushcfunction(L, lua_ImguiPlotLines);   lua_setglobal(L,"imgui_plot_lines");
    lua_pushcfunction(L, lua_ImguiProgress);    lua_setglobal(L,"imgui_progress");
    lua_pushlightuserdata(L,&sc);
    lua_pushcclosure(L, lua_ImguiCombo,     1); lua_setglobal(L,"imgui_combo");
#endif
}

// ─── BindToScene ────────────────────────────────────────────────────────────

inline void LuaUpdaterEditor::BindToScene(Scene& sc)
{
    boundScene = &sc;
#ifdef ENABLE_LUA
    RegisterSceneAPI(sc);
    RegisterImGuiAPI(sc);
#endif
    sc.luaReApplyCallback = [this](const std::vector<Primitive*>& prims){
        ReApplyAll(prims);
    };
    sc.luaCompileControllerCallback = [this](SceneController& ctrl){
        if (boundScene) CompileController(ctrl, *boundScene);
    };
    sc.luaSaveStateCallback  = [this](){ return SerializeLuaState(); };
    sc.luaRestoreStateCallback = [this](const nlohmann::json& j){ RestoreLuaState(j); };

    if (sc.controller) {
        CompileController(*sc.controller, sc);
        if (sc.controller->compiledTick)  sc.sceneTick  = sc.controller->compiledTick;
        if (sc.controller->compiledReset) sc.sceneReset = sc.controller->compiledReset;
    }
    ReApplyAll(sc.primitives);
}

// ─── CompileController ──────────────────────────────────────────────────────

inline void LuaUpdaterEditor::CompileController(SceneController& ctrl, Scene& sc)
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

// ─── DrawControllerWindows ──────────────────────────────────────────────────

inline void LuaUpdaterEditor::DrawControllerWindows(Scene& sc, bool* blockMousePick)
{
#ifdef ENABLE_LUA
    if (!sc.controller) return;
    // Rendered inline as collapsing sections inside the left-panel scene-windows area,
    // for consistency with C++ SceneWindow content (no separate floating windows).
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
    (void)blockMousePick; // hover handled by the enclosing scene-area window
#endif
}

// ─── DrawControllerScripts (Console tabs for the selected controller) ─────────

inline void LuaUpdaterEditor::DrawControllerScripts(Scene& scene, bool* outBlockWheel)
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

// ─── Lua state serialization ────────────────────────────────────────────────

inline nlohmann::json LuaUpdaterEditor::LuaValueToJson(lua_State* L, int idx, int depth)
{
#ifdef ENABLE_LUA
    if (depth > 64) return nlohmann::json(); // guard against cyclic/self-referential tables
    int tp = lua_type(L, idx);
    if (tp == LUA_TNUMBER)  return lua_tonumber(L, idx);
    if (tp == LUA_TBOOLEAN) return (bool)lua_toboolean(L, idx);
    if (tp == LUA_TSTRING)  return std::string(lua_tostring(L, idx));
    if (tp == LUA_TTABLE) {
        bool isSeq = true; int n = 0;
        lua_pushnil(L);
        while (lua_next(L, idx < 0 ? idx-1 : idx)) {
            if (lua_type(L,-2)==LUA_TNUMBER && (int)lua_tonumber(L,-2)==n+1) n++;
            else { isSeq=false; lua_pop(L,2); break; }
            lua_pop(L,1);
        }
        if (isSeq) {
            nlohmann::json arr = nlohmann::json::array();
            for (int i=1;i<=n;i++) {
                lua_geti(L, idx<0?idx-0:idx, i);
                arr.push_back(LuaValueToJson(L,-1,depth+1));
                lua_pop(L,1);
            }
            return arr;
        } else {
            nlohmann::json obj = nlohmann::json::object();
            lua_pushnil(L);
            while (lua_next(L, idx<0?idx-1:idx)) {
                if (lua_type(L,-2)==LUA_TSTRING) {
                    std::string k=lua_tostring(L,-2);
                    obj[k]=LuaValueToJson(L,-1,depth+1);
                }
                lua_pop(L,1);
            }
            return obj;
        }
    }
#endif
    return nlohmann::json();
}

inline void LuaUpdaterEditor::JsonToLuaValue(lua_State* L, const nlohmann::json& j)
{
#ifdef ENABLE_LUA
    if (j.is_number())      { lua_pushnumber(L, j.get<double>()); return; }
    if (j.is_boolean())     { lua_pushboolean(L, j.get<bool>() ? 1 : 0); return; }
    if (j.is_string())      { lua_pushstring(L, j.get<std::string>().c_str()); return; }
    if (j.is_array()) {
        lua_newtable(L);
        for (int i=0;i<(int)j.size();i++) {
            JsonToLuaValue(L, j[i]);
            lua_seti(L,-2,i+1);
        }
        return;
    }
    if (j.is_object()) {
        lua_newtable(L);
        for (auto& [k,v] : j.items()) {
            lua_pushstring(L,k.c_str());
            JsonToLuaValue(L,v);
            lua_settable(L,-3);
        }
        return;
    }
    lua_pushnil(L);
#endif
}

inline nlohmann::json LuaUpdaterEditor::SerializeLuaState()
{
    nlohmann::json result = nlohmann::json::object();
#ifdef ENABLE_LUA
    if (!L) return result;
    lua_getglobal(L, "_G");
    if (!lua_istable(L,-1)) { lua_pop(L,1); return result; }
    lua_pushnil(L);
    while (lua_next(L,-2)) {
        if (lua_type(L,-2)==LUA_TSTRING) {
            const char* key = lua_tostring(L,-2);
            if (key && key[0]=='_' && key[1]!='\0') {
                std::string ks(key);
                // _G is self-referential (infinite recursion); _VERSION is engine noise
                if (ks != "_G" && ks != "_VERSION") {
                    auto val = LuaValueToJson(L,-1);
                    if (!val.is_null()) result[key] = val;
                }
            }
        }
        lua_pop(L,1);
    }
    lua_pop(L,1);
#endif
    return result;
}

inline void LuaUpdaterEditor::RestoreLuaState(const nlohmann::json& j)
{
#ifdef ENABLE_LUA
    if (!L || !j.is_object()) return;
    for (auto& [key, val] : j.items()) {
        JsonToLuaValue(L, val);
        lua_setglobal(L, key.c_str());
    }
#endif
}

#endif
