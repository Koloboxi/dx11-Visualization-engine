#pragma once
#include <DirectXMath.h>

struct Vertex
{
	Vertex(){}
	Vertex(float x, float y, float z)
		: pos(x, y, z), normal(0, 0, 0){}
	Vertex(const DirectX::XMFLOAT3& position)
		: pos(position), normal(0, 0, 0) {
	}

	Vertex(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& normal)
		: pos(position), normal(normal) {
	}

	DirectX::XMFLOAT3 pos;
	DirectX::XMFLOAT3 normal;
};