#pragma once
#include "renderWindow.h"
#include "..\graphics\graphics.h"
#include "..\IO\keyboardClass.h"
#include "..\IO\mouseClass.h"

class WindowContainer{
public:
	WindowContainer();
	LRESULT WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
protected:
	RenderWindow render_window;
	Graphics gfx;
	KeyboardClass keyboard;
	MouseClass  mouse;
private:
};