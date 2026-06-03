#pragma once
#include "../imgui/imgui.h"

struct LayoutState {
    float panelW   = 420.f;
    float colW     = 205.f;
    float consoleH = 165.f;
    float timeH    = -1.f;
    float luaH     = -1.f;
    static constexpr float STRIP_H = 34.f;

    void Validate(float wW, float wH) {
        if (consoleH < 60.f)       consoleH = 60.f;
        if (consoleH > wH * .55f)  consoleH = wH * .55f;
        float pH = wH - STRIP_H - consoleH;
        if (panelW < 200.f)        panelW = 200.f;
        if (panelW > wW - 200.f)   panelW = wW - 200.f;
        if (colW < 80.f)           colW = 80.f;
        if (colW > panelW - 80.f)  colW = panelW - 80.f;
        if (timeH < 0.f)           timeH = pH * .33f;
        if (luaH  < 0.f)           luaH  = pH * .33f;
        if (timeH < 60.f)          timeH = 60.f;
        if (timeH > pH - 60.f)     timeH = pH - 60.f;
        if (luaH  < 60.f)          luaH  = 60.f;
        if (luaH  > pH - 60.f)     luaH  = pH - 60.f;
    }
};
