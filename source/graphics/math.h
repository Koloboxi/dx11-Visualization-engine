#pragma once
#include <DirectXMath.h>
using namespace DirectX;

namespace math {

	inline XMFLOAT3 operator+(XMFLOAT3 p1, XMFLOAT3 p2) {
		return XMFLOAT3(p1.x + p2.x, p1.y + p2.y, p1.z + p2.z);
	}
	inline XMFLOAT3 operator/(XMFLOAT3 p, float d) {
		return XMFLOAT3(p.x / d, p.y / d, p.z / d);
	}

	inline XMFLOAT3 GetCenterOfMass(const std::vector<XMFLOAT3>& poses) {
		XMFLOAT3 sum{};
		for (XMFLOAT3 p : poses) {
			sum = sum + p;
		}
		return sum / poses.size();
	}

	inline XMFLOAT3 ComputeNormal(const XMFLOAT3& p0, const XMFLOAT3& p1, const XMFLOAT3& p2)
	{
		XMVECTOR v0 = XMLoadFloat3(&p0);
		XMVECTOR v1 = XMLoadFloat3(&p1);
		XMVECTOR v2 = XMLoadFloat3(&p2);

		XMVECTOR e1 = XMVectorSubtract(v1, v0);
		XMVECTOR e2 = XMVectorSubtract(v2, v0);

		XMVECTOR n = XMVector3Normalize(XMVector3Cross(e1, e2));

		XMFLOAT3 normal;
		XMStoreFloat3(&normal, n);
		return normal;
	}
}