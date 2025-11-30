#include "graphics.h"

bool Graphics::Initialize(HWND hwnd, int width, int height) {
	this->windowWidth = width;
	this->windowHeight = height;
	this->fpsTimer.Start();

	if (!InitializeDirectX(hwnd))
		return false;
	if (!InitializeShaders())
		return false;

	if (!this->scene.Initialize(this->device.Get(), this->deviceContext.Get(), this->shadersPath, this->depthStencilView.Get(), this->depthStencilViewNoMSAA.Get(), this->renderTargetView.Get(), &this->vertexshader, this->windowWidth, this->windowHeight))
		return false;	


	ID3D11RenderTargetView* rtvs[] = { this->renderTargetView.Get(), this->scene.GetMaskRTV() };
	this->deviceContext->OMSetRenderTargets(2, rtvs, this->depthStencilView.Get());

	//IMGUI
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImPlot::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX11_Init(this->device.Get(), this->deviceContext.Get());
	ImGui::StyleColorsDark();
	
	return true;
}

void Graphics::OnResize(int newWidth, int newHeight)
{
	if (!this->deviceContext || newWidth == 0 || newHeight == 0) return;
	try{
		this->windowWidth = newWidth;
		this->windowHeight = newHeight;

		this->deviceContext->OMSetRenderTargets(0, nullptr, nullptr);
		this->deviceContext->Flush();
		this->renderTargetView.Reset();
		this->depthStencilView.Reset();
		this->depthStencilBuffer.Reset();
		
		HRESULT hr = this->swapchain->ResizeBuffers(
			0,
			newWidth,
			newHeight,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH
		);
		COM_ERROR_IF_FAILED(hr, "Failed to resize swap chain buffers.");

		
		Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
		hr = this->swapchain->GetBuffer(
			0,
			__uuidof(ID3D11Texture2D),
			reinterpret_cast<void**>(backBuffer.GetAddressOf())
		);
		COM_ERROR_IF_FAILED(hr, "Failed to get back buffer after resize.");
				
		hr = this->device->CreateRenderTargetView(
			backBuffer.Get(),
			nullptr,
			this->renderTargetView.GetAddressOf()
		);
		COM_ERROR_IF_FAILED(hr, "Failed to recreate render target view after resize.");

		
		CD3D11_TEXTURE2D_DESC depthStencilDesc(
			DXGI_FORMAT_D24_UNORM_S8_UINT,
			newWidth,
			newHeight
		);
		depthStencilDesc.MipLevels = 1;
		depthStencilDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		depthStencilDesc.SampleDesc.Count = 8;  
		depthStencilDesc.SampleDesc.Quality = 0;

		hr = this->device->CreateTexture2D(
			&depthStencilDesc,
			nullptr,
			this->depthStencilBuffer.GetAddressOf()
		);
		COM_ERROR_IF_FAILED(hr, "Failed to recreate depth stencil buffer after resize.");

		
		hr = this->device->CreateDepthStencilView(
			this->depthStencilBuffer.Get(),
			nullptr,
			this->depthStencilView.GetAddressOf()
		);
		COM_ERROR_IF_FAILED(hr, "Failed to recreate depth stencil view after resize.");

		depthStencilDesc.SampleDesc.Count = 1;
		hr = this->device->CreateDepthStencilView(
			this->depthStencilBuffer.Get(),
			nullptr,
			this->depthStencilViewNoMSAA.GetAddressOf()
		);
		COM_ERROR_IF_FAILED(hr, "Failed to recreate depth stencil view after resize.");

		CD3D11_VIEWPORT viewport(
			0.0f,
			0.0f,
			static_cast<float>(newWidth),
			static_cast<float>(newHeight)
		);
		this->deviceContext->RSSetViewports(1, &viewport);


		this->scene.OnResize(this->depthStencilView.Get(), this->depthStencilViewNoMSAA.Get(), this->renderTargetView.Get(), this->windowWidth, this->windowHeight);

		this->cb_gs_geometryshader.data.AspectRatio = (float)this->windowWidth / (float)this->windowHeight;
		this->cb_gs_geometryshader.ApplyChanges();
		this->deviceContext->GSSetConstantBuffers(0, 1, this->cb_gs_geometryshader.GetAddressOf());
	}
	catch (COMException& exception) {
		ErrorLogger::Log(exception);
	}
}

void Graphics::RenderFrame()
{
	static float bgcolor[] = { 0.117647f, 0.117647f, 0.117647f, 0.117647f };
	this->deviceContext->ClearRenderTargetView(this->renderTargetView.Get(), bgcolor);
	this->deviceContext->ClearDepthStencilView(this->depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	this->deviceContext->ClearDepthStencilView(this->depthStencilViewNoMSAA.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	this->deviceContext->IASetInputLayout(this->vertexshader.GetInputLayout());
	this->deviceContext->VSSetShader(this->vertexshader.GetShader(), NULL, 0);

	this->deviceContext->OMSetBlendState(this->blendState.Get(), NULL, 0xFFFFFFFF);

	this->scene.Draw();
	this->Gui();

	this->swapchain->Present(1, NULL);
}

int Graphics::GetClientWindowWidth()
{
	return this->windowWidth;
}

int Graphics::GetClientWindowHeight()
{
	return this->windowHeight;
}

XMFLOAT2 Graphics::ScreenCoords2NDC(int x, int y)
{
	float xf = (2.f * x / this->windowWidth) - 1.f;
	float yf = 1.f - (2.f * y / this->windowHeight);

	return XMFLOAT2(xf, yf);
}

bool Graphics::InitializeDirectX(HWND hwnd) {
	try {
		//Adapters (GPUS)
		std::vector<AdapterData> adapters = AdapterReader::GetAdapters();

		if (adapters.size() < 1) {
			ErrorLogger::Log("No IDXGI Adapters Found.");
			return false;
		}

		//Create device and swapchain
		DXGI_SWAP_CHAIN_DESC swap_chain_desc{};

		swap_chain_desc.BufferDesc.Width = this->windowWidth;
		swap_chain_desc.BufferDesc.Height = this->windowHeight;
		swap_chain_desc.BufferDesc.RefreshRate.Numerator = 120;
		swap_chain_desc.BufferDesc.RefreshRate.Denominator = 1;
		swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; 
		swap_chain_desc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		swap_chain_desc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

		swap_chain_desc.SampleDesc.Count = 8;
		swap_chain_desc.SampleDesc.Quality = 0;

		swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swap_chain_desc.BufferCount = 1;
		swap_chain_desc.OutputWindow = hwnd;
		swap_chain_desc.Windowed = TRUE;
		swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		swap_chain_desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

		HRESULT hr;
		hr = D3D11CreateDeviceAndSwapChain(
			adapters[0].pAdapter,
			D3D_DRIVER_TYPE_UNKNOWN,
			NULL,
			NULL,
			nullptr,
			0,
			D3D11_SDK_VERSION,
			&swap_chain_desc,
			this->swapchain.GetAddressOf(),
			this->device.GetAddressOf(),
			nullptr,
			this->deviceContext.GetAddressOf()
		);
		COM_ERROR_IF_FAILED(hr, "Failed to create device and swapchain.");

		Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
		hr = this->swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(backBuffer.GetAddressOf()));
		COM_ERROR_IF_FAILED(hr, "Failed to get buffer.");

		hr = this->device->CreateRenderTargetView(backBuffer.Get(), NULL, this->renderTargetView.GetAddressOf());
		COM_ERROR_IF_FAILED(hr, "Failed to create render target view.");

		//Describe Depth stencil buffer
		CD3D11_TEXTURE2D_DESC depthStencilDesc(DXGI_FORMAT_D24_UNORM_S8_UINT, this->windowWidth, this->windowHeight);
		depthStencilDesc.MipLevels = 1;
		depthStencilDesc.SampleDesc.Count = 8;
		depthStencilDesc.SampleDesc.Quality = 0;
		depthStencilDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

		hr = this->device->CreateTexture2D(&depthStencilDesc, NULL, this->depthStencilBuffer.GetAddressOf());
		COM_ERROR_IF_FAILED(hr, "Failed to create depth stencil buffer.");

		hr = this->device->CreateDepthStencilView(this->depthStencilBuffer.Get(), NULL, this->depthStencilView.GetAddressOf());
		COM_ERROR_IF_FAILED(hr, "Failed to create depth stencil view.");

		depthStencilDesc.SampleDesc.Count = 1;
		hr = this->device->CreateTexture2D(&depthStencilDesc, NULL, this->depthStencilBufferNoMSAA.GetAddressOf());
		COM_ERROR_IF_FAILED(hr, "Failed to create depth stencil buffer.");

		hr = this->device->CreateDepthStencilView(this->depthStencilBufferNoMSAA.Get(), NULL, this->depthStencilViewNoMSAA.GetAddressOf());
		COM_ERROR_IF_FAILED(hr, "Failed to create depth stencil view.");

		//Create + set the Viewport
		CD3D11_VIEWPORT viewport(0.0f, 0.0f, static_cast<FLOAT>(this->windowWidth), static_cast<FLOAT>(this->windowHeight));

		this->deviceContext->RSSetViewports(1, &viewport);

		//Create blend state
		D3D11_BLEND_DESC blendDesc{};
		D3D11_RENDER_TARGET_BLEND_DESC rtbd{};

		rtbd.BlendEnable = true;
		rtbd.SrcBlend = D3D11_BLEND::D3D11_BLEND_SRC_ALPHA;
		rtbd.DestBlend = D3D11_BLEND::D3D11_BLEND_INV_SRC_ALPHA;
		rtbd.BlendOp = D3D11_BLEND_OP::D3D11_BLEND_OP_ADD;
		rtbd.SrcBlendAlpha = D3D11_BLEND::D3D11_BLEND_ONE;
		rtbd.DestBlendAlpha = D3D11_BLEND::D3D11_BLEND_ZERO;
		rtbd.BlendOpAlpha = D3D11_BLEND_OP::D3D11_BLEND_OP_ADD;
		rtbd.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE::D3D11_COLOR_WRITE_ENABLE_ALL;

		blendDesc.RenderTarget[0] = rtbd;

		hr = this->device->CreateBlendState(&blendDesc, this->blendState.GetAddressOf());
		COM_ERROR_IF_FAILED(hr, "Failed to create blend state.");
	}
	catch (COMException& exception) {
		ErrorLogger::Log(exception);
	}
	return true;
}

bool Graphics::InitializeShaders()
{
	wchar_t buff[256];
	_wgetcwd(buff, 256);

	this->shadersPath = std::wstring(buff) + L"\\";

	D3D11_INPUT_ELEMENT_DESC layout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,   D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,  D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};
	UINT numElements = ARRAYSIZE(layout);

	if (!vertexshader.Initialize(this->device.Get(), shadersPath + L"vertexshader.cso", layout, numElements))
		return false;

	this->cb_gs_geometryshader.Initialize(this->device.Get(), this->deviceContext.Get());
	this->cb_gs_geometryshader.data.AspectRatio = (float)this->windowWidth / (float)this->windowHeight;
	this->cb_gs_geometryshader.ApplyChanges();
	this->deviceContext->GSSetConstantBuffers(0, 1, this->cb_gs_geometryshader.GetAddressOf());

	return true;
}
