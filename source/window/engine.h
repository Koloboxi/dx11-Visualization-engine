#pragma once
#include "windowContainer.h"
#include "..\utils\Timer.h"

class Engine : WindowContainer
{
public:
	bool Initialize(HINSTANCE hInstance, std::string window_title, std::string window_class, int width, int height);
	bool ProcessMessages();
	void RenderFrame();
	void Update();
private:
	Timer timer;
};