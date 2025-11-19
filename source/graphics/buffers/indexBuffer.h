#pragma once
#ifndef IndicesBuffer_h__
#define IndicesBuffer_h__
#endif 
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include "..\..\utils\errorLogger.h"

class IndexBuffer
{
private:
	IndexBuffer(const IndexBuffer& rhs);

private:
	std::vector<DWORD> data{};
	Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
	UINT bufferSize = 0;
public:
	IndexBuffer(){}

	ID3D11Buffer* Get() const {
		return buffer.Get();
	}

	ID3D11Buffer* const* GetAddressOf() const {
		return buffer.GetAddressOf();
	}

	UINT GetBufferSize()const {
		return bufferSize;
	}

	std::vector<DWORD> GetData()const {
		return this->data;
	}

	HRESULT Initialize(ID3D11Device* device, DWORD* data, UINT& numIndices) {
		if (buffer.Get() != nullptr)
			buffer.Reset();

		this->bufferSize = numIndices;

		D3D11_BUFFER_DESC indexBufferDesc;
		ZeroMemory(&indexBufferDesc, sizeof(indexBufferDesc));

		indexBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		indexBufferDesc.ByteWidth = sizeof(DWORD) * numIndices;
		indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		indexBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		indexBufferDesc.MiscFlags = 0;

		D3D11_SUBRESOURCE_DATA indexBufferData;
		ZeroMemory(&indexBufferData, sizeof(indexBufferData));
		indexBufferData.pSysMem = data;

		this->data.resize(numIndices);
		std::copy(data, data + numIndices, this->data.data());

		HRESULT hr = device->CreateBuffer(&indexBufferDesc, &indexBufferData, this->buffer.GetAddressOf());
		return hr;
	}
};
