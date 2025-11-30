#pragma once
#include "..\utils\adapterReader.h"
#include "..\utils\Timer.h"
#include "shaders\shaders.h"
#include <SpriteBatch.h>
#include <SpriteFont.h>
#include <WICTextureLoader.h>
#include "imgui\imgui.h"
#include "imgui\implot.h"
#include "imgui\imgui_impl_win32.h"
#include "imgui\imgui_impl_dx11.h"
#include "imgui\imgui_stdlib.h"
#include "Model.h"
#include "scene\scene.h"

class Graphics
{
public:
	bool Initialize(HWND hwnd, int width, int height);
	void OnResize(int newWidth, int newHeight);
	void RenderFrame();
	
	int GetClientWindowWidth();
	int GetClientWindowHeight();
	XMFLOAT2 ScreenCoords2NDC(int x, int y);

	Scene scene;
private:
	bool InitializeDirectX(HWND hwnd);
	bool InitializeShaders();

	void Gui();

	int windowWidth = 0;
	int windowHeight = 0;

	Microsoft::WRL::ComPtr<ID3D11Device> device;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext;
	Microsoft::WRL::ComPtr<IDXGISwapChain> swapchain;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTargetView;

	std::wstring shadersPath;
	VertexShader vertexshader;

	ConstantBuffer<CB_GS_geometryshader> cb_gs_geometryshader{};

	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthStencilView;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> depthStencilBuffer;

	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthStencilViewNoMSAA;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> depthStencilBufferNoMSAA;

	Microsoft::WRL::ComPtr<ID3D11BlendState> blendState;

	Timer fpsTimer;
};
