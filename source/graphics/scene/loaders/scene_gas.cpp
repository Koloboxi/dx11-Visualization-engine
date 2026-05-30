#include "../scene.h"

// Particle count, box size, mass and avg-speed are all driven by Lua globals
// (gas_N, gas_L, gas_mass, gas_avg_speed) exposed as DragFloat sliders in the
// "Lua Globals" window. The tick reconciles the live particle set to gas_N every
// frame (even while paused), so changes apply on the fly.
static const char* GAS_TICK = R"lua(
_vel   = _vel or {}
_gas_N = _gas_N or 0
local L        = gas_L or 150
local mass     = gas_mass or 1
local target   = math.floor((gas_N or 100) + 0.5)
if target < 0 then target = 0 end
local curSpeed = gas_avg_speed or 50
local sigma    = curSpeed / math.sqrt(3)

-- live avg-speed change: rescale existing velocities (keeps directions)
_gas_prev_speed = _gas_prev_speed or curSpeed
if _gas_prev_speed > 1e-6 and math.abs(curSpeed - _gas_prev_speed) > 1e-6 then
    local ratio = curSpeed / _gas_prev_speed
    for j = 1, _gas_N * 3 do
        if _vel[j] then _vel[j] = _vel[j] * ratio end
    end
end
_gas_prev_speed = curSpeed

-- live reconcile particle count (runs regardless of dt / pause)
while _gas_N < target do
    local k = _gas_N + 1
    local x = (math.random()*2-1)*L*0.85
    local y = (math.random()*2-1)*L*0.85
    local z = (math.random()*2-1)*L*0.85
    local id = scene_add_point(x, y, z, 0.5, 0.5, 1, 1)
    scene_set_name(id, "gas_p_" .. k)
    _vel[k*3-2] = gauss(0, sigma)
    _vel[k*3-1] = gauss(0, sigma)
    _vel[k*3]   = gauss(0, sigma)
    _gas_N = k
end
while _gas_N > target do
    scene_remove_by_name("gas_p_" .. _gas_N)
    _vel[_gas_N*3-2] = nil; _vel[_gas_N*3-1] = nil; _vel[_gas_N*3] = nil
    _gas_N = _gas_N - 1
end

local N = _gas_N
local boxScale = L / (_gas_box_L0 or L)

-- integrate motion + elastic wall bounces; resize box live; accumulate kinetic energy
local sumV2 = 0
for i = 1, #scene do
    local nm = scene[i].name
    if nm == "gas_box" then
        scene[i].scale = boxScale
    elseif nm and string.sub(nm,1,6) == "gas_p_" then
        local k = tonumber(string.sub(nm,7))
        if k and _vel[k*3-2] then
            local vx=_vel[k*3-2]; local vy=_vel[k*3-1]; local vz=_vel[k*3]
            local x = scene[i].x + vx * dt
            local y = scene[i].y + vy * dt
            local z = scene[i].z + vz * dt
            if x < -L then x = -2*L - x; _vel[k*3-2] =  math.abs(vx) end
            if x >  L then x =  2*L - x; _vel[k*3-2] = -math.abs(vx) end
            if y < -L then y = -2*L - y; _vel[k*3-1] =  math.abs(vy) end
            if y >  L then y =  2*L - y; _vel[k*3-1] = -math.abs(vy) end
            if z < -L then z = -2*L - z; _vel[k*3]   =  math.abs(vz) end
            if z >  L then z =  2*L - z; _vel[k*3]   = -math.abs(vz) end
            scene[i].x = x; scene[i].y = y; scene[i].z = z
            vx=_vel[k*3-2]; vy=_vel[k*3-1]; vz=_vel[k*3]
            sumV2 = sumV2 + vx*vx + vy*vy + vz*vz
        end
    end
end

if N == 0 then
    gas_temperature = 0; gas_pressure = 0; gas_volume = 8*L*L*L
    return
end

local T = mass * sumV2 / (3 * N)
local V = 8 * L * L * L
local P = N * T / V
gas_temperature = T
gas_pressure    = P
gas_volume      = V

-- recolor by speed (blue = cold, red = hot)
local vRMS = T > 0 and math.sqrt(3 * T / math.max(mass, 1e-6)) or 1
local vRef = 2 * vRMS
if vRef < 1e-6 then vRef = 1 end
for i = 1, #scene do
    local nm = scene[i].name
    if nm and string.sub(nm,1,6) == "gas_p_" then
        local k = tonumber(string.sub(nm,7))
        if k and _vel[k*3-2] then
            local vx=_vel[k*3-2]; local vy=_vel[k*3-1]; local vz=_vel[k*3]
            local spd = math.sqrt(vx*vx + vy*vy + vz*vz)
            local t_n = 1 - math.min(spd / vRef, 1)
            local r,g,b = hsv_to_rgb(t_n * 0.67, 1, 1)
            scene[i].r = r; scene[i].g = g; scene[i].b = b; scene[i].a = 1
        end
    end
end

_hist_timer = (_hist_timer or 0) + dt
if _hist_timer >= 0.2 then
    _hist_timer = 0
    _temp_hist = _temp_hist or {}
    _pres_hist = _pres_hist or {}
    if #_temp_hist >= 200 then table.remove(_temp_hist, 1) end
    if #_pres_hist >= 200 then table.remove(_pres_hist, 1) end
    _temp_hist[#_temp_hist+1] = T
    _pres_hist[#_pres_hist+1] = P
end
)lua";

// Reset only clears particles + state and (re)builds the box; the tick repopulates
// up to gas_N on its next run (which happens even while paused, dt = 0).
static const char* GAS_RESET = R"lua(
scene_remove_by_prefix("gas_p_")
scene_remove_by_name("gas_box")

_vel            = {}
_gas_N          = 0
_temp_hist      = {}
_pres_hist      = {}
_hist_timer     = 0
_gas_prev_speed = gas_avg_speed or 50

math.randomseed(os.time())
local L = gas_L or 150
local boxId = scene_add_cube_wireframe(L, 0, 0, 0, 0.38, 0.48, 0.62, 0.9)
scene_set_name(boxId, "gas_box")
_gas_box_L0 = L

gas_temperature = 0
gas_pressure    = 0
gas_volume      = 8*L*L*L
)lua";

static const char* GAS_DRAW_STATS = R"lua(
local N = _gas_N or 0
local T = gas_temperature or 0
local P = gas_pressure    or 0
local V = gas_volume      or 0
imgui_text(string.format("N (particles):  %d",   N))
imgui_text(string.format("Volume:         %.4g", V))
imgui_text(string.format("Temperature:    %.4g", T))
imgui_text(string.format("Pressure:       %.4g", P))
if N > 0 and P > 0 then
    imgui_text(string.format("P*V/N (=T):     %.4g", P*V/N))
end
imgui_separator()
imgui_plot_lines("Temperature", "_temp_hist", 60)
imgui_plot_lines("Pressure",    "_pres_hist", 60)
)lua";

void Scene::LoadIdealGasScene()
{
    for (Primitive* p : primitives) delete p;
    primitives.clear();
    root.children.clear();
    m_sortedDirty = true;
    orientationTransformer.SetTargetObjects({});
    ClearTrajectories();
    ClearSceneCustomState();
    sceneName  = "Ideal Gas";
    root.name  = sceneName;

    sceneFloats["gas_N"]           = std::make_shared<float>(100.f);
    sceneFloats["gas_avg_speed"]   = std::make_shared<float>(50.f);
    sceneFloats["gas_mass"]        = std::make_shared<float>(1.f);
    sceneFloats["gas_L"]           = std::make_shared<float>(150.f);
    sceneFloats["gas_temperature"] = std::make_shared<float>(0.f);
    sceneFloats["gas_pressure"]    = std::make_shared<float>(0.f);
    sceneFloats["gas_volume"]      = std::make_shared<float>(0.f);

    auto* ctrl = new SceneController();
    ctrl->SetScript("tick",           GAS_TICK);
    ctrl->SetScript("reset",          GAS_RESET);
    ctrl->SetScript("draw_gas_stats", GAS_DRAW_STATS);

    ctrl->sliderDefs = {
        {"Particles",     "gas_N",          1.f,    5000.f},
        {"Avg speed",     "gas_avg_speed",  0.1f,   1e5f},
        {"Mass",          "gas_mass",       0.01f,  1000.f},
        {"Box half-size", "gas_L",          10.f,   3000.f},
    };

    ctrl->windowDefs = {
        {"gas_stats", "Gas Stats", {50.f, 280.f}, {285.f, 320.f}, {}},
    };

    SetController(ctrl);

    ResetTime();
}
