#include "../scene.h"


static const char* NEWTON_GENERATE = R"lua(
math.randomseed(os.time())
scene_remove_by_prefix("nb_")
scene_remove_by_prefix("ast_")

local G    = newton_G or 1000
local Msun = newton_Msun or 100000

_nb = {}
_nb[1] = {px=0,py=0,pz=0, vx=0,vy=0,vz=0, m=Msun, name="nb_Sun"}
scene_set_name(scene_add_sphere(28, 0,0,0, 2, 1.0,0.8,0.2,1.0), "nb_Sun")

-- name, orbit radius, mass, visual radius, inclination, r, g, b
local planets = {
  {"Mercury",  80, 0.5,  4, 0.06, 0.70,0.70,0.70},
  {"Venus",   120, 2.0,  7, 0.03, 0.90,0.72,0.42},
  {"Earth",   165, 2.5,  7, 0.00, 0.30,0.55,1.00},
  {"Mars",    215, 1.2,  5, 0.03, 0.90,0.40,0.30},
  {"Jupiter", 430, 40,  18, 0.02, 0.90,0.75,0.50},
  {"Saturn",  580, 25,  15, 0.04, 0.92,0.85,0.62},
  {"Uranus",  720, 10,  11, 0.05, 0.60,0.90,0.92},
  {"Neptune", 860, 11,  11, 0.03, 0.30,0.45,0.95},
}
for i = 1, #planets do
  local p = planets[i]
  local nm, orb, m, vis, inc = p[1], p[2], p[3], p[4], p[5]
  local cr, cg, cb = p[6], p[7], p[8]
  local ang = math.random() * 2 * math.pi
  local v   = math.sqrt(G * Msun / orb)
  local px  = orb * math.cos(ang)
  local py  = orb * math.sin(ang)
  local pz  = (math.random()*2 - 1) * orb * inc
  _nb[#_nb+1] = {px=px,py=py,pz=pz, vx=-v*math.sin(ang), vy=v*math.cos(ang), vz=0, m=m, name="nb_"..nm}
  scene_set_name(scene_add_sphere(vis, px,py,pz, 2, cr,cg,cb,1.0), "nb_"..nm)
end

-- big named belt asteroids
local bigast = {
  {"Ceres",  300, 0.05, 3, 0.80,0.80,0.75},
  {"Vesta",  330, 0.03, 3, 0.72,0.70,0.60},
  {"Pallas", 280, 0.03, 3, 0.66,0.62,0.55},
}
for i = 1, #bigast do
  local a = bigast[i]
  local nm, orb, m, vis = a[1], a[2], a[3], a[4]
  local cr, cg, cb = a[5], a[6], a[7]
  local ang = math.random() * 2 * math.pi
  local v   = math.sqrt(G * Msun / orb)
  local px  = orb * math.cos(ang)
  local py  = orb * math.sin(ang)
  local pz  = (math.random()*2 - 1) * 15
  _nb[#_nb+1] = {px=px,py=py,pz=pz, vx=-v*math.sin(ang), vy=v*math.cos(ang), vz=0, m=m, name="nb_"..nm}
  scene_set_name(scene_add_sphere(vis, px,py,pz, 1, cr,cg,cb,1.0), "nb_"..nm)
end

-- small belt asteroids (test particles, feel only the Sun)
_belt = {}
local nbelt = math.floor((newton_belt or 80) + 0.5)
for i = 1, nbelt do
  local orb = 250 + math.random() * 110
  local ang = math.random() * 2 * math.pi
  local v   = math.sqrt(G * Msun / orb) * (0.97 + 0.06*math.random())
  local px  = orb * math.cos(ang)
  local py  = orb * math.sin(ang)
  _belt[i] = {px=px,py=py,pz=(math.random()*2-1)*10, vx=-v*math.sin(ang), vy=v*math.cos(ang), vz=0}
  scene_set_name(scene_add_point(px,py,(math.random()*2-1)*10, 0.60,0.58,0.50,1.0), "ast_"..i)
end
-- Kuiper-belt points beyond Neptune
local nkui = math.floor((newton_belt or 80) * 0.5 + 0.5)
for i = 1, nkui do
  local k   = nbelt + i
  local orb = 950 + math.random() * 250
  local ang = math.random() * 2 * math.pi
  local v   = math.sqrt(G * Msun / orb) * (0.97 + 0.06*math.random())
  local px  = orb * math.cos(ang)
  local py  = orb * math.sin(ang)
  _belt[k] = {px=px,py=py,pz=(math.random()*2-1)*30, vx=-v*math.sin(ang), vy=v*math.cos(ang), vz=0}
  scene_set_name(scene_add_point(px,py,(math.random()*2-1)*30, 0.50,0.60,0.70,1.0), "ast_"..k)
end

scene_update_light()

-- snapshot initial state for Reset
_nb0 = {}
for i = 1, #_nb do local b=_nb[i]; _nb0[i]={px=b.px,py=b.py,pz=b.pz,vx=b.vx,vy=b.vy,vz=b.vz,m=b.m,name=b.name} end
_belt0 = {}
for i = 1, #_belt do local b=_belt[i]; _belt0[i]={px=b.px,py=b.py,pz=b.pz,vx=b.vx,vy=b.vy,vz=b.vz} end
)lua";

static const char* NEWTON_RESET = R"lua(
if _nb0 then
  _nb = {}
  for i=1,#_nb0 do local b=_nb0[i]; _nb[i]={px=b.px,py=b.py,pz=b.pz,vx=b.vx,vy=b.vy,vz=b.vz,m=b.m,name=b.name} end
end
if _belt0 then
  _belt = {}
  for i=1,#_belt0 do local b=_belt0[i]; _belt[i]={px=b.px,py=b.py,pz=b.pz,vx=b.vx,vy=b.vy,vz=b.vz} end
end
)lua";

static const char* NEWTON_TICK = R"lua(
if not _nb then return end
local G = newton_G or 1000
if _nb[1] then _nb[1].m = newton_Msun or 100000 end
local soft = 25
local M = #_nb

if dt > 0 then
  local fx,fy,fz = {},{},{}
  for i=1,M do fx[i]=0; fy[i]=0; fz[i]=0 end
  for i=1,M do
    for j=i+1,M do
      local dx=_nb[j].px-_nb[i].px; local dy=_nb[j].py-_nb[i].py; local dz=_nb[j].pz-_nb[i].pz
      local r2=dx*dx+dy*dy+dz*dz+soft*soft
      local r=math.sqrt(r2)
      local f=G*_nb[i].m*_nb[j].m/r2
      local ex,ey,ez=f*dx/r,f*dy/r,f*dz/r
      fx[i]=fx[i]+ex; fy[i]=fy[i]+ey; fz[i]=fz[i]+ez
      fx[j]=fx[j]-ex; fy[j]=fy[j]-ey; fz[j]=fz[j]-ez
    end
  end
  for i=1,M do
    local m=_nb[i].m
    _nb[i].vx=_nb[i].vx+fx[i]/m*dt; _nb[i].vy=_nb[i].vy+fy[i]/m*dt; _nb[i].vz=_nb[i].vz+fz[i]/m*dt
    _nb[i].px=_nb[i].px+_nb[i].vx*dt; _nb[i].py=_nb[i].py+_nb[i].vy*dt; _nb[i].pz=_nb[i].pz+_nb[i].vz*dt
  end
  if _belt and _nb[1] then
    local sx,sy,sz = _nb[1].px,_nb[1].py,_nb[1].pz
    local GM = G*_nb[1].m
    for i=1,#_belt do
      local b=_belt[i]
      local dx=sx-b.px; local dy=sy-b.py; local dz=sz-b.pz
      local r2=dx*dx+dy*dy+dz*dz+soft*soft
      local r=math.sqrt(r2)
      local a=GM/r2
      b.vx=b.vx+a*dx/r*dt; b.vy=b.vy+a*dy/r*dt; b.vz=b.vz+a*dz/r*dt
      b.px=b.px+b.vx*dt; b.py=b.py+b.vy*dt; b.pz=b.pz+b.vz*dt
    end
  end
end

for i=1,#scene do
  local nm = scene[i].name
  if nm then
    if string.sub(nm,1,3) == "nb_" then
      for j=1,M do
        if _nb[j].name == nm then scene[i].x=_nb[j].px; scene[i].y=_nb[j].py; scene[i].z=_nb[j].pz; break end
      end
    elseif string.sub(nm,1,4) == "ast_" then
      local k = tonumber(string.sub(nm,5))
      if k and _belt and _belt[k] then scene[i].x=_belt[k].px; scene[i].y=_belt[k].py; scene[i].z=_belt[k].pz end
    end
  end
end
)lua";

static const char* NEWTON_DRAW_INFO = R"lua(
local G = newton_G or 1000
if not _nb then imgui_text("(initializing...)"); return end
local M = #_nb
local Mtot = 0
for i=1,M do Mtot = Mtot + _nb[i].m end
imgui_text(string.format("G = %.0f   bodies = %d   M_tot = %.0f", G, M, Mtot))
imgui_text(string.format("Belt / Kuiper particles: %d", _belt and #_belt or 0))
imgui_separator()
local Etot = 0
for i=1,M do
  local b=_nb[i]
  Etot = Etot + 0.5*b.m*(b.vx*b.vx+b.vy*b.vy+b.vz*b.vz)
  for j=i+1,M do
    local bj=_nb[j]
    local dx=b.px-bj.px; local dy=b.py-bj.py; local dz=b.pz-bj.pz
    local r=math.sqrt(dx*dx+dy*dy+dz*dz)
    if r>0.001 then Etot = Etot - G*b.m*bj.m/r end
  end
end
imgui_text(string.format("Total energy: %.4g", Etot))
imgui_separator()
local sx,sy,sz = _nb[1].px,_nb[1].py,_nb[1].pz
imgui_text(string.format("%-9s %8s %7s", "Body", "r", "|v|"))
imgui_separator()
for i=2,M do
  local b=_nb[i]
  local dx=b.px-sx; local dy=b.py-sy; local dz=b.pz-sz
  local r = math.sqrt(dx*dx+dy*dy+dz*dz)
  local v = math.sqrt(b.vx*b.vx+b.vy*b.vy+b.vz*b.vz)
  imgui_text(string.format("%-9s %8.1f %7.2f", string.sub(b.name,4), r, v))
end
)lua";

void Scene::LoadNewtonDemo()
{
    for (Primitive* p : this->primitives) delete p;
    this->primitives.clear();
    root.children.clear();
    m_sortedDirty = true;
    this->orientationTransformer.SetTargetObjects({});
    this->ClearTrajectories();
    this->ClearSceneCustomState();
    this->sceneName = "Newton";
    root.name = sceneName;

    this->sceneFloats["newton_G"]    = std::make_shared<float>(1000.f);
    this->sceneFloats["newton_Msun"] = std::make_shared<float>(100000.f);
    this->sceneFloats["newton_belt"] = std::make_shared<float>(80.f);

    auto* ctrl = new SceneController();
    ctrl->SetScript("tick",             NEWTON_TICK);
    ctrl->SetScript("reset",            NEWTON_RESET);
    ctrl->SetScript("generate",         NEWTON_GENERATE);
    ctrl->SetScript("draw_newton_info", NEWTON_DRAW_INFO);

    ctrl->sliderDefs = {
        {"G",        "newton_G",    1.f,    5000.f},
        {"Sun mass", "newton_Msun", 1000.f, 500000.f},
    };

    ctrl->windowDefs = {
        {"newton_info", "Solar System", {50.f, 380.f}, {340.f, 260.f}, {}},
    };

    SetController(ctrl);

    if (controller && controller->compiledFns.count("generate"))
        controller->compiledFns["generate"](*this);

    this->UpdateLight();
    this->ResetTime();
}
