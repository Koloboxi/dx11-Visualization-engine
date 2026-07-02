#include "Camera.h"
#include <algorithm>

static constexpr double SCALE_MIN = 1e-12;
static constexpr double SCALE_MAX = 1e12;

// Depth range = max(viewWidth, viewHeight) * scale * DEPTH_FACTOR.
// Factor 10 means the clip volume is 10 viewport-widths deep on each side —
// large enough to contain any visible scene, small enough to give full
// depth-buffer precision at every zoom level.
static constexpr float DEPTH_FACTOR = 10.0f;

Camera::Camera() {
	this->pos = XMFLOAT3(0.0f, 0.0f, 0.0f);
	this->posVector = XMLoadFloat3(&this->pos);
	UpdateViewMatrix();
}

float Camera::DynamicHalfDepth() const {
	float maxDim = baseViewWidth > baseViewHeight ? baseViewWidth : baseViewHeight;
	float h = static_cast<float>(maxDim * scale * DEPTH_FACTOR);
	return h < 1.0f ? 1.0f : h;
}

void Camera::SetProjectionValues(float viewWidth, float viewHeight, float /*nearZ*/, float /*farZ*/) {
	this->baseViewWidth  = viewWidth;
	this->baseViewHeight = viewHeight;
	float h = DynamicHalfDepth();
	this->projectionMatrix = XMMatrixOrthographicRH(
		static_cast<float>(this->baseViewWidth  * this->scale),
		static_cast<float>(this->baseViewHeight * this->scale),
		-h, h);
}

const XMVECTOR& Camera::GetForwardVector()
{
	return this->vec_forward;
}
const XMVECTOR& Camera::GetRightVector()
{
	return this->vec_right;
}
const XMVECTOR& Camera::GetBackwardVector()
{
	return this->vec_backward;
}
const XMVECTOR& Camera::GetLeftVector()
{
	return this->vec_left;
}

const XMVECTOR& Camera::GetUpwardVector()
{
	return this->vec_upward;
}

const XMMATRIX& Camera::GetViewMatrix() const {
	return this->viewMatrix;
}
const XMMATRIX& Camera::GetProjectionMatrix() const {
	return this->projectionMatrix;
}
const XMMATRIX Camera::GetInverseMatrix() const
{
	return XMMatrixInverse(nullptr, this->viewMatrix * this->projectionMatrix);
}
const XMVECTOR& Camera::GetPositionVector() const {
	return this->posVector;
}
const XMFLOAT3& Camera::GetPositionFloat3() const {
	return this->pos;
}

const XMMATRIX& Camera::GetRotMatrix() const
{
	return this->rotMatrix;
}
const float Camera::GetScale() const
{
	return static_cast<float>(this->scale);
}

void Camera::SetScale(const float& scaleFactor)
{
	this->scale = std::clamp(static_cast<double>(scaleFactor), SCALE_MIN, SCALE_MAX);
	float h = DynamicHalfDepth();
	this->projectionMatrix = XMMatrixOrthographicRH(
		static_cast<float>(this->baseViewWidth  * this->scale),
		static_cast<float>(this->baseViewHeight * this->scale),
		-h, h);
	UpdateViewMatrix();
}

void Camera::AdjustScale(const float& scaleFactor, XMFLOAT2 scaleCenterNDC)
{
	double newScale = std::clamp(this->scale * scaleFactor, SCALE_MIN, SCALE_MAX);
	double actualFactor = newScale / this->scale;
	this->scale = newScale;

	double oldWidth  = baseViewWidth  * (this->scale / actualFactor);
	double oldHeight = baseViewHeight * (this->scale / actualFactor);
	double newWidth  = baseViewWidth  * this->scale;
	double newHeight = baseViewHeight * this->scale;

	float dx = static_cast<float>((oldWidth  - newWidth)  * 0.5 * scaleCenterNDC.x);
	float dy = static_cast<float>((oldHeight - newHeight) * 0.5 * scaleCenterNDC.y);

	XMVECTOR dPos = this->vec_left * dx + this->vec_upward * dy;
	this->AdjustPosition(dPos);

	float h = DynamicHalfDepth();
	this->projectionMatrix = XMMatrixOrthographicRH(
		static_cast<float>(newWidth), static_cast<float>(newHeight), -h, h);
	UpdateViewMatrix();
}

void Camera::SetPosition(const XMFLOAT3& pos)
{
	this->pos.x = pos.x;
	this->pos.y = pos.y;
	this->pos.z = pos.z;
	this->posVector = XMLoadFloat3(&this->pos);
	this->UpdateViewMatrix();
}
void Camera::AdjustPosition(const XMVECTOR& pos) {
	this->posVector += pos;
	XMStoreFloat3(&this->pos, this->posVector);
	this->UpdateViewMatrix();
}
void Camera::AdjustPosition(const XMFLOAT3& pos)
{
	this->pos.x += pos.x;
	this->pos.y += pos.y;
	this->pos.z += pos.z;
	this->posVector = XMLoadFloat3(&this->pos);
	this->UpdateViewMatrix();
}

void Camera::SetRotation(const XMMATRIX& rotMatrix) {
	this->rotMatrix = rotMatrix;
	this->UpdateViewMatrix();
}
void Camera::AdjustRotation(const XMMATRIX& rotMatrix)
{
	this->rotMatrix *= rotMatrix;
	this->UpdateViewMatrix();
}

void Camera::UpdateViewMatrix() {
	XMVECTOR camTarget = XMVector3TransformCoord(this->DEFAULT_FORWARD_VECTOR, this->rotMatrix);
	camTarget += this->posVector;

	this->vec_upward  = XMVector3TransformCoord(this->DEFAULT_UP_VECTOR,       this->rotMatrix);
	this->vec_left    = XMVector3TransformCoord(this->DEFAULT_LEFT_VECTOR,     this->rotMatrix);
	this->vec_right   = XMVector3TransformCoord(this->DEFAULT_RIGHT_VECTOR,    this->rotMatrix);
	this->vec_forward = XMVector3TransformCoord(this->DEFAULT_FORWARD_VECTOR,  this->rotMatrix);
	this->vec_backward= XMVector3TransformCoord(this->DEFAULT_BACKWARD_VECTOR, this->rotMatrix);

	this->viewMatrix = XMMatrixLookAtRH(this->posVector, camTarget, this->vec_upward);
}
