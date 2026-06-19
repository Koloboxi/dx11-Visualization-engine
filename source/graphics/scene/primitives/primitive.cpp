#include "primitive.h"
#include <algorithm>
#include <utility>

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

	if (this->psCBDirty) {
		this->cb_ps_pixelshader.ApplyChanges();
		this->psCBDirty = false;
	}
	this->deviceContext->PSSetConstantBuffers(0, 1, this->cb_ps_pixelshader.GetAddressOf());

	UINT offset = 0;
	this->deviceContext->IASetPrimitiveTopology(this->primitiveTopology);

	const bool isLine  = (this->primitiveTopology == D3D10_PRIMITIVE_TOPOLOGY_LINESTRIP);
	const bool isPoint = (this->primitiveTopology == D3D10_PRIMITIVE_TOPOLOGY_POINTLIST);
	const bool isMesh  = !isLine && !isPoint;

	VertexBuffer<Vertex>* activeBuffer =
		(isMesh && !this->smoothShade && this->vertexBufferFaces.GetBufferSize() > 0)
		? &this->vertexBufferFaces
		: &this->vertexBuffer;

	this->deviceContext->IASetVertexBuffers(0, 1, activeBuffer->GetAddressOf(), activeBuffer->GetStridePtr(), &offset);

	if (isMesh && this->smoothShade && this->indexBuffer.GetBufferSize() > 0) {
		this->deviceContext->IASetIndexBuffer(this->indexBuffer.Get(), DXGI_FORMAT::DXGI_FORMAT_R32_UINT, 0);
		this->deviceContext->DrawIndexed(this->indexBuffer.GetBufferSize(), 0, 0);
	}
	else {
		this->deviceContext->Draw(activeBuffer->GetBufferSize(), 0);
	}
}

void Primitive::AddColorSet(const std::string& name, const std::vector<XMFLOAT4>& gpuOrderColors)
{
	this->colorSets[name] = gpuOrderColors;
}

bool Primitive::HasColorSet(const std::string& name) const
{
	return this->colorSets.find(name) != this->colorSets.end();
}

bool Primitive::ActivateColorSet(const std::string& name)
{
	auto it = this->colorSets.find(name);
	if (it == this->colorSets.end()) return false;

	const std::vector<XMFLOAT4>& cols = it->second;
	std::vector<Vertex> verts = this->vertexBuffer.GetData();
	if (cols.size() != verts.size() || verts.empty()) return false;

	// Re-skin the smooth (indexed/non-indexed) buffer in place.
	for (size_t i = 0; i < verts.size(); ++i) verts[i].color = cols[i];
	this->vertexBuffer.Update(this->deviceContext, verts.data(), (UINT)verts.size());

	// Keep the flat-shading buffer consistent if it has been built: it expands
	// the vertex buffer through the index buffer, so each face vertex i takes the
	// colour of source vertex indices[i]. Positions/normals are left untouched.
	if (this->vertexBufferFaces.GetBufferSize() > 0 && this->indexBuffer.GetBufferSize() > 0) {
		std::vector<Vertex> faceVerts = this->vertexBufferFaces.GetData();
		std::vector<DWORD>  indices   = this->indexBuffer.GetData();
		for (size_t i = 0; i < indices.size() && i < faceVerts.size(); ++i)
			if (indices[i] < cols.size()) faceVerts[i].color = cols[indices[i]];
		this->vertexBufferFaces.Update(this->deviceContext, faceVerts.data(), (UINT)faceVerts.size());
	}

	this->activeColorSet = name;
	return true;
}

Primitive* Primitive::CloneMirrored(const XMFLOAT4& plane) const
{
	XMVECTOR N = XMVectorSet(plane.x, plane.y, plane.z, 0.0f);
	const float len = XMVectorGetX(XMVector3Length(N));
	if (len < 1e-9f) return nullptr;
	N = XMVectorScale(N, 1.0f / len);
	const float dUnit = plane.w / len;

	auto reflectPos = [&](const XMFLOAT3& p) {
		XMVECTOR v = XMLoadFloat3(&p);
		const float s = XMVectorGetX(XMVector3Dot(N, v)) + dUnit;
		XMFLOAT3 o; XMStoreFloat3(&o, XMVectorSubtract(v, XMVectorScale(N, 2.0f * s)));
		return o;
	};
	auto reflectDir = [&](const XMFLOAT3& d) {
		XMVECTOR v = XMLoadFloat3(&d);
		const float s = XMVectorGetX(XMVector3Dot(N, v));
		XMFLOAT3 o; XMStoreFloat3(&o, XMVectorSubtract(v, XMVectorScale(N, 2.0f * s)));
		return o;
	};

	const bool triList = (this->primitiveTopology == D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	auto reflectVerts = [&](std::vector<Vertex> verts, bool swapWinding) {
		for (Vertex& v : verts) { v.pos = reflectPos(v.pos); v.normal = reflectDir(v.normal); }
		if (swapWinding)
			for (size_t i = 0; i + 2 < verts.size(); i += 3) std::swap(verts[i + 1], verts[i + 2]);
		return verts;
	};

	Primitive* clone = new Primitive(this->device, this->deviceContext);

	std::vector<Vertex> ib_verts = this->vertexBuffer.GetData();
	std::vector<DWORD>  indices  = this->indexBuffer.GetData();
	const bool indexed = !indices.empty();

	// The winding lives in the vertex order for a non-indexed triangle list, and
	// in the index buffer for an indexed one — reverse it in whichever holds it.
	std::vector<Vertex> vbm = reflectVerts(std::move(ib_verts), triList && !indexed);
	if (triList && indexed)
		for (size_t i = 0; i + 2 < indices.size(); i += 3) std::swap(indices[i + 1], indices[i + 2]);

	clone->SetVertexIndexBuffers(vbm.data(), (UINT)vbm.size(),
	                             indices.empty() ? nullptr : indices.data(), (UINT)indices.size(),
	                             this->dimension);
	clone->SetPrimitiveTopology(this->primitiveTopology);

	// The flat-shading buffer, when built, is always an expanded triangle list.
	if (this->vertexBufferFaces.GetBufferSize() > 0) {
		std::vector<Vertex> fb = reflectVerts(this->vertexBufferFaces.GetData(), triList);
		UINT size = fb.size();
		clone->vertexBufferFaces.Initialize(this->device, fb.data(), size);
	}

	clone->cb_ps_pixelshader.data   = this->cb_ps_pixelshader.data;
	clone->psCBDirty                = true;
	clone->illuminationCapability   = this->illuminationCapability;
	clone->smoothShade              = this->smoothShade;
	clone->transparent              = this->transparent;

	// Per-vertex colour sets are stored in GPU vertex order; keep them aligned
	// with the (possibly winding-swapped) vertex buffer.
	for (const auto& kv : this->colorSets) {
		std::vector<XMFLOAT4> cols = kv.second;
		if (triList && !indexed)
			for (size_t i = 0; i + 2 < cols.size(); i += 3) std::swap(cols[i + 1], cols[i + 2]);
		clone->colorSets[kv.first] = std::move(cols);
	}
	clone->activeColorSet = this->activeColorSet;

	clone->SetScale(this->scale);
	clone->SetRotationZero(this->rotZero);
	clone->SetRotation(this->rotQuat);
	clone->SetPosition(this->pos);
	return clone;
}

void Primitive::SetColor(const XMFLOAT4& col)
{
	this->cb_ps_pixelshader.data.color[0] = col.x;
	this->cb_ps_pixelshader.data.color[1] = col.y;
	this->cb_ps_pixelshader.data.color[2] = col.z;
	this->cb_ps_pixelshader.data.color[3] = col.w;
	this->psCBDirty = true;
	this->transparent = (col.w < 1.0f);
}

void Primitive::SetAlpha(const float a)
{
	this->cb_ps_pixelshader.data.color[3] = a;
	this->psCBDirty = true;
	this->transparent = (a < 1.0f);
}

void Primitive::SetUseVertexColor(const bool v)
{
	this->cb_ps_pixelshader.data.useVertexColor = v ? 1 : 0;
	this->psCBDirty = true;
}

void Primitive::SetTwoSided(bool enable, const XMFLOAT4& backColor)
{
	this->cb_ps_pixelshader.data.twoSided      = enable ? 1 : 0;
	this->cb_ps_pixelshader.data.backColor[0]  = backColor.x;
	this->cb_ps_pixelshader.data.backColor[1]  = backColor.y;
	this->cb_ps_pixelshader.data.backColor[2]  = backColor.z;
	this->cb_ps_pixelshader.data.backColor[3]  = backColor.w;
	this->psCBDirty = true;
}

void Primitive::SetIlluminationCapability(const bool l)
{
	this->illuminationCapability = l;
	this->cb_ps_pixelshader.data.illuminated = l;
	this->psCBDirty = true;
}

void Primitive::SetLighting(const float ambient, const float intensity, const float shininess)
{
	this->cb_ps_pixelshader.data.ambient = ambient;
	this->cb_ps_pixelshader.data.intensity = intensity;
	this->cb_ps_pixelshader.data.shininess = shininess;
	this->psCBDirty = true;
}

void Primitive::SetSmoothShading(const bool sh)
{
	this->smoothShade = sh;
	if (this->smoothShade) return;
	if (this->vertexBufferFaces.GetBufferSize()) return;

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
	XMMATRIX rotMatrix = XMMatrixRotationQuaternion(XMLoadFloat4(&this->rotQuat));
	XMMATRIX rotZeroMatrix = XMMatrixRotationRollPitchYaw(this->rotZero.x, this->rotZero.y, this->rotZero.z);
	this->worldMatrix *= rotZeroMatrix * rotMatrix;
	this->worldMatrix *= XMMatrixTranslation(this->pos.x, this->pos.y, this->pos.z);
}

const XMFLOAT3& Primitive::GetPosition() const
{
	return this->pos;
}

const XMFLOAT4& Primitive::GetRotation() const
{
	return this->rotQuat;
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
	this->pos.x += pos.x;
	this->pos.y += pos.y;
	this->pos.z += pos.z;
	this->UpdateWorldMatrix();
}

void Primitive::SetScale(const float s)
{
	this->scale = s;
	this->UpdateWorldMatrix();
}


UCHAR Primitive::GetDimension() const
{
	return this->dimension;
}

void Primitive::SetRotation(const XMFLOAT3& rot)
{
	XMStoreFloat4(&this->rotQuat, XMQuaternionRotationRollPitchYaw(rot.x, rot.y, rot.z));
	this->UpdateWorldMatrix();
}

void Primitive::SetRotation(const XMFLOAT4& rot)
{
	this->rotQuat = rot;
	this->UpdateWorldMatrix();
}

void Primitive::SetRotationZero(const XMFLOAT3& rot)
{
	this->rotZero = rot;
	this->rotQuat = { 0.0f, 0.0f, 0.0f, 1.0f };
	this->UpdateWorldMatrix();
}

void Primitive::RotateAroundAxis(XMVECTOR axis, float angle)
{
	XMVECTOR delta = XMQuaternionRotationAxis(axis, angle);
	XMVECTOR current = XMLoadFloat4(&this->rotQuat);
	XMStoreFloat4(&this->rotQuat, XMQuaternionMultiply(current, delta));
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

float Primitive::GetScale() const
{
	return this->scale;
}
