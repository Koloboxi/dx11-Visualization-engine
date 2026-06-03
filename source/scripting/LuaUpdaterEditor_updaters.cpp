#include "LuaUpdaterEditor.h"

void LuaUpdaterEditor::PushPrimTable(Primitive& p) {
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

void LuaUpdaterEditor::ReadPrimTable(int idx, Primitive& p) {
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

bool LuaUpdaterEditor::CompileAndApply(
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


void LuaUpdaterEditor::PushSceneTable(const std::vector<Primitive*>& prims)
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

void LuaUpdaterEditor::ReadSceneTableBack(const std::vector<Primitive*>& prims)
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

void LuaUpdaterEditor::PushSceneFloats(Scene& sc)
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

void LuaUpdaterEditor::ReadSceneFloatsBack(Scene& sc)
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
