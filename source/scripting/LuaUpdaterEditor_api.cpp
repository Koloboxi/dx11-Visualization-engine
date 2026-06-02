#include "LuaUpdaterEditor.h"
#include <cstdio>

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
    if (child->parent) child->parent->RemoveChild(child);
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

void LuaUpdaterEditor::RegisterSceneAPI(Scene& sc)
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

void LuaUpdaterEditor::RegisterImGuiAPI(Scene& sc)
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


void LuaUpdaterEditor::BindToScene(Scene& sc)
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
