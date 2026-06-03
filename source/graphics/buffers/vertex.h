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

	Vertex(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT4& vcolor)
		: pos(position), normal(0, 0, 0), color(vcolor) {
	}

	DirectX::XMFLOAT3 pos;
	DirectX::XMFLOAT3 normal;
	DirectX::XMFLOAT4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
};