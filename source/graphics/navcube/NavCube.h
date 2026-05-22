#pragma once
#include "..\imgui\imgui.h"
#include "..\camera\Camera.h"
#include "..\camera\Projections.h"
#include <algorithm>

// Navigation cube rendered via ImGui DrawList in the viewport top-right corner.
// Click on a face, edge or corner to align the camera to that direction.
class NavCube {
public:
    static constexpr float SIZE = 80.f;     // widget width/height in pixels
    static constexpr float PADDING = 10.f;  // distance from window edges

    // Call from gui.cpp after scene rendering but before ImGui::Render().
    // windowSize = full viewport size; camera is modified on click.
    // Returns true when mouse is hovering over or clicked the cube (block scene picking).
    static bool Draw(ImVec2 windowSize, Camera& camera) {
        ImDrawList* dl = ImGui::GetForegroundDrawList();

        // Widget centre
        ImVec2 centre{
            windowSize.x - PADDING - SIZE * 0.5f,
            PADDING + SIZE * 0.5f
        };

        // Extract pure rotation from view matrix (no translation).
        XMFLOAT4X4 vmf;
        XMStoreFloat4x4(&vmf, camera.GetViewMatrix());

        // Transform a world-space direction into screen space.
        // In LookAtRH the view matrix rows are (xAxis, yAxis, zAxis, translation).
        // col0=(M_11,M_21,M_31), col1=(M_12,M_22,M_32), col2=(M_13,M_23,M_33)
        // TransformNormal: result = v × M (row-vector × matrix)
        auto project = [&](XMFLOAT3 world) -> ImVec2 {
            float x = world.x * vmf._11 + world.y * vmf._21 + world.z * vmf._31;
            float y = world.x * vmf._12 + world.y * vmf._22 + world.z * vmf._32;
            float z = world.x * vmf._13 + world.y * vmf._23 + world.z * vmf._33;
            // slight perspective feel: scale by (1 + z*0.15)
            float s = SIZE * 0.38f * (1.0f + z * 0.15f);
            return ImVec2(centre.x + x * s, centre.y - y * s);
        };

        // 8 unit cube corners
        static const XMFLOAT3 corners[8] = {
            {-1,-1,-1}, {+1,-1,-1}, {+1,+1,-1}, {-1,+1,-1},
            {-1,-1,+1}, {+1,-1,+1}, {+1,+1,+1}, {-1,+1,+1}
        };

        struct FaceDef {
            int c[4];           // corner indices
            XMFLOAT3 normal;    // face normal (world space)
            const char* label;
            const XMMATRIX* proj;
            ImU32 baseCol;
        };

        static const FaceDef faces[6] = {
            {{4,5,6,7}, {0, 0,+1}, "TOP",   &Projections::XY,      IM_COL32( 70,130,190,210)},
            {{0,3,2,1}, {0, 0,-1}, "BOT",   &Projections::XY_BOT,  IM_COL32( 40, 80,130,180)},
            {{2,6,7,3}, {0,+1, 0}, "FRONT", &Projections::XZ,      IM_COL32( 60,150, 80,210)},
            {{0,1,5,4}, {0,-1, 0}, "BACK",  &Projections::XZ_BACK, IM_COL32( 30, 90, 50,180)},
            {{1,2,6,5}, {+1,0, 0}, "RIGHT", &Projections::YZ,      IM_COL32(190, 90, 60,210)},
            {{0,4,7,3}, {-1,0, 0}, "LEFT",  &Projections::YZ_LEFT, IM_COL32(130, 50, 30,180)},
        };

        // Compute projected corners
        ImVec2 projected[8];
        for (int i = 0; i < 8; ++i)
            projected[i] = project(corners[i]);

        // Compute face depths (z of face center) for back-to-front sorting
        struct FaceEntry { int idx; float depth; };
        FaceEntry order[6];
        for (int f = 0; f < 6; ++f) {
            XMFLOAT3 cn{};
            for (int k = 0; k < 4; ++k) {
                cn.x += corners[faces[f].c[k]].x;
                cn.y += corners[faces[f].c[k]].y;
                cn.z += corners[faces[f].c[k]].z;
            }
            cn.x *= 0.25f; cn.y *= 0.25f; cn.z *= 0.25f;
            // z in camera space (depth along zAxis)
            float cz = cn.x * vmf._13 + cn.y * vmf._23 + cn.z * vmf._33;
            order[f] = { f, cz };
        }
        std::sort(order, order + 6, [](const FaceEntry& a, const FaceEntry& b) {
            return a.depth < b.depth; // smaller cz = further away = draw first
        });

        // Hover detection (face under mouse)
        ImVec2 mp = ImGui::GetMousePos();
        int hovered = -1;

        // Outer background circle
        dl->AddCircleFilled(centre, SIZE * 0.52f, IM_COL32(20, 25, 35, 180), 40);
        dl->AddCircle(centre, SIZE * 0.52f, IM_COL32(80, 100, 140, 200), 40, 1.5f);

        // Draw faces back to front
        for (int fi = 0; fi < 6; ++fi) {
            int f = order[fi].idx;
            float depth = order[fi].depth;

            // Only draw front-facing faces (depth > 0 = face normal points toward camera)
            // Since zAxis points from scene to camera, normal.dot(zAxis) > 0 means visible
            XMFLOAT3 nw = faces[f].normal;
            float nd = nw.x * vmf._13 + nw.y * vmf._23 + nw.z * vmf._33;
            if (nd <= 0.01f) continue;

            ImVec2 pts[4];
            for (int k = 0; k < 4; ++k)
                pts[k] = projected[faces[f].c[k]];

            // Check mouse hover (simple point-in-convex-quad)
            bool hover = isPointInQuad(mp, pts);
            if (hover) hovered = f;

            ImU32 col = hover ? brighten(faces[f].baseCol, 60) : faces[f].baseCol;
            dl->AddQuadFilled(pts[0], pts[1], pts[2], pts[3], col);
            dl->AddQuad(pts[0], pts[1], pts[2], pts[3], IM_COL32(180, 200, 255, 160), 1.2f);

            // Face label
            ImVec2 textCenter = quadCenter(pts);
            const char* lbl = faces[f].label;
            ImVec2 ts = ImGui::CalcTextSize(lbl);
            dl->AddText(
                ImVec2(textCenter.x - ts.x * 0.5f, textCenter.y - ts.y * 0.5f),
                IM_COL32(240, 245, 255, 230), lbl
            );
        }

        // Handle click
        bool inWidget = ImVec2Dist(mp, centre) < SIZE * 0.52f;
        if (inWidget && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (hovered >= 0)
                camera.SetRotation(*faces[hovered].proj);
            else
                camera.SetRotation(Projections::ISO);
        }

        return inWidget;
    }

private:
    static float ImVec2Dist(ImVec2 a, ImVec2 b) {
        float dx = a.x - b.x, dy = a.y - b.y;
        return sqrtf(dx * dx + dy * dy);
    }

    // Point-in-convex-quad using cross products (assumes CW or CCW winding)
    static bool isPointInQuad(ImVec2 p, ImVec2 q[4]) {
        auto cross2d = [](ImVec2 o, ImVec2 a, ImVec2 b) {
            return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
        };
        float s0 = cross2d(q[0], q[1], p);
        float s1 = cross2d(q[1], q[2], p);
        float s2 = cross2d(q[2], q[3], p);
        float s3 = cross2d(q[3], q[0], p);
        return (s0 >= 0 && s1 >= 0 && s2 >= 0 && s3 >= 0)
            || (s0 <= 0 && s1 <= 0 && s2 <= 0 && s3 <= 0);
    }

    static ImVec2 quadCenter(ImVec2 q[4]) {
        return ImVec2((q[0].x + q[1].x + q[2].x + q[3].x) * 0.25f,
                      (q[0].y + q[1].y + q[2].y + q[3].y) * 0.25f);
    }

    // IM_COL32(R,G,B,A) = (A<<24)|(B<<16)|(G<<8)|R
    static ImU32 brighten(ImU32 col, int amount) {
        int r = (col >>  0) & 0xFF;
        int g = (col >>  8) & 0xFF;
        int b = (col >> 16) & 0xFF;
        int a = (col >> 24) & 0xFF;
        r = std::min(255, r + amount);
        g = std::min(255, g + amount);
        b = std::min(255, b + amount);
        return IM_COL32(r, g, b, a);
    }
};
