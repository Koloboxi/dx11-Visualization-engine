#include "window\Engine.h"

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine,
	_In_ int nCmdShow) 
{
	try {
		HRESULT hr = CoInitialize(NULL);
		if (FAILED(hr)) {
			ErrorLogger::Log(hr, "Failed to CoInitialize.");
			return 0;
		}

		Engine engine;

		RECT workArea;
		SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);

		int width = workArea.right - workArea.left; 
		int height = workArea.bottom - workArea.top;

		if (engine.Initialize(hInstance, "Title", "MyWindowClass", width, height)) {
			while (engine.ProcessMessages()) {
				engine.Update();
				engine.RenderFrame();
			}
		}
	}
	catch (const std::exception& e) {
		ErrorLogger::Log(e.what());
	}
	
	return 0;
}