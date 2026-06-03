#include "auxiliaryObjects.h"
#include "../misc/Colors.h"

const float axisLength = 100.f;
const float axisRadius = 3.f;
const float oBallRadius = 7.f;

const XMFLOAT3 xDir = XMFLOAT3(1.0f, 0.0f, 0.0f);
const XMFLOAT3 yDir = XMFLOAT3(0.0f, 1.0f, 0.0f);
const XMFLOAT3 zDir = XMFLOAT3(0.0f, 0.0f, 1.0f);

const std::vector<XMFLOAT3> Xposes = { XMFLOAT3(0.0f, 0.0f, 0.0f), xDir * axisLength };
const std::vector<XMFLOAT3> Yposes = { XMFLOAT3(0.0f, 0.0f, 0.0f), yDir * axisLength };
const std::vector<XMFLOAT3> Zposes = { XMFLOAT3(0.0f, 0.0f, 0.0f), zDir * axisLength };

constexpr UINT auxObjectsIDs[] = {
	MAXUINT,
	MAXUINT - 1,
	MAXUINT - 2,
	MAXUINT - 3,
	MAXUINT - 4,
	MAXUINT - 5,
	MAXUINT - 6
};

void OrientationTransformer::Initialize(ID3D11Device* device, ID3D11DeviceContext* deviceContext)
{
	this->device = device;
	this->deviceContext = deviceContext;

	this->xAx = PrimitiveConstructor::Line3d(axisRadius, Xposes, 12, Colors::RED, auxObjectsIDs[0]);
	this->yAx = PrimitiveConstructor::Line3d(axisRadius, Yposes, 12, Colors::GREEN, auxObjectsIDs[1]);
	this->zAx = PrimitiveConstructor::Line3d(axisRadius, Zposes, 12, Colors::BLUE, auxObjectsIDs[2]);
	this->oBall = PrimitiveConstructor::Sphere(oBallRadius, BaseVectors::ORIGIN, 1, Colors::WHITE, auxObjectsIDs[3]);
	this->xRot = PrimitiveConstructor::Arc3d(axisLength * .75f, axisRadius, 90.f, BaseVectors::ORIGIN, 32, Colors::RED, auxObjectsIDs[4]);
	this->xRot->SetRotationZero({ 0, -XM_PIDIV2, 0 });
	this->yRot = PrimitiveConstructor::Arc3d(axisLength * .75f, axisRadius, 90.f, BaseVectors::ORIGIN, 32, Colors::GREEN, auxObjectsIDs[5]);
	this->yRot->SetRotationZero({ XM_PIDIV2, 0, 0 });
	this->zRot = PrimitiveConstructor::Arc3d(axisLength * .75f, axisRadius, 90.f, BaseVectors::ORIGIN, 32, Colors::BLUE, auxObjectsIDs[6]);

	this->auxObjects = std::vector<Primitive*>{
		this->xAx,
		this->yAx,
		this->zAx,
		this->oBall,
		this->xRot,
		this->yRot,
		this->zRot,
	};
}

void OrientationTransformer::SetTargetObjects(const std::vector<Primitive*>& objs)
{
	this->targetObjects = objs;
	this->Update();
}

bool OrientationTransformer::HasTargets() const
{
	return !this->targetObjects.empty();
}

std::vector<UINT> OrientationTransformer::GetAuxiliaryObjectsIDs() const
{
	return std::vector<UINT>(std::begin(auxObjectsIDs), std::end(auxObjectsIDs));
}

bool OrientationTransformer::HasActiveObject() const
{
	return this->activeObject != nullptr;
}

void OrientationTransformer::UpdateLighting(const float ambient, const float intensity, const float shininess, const bool sh)
{
	for (Primitive* obj : this->auxObjects) {
		obj->SetLighting(ambient, intensity, shininess);
		obj->SetSmoothShading(sh);
	}
}

void OrientationTransformer::Draw(const XMMATRIX& viewMatrix, const XMMATRIX& projectionMatrix, const float scale)
{
	if (targetObjects.empty()) return;

	for (Primitive* obj : this->auxObjects) {
		obj->SetScale(scale);
		obj->Draw(viewMatrix, projectionMatrix);
	}
}

void OrientationTransformer::DrawID(const XMMATRIX& viewMatrix, const XMMATRIX& projectionMatrix, const float scale, const UINT id)
{
	if (targetObjects.empty()) return;

	for (Primitive* obj : this->auxObjects) {
		if (obj->id == id) {
			obj->SetScale(scale);
			obj->Draw(viewMatrix, projectionMatrix);
			break;
		}
	}
}

XMFLOAT4 originalObjectColor;
void OrientationTransformer::HandleObjPress(UINT id)
{
	XMFLOAT3 activeObjectDir = XMFLOAT3{};

	this->activeObject = this->auxObjects[~id];
	originalObjectColor = this->activeObject->GetColor();
	this->activeObject->SetColor(Colors::YELLOW);

	if (id != auxObjectsIDs[3]) {
		switch (id) {
		case auxObjectsIDs[0]: case auxObjectsIDs[4]: activeObjectDir = xDir; break;
		case auxObjectsIDs[1]: case auxObjectsIDs[5]: activeObjectDir = yDir; break;
		case auxObjectsIDs[2]: case auxObjectsIDs[6]: activeObjectDir = zDir; break;
		}

		if (targetObjects.size() == 1) {
			XMVECTOR vv = XMLoadFloat3(&activeObjectDir);
			XMFLOAT4 rpy = targetObjects[0]->GetRotation();
			XMMATRIX m = XMMatrixRotationQuaternion(XMLoadFloat4(&rpy));
			XMStoreFloat3(&this->activeObjectActionAxis, XMVector3Normalize(XMVector3TransformCoord(vv, m)));
		} else {
			this->activeObjectActionAxis = activeObjectDir;
		}
		return;
	}
}

static XMFLOAT2 prevActionPointNdc{};

void OrientationTransformer::HandleObjMove(XMFLOAT2& actionAxisScreen, XMFLOAT2& actionPointNdc, const XMMATRIX& vm, const XMMATRIX& pm, const float cameraScale)
{
	if (this->activeObject->id > auxObjectsIDs[3]) {
		XMVECTOR activeAxis = XMLoadFloat3(&this->activeObjectActionAxis);
		XMVECTOR activeAxisScreen = XMVector3TransformNormal(activeAxis, vm);

		activeAxisScreen = XMVectorSetZ(activeAxisScreen, 0.0f);
		XMVECTOR activeAxisScreenNorm = XMVector3Normalize(activeAxisScreen);

		XMVECTOR actionAxisScreenVec = XMVectorSet(actionAxisScreen.x, actionAxisScreen.y, 0.0f, 0.0f);

		float dot = XMVectorGetX(XMVector3Dot(actionAxisScreenVec, activeAxisScreenNorm));
		float axisScreenLen = XMVectorGetX(XMVector3Length(activeAxisScreen));
		float invLen = axisScreenLen != 0.0f ? (1.f / axisScreenLen) : 0.0f;

		XMFLOAT3 offset;
		XMStoreFloat3(&offset, activeAxis * dot * invLen * cameraScale * .2625f);

		for (Primitive* target : targetObjects)
			target->AdjustPosition(offset);
	}
	else if (this->activeObject->id == auxObjectsIDs[3]) {
		XMFLOAT3 actionAxisScreen3 = XMFLOAT3(actionAxisScreen.x, actionAxisScreen.y, 0.0f);
		XMVECTOR actionAxisScreenVec = XMLoadFloat3(&actionAxisScreen3);
		XMVECTOR actionAxisWorldVec = XMVector3TransformNormal(actionAxisScreenVec, XMMatrixInverse(nullptr, vm));
		XMFLOAT3 offset;
		XMStoreFloat3(&offset, actionAxisWorldVec * cameraScale * .25f);

		for (Primitive* target : targetObjects)
			target->AdjustPosition(offset);
	}
	else if (this->activeObject->id < auxObjectsIDs[3]) {
		if (prevActionPointNdc.x == 0.0f && prevActionPointNdc.y == 0.0f) {
			prevActionPointNdc = actionPointNdc;
			return;
		}

		XMVECTOR objWorld = XMLoadFloat3(&centroid);
		XMVECTOR objClip = XMVector3TransformCoord(objWorld, XMMatrixMultiply(vm, pm));
		XMFLOAT2 objNdc = {
			XMVectorGetX(objClip),
			XMVectorGetY(objClip)
		};

		XMVECTOR origin = XMVectorSet(objNdc.x, objNdc.y, 0.0f, 0.0f);
		XMVECTOR curr = XMVectorSet(actionPointNdc.x, actionPointNdc.y, 0.0f, 0.0f);
		XMVECTOR prev = XMVectorSet(prevActionPointNdc.x, prevActionPointNdc.y, 0.0f, 0.0f);

		XMVECTOR toCurr = curr - origin;
		XMVECTOR toPrev = prev - origin;

		if (XMVectorGetX(XMVector2Length(toCurr)) < 1e-5f ||
			XMVectorGetX(XMVector2Length(toPrev)) < 1e-5f) {
			prevActionPointNdc = actionPointNdc;
			return;
		}

		toCurr = XMVector2Normalize(toCurr);
		toPrev = XMVector2Normalize(toPrev);

		float dot = std::clamp(XMVectorGetX(XMVector2Dot(toPrev, toCurr)), -1.0f, 1.0f);
		float angle = acosf(dot);

		float cross = XMVectorGetX(toPrev) * XMVectorGetY(toCurr)
			- XMVectorGetY(toPrev) * XMVectorGetX(toCurr);
		if (cross < 0.0f) angle = -angle;

		XMFLOAT4X4 vmf;
		XMStoreFloat4x4(&vmf, vm);
		XMVECTOR camForward = XMVectorSet(-vmf._13, -vmf._23, -vmf._33, 0.0f);
		XMVECTOR axis3d = XMLoadFloat3(&this->activeObjectActionAxis);
		if (XMVectorGetX(XMVector3Dot(axis3d, camForward)) > 0.0f) angle = -angle;

		XMVECTOR center = XMLoadFloat3(&centroid);
		XMVECTOR rotQuat = XMQuaternionRotationAxis(axis3d, angle);

		for (Primitive* target : targetObjects) {
			XMVECTOR pos = XMLoadFloat3(&target->GetPosition());
			XMVECTOR rel = pos - center;
			XMFLOAT3 newPos;
			XMStoreFloat3(&newPos, center + XMVector3Rotate(rel, rotQuat));
			target->SetPosition(newPos);
			target->RotateAroundAxis(axis3d, angle);
		}

		prevActionPointNdc = actionPointNdc;
	}

	this->Update();
}

void OrientationTransformer::HandleObjRelease()
{
	if (this->activeObject) {
		this->activeObject->SetColor(originalObjectColor);
		this->activeObject = nullptr;
	}
	this->activeObjectActionAxis = XMFLOAT3{};
	prevActionPointNdc = XMFLOAT2{};
}

void OrientationTransformer::Update()
{
	if (targetObjects.empty()) return;

	XMFLOAT3 sum{};
	for (Primitive* p : targetObjects) {
		XMFLOAT3 pos = p->GetPosition();
		sum.x += pos.x; sum.y += pos.y; sum.z += pos.z;
	}
	float n = (float)targetObjects.size();
	centroid = { sum.x / n, sum.y / n, sum.z / n };

	XMFLOAT4 rot = (targetObjects.size() == 1)
		? targetObjects[0]->GetRotation()
		: XMFLOAT4(0, 0, 0, 1);

	for (Primitive* obj : auxObjects) {
		obj->SetPosition(centroid);
		obj->SetRotation(rot);
	}
}
