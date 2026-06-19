#pragma once
#include "scene_node.h"
#include "primitives/primitive.h"

// Service nodes are scene objects that exist to drive the tool, not to be part of
// the modelled geometry: they wrap a pickable primitive and expose higher-level
// parameters extracted from that primitive's transform. They are flagged via
// SceneNode::IsService() so the tree / picking / windows can treat them apart
// from ordinary primitives.
class ServiceNode : public SceneNode {
public:
    bool IsService() const override { return true; }
};

// A clip half-space, edited by moving/rotating its visual rectangle with the
// orientation transformer. The plane is nx*x+ny*y+nz*z+d >= 0 (kept side =
// +normal). The normal is the rectangle's local +Z rotated into world space; d
// places the plane through the rectangle's position. Setting a (normal, d)
// positions and orients the rectangle accordingly.
class ClipPlaneNode : public ServiceNode {
public:
    Primitive* rect = nullptr;     // visual + pickable quad; a child of this node
    bool       showMirror = false; // "show mirror primitives" toggle for this plane

    XMFLOAT3 GetNormal() const {
        if (!rect) return XMFLOAT3(1, 0, 0);
        XMVECTOR q = XMLoadFloat4(&rect->GetRotation());
        XMVECTOR n = XMVector3Normalize(XMVector3Rotate(XMVectorSet(0, 0, 1, 0), q));
        XMFLOAT3 o; XMStoreFloat3(&o, n); return o;
    }

    float GetD() const {
        if (!rect) return 0.0f;
        XMFLOAT3 nf = GetNormal();
        XMVECTOR n = XMLoadFloat3(&nf);
        XMVECTOR p = XMLoadFloat3(&rect->GetPosition());
        return -XMVectorGetX(XMVector3Dot(n, p));
    }

    XMFLOAT4 GetPlane() const {
        XMFLOAT3 n = GetNormal();
        return XMFLOAT4(n.x, n.y, n.z, GetD());
    }

    void SetPlane(const XMFLOAT4& pl) {
        if (!rect) return;
        XMVECTOR n = XMVectorSet(pl.x, pl.y, pl.z, 0.0f);
        float len = XMVectorGetX(XMVector3Length(n));
        if (len < 1e-9f) { n = XMVectorSet(1, 0, 0, 0); len = 1.0f; }
        n = XMVectorScale(n, 1.0f / len);
        const float dUnit = pl.w / len;

        XMVECTOR q = QuatFromZTo(n);
        XMFLOAT4 qf; XMStoreFloat4(&qf, q);
        rect->SetRotation(qf);

        XMFLOAT3 pos; XMStoreFloat3(&pos, XMVectorScale(n, -dUnit));
        rect->SetPosition(pos);
    }

private:
    static XMVECTOR QuatFromZTo(XMVECTOR n) {
        XMVECTOR z = XMVectorSet(0, 0, 1, 0);
        float d = XMVectorGetX(XMVector3Dot(z, n));
        if (d > 0.99999f) return XMQuaternionIdentity();
        if (d < -0.99999f) return XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), XM_PI);
        XMVECTOR axis = XMVector3Normalize(XMVector3Cross(z, n));
        float angle = acosf(d);
        return XMQuaternionRotationAxis(axis, angle);
    }
};
