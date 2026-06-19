#include "auxiliaryObjects.h"
#include "../misc/Colors.h"
#include <Windows.h>
#include <cmath>

namespace {
	bool AltHeld()   { return (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0; }
	bool ShiftHeld() { return (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0; }

	// Snap a raw cumulative rotation angle (radians) per the held modifiers:
	//   alt   -> free (no snap);
	//   shift -> hard snap to the nearest multiple of 90 deg;
	//   else  -> "magnetise": when the angle is within a small neighbourhood of a
	//            multiple of 30 or 45 deg, snap exactly to it, otherwise free.
	float SnapRotation(float raw) {
		if (AltHeld()) return raw;
		const float deg = XM_PI / 180.0f;
		if (ShiftHeld()) {
			const float step = 90.0f * deg;
			return std::round(raw / step) * step;
		}
		const float tol = 4.0f * deg;
		float best = raw, bestErr = tol;
		for (float baseDeg : { 30.0f, 45.0f }) {
			const float step = baseDeg * deg;
			const float cand = std::round(raw / step) * step;
			const float err = std::fabs(raw - cand);
			if (err < bestErr) { bestErr = err; best = cand; }
		}
		return best;
	}

	// If v is (nearly) parallel to a world axis, return its index (0=X,1=Y,2=Z);
	// otherwise -1.
	int WorldAxisIndex(const XMFLOAT3& v) {
		if (std::fabs(v.x) > 0.999f && std::fabs(v.y) < 0.02f && std::fabs(v.z) < 0.02f) return 0;
		if (std::fabs(v.y) > 0.999f && std::fabs(v.x) < 0.02f && std::fabs(v.z) < 0.02f) return 1;
		if (std::fabs(v.z) > 0.999f && std::fabs(v.x) < 0.02f && std::fabs(v.y) < 0.02f) return 2;
		return -1;
	}

	// Translation snap grid: a power of ten that grows as the camera zooms out
	// (larger camera scale -> coarser grid), so n in 10^n falls as the scale falls.
	float GridStep(float cameraScale) {
		float s = cameraScale > 1e-6f ? cameraScale : 1e-6f;
		return std::pow(10.0f, std::floor(std::log10(s)));
	}
}

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

		// Rotation arcs (ids 4..6): snapshot the start orientation/position of
		// every target so the snapped angle can be applied absolutely from here.
		const bool isRotGizmo = (id == auxObjectsIDs[4] || id == auxObjectsIDs[5] || id == auxObjectsIDs[6]);
		if (isRotGizmo) {
			this->m_rotating = true;
			this->m_rotAccum = 0.0f;
			this->m_rotCenter = this->centroid;
			this->m_startPos.clear();
			this->m_startRot.clear();
			for (Primitive* t : targetObjects) {
				this->m_startPos.push_back(t->GetPosition());
				this->m_startRot.push_back(t->GetRotation());
			}
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

		// Shift + drag along a world-parallel axis: magnetise the moved world
		// coordinate to the nearest multiple of the zoom-dependent grid step when
		// it lands within a neighbourhood of one. A non-world-parallel axis (e.g. a
		// rotated single target's local axis) is left free.
		const int axisIdx = WorldAxisIndex(this->activeObjectActionAxis);
		if (ShiftHeld() && axisIdx >= 0 && !targetObjects.empty()) {
			float sum = 0.0f;
			for (Primitive* t : targetObjects) {
				const XMFLOAT3& p = t->GetPosition();
				sum += (axisIdx == 0 ? p.x : axisIdx == 1 ? p.y : p.z);
			}
			const float c = sum / (float)targetObjects.size();
			const float step = GridStep(cameraScale);
			const float nearest = std::round(c / step) * step;
			if (std::fabs(c - nearest) < 0.25f * step) {
				const float corr = nearest - c;
				XMFLOAT3 d{};
				(axisIdx == 0 ? d.x : axisIdx == 1 ? d.y : d.z) = corr;
				for (Primitive* t : targetObjects) t->AdjustPosition(d);
			}
		}
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

		// Accumulate the raw turn since the press, then apply the snapped angle
		// absolutely from the per-target snapshots (so magnetising never drifts).
		this->m_rotAccum += angle;
		const float applied = SnapRotation(this->m_rotAccum);

		XMVECTOR center = XMLoadFloat3(&this->m_rotCenter);
		XMVECTOR rotQuat = XMQuaternionRotationAxis(axis3d, applied);

		if (this->m_rotating && this->m_startPos.size() == targetObjects.size()) {
			for (size_t i = 0; i < targetObjects.size(); ++i) {
				XMVECTOR rel = XMLoadFloat3(&this->m_startPos[i]) - center;
				XMFLOAT3 newPos;
				XMStoreFloat3(&newPos, center + XMVector3Rotate(rel, rotQuat));
				targetObjects[i]->SetPosition(newPos);

				XMVECTOR startQ = XMLoadFloat4(&this->m_startRot[i]);
				XMFLOAT4 q;
				XMStoreFloat4(&q, XMQuaternionMultiply(startQ, rotQuat));
				targetObjects[i]->SetRotation(q);
			}
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
	this->m_rotating = false;
	this->m_rotAccum = 0.0f;
	this->m_startPos.clear();
	this->m_startRot.clear();
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
