#pragma once
#include "..\..\buffers\vertex.h"
#include "..\..\buffers\vertexBuffer.h"
#include "..\..\buffers\indexBuffer.h"
#include "..\..\buffers\constantBuffer.h"
#include "..\..\math.h"
#include "..\scene_node.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace DirectX;

namespace BaseVectors {
	static const XMFLOAT3 ORIGIN = XMFLOAT3(0, 0, 0);
	static const XMFLOAT3 XVEC = XMFLOAT3(1, 0, 0);
	static const XMFLOAT3 YVEC = XMFLOAT3(0, 1, 0);
	static const XMFLOAT3 ZVEC = XMFLOAT3(0, 0, 1);
}

class Primitive : public SceneNode
{
public:
	Primitive(ID3D11Device* device, ID3D11DeviceContext* deviceContext);
	HRESULT SetVertexIndexBuffers(Vertex* vertexData, UINT vertexNumVertices, DWORD* indexData, UINT indexNumVertices, UCHAR dim);

	bool IsPrimitive() const override { return true; }

	UINT id{};

	void Draw(const XMMATRIX& viewMatrix, const XMMATRIX& projectionMatrix);
	bool selected = false;

	bool staging = false;

	std::string semSourcePath;
	// SEM pipeline "session" folder this source serializes into
	// (%TEMP%/sem/<stem>_<N>/). Chosen at import; SemSession::Bind reads it and
	// allocates a fresh one when empty. See SemSession.
	std::string semWorkDir;

	void SetColor(const XMFLOAT4& col);
	void SetAlpha(const float a);
	void SetUseVertexColor(const bool v);
	void SetIlluminationCapability(const bool l);
	void SetTwoSided(bool enable, const XMFLOAT4& backColor = XMFLOAT4(1, 0, 0, 1));
	void SetLighting(const float ambient, const float intensity, const float shininess);
	void SetSmoothShading(const bool sh);

	// --- Named per-vertex colour sets ------------------------------------
	// A primitive built once can hold several alternative per-vertex colourings
	// and switch between them cheaply (no geometry rebuild). Each set stores one
	// colour per vertex in the vertex buffer's own order; activating a set maps
	// the dynamic vertex buffer(s) and overwrites only the colour field. Used by
	// the SEM mesh to flip between the "T field" gradient and the "BC" view.
	void AddColorSet(const std::string& name, const std::vector<XMFLOAT4>& gpuOrderColors);
	bool ActivateColorSet(const std::string& name);
	bool HasColorSet(const std::string& name) const;
	const std::string& GetActiveColorSet() const { return activeColorSet; }
	UINT GetVertexCount() const { return vertexBuffer.GetBufferSize(); }

	void SetScale(const float s);

	void SetPosition(const XMFLOAT3& pos);
	void AdjustPosition(const XMFLOAT3& pos);

	void SetRotation(const XMFLOAT3& rot);
	void SetRotation(const XMFLOAT4& rot);
	void SetRotationZero(const XMFLOAT3& rot);
	void RotateAroundAxis(XMVECTOR axis, float angle);

	void SetPrimitiveTopology(const D3D10_PRIMITIVE_TOPOLOGY& top);

	XMFLOAT4 GetColor() const;
	bool GetTransparent() const;
	bool GetIlluminationCapability() const;

	float GetScale() const;

	const XMFLOAT3& GetPosition() const;
	const XMFLOAT4& GetRotation() const;

	const D3D10_PRIMITIVE_TOPOLOGY& GetPrimitiveTopology() const;
	const std::vector<Vertex> GetVertexData() const;
	const std::vector<DWORD> GetIndexData() const;

	UCHAR GetDimension() const;

	std::string luaScript;

	XMFLOAT3 velocity{};

	using Updater = std::function<void(Primitive&, float t, float dt)>;
	void SetUpdater(Updater fn) { updater = std::move(fn); }
	void ClearUpdater() { updater = nullptr; }
	bool HasUpdater() const { return updater != nullptr; }
	void Update(float t, float dt) {
		if (updater) updater(*this, t, dt);
		if (velTracked && dt > 0.0001f) {
			velocity.x = (pos.x - velPrevPos.x) / dt;
			velocity.y = (pos.y - velPrevPos.y) / dt;
			velocity.z = (pos.z - velPrevPos.z) / dt;
		}
		velPrevPos = pos;
		velTracked = true;
	}

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
	bool psCBDirty = true;

	float scale = 1.0f;

	bool illuminationCapability;
	bool smoothShade = true;
	bool transparent;

	XMFLOAT3 pos{};
	XMFLOAT4 rotQuat = { 0.0f, 0.0f, 0.0f, 1.0f };
	XMFLOAT3 rotZero{};

	XMFLOAT3 velPrevPos{};
	bool     velTracked = false;

	Updater updater;

	// Alternative per-vertex colourings (GPU vertex order) and the active one.
	std::unordered_map<std::string, std::vector<XMFLOAT4>> colorSets;
	std::string activeColorSet;
};

namespace PrimitiveConstructor {
	extern ID3D11Device* device;
	extern ID3D11DeviceContext* deviceContext;

	Primitive* Point(const XMFLOAT3& pos, const XMFLOAT4& col, UINT id);
	Primitive* Line(const std::vector<XMFLOAT3>& poses, const XMFLOAT4& col, UINT id);
	// srcIndexOut (optional): receives, for each emitted GPU vertex, the index of
	// the source poses/cols entry it came from. ColoredTriangles can reorder a
	// triangle's corners (winding flip), so this mapping is the only reliable way
	// to build an alternative per-vertex colour set in GPU order afterwards.
	Primitive* ColoredLine(const std::vector<XMFLOAT3>& poses, const std::vector<XMFLOAT4>& cols, bool asLineList, UINT id, std::vector<UINT>* srcIndexOut = nullptr);
	Primitive* ColoredTriangles(const std::vector<XMFLOAT3>& poses, const std::vector<XMFLOAT4>& cols, UINT id, bool ensureCCW = true, std::vector<UINT>* srcIndexOut = nullptr);
	Primitive* Polygon(const std::vector<XMFLOAT3>& poses, const XMFLOAT4& col, UINT id);

	Primitive* Sphere(float radius, const XMFLOAT3& pos, const UINT numSubdivides, const XMFLOAT4& col, UINT id);
	Primitive* Line3d(float radius, const std::vector<XMFLOAT3>& poses, const UINT numSubdivides, const XMFLOAT4& col, UINT id);

	Primitive* Arc3d(float arcRadius, float lineRadius, float angleDeg, const XMFLOAT3& center, const UINT numSubdivides, const XMFLOAT4& col, UINT id);

	Primitive* Arrow3d(float shaftRadius, float headRadius, float headLength, const XMFLOAT3& from, const XMFLOAT3& to, UINT sides, const XMFLOAT4& col, UINT id);

	Primitive* CubeWireframe(float halfSize, const XMFLOAT3& center, const XMFLOAT4& col, UINT id);
	Primitive* CubeSolid(float halfSize, const XMFLOAT3& center, const XMFLOAT4& col, UINT id);

	Primitive* RevolutionSurface(const std::vector<XMFLOAT3>& profile, UINT segments, const XMFLOAT4& col, UINT id);
}