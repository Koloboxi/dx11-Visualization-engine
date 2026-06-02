#include "../scene.h"


static const char* PH_GENERATE = R"lua(
math.randomseed(os.time())
scene_remove_by_prefix("ph_pt_")
scene_remove_by_prefix("ph_sph_")

_ph_cloud = {}
local n      = math.floor((ph_pointCount or 20) + 0.5)
local dist   = math.floor((ph_distrib or 0) + 0.5)
local bounds = ph_bounds or 200
local sigma  = ph_gaussSigma or 80
local sphR   = ph_sphereR or 120

for i = 1, n do
    local x, y, z
    if dist == 1 then
        x = gauss(0, sigma); y = gauss(0, sigma); z = gauss(0, sigma)
    elseif dist == 2 then
        x = gauss(0, 1); y = gauss(0, 1); z = gauss(0, 1)
        local len = math.sqrt(x*x + y*y + z*z); if len < 1e-6 then len = 1 end
        x = x/len*sphR; y = y/len*sphR; z = z/len*sphR
    else
        x = (math.random()*2-1)*bounds
        y = (math.random()*2-1)*bounds
        z = (math.random()*2-1)*bounds
    end
    _ph_cloud[i] = { x, y, z }
    local ptId  = scene_add_point(x, y, z, 1.0, 0.82, 0.22, 1.0)
    scene_set_name(ptId, "ph_pt_" .. i)
    local sphId = scene_add_sphere(1.0, x, y, z, 2, 0.28, 0.62, 1.0, 0.20)
    scene_set_name(sphId, "ph_sph_" .. i)
    scene_set_parent(sphId, ptId)
end

scene_update_light()
_ph_covR = scene_ph_coverage(_ph_cloud)
ph_radius = 0
scene_set_time(0)
scene_set_time_max(0)
scene_set_paused(true)
local b0,b1,e,tr = scene_ph_topology(_ph_cloud, 0)
_ph_b0=b0; _ph_b1=b1; _ph_edges=e; _ph_tris=tr; _ph_conn=false
)lua";

static const char* PH_TICK = R"lua(
local speed = ph_radius_speed or 25
if not paused then
    ph_radius = t * speed
else
    if speed > 0.0001 then scene_set_time(ph_radius / speed) end
end

local r = ph_radius or 0
if _ph_cloud then
    local b0,b1,e,tr = scene_ph_topology(_ph_cloud, r)
    _ph_b0=b0; _ph_b1=b1; _ph_edges=e; _ph_tris=tr; _ph_conn = (b0 <= 1)
end

local rs = r > 0.001 and r or 0.001
for i = 1, #scene do
    local nm = scene[i].name
    if nm and _ph_cloud then
        if string.sub(nm,1,6) == "ph_pt_" then
            local k = tonumber(string.sub(nm,7))
            local c = k and _ph_cloud[k]
            if c then scene[i].x=c[1]; scene[i].y=c[2]; scene[i].z=c[3] end
        elseif string.sub(nm,1,7) == "ph_sph_" then
            local k = tonumber(string.sub(nm,8))
            local c = k and _ph_cloud[k]
            if c then scene[i].x=c[1]; scene[i].y=c[2]; scene[i].z=c[3] end
            scene[i].scale = rs
        end
    end
end

if not paused and _ph_conn then
    if scene_get_loop() then
        scene_set_time(0)   -- restart the growth; ph_radius follows next frame
        ph_radius = 0
    else
        scene_set_paused(true)
    end
end
)lua";

static const char* PH_RESET = R"lua(
ph_radius = 0
scene_set_time(0)
scene_set_time_max(0)
if _ph_cloud then
    local b0,b1,e,tr = scene_ph_topology(_ph_cloud, 0)
    _ph_b0=b0; _ph_b1=b1; _ph_edges=e; _ph_tris=tr; _ph_conn=false
end
)lua";

static const char* PH_DRAW_GEN = R"lua(
imgui_drag_float("ph_pointCount", "pts##ph",    1, 3,  300, "%.0f")
imgui_drag_float("ph_bounds",     "bounds##ph", 2, 10, 5000, "%.0f")
imgui_combo("ph_distrib", "Distrib##ph", {"Uniform", "Gaussian", "Sphere surf"})
local d = math.floor((ph_distrib or 0) + 0.5)
if d == 1 then imgui_drag_float("ph_gaussSigma", "sigma##ph", 1, 1,  3000, "%.0f") end
if d == 2 then imgui_drag_float("ph_sphereR",    "R##ph",     1, 10, 3000, "%.0f") end
imgui_separator()
imgui_button("Generate Random##ph", "generate")
imgui_text(string.format("Coverage r: %.2f", _ph_covR or 0))
)lua";

static const char* PH_DRAW_TOPO = R"lua(
local N = _ph_cloud and #_ph_cloud or 0
imgui_text(string.format("Points: %d     r = %.2f", N, ph_radius or 0))
imgui_separator()
imgui_text(string.format("Beta-0  (components): %d", _ph_b0 or 0))
imgui_text(string.format("Beta-1  (1-cycles):   %d", _ph_b1 or 0))
imgui_separator()
imgui_text(string.format("Edges:     %d", _ph_edges or 0))
imgui_text(string.format("Triangles: %d", _ph_tris or 0))
imgui_separator()
local covR = _ph_covR or 0
imgui_text(string.format("Coverage r:  %.2f", covR))
if covR > 0.001 then
    local prog = (ph_radius or 0) / covR
    if prog > 1 then prog = 1 end
    imgui_progress(prog, string.format("%.1f%%", prog * 100))
end
imgui_separator()
if _ph_conn then
    imgui_text_colored(0.3, 1.0, 0.4, 1, "Connected! (beta0=1)")
elseif not paused then
    imgui_text_colored(1.0, 0.8, 0.2, 1, "Animating...")
else
    imgui_text_colored(0.6, 0.6, 0.6, 1, "Paused")
end
)lua";

void Scene::LoadPersistentHomologyScene()
{
    for (Primitive* p : this->primitives) delete p;
    this->primitives.clear();
    root.children.clear();
    m_sortedDirty = true;
    this->orientationTransformer.SetTargetObjects({});
    this->ClearTrajectories();
    this->ClearSceneCustomState();
    this->sceneName = "PH Demo";
    root.name = sceneName;

    this->sceneFloats["ph_radius"]       = std::make_shared<float>(0.0f);
    this->sceneFloats["ph_radius_speed"] = std::make_shared<float>(25.0f);
    this->sceneFloats["ph_pointCount"]   = std::make_shared<float>(20.0f);
    this->sceneFloats["ph_bounds"]       = std::make_shared<float>(200.0f);
    this->sceneFloats["ph_distrib"]      = std::make_shared<float>(0.0f);
    this->sceneFloats["ph_gaussSigma"]   = std::make_shared<float>(80.0f);
    this->sceneFloats["ph_sphereR"]      = std::make_shared<float>(120.0f);
    this->sceneData["ph_distrib"]        = 0;

    auto* ctrl = new SceneController();
    ctrl->SetScript("tick",          PH_TICK);
    ctrl->SetScript("reset",         PH_RESET);
    ctrl->SetScript("generate",      PH_GENERATE);
    ctrl->SetScript("draw_ph_gen",   PH_DRAW_GEN);
    ctrl->SetScript("draw_ph_topo",  PH_DRAW_TOPO);

    ctrl->sliderDefs = {
        {"PH Radius", "ph_radius",        0.f,  600.f},
        {"PH Speed",  "ph_radius_speed",  0.1f, 2000.f},
    };

    ctrl->windowDefs = {
        {"ph_gen",  "PH Generator", {50.f,  370.f}, {300.f, 235.f}, {}},
        {"ph_topo", "PH Topology",  {365.f, 370.f}, {300.f, 235.f}, {}},
    };

    SetController(ctrl);

    if (controller && controller->compiledFns.count("generate"))
        controller->compiledFns["generate"](*this);

    this->ResetTime();
}
