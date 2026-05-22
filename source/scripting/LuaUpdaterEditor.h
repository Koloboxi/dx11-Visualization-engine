#pragma once
#include "..\graphics\scene\primitives\primitive.h"
#include "..\graphics\imgui\imgui.h"
#include "..\graphics\imgui\imgui_stdlib.h"
#include <string>
#include <vector>
#include <memory>

#define ENABLE_LUA
#ifdef ENABLE_LUA
extern "C" {
#include "..\external\lua\lua.h"
#include "..\external\lua\lualib.h"
#include "..\external\lua\lauxlib.h"
}
#endif

// ImGui window that shows when a primitive is selected.
// Lets you write a Lua script that runs every frame as the primitive's updater.
//
// Lua environment per frame:
//   p        – table with current primitive state:
//              x, y, z       – position
//              scale         – uniform scale
//              rx, ry, rz, rw – rotation quaternion
//              r, g, b, a    – RGBA color [0..1]
//   scene    – table[1..N] of {x, y, z, scale} for all scene primitives (readable)
//   t        – current time (seconds)
//   dt       – delta time (seconds)
//
// Example:
//   p.x = 100 * math.cos(t)
//   p.z = 100 * math.sin(t)
//   p.r = 0.5 + 0.5 * math.sin(t * 3)
class LuaUpdaterEditor {
public:
    LuaUpdaterEditor()  { Init(); }
    ~LuaUpdaterEditor() { Shutdown(); }

    LuaUpdaterEditor(const LuaUpdaterEditor&) = delete;
    LuaUpdaterEditor& operator=(const LuaUpdaterEditor&) = delete;

    // Call once per frame.  Returns true when the editor window has mouse focus.
    bool Draw(Primitive* selected, const std::vector<Primitive*>& allPrimitives);

    // Always-visible globals window. Returns true when hovered.
    bool DrawGlobals();

    // ── Global Lua variables ──────────────────────────────────────────────────
    struct LuaGlobal { std::string name; float value = 0.0f; };
    std::vector<LuaGlobal> globals;

    // Set to true by Draw() when "Pick vec" is clicked; cleared by caller.
    bool awaitingVectorPick = false;

    // Call before removing a primitive so its Lua updater is cleaned up.
    void OnPrimitiveRemoved(Primitive* p) {
        if (p) p->ClearUpdater();   // destroys lambda → RAII unrefs Lua funcRef
    }

    // Re-apply all non-empty luaScript fields (e.g. after LoadScene).
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

#ifdef ENABLE_LUA
    lua_State* L = nullptr;

    // RAII wrapper: unrefs Lua chunk from the registry on destruction.
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

// ─────────────────────────────────────────────────────────────────────────────
// Implementation
// ─────────────────────────────────────────────────────────────────────────────

inline bool LuaUpdaterEditor::DrawGlobals() {
    ImGui::SetNextWindowSize(ImVec2(320, 200), ImGuiCond_FirstUseEver);
    ImGui::Begin("Lua Globals");
    bool blockPick = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    float childH = -ImGui::GetFrameHeightWithSpacing() - 4;
    ImGui::BeginChild("##glist", ImVec2(0, childH));
    for (int i = 0; i < (int)globals.size(); i++) {
        ImGui::PushID(i);
        ImGui::SetNextItemWidth(130);
        ImGui::InputText("##n", &globals[i].name);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::DragFloat("##v", &globals[i].value, 0.1f, 0, 0, "%.4g");
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            globals.erase(globals.begin() + i);
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
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
    // Register euler(rx,ry,rz) → qx,qy,qz,qw  (XMQuaternionRotationRollPitchYaw equivalent)
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

inline bool LuaUpdaterEditor::Draw(Primitive* selected, const std::vector<Primitive*>& allPrimitives)
{
    if (!selected) return false;

    ImGui::SetNextWindowSize(ImVec2(420, 280), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(400, 400), ImGuiCond_FirstUseEver);

    bool open = true;
    ImGui::Begin("Lua Updater", &open, ImGuiWindowFlags_NoCollapse);
    bool blockPick = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

#ifndef ENABLE_LUA
    ImGui::TextColored(ImVec4(1.0f,0.8f,0.2f,1.0f),
        "Lua disabled. See LuaUpdaterEditor.h for setup.");
    ImGui::Separator();
    ImGui::Text("Script (stored, not executed):");
    ImGui::InputTextMultiline("##script", &selected->luaScript,
        ImVec2(-1, -ImGui::GetFrameHeightWithSpacing() - 4));
    ImGui::BeginDisabled();
    ImGui::Button("Apply");
    ImGui::EndDisabled();
#else
    // Status line
    if (selected->HasUpdater())
        ImGui::TextColored(ImVec4(0.3f,1.0f,0.4f,1.0f), "[active]");
    else
        ImGui::TextColored(ImVec4(0.55f,0.55f,0.55f,1.0f), "[no updater]");

    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
        selected->ClearUpdater();
        selected->luaScript.clear();
        errorMsg.clear();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Pick vec")) awaitingVectorPick = true;
    if (awaitingVectorPick) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f,0.8f,0.1f,1.0f), "click a primitive...");
    }
    ImGui::Separator();
    ImGui::TextDisabled("p.{x,y,z,scale,rx,ry,rz,rw,r,g,b,a,mass}  vx,vy,vz(ro)  scene[i]  t  dt");

    ImGui::InputTextMultiline("##script", &selected->luaScript,
        ImVec2(-1, -ImGui::GetFrameHeightWithSpacing() - 4),
        ImGuiInputTextFlags_AllowTabInput);

    if (ImGui::Button("Apply")) {
        errorMsg.clear();
        if (!CompileAndApply(selected, allPrimitives, selected->luaScript, errorMsg))
            errorMsg = "Error: " + errorMsg;
    }
    if (!errorMsg.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f,0.3f,0.3f,1.0f), "%s", errorMsg.c_str());
    }
#endif

    ImGui::End();
    return blockPick;
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
    setNum("mass", p.mass);
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
    p.mass = getNum("mass");
}

inline bool LuaUpdaterEditor::CompileAndApply(
    Primitive* prim, const std::vector<Primitive*>& allPrims,
    const std::string& script, std::string& outError)
{
    prim->ClearUpdater(); // destroy old lambda → RAII cleans old LuaChunk

    if (script.empty()) return true;

    if (luaL_loadstring(L, script.c_str()) != LUA_OK) {
        outError = lua_tostring(L, -1);
        lua_pop(L, 1);
        return false;
    }

    // Store compiled chunk; shared_ptr provides automatic Lua unref.
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    auto chunk = std::make_shared<LuaChunk>(L, ref);

    lua_State* LS = L;
    const std::vector<Primitive*>* primsPtr = &allPrims;

    prim->SetUpdater([this, LS, chunk, primsPtr](Primitive& self, float t, float dt) {
        lua_pushnumber(LS, t);  lua_setglobal(LS, "t");
        lua_pushnumber(LS, dt); lua_setglobal(LS, "dt");

        PushPrimTable(self);
        lua_setglobal(LS, "p");

        // build scene table
        lua_newtable(LS);
        for (int i = 0; i < (int)primsPtr->size(); ++i) {
            Primitive* o = (*primsPtr)[i];
            lua_newtable(LS);
            XMFLOAT3 op = o->GetPosition();
            lua_pushnumber(LS, op.x);       lua_setfield(LS, -2, "x");
            lua_pushnumber(LS, op.y);       lua_setfield(LS, -2, "y");
            lua_pushnumber(LS, op.z);       lua_setfield(LS, -2, "z");
            lua_pushnumber(LS, o->GetScale()); lua_setfield(LS, -2, "scale");
            lua_pushnumber(LS, o->mass);    lua_setfield(LS, -2, "mass");
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

        lua_rawgeti(LS, LUA_REGISTRYINDEX, chunk->ref);
        if (lua_pcall(LS, 0, 0, 0) != LUA_OK) {
            lua_pop(LS, 1); return;
        }

        lua_getglobal(LS, "p");
        if (lua_istable(LS, -1)) ReadPrimTable(-1, self);
        lua_pop(LS, 1);

        // ── Writable scene table: apply any changes to other primitives ────
        // Find self index so we skip it (self is handled by ReadPrimTable above)
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
                    other->mass     = gN("mass");
                    other->velocity = {gN("vx"), gN("vy"), gN("vz")};
                }
                lua_pop(LS, 1);
            }
        }
        lua_pop(LS, 1);
    });

    return true;
}

#endif // ENABLE_LUA
