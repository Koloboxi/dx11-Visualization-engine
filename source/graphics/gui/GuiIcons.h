#pragma once
#include "../imgui/imgui.h"
#include <cmath>

namespace GuiIcons {

inline void SolidCube(ImDrawList* dl, ImVec2 c, float r) {
    float rx = r * 0.87f, ry = r * 0.5f;
    ImVec2 T={c.x,c.y-r}, TR={c.x+rx,c.y-ry}, BR={c.x+rx,c.y+ry};
    ImVec2 B={c.x,c.y+r}, BL={c.x-rx,c.y+ry}, TL={c.x-rx,c.y-ry}, M=c;
    dl->AddQuadFilled(T,TR,M,TL, IM_COL32(195,215,248,230));
    dl->AddQuadFilled(TR,BR,B,M, IM_COL32(105,135,200,230));
    dl->AddQuadFilled(TL,M,B,BL, IM_COL32(145,170,220,230));
    ImU32 e=IM_COL32(215,230,255,180); float t=.8f;
    dl->AddLine(T,TR,e,t); dl->AddLine(T,TL,e,t);
    dl->AddLine(TR,BR,e,t); dl->AddLine(TL,BL,e,t);
    dl->AddLine(BR,B,e,t); dl->AddLine(BL,B,e,t);
    dl->AddLine(T,M,e,t); dl->AddLine(TR,M,e,t);
    dl->AddLine(TL,M,e,t); dl->AddLine(M,B,e,t);
}

inline void WireframeCube(ImDrawList* dl, ImVec2 c, float r) {
    float rx = r * 0.87f, ry = r * 0.5f;
    ImVec2 T={c.x,c.y-r}, TR={c.x+rx,c.y-ry}, BR={c.x+rx,c.y+ry};
    ImVec2 B={c.x,c.y+r}, BL={c.x-rx,c.y+ry}, TL={c.x-rx,c.y-ry}, M=c;
    ImU32 v=IM_COL32(235,242,255,235), h=IM_COL32(185,195,225,90);
    dl->AddLine(T,TR,v); dl->AddLine(T,TL,v);
    dl->AddLine(TR,BR,v); dl->AddLine(TL,BL,v);
    dl->AddLine(BR,B,v); dl->AddLine(BL,B,v);
    dl->AddLine(T,M,v); dl->AddLine(TR,M,v);
    dl->AddLine(TL,M,v); dl->AddLine(M,B,v);
    dl->AddLine(BR,BL,h,.8f);
}

inline void GridIcon(ImDrawList* dl, ImVec2 c, float r) {
    ImU32 g = IM_COL32(185,198,228,205);
    float s = r * .67f;
    for (int i = -1; i <= 1; ++i) {
        dl->AddLine({c.x+i*s,c.y-r},{c.x+i*s,c.y+r},g,.8f);
        dl->AddLine({c.x-r,c.y+i*s},{c.x+r,c.y+i*s},g,.8f);
    }
}

inline void AxesIcon(ImDrawList* dl, ImVec2 c, float r) {
    float len = r * .88f;
    dl->AddLine(c,{c.x+len,c.y},           IM_COL32(225,80,80,225),  1.2f);
    dl->AddLine(c,{c.x,c.y-len},           IM_COL32(80,215,80,225),  1.2f);
    dl->AddLine(c,{c.x-len*.55f,c.y+len*.55f}, IM_COL32(80,130,235,225),1.2f);
    dl->AddTriangleFilled({c.x+len,c.y},   {c.x+len-3,c.y-2},{c.x+len-3,c.y+2}, IM_COL32(225,80,80,225));
    dl->AddTriangleFilled({c.x,c.y-len},   {c.x-2,c.y-len+3},{c.x+2,c.y-len+3}, IM_COL32(80,215,80,225));
}

inline void OutlineIcon(ImDrawList* dl, ImVec2 c, float r) {
    float rx = r * .87f, ry = r * .5f;
    ImVec2 T={c.x,c.y-r}, TR={c.x+rx,c.y-ry}, BR={c.x+rx,c.y+ry};
    ImVec2 B={c.x,c.y+r}, BL={c.x-rx,c.y+ry}, TL={c.x-rx,c.y-ry}, M=c;
    dl->AddQuadFilled(T,TR,M,TL, IM_COL32(120,150,200,70));
    dl->AddQuadFilled(TR,BR,B,M, IM_COL32(65,95,165,70));
    dl->AddQuadFilled(TL,M,B,BL, IM_COL32(90,120,185,70));
    ImU32 e = IM_COL32(255,225,90,225);
    dl->AddLine(T,TR,e); dl->AddLine(T,TL,e);
    dl->AddLine(TR,BR,e); dl->AddLine(TL,BL,e);
    dl->AddLine(BR,B,e); dl->AddLine(BL,B,e);
    dl->AddLine(T,M,e); dl->AddLine(TR,M,e);
    dl->AddLine(TL,M,e); dl->AddLine(M,B,e);
    dl->AddLine(BR,BL,e,.8f);
}

inline void SmoothSphereIcon(ImDrawList* dl, ImVec2 c, float r) {
    dl->AddCircleFilled(c, r, IM_COL32(82,108,190,218));
    dl->AddCircleFilled({c.x+r*.28f,c.y-r*.28f}, r*.38f, IM_COL32(205,222,255,145));
    dl->AddCircle(c, r, IM_COL32(180,200,238,155), 0, .8f);
}

inline void LightbulbIcon(ImDrawList* dl, ImVec2 c, float r) {
    float br = r * .60f;
    ImVec2 bc = {c.x, c.y - r*.18f};
    dl->AddCircleFilled(bc, br, IM_COL32(252,232,100,232));
    dl->AddCircle(bc, br, IM_COL32(232,202,68,185), 0, .8f);
    float bTop = bc.y+br, bwt = br*.45f, bwb = br*.52f, bh = r*.30f;
    dl->AddQuadFilled({c.x-bwt,bTop},{c.x+bwt,bTop},{c.x+bwb,bTop+bh},{c.x-bwb,bTop+bh}, IM_COL32(202,182,72,215));
    ImU32 lc = IM_COL32(202,182,72,185);
    dl->AddLine({c.x-bwb,bTop+bh},{c.x+bwb,bTop+bh},lc,.8f);
    float ry2 = bTop+bh+r*.13f;
    if (ry2 < c.y+r) dl->AddLine({c.x-bwb*.7f,ry2},{c.x+bwb*.7f,ry2},lc,.8f);
}

template<typename DrawFn>
inline bool ToggleIconButton(const char* id, bool* toggled, float sz, DrawFn drawFn) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, {sz, sz});
    if (clicked) *toggled = !*toggled;
    bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg = *toggled
        ? (hov ? IM_COL32(92,152,228,238) : IM_COL32(68,128,202,212))
        : (hov ? IM_COL32(78,78,98,212)   : IM_COL32(52,52,68,172));
    dl->AddRectFilled(p, {p.x+sz,p.y+sz}, bg, 3.f);
    dl->AddRect(p, {p.x+sz,p.y+sz}, IM_COL32(112,122,152,165), 3.f, 0, .8f);
    float pad = sz * .18f;
    drawFn(dl, {p.x+sz*.5f, p.y+sz*.5f}, sz*.5f - pad);
    return clicked;
}

}
