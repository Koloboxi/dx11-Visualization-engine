#pragma once
// NavGizmo — a 3D orientation widget pinned in the top-right corner that drives
// the camera (it does NOT move scene objects, unlike OrientationTransformer,
// which it is modelled on: source/graphics/scene/auxiliaryObjects.{h,cpp}).
//
//   - axes (X/Y/Z)  : drawn, inert.
//   - arcs (X/Y/Z)  : drag to rotate the camera about that world axis.
//   - faces         : flat quads in the three coordinate planes; click snaps the
//                     camera normal to that plane (up = nearest cardinal to the
//                     current up).
//   - centre ball   : click → isometric; click again without moving the camera →
//                     dimetric (toggles), honouring scene.projUpAxis / projUpSign.
//
// Rendering and ID-picking are driven from Scene::Draw, which sets the pipeline
// state and a fixed corner viewport, then calls Draw()/DrawID(). The gizmo
// reflects the live orientation via GizmoView(camView); it is always drawn.

#include "..\imgui\imgui.h"
#include "..\scene\primitives\primitive.h"
#include "..\misc\Colors.h"
#include "..\camera\Camera.h"
#include "..\camera\Projections.h"
#include <vector>
#include <cmath>
#include <algorithm>

class NavGizmo {
public:
    // Square corner-viewport size (px) and the margins placing it under the menu
    // bar in the top-right corner. NavCube's control panel aligns to these.
    static constexpr float VIEW_PX     = 150.f;
    static constexpr float TOP_MARGIN  = 30.f;
    static constexpr float RIGHT_MARGIN = 4.f;

    // Distinct ID range (transformer occupies MAXUINT..MAXUINT-6).
    static constexpr UINT ID_BALL = MAXUINT - 10;
    static constexpr UINT ID_XAX  = MAXUINT - 11, ID_YAX = MAXUINT - 12, ID_ZAX = MAXUINT - 13;
    static constexpr UINT ID_XROT = MAXUINT - 14, ID_YROT = MAXUINT - 15, ID_ZROT = MAXUINT - 16;
    static constexpr UINT ID_FYZ  = MAXUINT - 17;  // quad in YZ plane (normal X)
    static constexpr UINT ID_FXZ  = MAXUINT - 18;  // quad in XZ plane (normal Y)
    static constexpr UINT ID_FXY  = MAXUINT - 19;  // quad in XY plane (normal Z)

    void Initialize(ID3D11Device* device, ID3D11DeviceContext* deviceContext) {
        const float axisLength = 108.f, axisRadius = 3.6f, ballRadius = 10.8f;  // ~1.2x larger geometry
        const XMFLOAT3 O = BaseVectors::ORIGIN;

        xAx = PrimitiveConstructor::Line3d(axisRadius, { O, XMFLOAT3(axisLength,0,0) }, 12, Colors::RED,   ID_XAX);
        yAx = PrimitiveConstructor::Line3d(axisRadius, { O, XMFLOAT3(0,axisLength,0) }, 12, Colors::GREEN, ID_YAX);
        zAx = PrimitiveConstructor::Line3d(axisRadius, { O, XMFLOAT3(0,0,axisLength) }, 12, Colors::BLUE,  ID_ZAX);

        oBall = PrimitiveConstructor::Sphere(ballRadius, O, 2, Colors::WHITE, ID_BALL);

        xRot = PrimitiveConstructor::Arc3d(axisLength * .72f, axisRadius, 90.f, O, 32, Colors::RED,   ID_XROT);
        xRot->SetRotationZero({ 0, -XM_PIDIV2, 0 });
        yRot = PrimitiveConstructor::Arc3d(axisLength * .72f, axisRadius, 90.f, O, 32, Colors::GREEN, ID_YROT);
        yRot->SetRotationZero({ XM_PIDIV2, 0, 0 });
        zRot = PrimitiveConstructor::Arc3d(axisLength * .72f, axisRadius, 90.f, O, 32, Colors::BLUE,  ID_ZROT);

        const XMFLOAT4 faceCol(0.78f, 0.80f, 0.88f, 1.0f);
        fXY = MakeFace(2, axisLength, faceCol, ID_FXY);  // normal Z
        fXZ = MakeFace(1, axisLength, faceCol, ID_FXZ);  // normal Y
        fYZ = MakeFace(0, axisLength, faceCol, ID_FYZ);  // normal X

        auxObjects = { xAx, yAx, zAx, oBall, xRot, yRot, zRot, fXY, fXZ, fYZ };
    }

    void UpdateLighting(float ambient, float intensity, float shininess, bool sh) {
        for (Primitive* o : auxObjects) { o->SetLighting(ambient, intensity, shininess); o->SetSmoothShading(sh); }
    }

    std::vector<UINT> GetIDs() const {
        return { ID_XAX, ID_YAX, ID_ZAX, ID_BALL, ID_XROT, ID_YROT, ID_ZROT, ID_FXY, ID_FXZ, ID_FYZ };
    }

    void Draw(const XMMATRIX& view, const XMMATRIX& proj) {
        for (Primitive* o : auxObjects) o->Draw(view, proj);
    }
    void DrawID(const XMMATRIX& view, const XMMATRIX& proj, UINT id) {
        for (Primitive* o : auxObjects) if (o->id == id) { o->Draw(view, proj); break; }
    }

    // Fixed orthographic projection + view (camera rotation only) that keeps the
    // gizmo a constant size in its corner while reflecting the live orientation.
    static XMMATRIX GizmoView(const XMMATRIX& camView) {
        XMMATRIX r = camView;
        r.r[3] = XMVectorSet(0, 0, 0, 1);                  // drop the camera translation
        return XMMatrixMultiply(r, XMMatrixTranslation(0, 0, -400.f));
    }
    static XMMATRIX GizmoProj() {
        return XMMatrixOrthographicRH(260.f, 260.f, 100.f, 700.f);
    }

    bool HasActiveObject() const { return m_activeArc != 0; }

    void HandleObjPress(UINT id, Camera& cam) {
        m_activeArc = 0;
        switch (id) {
            case ID_XROT: m_activeArc = id; m_activeAxis = XMVectorSet(1,0,0,0); m_prevValid = false; break;
            case ID_YROT: m_activeArc = id; m_activeAxis = XMVectorSet(0,1,0,0); m_prevValid = false; break;
            case ID_ZROT: m_activeArc = id; m_activeAxis = XMVectorSet(0,0,1,0); m_prevValid = false; break;
            case ID_FXY:  SnapToPlane(cam, XMVectorSet(0,0,1,0)); break;
            case ID_FXZ:  SnapToPlane(cam, XMVectorSet(0,1,0,0)); break;
            case ID_FYZ:  SnapToPlane(cam, XMVectorSet(1,0,0,0)); break;
            case ID_BALL: BallClick(cam); break;
            default: break;   // axes: inert
        }
    }

    void HandleObjMove(int px, int py, int winW, int winH, Camera& cam) {
        if (!m_activeArc) return;
        ImVec2 ctr = CenterPx(winW);
        float pvx = m_prevPx - ctr.x, pvy = m_prevPy - ctr.y;
        if (!m_prevValid || (pvx*pvx + pvy*pvy) < 1.f) { m_prevPx = (float)px; m_prevPy = (float)py; m_prevValid = true; return; }
        float cvx = px - ctr.x, cvy = py - ctr.y;
        if ((cvx*cvx + cvy*cvy) < 1.f) return;

        // Signed angle swept around the gizmo centre (screen y is down).
        float cross = pvx * cvy - pvy * cvx;
        float dot   = pvx * cvx + pvy * cvy;
        float angle = std::atan2(cross, dot);

        // Keep the felt direction consistent whether the axis points toward or
        // away from the viewer (mirrors OrientationTransformer's arc handling).
        XMVECTOR camFwd = cam.GetForwardVector();
        if (XMVectorGetX(XMVector3Dot(m_activeAxis, camFwd)) > 0.f) angle = -angle;

        cam.AdjustRotation(XMMatrixRotationAxis(m_activeAxis, angle));

        m_prevPx = (float)px; m_prevPy = (float)py;
    }

    void HandleObjRelease() { m_activeArc = 0; m_prevValid = false; }

    // Iso/dim presets honouring the up-axis & sign from the projection popup.
    static XMMATRIX Iso(int upAxis, int upSign) { return Projections::ISO * UpAlign(upAxis, upSign); }
    static XMMATRIX Dim(int upAxis, int upSign) { return Projections::DIM * UpAlign(upAxis, upSign); }

    void SetProjection(int upAxis, int upSign) { m_upAxis = upAxis; m_upSign = upSign; }

private:
    Primitive *oBall{}, *xAx{}, *yAx{}, *zAx{}, *xRot{}, *yRot{}, *zRot{}, *fXY{}, *fXZ{}, *fYZ{};
    std::vector<Primitive*> auxObjects;

    UINT     m_activeArc = 0;
    XMVECTOR m_activeAxis{};
    float    m_prevPx = 0.f, m_prevPy = 0.f;
    bool     m_prevValid = false;

    int      m_upAxis = 2, m_upSign = 1;

    // Ball iso/dim toggle state.
    XMFLOAT4X4 m_lastBallRot{};
    bool       m_ballArmed = false;
    int        m_ballMode  = 0;   // 0 = iso, 1 = dim

    static ImVec2 CenterPx(int winW) {
        float x = winW - RIGHT_MARGIN - VIEW_PX * 0.5f;
        float y = TOP_MARGIN + VIEW_PX * 0.5f;
        return ImVec2(x, y);
    }

    // A flat square quad in coordinate plane `planeAxis` (0=YZ,1=XZ,2=XY), placed
    // in the positive octant between the two spanning axes.
    Primitive* MakeFace(int planeAxis, float axisLength, const XMFLOAT4& col, UINT id) {
        float c = axisLength * 0.42f, h = axisLength * 0.20f;
        auto P = [&](float u, float v) -> XMFLOAT3 {
            // u,v live in the plane; map to world depending on the plane normal axis.
            if (planeAxis == 2) return XMFLOAT3(u, v, 0);   // XY plane, normal Z
            if (planeAxis == 1) return XMFLOAT3(u, 0, v);   // XZ plane, normal Y
            return XMFLOAT3(0, u, v);                        // YZ plane, normal X
        };
        XMFLOAT3 v0 = P(c-h, c-h), v1 = P(c+h, c-h), v2 = P(c+h, c+h), v3 = P(c-h, c+h);
        std::vector<XMFLOAT3> poses = { v0, v1, v2, v0, v2, v3 };
        std::vector<XMFLOAT4> cols(6, col);
        Primitive* p = PrimitiveConstructor::ColoredTriangles(poses, cols, id, false);
        p->SetUseVertexColor(false);
        p->SetColor(col);
        p->SetIlluminationCapability(false);   // flat, uniformly visible from both sides
        return p;
    }

    // Build a camera rotation matrix that looks along `fwd` with up `up`, matching
    // the Camera conventions: row0 = world-left, row1 = -world-forward, row2 = up.
    // The engine's orientation matrices (Projections::ISO/DIM/XY/YZ...) are improper
    // (det = -1): they use row0 = forward × up. We must follow the SAME handedness,
    // otherwise the camera rotMatrix becomes a mirror of the rest of the engine and
    // mouse drag-rotation feels inverted until the ball (which uses ISO/DIM) resets it.
    static XMMATRIX BuildRot(XMVECTOR fwd, XMVECTOR up) {
        XMVECTOR f = XMVector3Normalize(fwd);
        XMVECTOR u = XMVector3Normalize(XMVectorSubtract(up, XMVectorScale(f, XMVectorGetX(XMVector3Dot(f, up)))));
        XMVECTOR l = XMVector3Normalize(XMVector3Cross(f, u));   // engine convention: row0 = forward × up
        XMMATRIX m;
        m.r[0] = XMVectorSetW(l, 0.f);
        m.r[1] = XMVectorSetW(XMVectorNegate(f), 0.f);
        m.r[2] = XMVectorSetW(u, 0.f);
        m.r[3] = XMVectorSet(0, 0, 0, 1);
        return m;
    }

    // Snap the camera to look perpendicular at the plane with normal N, from the
    // side it is currently nearer; up = the cardinal axis (of the 4 perpendicular
    // to N) closest to the current camera up.
    void SnapToPlane(Camera& cam, XMVECTOR N) {
        XMVECTOR curFwd = cam.GetForwardVector();
        XMVECTOR curUp  = cam.GetUpwardVector();
        float side = (XMVectorGetX(XMVector3Dot(curFwd, N)) >= 0.f) ? 1.f : -1.f;
        XMVECTOR fwd = XMVectorScale(N, side);

        const XMVECTOR cardinals[6] = {
            XMVectorSet(1,0,0,0), XMVectorSet(-1,0,0,0),
            XMVectorSet(0,1,0,0), XMVectorSet(0,-1,0,0),
            XMVectorSet(0,0,1,0), XMVectorSet(0,0,-1,0),
        };
        XMVECTOR best = XMVectorSet(0,0,1,0); float bestDot = -2.f;
        for (const XMVECTOR& c : cardinals) {
            if (std::fabs(XMVectorGetX(XMVector3Dot(c, fwd))) > 0.99f) continue;  // not perpendicular
            float d = XMVectorGetX(XMVector3Dot(c, curUp));
            if (d > bestDot) { bestDot = d; best = c; }
        }
        cam.SetRotation(BuildRot(fwd, best));
        m_ballArmed = false;
    }

    void BallClick(Camera& cam) {
        XMFLOAT4X4 cur; XMStoreFloat4x4(&cur, cam.GetRotMatrix());
        if (m_ballArmed && MatNear(cur, m_lastBallRot)) m_ballMode = 1 - m_ballMode;
        else                                            m_ballMode = 0;   // fresh → iso
        XMMATRIX rot = (m_ballMode == 0) ? Iso(m_upAxis, m_upSign) : Dim(m_upAxis, m_upSign);
        cam.SetRotation(rot);
        XMStoreFloat4x4(&m_lastBallRot, cam.GetRotMatrix());
        m_ballArmed = true;
    }

    static bool MatNear(const XMFLOAT4X4& a, const XMFLOAT4X4& b) {
        const float* pa = &a._11; const float* pb = &b._11;
        for (int i = 0; i < 16; ++i) if (std::fabs(pa[i] - pb[i]) > 1e-4f) return false;
        return true;
    }

    // Minimal rotation taking +Z to the desired up axis (so ISO/DIM, authored
    // with +Z up, can be re-pointed at any up axis & sign).
    static XMMATRIX UpAlign(int upAxis, int upSign) {
        XMVECTOR z = XMVectorSet(0, 0, 1, 0);
        XMVECTOR u = (upAxis == 0) ? XMVectorSet(1,0,0,0)
                   : (upAxis == 1) ? XMVectorSet(0,1,0,0)
                                   : XMVectorSet(0,0,1,0);
        u = XMVectorScale(u, (float)((upSign < 0) ? -1 : 1));
        float d = XMVectorGetX(XMVector3Dot(z, u));
        if (d > 0.9999f)  return XMMatrixIdentity();
        if (d < -0.9999f) return XMMatrixRotationAxis(XMVectorSet(1,0,0,0), XM_PI);
        XMVECTOR axis = XMVector3Normalize(XMVector3Cross(z, u));
        return XMMatrixRotationAxis(axis, std::acos(d));
    }
};
