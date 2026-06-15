#pragma once
#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include "../../utils/errorLogger.h"

struct CB_VS_vertexshader
{
	DirectX::XMMATRIX view;
	DirectX::XMMATRIX world;
	DirectX::XMMATRIX projection;
};

struct CB_GS_geometryshader
{
	float AspectRatio;
	float Thickness = 0.001f;
	float pad[2]    = { 0, 0 };
};

// Global section / clip-plane state, bound once per frame on VS slot b1.
// Geometry is kept where dot(worldPos, planeNormal) - planeD >= 0.
struct CB_VS_section
{
	float planeNormal[4] = { 1, 0, 0, 0 };
	float planeD         = 0.0f;
	int   enabled        = 0;
	float pad[2]         = { 0, 0 };
};

struct CB_PS_pixelshader
{
	float color[4] = { 1, 1, 0, 1 };
	float ambient;
	float intensity;
	float shininess;
	bool  illuminated;
	int   useVertexColor = 0;
	int   twoSided       = 0;
	float pad[2]         = { 0, 0 };
	float backColor[4]   = { 1, 0, 0, 1 };
};

struct CB_PS_pixelshaderOutline
{
	float outlineColor[4];
	float screenSize[2];
	float outlineScale;
	float pad;
};

struct CB_PS_id
{
	UINT id;
	float pad[3];
};

template<class T>
class ConstantBuffer
{
private:
	ConstantBuffer(const ConstantBuffer<T>& rhs);

private:
	Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
	ID3D11DeviceContext* deviceContext = nullptr;

public:
	ConstantBuffer(){}

	T data;

	ID3D11Buffer* Get()const {
		return buffer.Get();
	}

	ID3D11Buffer* const* GetAddressOf()const {
		return buffer.GetAddressOf();
	}

	HRESULT Initialize(ID3D11Device* device, ID3D11DeviceContext* deviceContext) {
		if (buffer.Get() != nullptr)
			buffer.Reset();

		this->deviceContext = deviceContext;

		D3D11_BUFFER_DESC desc;
		ZeroMemory(&desc, sizeof(desc));

		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = 0;
		desc.ByteWidth = static_cast<UINT>(sizeof(T) + (16 - (sizeof(T) % 16)));
		desc.StructureByteStride = 0;

		HRESULT hr = device->CreateBuffer(&desc, nullptr, buffer.GetAddressOf());
		return hr;
	}

	bool ApplyChanges() {
		D3D11_MAPPED_SUBRESOURCE mappedResource;
		HRESULT hr = this->deviceContext->Map(buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
		if (FAILED(hr)) {
			ErrorLogger::Log(hr, "Failed to map constant buffer.");
			return false;
		}
		CopyMemory(mappedResource.pData, &data, sizeof(T));
		this->deviceContext->Unmap(buffer.Get(), 0);
		return true;
	}
};