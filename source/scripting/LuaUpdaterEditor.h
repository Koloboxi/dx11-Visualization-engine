#pragma once
#include "..\graphics\scene\scene.h"
#include "..\graphics\imgui\imgui.h"
#include "..\graphics\imgui\imgui_stdlib.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <sstream>

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

private:
    void Init();
    void Shutdown();

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

    if (selected.empty()) {
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
            bool ro = slider.readOnly && slider.readOnly();
            ImGui::TextUnformatted(slider.label.c_str());
            ImGui::SameLine();
            ImGui::SetNextItemWidth(130);
            if (ro) ImGui::BeginDisabled();
            float prev = *slider.valuePtr;
            ImGui::PushID(slider.label.c_str());
            ImGui::SliderFloat("##s", slider.valuePtr, slider.min, slider.max, "%.2f");
            ImGui::PopID();
            if (ro) ImGui::EndDisabled();
            if (!ro && slider.onChange && *slider.valuePtr != prev)
                slider.onChange();
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

#endif
