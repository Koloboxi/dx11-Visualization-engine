#include "LuaUpdaterEditor.h"

nlohmann::json LuaUpdaterEditor::LuaValueToJson(lua_State* L, int idx, int depth)
{
#ifdef ENABLE_LUA
    if (depth > 64) return nlohmann::json();
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

void LuaUpdaterEditor::JsonToLuaValue(lua_State* L, const nlohmann::json& j)
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

nlohmann::json LuaUpdaterEditor::SerializeLuaState()
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

void LuaUpdaterEditor::RestoreLuaState(const nlohmann::json& j)
{
#ifdef ENABLE_LUA
    if (!L || !j.is_object()) return;
    for (auto& [key, val] : j.items()) {
        JsonToLuaValue(L, val);
        lua_setglobal(L, key.c_str());
    }
#endif
}
