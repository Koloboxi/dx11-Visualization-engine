#pragma once
#include <DirectXMath.h>
#include <vector>
#include "buffers/vertex.h"
using namespace DirectX;

inline XMFLOAT3 operator+(XMFLOAT3 p1, XMFLOAT3 p2) {
	return XMFLOAT3(p1.x + p2.x, p1.y + p2.y, p1.z + p2.z);
}
inline XMFLOAT3 operator-(XMFLOAT3 p1, XMFLOAT3 p2) {
	return XMFLOAT3(p1.x - p2.x, p1.y - p2.y, p1.z - p2.z);
}
inline XMFLOAT3 operator*(const XMFLOAT3& v, float s) {
	return XMFLOAT3(v.x * s, v.y * s, v.z * s);
}
inline XMFLOAT3 operator/(XMFLOAT3 p, float d) {
	return XMFLOAT3(p.x / d, p.y / d, p.z / d);
}

inline void Subdivide(std::vector<Vertex>& vertices, std::vector<DWORD>& indices) {
	std::vector<DWORD> newIndices;
	newIndices.reserve(indices.size() * 4);

	for (size_t i = 0; i < indices.size(); i += 3) {
		DWORD i0 = indices[i];
		DWORD i1 = indices[i + 1];
		DWORD i2 = indices[i + 2];

		const Vertex& v0 = vertices[i0];
		const Vertex& v1 = vertices[i1];
		const Vertex& v2 = vertices[i2];

		Vertex m01, m12, m20;
		m01.pos.x = (v0.pos.x + v1.pos.x) * 0.5f;
		m01.pos.y = (v0.pos.y + v1.pos.y) * 0.5f;
		m01.pos.z = (v0.pos.z + v1.pos.z) * 0.5f;

		m12.pos.x = (v1.pos.x + v2.pos.x) * 0.5f;
		m12.pos.y = (v1.pos.y + v2.pos.y) * 0.5f;
		m12.pos.z = (v1.pos.z + v2.pos.z) * 0.5f;

		m20.pos.x = (v2.pos.x + v0.pos.x) * 0.5f;
		m20.pos.y = (v2.pos.y + v0.pos.y) * 0.5f;
		m20.pos.z = (v2.pos.z + v0.pos.z) * 0.5f;

		XMVECTOR posVec{};
		XMFLOAT3 posNorm{};

		posVec = XMLoadFloat3(&m01.pos);
		posVec = XMVector3Normalize(posVec);
		XMStoreFloat3(&posNorm, posVec);
		m01.normal = posNorm;

		posVec = XMLoadFloat3(&m12.pos);
		posVec = XMVector3Normalize(posVec);
		XMStoreFloat3(&posNorm, posVec);
		m12.normal = posNorm;

		posVec = XMLoadFloat3(&m20.pos);
		posVec = XMVector3Normalize(posVec);
		XMStoreFloat3(&posNorm, posVec);
		m20.normal = posNorm;

		vertices.push_back(m01);
		vertices.push_back(m12);
		vertices.push_back(m20);

		DWORD im01 = static_cast<DWORD>(vertices.size()) - 3;
		DWORD im12 = static_cast<DWORD>(vertices.size()) - 2;
		DWORD im20 = static_cast<DWORD>(vertices.size()) - 1;

		newIndices.push_back(i0);  newIndices.push_back(im01); newIndices.push_back(im20);
		newIndices.push_back(im01); newIndices.push_back(i1);   newIndices.push_back(im12);
		newIndices.push_back(im20); newIndices.push_back(im12); newIndices.push_back(i2);
		newIndices.push_back(im01); newIndices.push_back(im12); newIndices.push_back(im20);
	}

	indices = std::move(newIndices);
}

namespace math {
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
