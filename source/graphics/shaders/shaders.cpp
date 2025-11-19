#include "shaders.h"

bool VertexShader::Initialize(ID3D11Device* device, std::wstring shaderpath, D3D11_INPUT_ELEMENT_DESC* layoutDesc, UINT numElements) {
	HRESULT result = D3DReadFileToBlob(shaderpath.c_str(), this->shader_buffer.GetAddressOf());
	if (FAILED(result)) {
		ErrorLogger::Log(result, L"Failed to load shader: " + shaderpath);
		return false;
	}

	result = device->CreateVertexShader(this->shader_buffer->GetBufferPointer(), this->shader_buffer->GetBufferSize(), NULL, this->shader.GetAddressOf());
	if (FAILED(result)) {
		ErrorLogger::Log(result, L"Failed to create vertex shader: " + shaderpath);
		return false;
	}

	if(layoutDesc){
		result = device->CreateInputLayout(layoutDesc, numElements, this->shader_buffer->GetBufferPointer(), this->shader_buffer->GetBufferSize(), this->inputLayout.GetAddressOf());
		if (FAILED(result)) {
			ErrorLogger::Log(result, "Failed to create input layout. i'm here");
			return false;
		}
	}

	return true;
}

ID3D11VertexShader* VertexShader::GetShader() { return this->shader.Get();}
ID3D10Blob* VertexShader::GetBuffer() { return this->shader_buffer.Get(); }

ID3D11InputLayout* VertexShader::GetInputLayout()
{
	return this->inputLayout.Get();
}


bool PixelShader::Initialize(ID3D11Device* device, std::wstring shaderpath) {
	HRESULT result = D3DReadFileToBlob(shaderpath.c_str(), this->shader_buffer.GetAddressOf());
	if (FAILED(result)) {
		ErrorLogger::Log(result, L"Failed to load shader: " + shaderpath);
		return false;
	}

	result = device->CreatePixelShader(this->shader_buffer.Get()->GetBufferPointer(), this->shader_buffer.Get()->GetBufferSize(), NULL, this->shader.GetAddressOf());
	if (FAILED(result)) {
		ErrorLogger::Log(result, L"Failed to create pixel shader: " + shaderpath);
		return false;
	}

	return true;
}

ID3D11PixelShader* PixelShader::GetShader() {
	return this->shader.Get();
}
ID3D10Blob* PixelShader::GetBuffer() {
	return this->shader_buffer.Get();
}


bool GeometryShader::Initialize(ID3D11Device* device, std::wstring shaderpath)
{
	HRESULT result = D3DReadFileToBlob(shaderpath.c_str(), this->shader_buffer.GetAddressOf());
	if (FAILED(result)) {
		ErrorLogger::Log(result, L"Failed to load shader: " + shaderpath);
		return false;
	}

	result = device->CreateGeometryShader(this->shader_buffer.Get()->GetBufferPointer(), this->shader_buffer.Get()->GetBufferSize(), NULL, this->shader.GetAddressOf());
	if (FAILED(result)) {
		ErrorLogger::Log(result, L"Failed to create pixel shader: " + shaderpath);
		return false;
	}

	return true;
}

ID3D11GeometryShader* GeometryShader::GetShader()
{
	return this->shader.Get();
}

ID3D10Blob* GeometryShader::GetBuffer()
{
	return this->shader_buffer.Get();
}
