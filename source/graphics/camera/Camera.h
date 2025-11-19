#pragma once
#include <DirectXMath.h>
#include "Projections.h"
using namespace DirectX;

class Camera
{
public:
	Camera();
	void SetProjectionValues(float viewWidth, float viewHeight, float nearZ, float farZ);
	
	const XMMATRIX& GetViewMatrix()	const;
	const XMMATRIX& GetProjectionMatrix() const;
	const XMMATRIX& GetProjectionMatrixNoScale() const;

	const XMMATRIX GetInverseMatrix() const;

	const XMVECTOR& GetPositionVector() const;
	const XMFLOAT3& GetPositionFloat3() const;

	const XMMATRIX& GetRotMatrix() const;
	const float GetScale() const;

	void SetScale(const float& scaleFactor);
	void AdjustScale(const float& scaleFactor, XMFLOAT2 scaleCenter);

	void SetPosition(const XMFLOAT3& pos);
	void AdjustPosition(const XMFLOAT3& pos);
	void AdjustPosition(const XMVECTOR& pos);

	void SetRotation(const XMMATRIX& rotMatrix);
	void AdjustRotation(const XMMATRIX& rotMatrix);
	
	const XMVECTOR& GetForwardVector();
	const XMVECTOR& GetRightVector();
	const XMVECTOR& GetBackwardVector();
	const XMVECTOR& GetLeftVector();
	const XMVECTOR& GetUpwardVector();
private:
	void UpdateViewMatrix();

	XMVECTOR posVector;
	XMFLOAT3 pos;
	XMMATRIX rotMatrix;

	XMMATRIX viewMatrix;
	XMMATRIX projectionMatrix;
	XMMATRIX projectionMatrixNoScale;

	float scale = 1.0;
	float baseViewWidth;
	float baseViewHeight;
	float nearZ;
	float farZ;

	const XMVECTOR DEFAULT_FORWARD_VECTOR = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
	const XMVECTOR DEFAULT_UP_VECTOR = XMVectorSet(0.0f, 0.0f, 1.0, 0.0f);
	const XMVECTOR DEFAULT_BACKWARD_VECTOR = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	const XMVECTOR DEFAULT_LEFT_VECTOR = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
	const XMVECTOR DEFAULT_RIGHT_VECTOR = XMVectorSet(-1.0f, 0.0f, 0.0f, 0.0f);

	XMVECTOR vec_forward;
	XMVECTOR vec_left;
	XMVECTOR vec_right;
	XMVECTOR vec_backward;
	XMVECTOR vec_upward;
};