#pragma once
#ifndef VertexBuffer_h__
#define VertexBuffer_h__
#endif
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include <vector>
#include "..\..\utils\errorLogger.h"

template<class T>
class VertexBuffer 
{
private:
	std::vector<T> data{};
	Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
	std::shared_ptr<UINT> stride;
	UINT bufferSize = 0;

public:
	VertexBuffer() {}

	VertexBuffer(const VertexBuffer<T>& rhs) {
		this->buffer = rhs.buffer;
		this->bufferSize = rhs.bufferSize;
		this->stride = rhs.stride;
	}
	VertexBuffer<T>& operator=(const VertexBuffer<T>& a) {
		this->buffer = a.buffer;
		this->bufferSize = a.bufferSize;
		this->stride = a.stride;
		return *this;
	}

	ID3D11Buffer* Get() const {
		return this->buffer.Get();
	}

	ID3D11Buffer* const* GetAddressOf() const {
		return this->buffer.GetAddressOf();
	}

	UINT GetBufferSize()const {
		return bufferSize;
	}

	const UINT GetStride() const {
		return *this->stride.get();
	}

	const UINT* GetStridePtr()const {
		return this->stride.get();
	}

	std::vector<T> GetData()const {
		return this->data;
	}

	HRESULT Initialize(ID3D11Device* device, T* data, UINT& numVertices) {
		if (this->buffer.Get() != nullptr)
			this->buffer.Reset(); 
		this->bufferSize = numVertices;
		
		if(this->stride.get() == nullptr)
			this->stride = std::make_shared<UINT>(sizeof(T));

		D3D11_BUFFER_DESC vertexBufferDesc{};
		vertexBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		vertexBufferDesc.ByteWidth = sizeof(T) * numVertices;
		vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vertexBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		vertexBufferDesc.MiscFlags = 0;

		D3D11_SUBRESOURCE_DATA vertexBufferData;
		ZeroMemory(&vertexBufferData, sizeof(vertexBufferData));
		vertexBufferData.pSysMem = data;

		this->data.resize(numVertices);
		std::copy(data, data + numVertices, this->data.data());

		HRESULT hr = device->CreateBuffer(&vertexBufferDesc, &vertexBufferData, this->buffer.GetAddressOf());
		return hr;
	}
};