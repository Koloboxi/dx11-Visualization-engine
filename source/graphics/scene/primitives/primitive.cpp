#include "primitive.h"

Primitive::Primitive(ID3D11Device* device, ID3D11DeviceContext* deviceContext)
{
	this->device = device;
	this->deviceContext = deviceContext;
	this->cb_vs_vertexshader.Initialize(this->device, this->deviceContext);
	this->cb_ps_pixelshader.Initialize(this->device, this->deviceContext);
	
	this->SetPosition(BaseVectors::ORIGIN);
	this->SetRotation(BaseVectors::ORIGIN);
}

HRESULT Primitive::SetVertexIndexBuffers(Vertex* vertexData, UINT vertexNumVertices, DWORD* indexData, UINT indexNumVertices, UCHAR dim)
{
	HRESULT hr;
	hr = this->vertexBuffer.Initialize(this->device, vertexData, vertexNumVertices);
	if (indexData) {
		hr = this->indexBuffer.Initialize(this->device, indexData, indexNumVertices);
	}
	this->dimension = dim;

	return hr;
}

void Primitive::Draw(const XMMATRIX& viewMatrix, const XMMATRIX& projectionMatrix)
{
	this->cb_vs_vertexshader.data.view = XMMatrixTranspose(viewMatrix);
	this->cb_vs_vertexshader.data.world = XMMatrixTranspose(this->worldMatrix);
	this->cb_vs_vertexshader.data.projection = XMMatrixTranspose(projectionMatrix);
	this->cb_vs_vertexshader.ApplyChanges();

	this->deviceContext->VSSetConstantBuffers(0, 1, this->cb_vs_vertexshader.GetAddressOf());
	this->deviceContext->PSSetConstantBuffers(0, 1, this->cb_ps_pixelshader.GetAddressOf());

	UINT offset = 0;
	this->deviceContext->IASetPrimitiveTopology(this->primitiveTopology);

	VertexBuffer<Vertex>* activeBuffer = 
		this->smoothShade || this->primitiveTopology == D3D10_PRIMITIVE_TOPOLOGY_LINESTRIP  || this->primitiveTopology == D3D10_PRIMITIVE_TOPOLOGY_POINTLIST ?
		&this->vertexBuffer : &this->vertexBufferFaces;
	this->deviceContext->IASetVertexBuffers(0, 1, activeBuffer->GetAddressOf(), activeBuffer->GetStridePtr(), &offset);

	if (this->smoothShade && this->primitiveTopology != D3D10_PRIMITIVE_TOPOLOGY_LINESTRIP && this->primitiveTopology != D3D10_PRIMITIVE_TOPOLOGY_POINTLIST) {
		this->deviceContext->IASetIndexBuffer(this->indexBuffer.Get(), DXGI_FORMAT::DXGI_FORMAT_R32_UINT, 0);
		this->deviceContext->DrawIndexed(this->indexBuffer.GetBufferSize(), 0, 0);
	}
	else {
		this->deviceContext->Draw(activeBuffer->GetBufferSize(), 0);
	}
}

void Primitive::SetColor(const XMFLOAT4& col)
{
	this->cb_ps_pixelshader.data.color[0] = col.x;
	this->cb_ps_pixelshader.data.color[1] = col.y;
	this->cb_ps_pixelshader.data.color[2] = col.z;
	this->cb_ps_pixelshader.data.color[3] = col.w;
	this->cb_ps_pixelshader.ApplyChanges();

	this->transparent = (col.w < 1.0f);
}	

void Primitive::SetIlluminationCapability(const bool l)
{
	this->illuminationCapability = l;
	this->cb_ps_pixelshader.data.illuminated = l;
	this->cb_ps_pixelshader.ApplyChanges();
}

void Primitive::SetLighting(const float ambient, const float intensity, float shininess)
{
	this->cb_ps_pixelshader.data.ambient = ambient;
	this->cb_ps_pixelshader.data.intensity = intensity;
	this->cb_ps_pixelshader.data.shininess = shininess;
	this->cb_ps_pixelshader.ApplyChanges();
}

void Primitive::SetSmoothShading(const bool sh)
{
	this->smoothShade = sh;
	if (this->smoothShade) return;
	if (this->vertexBufferFaces.GetBufferSize()) return;
	
	// generate buffer with face normals (without index buffer)
	if (!this->indexBuffer.GetBufferSize()) return;

	std::vector<Vertex> oldVertices = this->vertexBuffer.GetData();
	std::vector<DWORD> indices = this->indexBuffer.GetData();
	UINT numVertices = indices.size();

	std::vector<Vertex> newVertices(numVertices);
	for (DWORD i = 0; i < numVertices; i += 3) {
		Vertex v[3] = { oldVertices[indices[i]], oldVertices[indices[i+1]], oldVertices[indices[i+2]] };
		XMFLOAT3 normal = math::ComputeNormal(v[0].pos, v[1].pos, v[2].pos);

		for (Vertex& vertex : v) {
			vertex.normal = normal;
		}

		newVertices[i] = v[0];
		newVertices[i+1] = v[1];
		newVertices[i+2] = v[2];
	}

	this->vertexBufferFaces.Initialize(this->device, newVertices.data(), numVertices);
}

void Primitive::UpdateWorldMatrix()
{
	this->worldMatrix = XMMatrixScaling(this->scale, this->scale, this->scale);
	this->worldMatrix *= XMMatrixRotationRollPitchYaw(this->rot.x, this->rot.y, this->rot.z);
	this->worldMatrix *= XMMatrixTranslation(this->pos.x, this->pos.y, this->pos.z);
	XMMATRIX vecRotationMatrix = XMMatrixRotationRollPitchYaw(0.0f, this->rot.y, 0.0f);
}

const XMFLOAT3& Primitive::GetPosition() const
{
	return this->pos;
}

const XMFLOAT3& Primitive::GetRotation() const
{
	return this->rot;
}

const D3D10_PRIMITIVE_TOPOLOGY& Primitive::GetPrimitiveTopology() const
{
	return this->primitiveTopology;
}

const std::vector<Vertex> Primitive::GetVertexData() const
{
	return this->vertexBuffer.GetData();
}

const std::vector<DWORD> Primitive::GetIndexData() const
{
	return this->indexBuffer.GetData();
}

void Primitive::SetPosition(const XMFLOAT3& pos)
{
	this->pos = pos;
	this->UpdateWorldMatrix();
}

void Primitive::AdjustPosition(const XMFLOAT3& pos)
{
	this->pos.x += pos.y;
	this->pos.y += pos.y;
	this->pos.z += pos.z;
	this->UpdateWorldMatrix();
}

void Primitive::SetScale(const float s)
{
	this->scale = s;
	this->UpdateWorldMatrix();
}

void Primitive::SetScalable(const bool b)
{
	this->scalable = b;
}

void Primitive::SetProjectionScalability(bool f)
{
	this->scalable = f;
}

bool Primitive::ProjectionScalabe() const
{
	return this->scalable;
}

UCHAR Primitive::GetDimension() const
{
	return this->dimension;
}

void Primitive::SetRotation(const XMFLOAT3& rot)
{
	this->rot = rot;
	this->UpdateWorldMatrix();
}

void Primitive::AdjustRotation(const XMFLOAT3& rot)
{
	this->rot.x += rot.x;
	this->rot.y += rot.y;
	this->rot.z += rot.z;
	this->UpdateWorldMatrix();
}

void Primitive::SetPrimitiveTopology(const D3D10_PRIMITIVE_TOPOLOGY& top)
{
	this->primitiveTopology = top;
}

XMFLOAT4 Primitive::GetColor() const
{
	return XMFLOAT4(this->cb_ps_pixelshader.data.color);
}

bool Primitive::GetTransparent() const
{
	return this->transparent;
}

bool Primitive::GetIlluminationCapability() const
{
	return this->illuminationCapability;
}

const bool Primitive::GetScale() const
{
	return this->scale;
}

const bool Primitive::GetScalable() const
{
	return this->scalable;
}
