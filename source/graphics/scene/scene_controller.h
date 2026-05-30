#pragma once
#include "scene_node.h"
#include "../../external/json.hpp"
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

class Scene;

struct ControllerScript {
    std::string name;
    std::string code;
};

struct SliderDef {
    std::string label;
    std::string luaGlobal;
    float       min      = 0.f;
    float       max      = 1.f;
    bool        readOnly = false;
};

struct WindowWidgetDef {
    std::string type;
    std::string label;
    std::string luaGlobal;
    std::string action;
    float       min    = 0.f;
    float       max    = 1.f;
    float       step   = 0.1f;
    float       height = 60.f;
    std::string format = "%.3f";
    std::vector<std::string> options;
};

struct WindowDef {
    std::string id;
    std::string title;
    float pos[2]  = { 50.f,  50.f };
    float size[2] = { 285.f, 200.f };
    std::vector<WindowWidgetDef> widgets;
};

class SceneController : public SceneNode {
public:
    SceneController() { name = "controller"; }
    bool IsController() const override { return true; }

    std::vector<ControllerScript> scripts;
    std::vector<SliderDef>        sliderDefs;
    std::vector<WindowDef>        windowDefs;

    std::function<void(Scene&, float, float, bool)>          compiledTick;
    std::function<void(Scene&)>                              compiledReset;
    std::unordered_map<std::string, std::function<void(Scene&)>> compiledFns;

    const std::string* GetScriptCode(const std::string& n) const {
        for (auto& s : scripts) if (s.name == n) return &s.code;
        return nullptr;
    }

    void SetScript(const std::string& n, std::string code) {
        for (auto& s : scripts) { if (s.name == n) { s.code = std::move(code); return; } }
        scripts.push_back({n, std::move(code)});
    }

    nlohmann::json ToJson() const {
        nlohmann::json j;
        j["name"] = name;

        j["scripts"] = nlohmann::json::array();
        for (auto& s : scripts)
            j["scripts"].push_back({{"name", s.name}, {"code", s.code}});

        j["sliderDefs"] = nlohmann::json::array();
        for (auto& sd : sliderDefs)
            j["sliderDefs"].push_back({{"label", sd.label}, {"lua_global", sd.luaGlobal},
                                       {"min", sd.min}, {"max", sd.max}, {"read_only", sd.readOnly}});

        j["windowDefs"] = nlohmann::json::array();
        for (auto& wd : windowDefs) {
            nlohmann::json wj;
            wj["id"]    = wd.id;
            wj["title"] = wd.title;
            wj["pos"]   = {wd.pos[0],  wd.pos[1]};
            wj["size"]  = {wd.size[0], wd.size[1]};
            wj["widgets"] = nlohmann::json::array();
            for (auto& w : wd.widgets) {
                nlohmann::json ww = {{"type", w.type}, {"label", w.label}};
                if (!w.luaGlobal.empty()) ww["lua_global"] = w.luaGlobal;
                if (!w.action.empty())    ww["action"]     = w.action;
                if (w.min  != 0.f)       ww["min"]        = w.min;
                if (w.max  != 1.f)       ww["max"]        = w.max;
                if (w.step != 0.1f)      ww["step"]       = w.step;
                if (w.height != 60.f)    ww["height"]     = w.height;
                if (w.format != "%.3f")  ww["format"]     = w.format;
                if (!w.options.empty())  ww["options"]    = w.options;
                wj["widgets"].push_back(ww);
            }
            j["windowDefs"].push_back(wj);
        }
        return j;
    }

    static SceneController* FromJson(const nlohmann::json& j) {
        auto* c = new SceneController();
        if (j.contains("name")) c->name = j["name"].get<std::string>();

        if (j.contains("scripts") && j["scripts"].is_array())
            for (auto& s : j["scripts"])
                c->scripts.push_back({s.value("name",""), s.value("code","")});

        if (j.contains("sliderDefs") && j["sliderDefs"].is_array())
            for (auto& s : j["sliderDefs"]) {
                SliderDef sd;
                sd.label     = s.value("label","");
                sd.luaGlobal = s.value("lua_global","");
                sd.min       = s.value("min", 0.f);
                sd.max       = s.value("max", 1.f);
                sd.readOnly  = s.value("read_only", false);
                c->sliderDefs.push_back(sd);
            }

        if (j.contains("windowDefs") && j["windowDefs"].is_array())
            for (auto& wj : j["windowDefs"]) {
                WindowDef wd;
                wd.id    = wj.value("id","");
                wd.title = wj.value("title","");
                if (wj.contains("pos"))  { wd.pos[0]  = wj["pos"][0];  wd.pos[1]  = wj["pos"][1]; }
                if (wj.contains("size")) { wd.size[0] = wj["size"][0]; wd.size[1] = wj["size"][1]; }
                if (wj.contains("widgets") && wj["widgets"].is_array())
                    for (auto& ww : wj["widgets"]) {
                        WindowWidgetDef w;
                        w.type      = ww.value("type","");
                        w.label     = ww.value("label","");
                        w.luaGlobal = ww.value("lua_global","");
                        w.action    = ww.value("action","");
                        w.min       = ww.value("min",  0.f);
                        w.max       = ww.value("max",  1.f);
                        w.step      = ww.value("step", 0.1f);
                        w.height    = ww.value("height", 60.f);
                        w.format    = ww.value("format", "%.3f");
                        if (ww.contains("options") && ww["options"].is_array())
                            for (auto& o : ww["options"]) w.options.push_back(o.get<std::string>());
                        wd.widgets.push_back(w);
                    }
                c->windowDefs.push_back(wd);
            }
        return c;
    }
};
