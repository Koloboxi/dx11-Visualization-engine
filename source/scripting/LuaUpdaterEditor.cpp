#include "LuaUpdaterEditor.h"

void LuaUpdaterEditor::Init() {
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

void LuaUpdaterEditor::Shutdown() {
#ifdef ENABLE_LUA
    if (L) { lua_close(L); L = nullptr; }
#endif
}

bool LuaUpdaterEditor::CompileChunk(const std::string& code, int& outRef, std::string& outErr)
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
