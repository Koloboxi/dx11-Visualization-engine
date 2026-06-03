#include "../scene.h"
#include <cmath>
#include <array>
#include <algorithm>

static const char* GR_BODY_LUA_0 = R"lua(
if _gr_t and t < _gr_t - 0.001 then _gr = nil end
_gr_t = t
local GM = gr_GM or 10000
local c2 = gr_c2 or 2000
local Rs = 2*GM/c2
local soft2 = (Rs*0.5)*(Rs*0.5)
if not _gr then
  _gr = {
    {px=200, py=0,  pz=0,  vx=0, vy=7.0,  vz=0},
    {px=120, py=0,  pz=0,  vx=0, vy=10.5, vz=0},
    {px=350, py=0,  pz=50, vx=0, vy=5.0,  vz=2.0}
  }
end
for i=1,3 do
  local b=_gr[i]
  local rx,ry,rz=b.px,b.py,b.pz
  local vx,vy,vz=b.vx,b.vy,b.vz
  local r2=rx*rx+ry*ry+rz*rz+soft2
  local r=math.sqrt(r2)
  local r3=r2*r
  local v2=vx*vx+vy*vy+vz*vz
  local rdotv=rx*vx+ry*vy+rz*vz
  local newt=GM/r3
  local pnC=GM/(c2*r3)
  local pnA=4*GM/r-v2
  local ax=-newt*rx+pnC*(pnA*rx+4*rdotv*vx)
  local ay=-newt*ry+pnC*(pnA*ry+4*rdotv*vy)
  local az=-newt*rz+pnC*(pnA*rz+4*rdotv*vz)
  b.vx=b.vx+ax*dt; b.vy=b.vy+ay*dt; b.vz=b.vz+az*dt
  b.px=b.px+b.vx*dt; b.py=b.py+b.vy*dt; b.pz=b.pz+b.vz*dt
end
p.x=_gr[1].px; p.y=_gr[1].py; p.z=_gr[1].pz
)lua";

static const char* GR_TICK = R"lua(
local GM = gr_GM or 10000
local c2 = gr_c2 or 2000
local Rs = 2*GM/c2
for i=1,#scene do
  local n = scene[i].name
  if n == "gr_bh"     then scene[i].scale = math.max(Rs,     0.001) end
  if n == "gr_photon" then scene[i].scale = math.max(1.5*Rs, 0.001) end
  if n == "gr_isco"   then scene[i].scale = math.max(3.0*Rs, 0.001) end
end
)lua";

static const char* GR_DRAW_ORBITS = R"lua(
local GM = gr_GM or 10000
local c2 = gr_c2 or 2000
local Rs = 2*GM/c2
local ISCO = 3*Rs
local labels = {"Mercury","Venus","Comet"}
imgui_text(string.format("%-8s  %6s  %6s  %8s", "Body","r","|v|","dt/dt"))
imgui_separator()
if not _gr then imgui_text("(initializing...)"); return end
local anyInside = false
for i=1,3 do
  local b = _gr[i]
  local r  = math.sqrt(b.px*b.px + b.py*b.py + b.pz*b.pz)
  local v2 = b.vx*b.vx + b.vy*b.vy + b.vz*b.vz
  local v  = math.sqrt(v2)
  local darg = 1 - Rs/math.max(r,0.001) - v2/math.max(c2,1e-9)
  if darg < 0 then darg = 0 end
  local tau = math.sqrt(darg)
  local inside = r < ISCO
  if inside then anyInside = true end
  local star = inside and "*" or " "
  imgui_text(string.format("%-8s  %6.1f  %6.2f  %8.5f %s", labels[i], r, v, tau, star))
end
imgui_separator()
imgui_text(string.format("Coord. time: %.3f", t))
if anyInside then
  imgui_text_colored(1, 0.35, 0.35, 1, string.format("* r < ISCO (%.1f)", ISCO))
end
)lua";

static const char* GR_DRAW_INFO = R"lua(
local GM = gr_GM or 10000
local c2 = gr_c2 or 2000
local c  = math.sqrt(c2)
local Rs = 2*GM/c2
imgui_text(string.format("GM:                    %.1f", GM))
imgui_text(string.format("c (speed of light):    %.2f u/s", c))
imgui_separator()
imgui_text(string.format("Schwarzschild radius Rs: %.2f", Rs))
imgui_text(string.format("Photon sphere:           %.2f", 1.5*Rs))
imgui_text(string.format("ISCO:                    %.2f", 3.0*Rs))
imgui_separator()
local labels = {"Mercury","Venus","Comet"}
imgui_text("1PN precession estimate:")
if _gr then
  for i=1,3 do
    local b = _gr[i]
    local r = math.sqrt(b.px*b.px + b.py*b.py + b.pz*b.pz)
    if r > 0.001 then
      local vcirc = math.sqrt(GM/r)
      local T = 2*math.pi*r/math.max(vcirc,0.001)
      local prec_deg = 6*math.pi*GM/(c2*r) * 180/math.pi
      imgui_text(string.format("  %-7s ~%.1f deg/orbit  T~%.1f", labels[i], prec_deg, T))
    end
  end
end
)lua";

void Scene::LoadGRScene()
{
    for (Primitive* p : this->primitives) delete p;
    this->primitives.clear();
    root.children.clear();
    this->orientationTransformer.SetTargetObjects({});
    this->ClearTrajectories();
    this->ClearSceneCustomState();
    this->sceneName = "GR Demo";
    root.name = sceneName;

    this->sceneFloats["gr_GM"] = std::make_shared<float>(10000.f);
    this->sceneFloats["gr_c2"] = std::make_shared<float>(2000.f);

    float GM = 10000.f, c2 = 2000.f;
    float Rs = 2.f * GM / c2;

    struct BodyDef { XMFLOAT3 pos; XMFLOAT3 vel; float radius; XMFLOAT4 col; const char* name; };
    static constexpr int N = 3;
    BodyDef bodyDefs[N] = {
        { {200.f,  0.f,  0.f}, {0.f,  7.0f,  0.f}, 8.f,  {0.5f,0.7f,1.0f,1.0f}, "gr_body_0" },
        { {120.f,  0.f,  0.f}, {0.f, 10.5f,  0.f}, 6.f,  {1.0f,0.7f,0.3f,1.0f}, "gr_body_1" },
        { {350.f,  0.f, 50.f}, {0.f,  5.0f,  2.0f},7.f,  {0.6f,1.0f,0.6f,1.0f}, "gr_body_2" },
    };

    this->AddSphere(1.0f, {0,0,0}, 2, {0.08f, 0.04f, 0.02f, 1.0f});
    this->primitives.back()->name = "gr_bh";
    this->primitives.back()->SetScale(Rs);

    this->AddArc3d(1.0f, 0.05f, 360.f, {0,0,0}, 32, {0.9f, 0.9f, 0.3f, 0.55f});
    this->primitives.back()->name = "gr_photon";
    this->primitives.back()->SetScale(1.5f * Rs);

    this->AddArc3d(1.0f, 0.05f, 360.f, {0,0,0}, 32, {1.0f, 0.3f, 0.3f, 0.45f});
    this->primitives.back()->name = "gr_isco";
    this->primitives.back()->SetScale(3.f * Rs);

    Primitive* grbody[N] = {};
    for (int i = 0; i < N; i++) {
        auto& d = bodyDefs[i];
        this->AddSphere(d.radius, d.pos, 2, d.col);
        this->primitives.back()->name = d.name;
        grbody[i] = this->primitives.back();
    }

    if (grbody[0]) grbody[0]->luaScript = GR_BODY_LUA_0;
    for (int i = 1; i < N; i++) {
        if (!grbody[i]) continue;
        int li = i + 1;
        grbody[i]->luaScript =
            "if _gr and _gr[" + std::to_string(li) + "] then "
            "p.x=_gr[" + std::to_string(li) + "].px; "
            "p.y=_gr[" + std::to_string(li) + "].py; "
            "p.z=_gr[" + std::to_string(li) + "].pz end";
    }

    auto* ctrl = new SceneController();
    ctrl->SetScript("tick",           GR_TICK);
    ctrl->SetScript("draw_gr_orbits", GR_DRAW_ORBITS);
    ctrl->SetScript("draw_gr_info",   GR_DRAW_INFO);

    ctrl->sliderDefs = {
        {"GM", "gr_GM", 1000.f, 50000.f},
        {"c²", "gr_c2",  100.f, 20000.f},
    };

    ctrl->windowDefs = {
        {"gr_orbits", "GR Orbits",     {50.f,  380.f}, {400.f, 150.f}, {}},
        {"gr_info",   "GR Parameters", {460.f, 380.f}, {320.f, 210.f}, {}},
    };

    SetController(ctrl);

    if (luaReApplyCallback) luaReApplyCallback(this->primitives);

    this->UpdateLight();
    this->ResetTime();
}
