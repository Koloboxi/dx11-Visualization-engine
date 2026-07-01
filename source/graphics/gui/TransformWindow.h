#pragma once
#include "../imgui/imgui.h"
#include "../scene/scene.h"
#include "../scene/scene_service.h"
#include "SEMWindow.h"
#include <DirectXMath.h>
#include <cmath>

// Floating editor shown whenever exactly one primitive is selected. For an
// ordinary primitive it edits position + Euler angles (and tracks gizmo drags
// live); for a clip-plane rectangle it instead edits the plane normal + d.
namespace TransformWindow {

using namespace DirectX;

// Euler extraction matching XMQuaternionRotationRollPitchYaw(pitch,yaw,roll):
// returns (pitch about X, yaw about Y, roll about Z) in radians, so feeding the
// result back through Primitive::SetRotation(XMFLOAT3) reproduces the rotation.
inline XMFLOAT3 QuatToEuler(const XMFLOAT4& q) {
    XMFLOAT4X4 R;
    XMStoreFloat4x4(&R, XMMatrixRotationQuaternion(XMLoadFloat4(&q)));
    float s = -R._32;
    if (s > 1.0f) s = 1.0f; if (s < -1.0f) s = -1.0f;
    float pitch = asinf(s);
    float yaw, roll;
    if (std::fabs(s) < 0.99999f) {
        yaw  = atan2f(R._31, R._33);
        roll = atan2f(R._12, R._22);
    } else {
        // Gimbal lock: fold roll into yaw.
        yaw  = atan2f(-R._13, R._11);
        roll = 0.0f;
    }
    return XMFLOAT3(pitch, yaw, roll);
}

inline bool QuatDiffers(const XMFLOAT4& a, const XMFLOAT4& b) {
    return std::fabs(a.x - b.x) > 1e-5f || std::fabs(a.y - b.y) > 1e-5f ||
           std::fabs(a.z - b.z) > 1e-5f || std::fabs(a.w - b.w) > 1e-5f;
}

inline void Draw(Scene& scene, bool& blockMousePick) {
    if (scene.controllerSelected) return;

    Primitive* sel = nullptr;
    int count = 0;
    for (Primitive* p : scene.primitives)
        if (p->selected) { sel = p; if (++count > 1) break; }
    if (count != 1 || !sel) return;

    SemSessionNS::SemSession& S = SEMWindow::Session();
    ClipPlaneNode* plane = S.FindClipPlaneByRect(sel);

    ImGui::SetNextWindowSize(ImVec2(260, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Transform");

    if (plane) {
        ImGui::TextDisabled("Clip plane");
        XMFLOAT4 pl = plane->GetPlane();
        bool changed = false;
        ImGui::SetNextItemWidth(200);
        changed |= ImGui::DragFloat3("normal", &pl.x, 0.01f, -1.0e6f, 1.0e6f, "%.3f");
        ImGui::SetNextItemWidth(200);
        changed |= ImGui::DragFloat("d", &pl.w, 0.5f, -1.0e6f, 1.0e6f, "%.3f");
        if (changed) {
            // Re-normalise the (just-edited) normal here. SetPlane interprets w as
            // the distance for a *unit* normal and divides by |n|; feeding it the
            // de-normalised vector that editing one component produces would rescale
            // d every frame, sliding the plane smoothly toward an intermediate spot.
            XMVECTOR n = XMVectorSet(pl.x, pl.y, pl.z, 0.0f);
            float len = XMVectorGetX(XMVector3Length(n));
            if (len > 1e-9f) { pl.x /= len; pl.y /= len; pl.z /= len; }
            plane->SetPlane(pl);
            if (plane->rect) plane->rect->MarkManuallyMoved();
            scene.orientationTransformer.Update();
            if (S.AnyClipMirror()) S.RebuildClipMirrors(scene);
        }
        ImGui::TextDisabled("Kept side: +normal (soft red).");
    } else {
        static UINT  sId = 0;
        static XMFLOAT4 sQuat{ 0, 0, 0, 1 };
        static float sEul[3] = { 0, 0, 0 };

        XMFLOAT4 q = sel->GetRotation();
        if (sel->id != sId || QuatDiffers(q, sQuat)) {
            XMFLOAT3 e = QuatToEuler(q);
            sEul[0] = e.x; sEul[1] = e.y; sEul[2] = e.z;
            sId = sel->id; sQuat = q;
        }

        XMFLOAT3 pos = sel->GetPosition();
        ImGui::SetNextItemWidth(200);
        if (ImGui::DragFloat3("pos", &pos.x, 0.5f, -1.0e9f, 1.0e9f, "%.3f")) {
            sel->SetPosition(pos);
            sel->MarkManuallyMoved();
            scene.orientationTransformer.Update();
        }

        float deg[3] = { XMConvertToDegrees(sEul[0]), XMConvertToDegrees(sEul[1]), XMConvertToDegrees(sEul[2]) };
        ImGui::SetNextItemWidth(200);
        if (ImGui::DragFloat3("euler XYZ", deg, 0.5f, -360.0f, 360.0f, "%.2f")) {
            sEul[0] = XMConvertToRadians(deg[0]);
            sEul[1] = XMConvertToRadians(deg[1]);
            sEul[2] = XMConvertToRadians(deg[2]);
            sel->SetRotation(XMFLOAT3(sEul[0], sEul[1], sEul[2]));
            sQuat = sel->GetRotation();
            scene.orientationTransformer.Update();
        }
        ImGui::TextDisabled("Degrees; X pitch, Y yaw, Z roll.");

        // Thick lines (dim==1) pick a named width from the scene's line styles.
        if (sel->GetDimension() == 1 && !scene.lineStyles.empty()) {
            ImGui::Separator();
            if (sel->lineStyle < 0 || sel->lineStyle >= (int)scene.lineStyles.size())
                sel->lineStyle = 0;
            ImGui::SetNextItemWidth(200);
            if (ImGui::BeginCombo("line style", scene.lineStyles[sel->lineStyle].name.c_str())) {
                for (int i = 0; i < (int)scene.lineStyles.size(); ++i) {
                    bool s = (sel->lineStyle == i);
                    char lbl[64];
                    snprintf(lbl, sizeof(lbl), "%s (%.4f)", scene.lineStyles[i].name.c_str(),
                             scene.lineStyles[i].thickness);
                    if (ImGui::Selectable(lbl, s)) sel->lineStyle = i;
                    if (s) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("Line-thickness preset used to draw this line. Edit the widths\n"
                                  "in the top-strip line-styles popup.");
        }
    }

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows))
        blockMousePick = true;
    ImGui::End();
}

}
