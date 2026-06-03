#pragma once
#include "..\imgui\imgui.h"
#include "..\camera\Camera.h"
#include "..\camera\Projections.h"
#include "..\gui\GuiIcons.h"
#include <algorithm>
#include <cmath>

class NavCube {
public:
    static constexpr float SIZE    = 80.f;
    static constexpr float PADDING = 10.f;

    static bool Draw(ImVec2 windowSize, Camera& camera,
                     float yOffset, bool& rsSolid, bool& rsWireframe) {
        ImDrawList* dl = ImGui::GetForegroundDrawList();

        ImVec2 centre{
            windowSize.x - PADDING - SIZE * 0.5f,
            yOffset + PADDING + SIZE * 0.5f
        };

        XMFLOAT4X4 vmf;
        XMStoreFloat4x4(&vmf, camera.GetViewMatrix());

        auto project = [&](XMFLOAT3 world) -> ImVec2 {
            float x = world.x * vmf._11 + world.y * vmf._21 + world.z * vmf._31;
            float y = world.x * vmf._12 + world.y * vmf._22 + world.z * vmf._32;
            float z = world.x * vmf._13 + world.y * vmf._23 + world.z * vmf._33;
            float s = SIZE * 0.38f * (1.0f + z * 0.15f);
            return ImVec2(centre.x + x * s, centre.y - y * s);
        };

        static const XMFLOAT3 corners[8] = {
            {-1,-1,-1}, {+1,-1,-1}, {+1,+1,-1}, {-1,+1,-1},
            {-1,-1,+1}, {+1,-1,+1}, {+1,+1,+1}, {-1,+1,+1}
        };

        struct FaceDef { int c[4]; XMFLOAT3 normal; const char* label; const XMMATRIX* proj; ImU32 baseCol; };
        static const FaceDef faces[6] = {
            {{4,5,6,7}, {0,0,+1}, "TOP",   &Projections::XY,      IM_COL32(70,130,190,210)},
            {{0,3,2,1}, {0,0,-1}, "BOT",   &Projections::XY_BOT,  IM_COL32(40,80,130,180)},
            {{2,6,7,3}, {0,+1,0}, "FRONT", &Projections::XZ,      IM_COL32(60,150,80,210)},
            {{0,1,5,4}, {0,-1,0}, "BACK",  &Projections::XZ_BACK, IM_COL32(30,90,50,180)},
            {{1,2,6,5}, {+1,0,0}, "RIGHT", &Projections::YZ,      IM_COL32(190,90,60,210)},
            {{0,4,7,3}, {-1,0,0}, "LEFT",  &Projections::YZ_LEFT, IM_COL32(130,50,30,180)},
        };

        ImVec2 projected[8];
        for (int i = 0; i < 8; ++i) projected[i] = project(corners[i]);

        struct FaceEntry { int idx; float depth; };
        FaceEntry order[6];
        for (int f = 0; f < 6; ++f) {
            XMFLOAT3 cn{};
            for (int k = 0; k < 4; ++k) { cn.x += corners[faces[f].c[k]].x; cn.y += corners[faces[f].c[k]].y; cn.z += corners[faces[f].c[k]].z; }
            cn.x *= .25f; cn.y *= .25f; cn.z *= .25f;
            float cz = cn.x * vmf._13 + cn.y * vmf._23 + cn.z * vmf._33;
            order[f] = { f, cz };
        }
        std::sort(order, order+6, [](const FaceEntry& a, const FaceEntry& b){ return a.depth < b.depth; });

        ImVec2 mp = ImGui::GetMousePos();
        int hovered = -1;
        for (int fi = 0; fi < 6; ++fi) {
            int f = order[fi].idx;
            XMFLOAT3 nw = faces[f].normal;
            float nd = nw.x*vmf._13 + nw.y*vmf._23 + nw.z*vmf._33;
            if (nd <= 0.01f) continue;
            ImVec2 pts[4];
            for (int k = 0; k < 4; ++k) pts[k] = projected[faces[f].c[k]];
            bool hover = isPointInQuad(mp, pts);
            if (hover) hovered = f;
            ImU32 col = hover ? brighten(faces[f].baseCol, 60) : faces[f].baseCol;
            dl->AddQuadFilled(pts[0], pts[1], pts[2], pts[3], col);
            dl->AddQuad(pts[0], pts[1], pts[2], pts[3], IM_COL32(180,200,255,160), 1.2f);
            ImVec2 tc = quadCenter(pts);
            ImVec2 ts = ImGui::CalcTextSize(faces[f].label);
            dl->AddText(ImVec2(tc.x-ts.x*.5f, tc.y-ts.y*.5f), IM_COL32(240,245,255,230), faces[f].label);
        }

        bool inWidget = ImVec2Dist(mp, centre) < SIZE * 0.52f;
        if (inWidget && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hovered >= 0)
            camera.SetRotation(*faces[hovered].proj);

        float panelX = windowSize.x - PADDING - SIZE;
        float panelY = yOffset + PADDING + SIZE + 4.f;
        ImGui::SetNextWindowPos(ImVec2(panelX, panelY), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.60f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.f, 4.f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(4.f, 3.f));
        ImGui::Begin("##navcube_panel", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::PopStyleVar(2);

        float btnW = (SIZE - 12.f) * 0.5f;

        if (ImGui::Button("Iso", ImVec2(btnW, 0))) camera.SetRotation(Projections::ISO);
        ImGui::SameLine();
        if (ImGui::Button("Dim", ImVec2(btnW, 0))) camera.SetRotation(Projections::DIM);

        if (ImGui::Button("Rst Pos", ImVec2(btnW, 0))) camera.SetPosition(XMFLOAT3(0.f, 0.f, 0.f));
        ImGui::SameLine();
        if (ImGui::Button("Rst Scl", ImVec2(btnW, 0))) camera.SetScale(1.f);

        float scale = camera.GetScale();
        float logS  = std::log10f(scale);
        ImGui::SetNextItemWidth(SIZE - 8.f);
        if (ImGui::SliderFloat("##zoom", &logS, -4.f, 5.f, "", ImGuiSliderFlags_NoRoundToFormat))
            camera.SetScale(std::powf(10.f, logS));

        GuiIcons::ToggleIconButton("##solid", &rsSolid,      btnW, GuiIcons::SolidCube);
        ImGui::SameLine();
        GuiIcons::ToggleIconButton("##wire",  &rsWireframe,  btnW, GuiIcons::WireframeCube);

        bool panelHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        ImGui::End();

        return inWidget || panelHovered;
    }

private:
    static float ImVec2Dist(ImVec2 a, ImVec2 b) {
        float dx = a.x-b.x, dy = a.y-b.y;
        return sqrtf(dx*dx + dy*dy);
    }
    static bool isPointInQuad(ImVec2 p, ImVec2 q[4]) {
        auto cross2d = [](ImVec2 o, ImVec2 a, ImVec2 b) {
            return (a.x-o.x)*(b.y-o.y) - (a.y-o.y)*(b.x-o.x);
        };
        float s0=cross2d(q[0],q[1],p), s1=cross2d(q[1],q[2],p);
        float s2=cross2d(q[2],q[3],p), s3=cross2d(q[3],q[0],p);
        return (s0>=0&&s1>=0&&s2>=0&&s3>=0) || (s0<=0&&s1<=0&&s2<=0&&s3<=0);
    }
    static ImVec2 quadCenter(ImVec2 q[4]) {
        return ImVec2((q[0].x+q[1].x+q[2].x+q[3].x)*.25f, (q[0].y+q[1].y+q[2].y+q[3].y)*.25f);
    }
    static ImU32 brighten(ImU32 col, int amount) {
        int r=(col>>0)&0xFF, g=(col>>8)&0xFF, b=(col>>16)&0xFF, a=(col>>24)&0xFF;
        r=std::min(255,r+amount); g=std::min(255,g+amount); b=std::min(255,b+amount);
        return IM_COL32(r,g,b,a);
    }
};
