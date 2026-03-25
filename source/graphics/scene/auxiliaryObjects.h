#include "primitives/primitive.h"

class OrientationTransformer {
public:
	void Initialize(ID3D11Device* device, ID3D11DeviceContext* deviceContext);
	void SetTargetObject(Primitive* obj);

	std::vector<UINT> GetAuxiliaryObjectsIDs() const;
	bool HasActiveObject() const;

	void UpdateLighting(const float ambient, const float intensity, const float shininess, const bool sh);

	void Draw(const XMMATRIX& viewMatrix, const XMMATRIX& projectionMatrix, const float scale);
	void DrawID(const XMMATRIX& viewMatrix, const XMMATRIX& projectionMatrix, const float scale, const UINT id);

	void HandleObjPress(UINT id);
	void HandleObjMove(XMFLOAT2& actionAxisScreen, XMFLOAT2& actionPointNdc, const XMMATRIX& vm, const XMMATRIX& pm, const float cameraScale);
	void HandleObjRelease();

private:
	ID3D11Device* device{};
	ID3D11DeviceContext* deviceContext{};

	void Update();

	Primitive* targetObject;

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
};
