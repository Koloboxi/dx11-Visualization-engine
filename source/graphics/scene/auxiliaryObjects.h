#pragma once
#include "primitives/primitive.h"

class OrientationTransformer {
public:
	void Initialize(ID3D11Device* device, ID3D11DeviceContext* deviceContext);
	void SetTargetObjects(const std::vector<Primitive*>& objs);

	std::vector<UINT> GetAuxiliaryObjectsIDs() const;
	bool HasActiveObject() const;
	bool HasTargets() const;

	void UpdateLighting(const float ambient, const float intensity, const float shininess, const bool sh);

	void Draw(const XMMATRIX& viewMatrix, const XMMATRIX& projectionMatrix, const float scale);
	void DrawID(const XMMATRIX& viewMatrix, const XMMATRIX& projectionMatrix, const float scale, const UINT id);

	void HandleObjPress(UINT id);
	void HandleObjMove(XMFLOAT2& actionAxisScreen, XMFLOAT2& actionPointNdc, const XMMATRIX& vm, const XMMATRIX& pm, const float cameraScale);
	void HandleObjRelease();

	void Update();

private:
	ID3D11Device* device{};
	ID3D11DeviceContext* deviceContext{};

	std::vector<Primitive*> targetObjects;
	XMFLOAT3 centroid{};

	Primitive* oBall;
	Primitive* xAx;
	Primitive* yAx;
	Primitive* zAx;
	Primitive* xRot;
	Primitive* yRot;
	Primitive* zRot;

	std::vector<Primitive*> auxObjects;

	Primitive* activeObject = nullptr;
	XMFLOAT3 activeObjectActionAxis{};

	// Rotation snapping state, captured at HandleObjPress on a rotation arc and
	// used to apply the snapped orientation absolutely from the gesture's start.
	// m_rotAccum is the raw (unsnapped) cumulative angle since the press; the
	// applied angle is m_rotAccum magnetised/snapped per the held modifier keys.
	bool                  m_rotating = false;
	float                 m_rotAccum = 0.0f;
	XMFLOAT3              m_rotCenter{};
	std::vector<XMFLOAT3> m_startPos;
	std::vector<XMFLOAT4> m_startRot;
};
