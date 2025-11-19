#include "engine.h"

bool Engine::Initialize(HINSTANCE hInstance, std::string window_title, std::string window_class, int width, int height){
	
	timer.Start();
	
	if (!this->render_window.Initialize(this, hInstance, window_title, window_class, width, height)) {
		return false;
	}
	if (!this->gfx.Initialize(this->render_window.GetHWND(), width, height)) {
		return false;
	}
	return true;
}

bool Engine::ProcessMessages() {
	return this->render_window.ProcessMessages();
}

void Engine::RenderFrame()
{
	this->gfx.RenderFrame();
}

void Engine::Update()
{
	float dt = static_cast<float>(timer.GetMillisecondsElapsed());
	timer.Restart();
	while (!keyboard.CharBufferIsEmpty()) {
		unsigned char ch = keyboard.ReadChar();
	}
	while (!keyboard.KeyBufferIsEmpty()) {
		KeyboardEvent eve = keyboard.ReadKey();
		unsigned char keycode = eve.GetKeyCode();
	}

	while (!mouse.EventBufferIsEmpty()) {
		MouseEvent me = mouse.ReadEvent();
		if (me.GetType() == MouseEvent::EventType::RAW_MOVE && this->mouse.IsRightDown()) {
			XMMATRIX rotMatrix = XMMatrixRotationAxis(this->gfx.scene.camera.GetUpwardVector(), -0.003f * me.GetPosX());
			rotMatrix *= XMMatrixRotationAxis(this->gfx.scene.camera.GetRightVector(), 0.003f * me.GetPosY());

			this->gfx.scene.camera.AdjustRotation(rotMatrix);
		}
		if (me.GetType() == MouseEvent::EventType::WheelUp) {
			POINT point = { me.GetPosX(), me.GetPosY() };
			ScreenToClient(this->render_window.GetHWND(), &point);
			XMFLOAT2 scaleCenter = this->gfx.ScreenCoords2NDC(point.x, point.y);
			this->gfx.scene.camera.AdjustScale(1.0f / 1.1f, scaleCenter);
		}
		if (me.GetType() == MouseEvent::EventType::WheelDown) {
			POINT point = { me.GetPosX(), me.GetPosY() };
			ScreenToClient(this->render_window.GetHWND(), &point);
			XMFLOAT2 scaleCenter = this->gfx.ScreenCoords2NDC(point.x, point.y);
			this->gfx.scene.camera.AdjustScale(1.1f, scaleCenter);
		}

		if (me.GetType() == MouseEvent::EventType::RAW_MOVE && this->mouse.IsMiddleDown()) {
			this->gfx.scene.camera.AdjustPosition(
				(this->gfx.scene.camera.GetLeftVector() *		-static_cast<float>(me.GetPosX())
				+this->gfx.scene.camera.GetUpwardVector() *	static_cast<float>(me.GetPosY())
				) * this->gfx.scene.camera.GetScale() * 0.25f
			);
		}

		/*if (me.GetType() == MouseEvent::LRelease) {
			XMFLOAT2 mouseRay = this->gfx.ScreenCoords2NDC(me.GetPosX(), me.GetPosY());

			XMFLOAT3 rayDir = {mouseRay.x, mouseRay.y, -1.f};
			XMFLOAT3 rayOri = {mouseRay.x, mouseRay.y, 0.f};

			XMVECTOR rayDirVec = XMLoadFloat3(&rayDir);
			XMVECTOR rayOriVec = XMLoadFloat3(&rayOri);

			rayDirVec = XMVector3TransformNormal(rayDirVec, this->gfx.scene.camera.GetInverseMatrix());
			rayOriVec = XMVector3TransformCoord(rayOriVec, this->gfx.scene.camera.GetInverseMatrix());

			this->gfx.scene.HandleMouseRay(rayOriVec, rayDirVec);
		}*/
	}
}
