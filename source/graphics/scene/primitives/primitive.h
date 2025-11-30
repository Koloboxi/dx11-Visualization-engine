#pragma once
#include "..\..\buffers\vertex.h"
#include "..\..\buffers\vertexBuffer.h"
#include "..\..\buffers\indexBuffer.h"
#include "..\..\buffers\constantBuffer.h"

#include "..\..\math.h"

using namespace DirectX;

namespace BaseVectors {
	static const XMFLOAT3 ORIGIN = XMFLOAT3(0, 0, 0);
	static const XMFLOAT3 XVEC = XMFLOAT3(1, 0, 0);
	static const XMFLOAT3 YVEC = XMFLOAT3(0, 1, 0);
	static const XMFLOAT3 ZVEC = XMFLOAT3(0, 0, 1);
}

class Primitive
{
public:
	Primitive(ID3D11Device* device, ID3D11DeviceContext* deviceContext);
	HRESULT SetVertexIndexBuffers(Vertex* vertexData, UINT vertexNumVertices, DWORD* indexData, UINT indexNumVertices, UCHAR dim);

	UINT id{};

	void Draw(const XMMATRIX& viewMatrix, const XMMATRIX& projectionMatrix);
	bool selected = false;

	void SetColor(const XMFLOAT4& col);
	void SetIlluminationCapability(const bool l);
	void SetLighting(const float ambient, const float intensity, float shininess);
	void SetSmoothShading(const bool sh);

	void SetScale(const float s);
	void SetScalable(const bool b);

	void SetPosition(const XMFLOAT3& pos);
	void AdjustPosition(const XMFLOAT3& pos);
	void SetRotation(const XMFLOAT3& rot);
	void AdjustRotation(const XMFLOAT3& rot);

	void SetPrimitiveTopology(const D3D10_PRIMITIVE_TOPOLOGY& top);

	XMFLOAT4 GetColor() const;
	bool GetTransparent() const;
	bool GetIlluminationCapability() const;

	const bool GetScale() const;
	const bool GetScalable() const;

	const XMFLOAT3& GetPosition() const;
	const XMFLOAT3& GetRotation() const;

	const D3D10_PRIMITIVE_TOPOLOGY& GetPrimitiveTopology() const;
	const std::vector<Vertex> GetVertexData() const;
	const std::vector<DWORD> GetIndexData() const;

	void SetProjectionScalability(const bool f);
	bool ProjectionScalabe() const;

	UCHAR GetDimension() const;
private:
	XMMATRIX worldMatrix = XMMatrixIdentity();
	void UpdateWorldMatrix();

	ID3D11Device* device{};
	ID3D11DeviceContext* deviceContext{};
	 
	VertexBuffer<Vertex> vertexBuffer{};
	IndexBuffer indexBuffer{};
	VertexBuffer<Vertex> vertexBufferFaces{};

	D3D10_PRIMITIVE_TOPOLOGY primitiveTopology{};
	UCHAR dimension{};

	ConstantBuffer<CB_VS_vertexshader> cb_vs_vertexshader{};
	ConstantBuffer<CB_PS_pixelshader> cb_ps_pixelshader{};
	
	bool scalable = true;
	float scale = 1.0f;

	bool illuminationCapability;
	bool smoothShade = true;
	bool transparent;

	XMFLOAT3 pos{};
	XMFLOAT3 rot{};
	XMFLOAT4 col{};
};