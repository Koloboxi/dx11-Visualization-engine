#include "Camera.h"

Camera::Camera() {
	this->pos = XMFLOAT3(0.0f, 0.0f, 0.0f);
	this->posVector = XMLoadFloat3(&this->pos);
	UpdateViewMatrix();
}
void Camera::SetProjectionValues(float viewWidth, float viewHeight, float nearZ, float farZ) {
	this->baseViewWidth = viewWidth;
	this->baseViewHeight = viewHeight;
	this->nearZ = nearZ;
	this->farZ = farZ;
	this->projectionMatrix = XMMatrixOrthographicRH(this->baseViewWidth, this->baseViewHeight, this->nearZ, this->farZ);
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
const XMMATRIX& Camera::GetProjectionMatrixNoScale() const
{
	return XMMatrixOrthographicRH(this->baseViewWidth, this->baseViewHeight, this->nearZ, this->farZ);
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
	return this->scale;
}

void Camera::SetScale(const float& scaleFactor)
{
	this->scale = scaleFactor;
	this->SetProjectionValues(this->baseViewWidth, this->baseViewHeight, this->nearZ, this->farZ);
}

void Camera::AdjustScale(const float& scaleFactor, XMFLOAT2 scaleCenterNDC)
{
	this->scale *= scaleFactor;

	float oldWidth = baseViewWidth * (scale / scaleFactor);
	float oldHeight = baseViewHeight * (scale / scaleFactor);
	float newWidth = baseViewWidth * scale;
	float newHeight = baseViewHeight * scale;

	float dx = (oldWidth - newWidth) * 0.5f * scaleCenterNDC.x;
	float dy = (oldHeight - newHeight) * 0.5f * scaleCenterNDC.y;

	XMVECTOR dPos = this->vec_left * dx + this->vec_upward * dy;
	this->AdjustPosition(dPos);

	this->projectionMatrix = XMMatrixOrthographicRH(newWidth, newHeight, nearZ, farZ);
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

	this->vec_upward = XMVector3TransformCoord(this->DEFAULT_UP_VECTOR, this->rotMatrix);
	this->vec_left = XMVector3TransformCoord(this->DEFAULT_LEFT_VECTOR, this->rotMatrix);
	this->vec_right = XMVector3TransformCoord(this->DEFAULT_RIGHT_VECTOR, this->rotMatrix);
	this->vec_forward = XMVector3TransformCoord(this->DEFAULT_FORWARD_VECTOR, this->rotMatrix);
	this->vec_backward = XMVector3TransformCoord(this->DEFAULT_BACKWARD_VECTOR, this->rotMatrix);
		
	this->viewMatrix = XMMatrixLookAtRH(this->posVector, camTarget, this->vec_upward);
}