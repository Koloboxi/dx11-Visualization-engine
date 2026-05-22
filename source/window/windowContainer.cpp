#include "windowContainer.h"
#include <shellapi.h>
#include <algorithm>

WindowContainer::WindowContainer()
{
	static bool raw_input_initialized = false;
	if (raw_input_initialized == false) {
		RAWINPUTDEVICE rid;

		rid.usUsagePage = 0x01;
		rid.usUsage = 0x02;
		rid.dwFlags = 0;
		rid.hwndTarget = 0;

		if (RegisterRawInputDevices(&rid, 1, sizeof(rid)) == FALSE) {
			ErrorLogger::Log(GetLastError(), "Failed to register raw input devices.");
			exit(-1);
		}

		raw_input_initialized = true;
	}
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WindowContainer::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam))
		return true;

	switch (uMsg)
	{
		// mouse
	case WM_MOUSEMOVE: {
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		mouse.OnMouseMove(x, y);
		return 0;
	}
	case WM_LBUTTONDOWN: {
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		mouse.OnLeftPressed(x, y);
		return 0;
	}
	case WM_LBUTTONUP: {
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		mouse.OnLeftReleased(x, y);
		return 0;
	}
	case WM_RBUTTONDOWN: {
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		mouse.OnRightPressed(x, y);
		return 0;
	}
	case WM_RBUTTONUP: {
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		mouse.OnRightReleased(x, y);
		return 0;
	}
	case WM_MBUTTONDOWN: {
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		mouse.OnMiddlePressed(x, y);
		return 0;
	}
	case WM_MBUTTONUP: {
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		mouse.OnMiddleReleased(x, y);
		return 0;
	}
	case WM_MOUSEWHEEL: {
		int x = (int)(short)LOWORD(lParam);
		int y = (int)(short)HIWORD(lParam);
		if (GET_WHEEL_DELTA_WPARAM(wParam) > 0) {
			mouse.OnWheelUp(x, y);
		}
		else if(GET_WHEEL_DELTA_WPARAM(wParam) < 0){
			mouse.OnWheelDown(x, y);
		}
		return 0;
	}
	case WM_INPUT: {
		UINT dataSize;

		GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, NULL, &dataSize, sizeof(RAWINPUTHEADER));

		if (dataSize > 0) {
			std::unique_ptr<BYTE[]> rawdata = std::make_unique<BYTE[]>(dataSize);
			if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, rawdata.get(), &dataSize, sizeof(RAWINPUTHEADER)) == dataSize) {
				RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(rawdata.get());
				if (raw->header.dwType == RIM_TYPEMOUSE) {
					mouse.OnMouseMoveRaw(raw->data.mouse.lLastX, raw->data.mouse.lLastY);
				}
			}
		}
		return 0;
	}


		// keyboard
	case WM_KEYDOWN: {
		unsigned char keycode = static_cast<unsigned char>(wParam);
		if (keyboard.IsKeysAutoRepeat()) {
			keyboard.OnKeyPressed(keycode);
		}
		else {
			const bool wasPressed = lParam & 0x40000000;
			if (!wasPressed) {
				keyboard.OnKeyPressed(keycode);
			}
		}
		return 0;
	}
	case WM_KEYUP: {
		unsigned char keycode = static_cast<unsigned char>(wParam);
		keyboard.OnKeyReleased(keycode);
		return 0;
	}
	case WM_CHAR: {
		unsigned char ch = static_cast<unsigned char>(wParam);
		if (keyboard.IsCharsAutoRepeat()) {
			keyboard.OnChar(ch);
		}
		else
		{
			const bool wasPressed = lParam & 0x40000000;
			if (!wasPressed) {
				keyboard.OnChar(ch);
			}
		}
		return 0;
	}

	case WM_DROPFILES: {
		HDROP hDrop = reinterpret_cast<HDROP>(wParam);
		UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
		for (UINT i = 0; i < fileCount; ++i) {
			WCHAR path[MAX_PATH];
			if (!DragQueryFileW(hDrop, i, path, MAX_PATH)) continue;
			std::wstring wpath(path);

			// Case-insensitive .stl check
			std::wstring lower = wpath;
			std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
			if (lower.size() < 4 || lower.compare(lower.size() - 4, 4, L".stl") != 0) continue;

			// Filename stem (without directory and extension)
			size_t slash = wpath.find_last_of(L"\\/");
			std::wstring stem = (slash != std::wstring::npos) ? wpath.substr(slash + 1) : wpath;
			if (stem.size() >= 4) stem = stem.substr(0, stem.size() - 4);

			std::string pathStr(wpath.begin(), wpath.end());
			std::string nameStr(stem.begin(), stem.end());
			gfx.scene.AddFromSTL(pathStr, XMFLOAT4(0.6f, 0.6f, 0.9f, 1.0f), nameStr);
		}
		DragFinish(hDrop);
		return 0;
	}

	case WM_SIZE:
	{
		int newWidth = LOWORD(lParam);
		int newHeight = HIWORD(lParam);
		gfx.OnResize(newWidth, newHeight);

		return 0;
	}
	default:
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}
}