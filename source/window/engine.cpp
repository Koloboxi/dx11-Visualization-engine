#include "engine.h"

bool Engine::Initialize(HINSTANCE hInstance, std::string window_title, std::string window_class, int width, int height){

	timer.Start();

	if (!this->render_window.Initialize(this, hInstance, window_title, window_class, width, height)) {
		return false;
	}
	if (!this->gfx.Initialize(this->render_window.GetHWND(), width, height)) {
		return false;
	}
	// The window is shown maximized before the graphics device exists, so the
	// WM_SIZE from maximizing was dropped (OnResize bails without a device) and
	// the swap chain kept the pre-maximize size. Reconcile it to the real client
	// size now — otherwise the backbuffer/viewport mismatch offsets mouse picks
	// by the caption height until the window is manually resized.
	RECT rc{};
	GetClientRect(this->render_window.GetHWND(), &rc);
	this->gfx.OnResize(rc.right - rc.left, rc.bottom - rc.top);
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
		if (keycode == VK_DELETE && eve.IsPress() && !ImGui::GetIO().WantTextInput &&
			this->gfx.scene.activeTab == 1) {
			for (Primitive* p : this->gfx.scene.primitives)
				if (p->selected) this->gfx.luaEditor.OnPrimitiveRemoved(p);
			this->gfx.scene.DeleteSelected();
		}
	}

	while (!mouse.EventBufferIsEmpty()) {
		MouseEvent me = mouse.ReadEvent();
		if (me.GetType() == MouseEvent::EventType::RAW_MOVE)
			this->gfx.scene.idPassNeeded = true;

		if (me.GetType() == MouseEvent::LPress)
			this->gfx.scene.idPassNeeded = true;

		if (me.GetType() == MouseEvent::EventType::RAW_MOVE && this->mouse.IsRightDown()) {
			XMMATRIX rotMatrix = XMMatrixRotationAxis(this->gfx.scene.camera.GetUpwardVector(), -0.003f * me.GetPosX());
			rotMatrix *= XMMatrixRotationAxis(this->gfx.scene.camera.GetRightVector(), 0.003f * me.GetPosY());

			this->gfx.scene.camera.AdjustRotation(rotMatrix);
		}
		if (me.GetType() == MouseEvent::EventType::WheelUp && !this->gfx.scene.blockMouseWheel) {
			POINT point = { me.GetPosX(), me.GetPosY() };
			ScreenToClient(this->render_window.GetHWND(), &point);
			XMFLOAT2 scaleCenter = this->gfx.ScreenCoords2NDC(point.x, point.y);
			this->gfx.scene.camera.AdjustScale(1.0f / 1.1f, scaleCenter);
		}
		if (me.GetType() == MouseEvent::EventType::WheelDown && !this->gfx.scene.blockMouseWheel) {
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

		if (me.GetType() == MouseEvent::LPress) {
			this->gfx.scene.HandleLMouse(me.GetPosX(), me.GetPosY(), true);
		}
		if (me.GetType() == MouseEvent::LRelease) {
			this->gfx.scene.HandleLMouse(me.GetPosX(), me.GetPosY(), false);
		}
		if (me.GetType() == MouseEvent::EventType::RAW_MOVE && this->mouse.IsLeftDown()) {
			if (this->gfx.scene.NavGizmoActive()) {
				POINT cp; GetCursorPos(&cp);
				ScreenToClient(this->render_window.GetHWND(), &cp);
				this->gfx.scene.HandleNavGizmoDrag(cp.x, cp.y);
			}
			else if (this->gfx.scene.orientationTransformer.HasActiveObject()) {
				XMFLOAT2 actionAxisScreen = { static_cast<float>(me.GetPosX()), -static_cast<float>(me.GetPosY()) };

				POINT actionPointPoint;
				GetCursorPos(&actionPointPoint);
				ScreenToClient(this->render_window.GetHWND(), &actionPointPoint);
				XMFLOAT2 actionPointNdc = this->gfx.ScreenCoords2NDC(actionPointPoint.x, actionPointPoint.y);

				this->gfx.scene.orientationTransformer.HandleObjMove(actionAxisScreen, actionPointNdc, this->gfx.scene.camera.GetViewMatrix(), this->gfx.scene.camera.GetProjectionMatrix(), this->gfx.scene.camera.GetScale());
			}
		}
	}
}
