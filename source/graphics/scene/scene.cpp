#include "scene.h"
#include <fstream>

bool Scene::Initialize(ID3D11Device* device, ID3D11DeviceContext* deviceContext, std::wstring shadersPath, ID3D11DepthStencilView* dsView, ID3D11DepthStencilView* dsViewNoMSAA, ID3D11RenderTargetView* mainRTV, VertexShader* vs, int windowWidth, int windowHeight)
{
	this->device = device;
	this->deviceContext = deviceContext;

	this->dsView = dsView;
	this->dsViewNoMSAA = dsViewNoMSAA;
	this->mainRTV = mainRTV;

	this->vsMain = vs;
	this->shadersPath = shadersPath;

	this->width = windowWidth;
	this->height = windowHeight;

	PrimitiveConstructor::device = device;
	PrimitiveConstructor::deviceContext = deviceContext;

	InitializeDirectX();

	camera.SetProjectionValues(this->width, this->height, -1000000, 1000000);
	this->camera.SetRotation(Projections::DIM);
	this->camera.SetPosition(BaseVectors::ORIGIN);

	/*XMFLOAT4 transparent = GenerateRandomFloat4(1);
	transparent.w = 0.4f;
	this->AddSphere(100, XMFLOAT3(0, 0, 0), 0, transparent);
	transparent.x = 1.f - transparent.x;
	transparent.y = 1.f - transparent.y;
	transparent.z = 1.f - transparent.z;
	this->AddSphere(100, XMFLOAT3(200, 0, 0), 1, transparent);

	this->AddSphere(150, XMFLOAT3(-200, 0, 0), 2, GenerateRandomFloat4(1));
	this->AddSphere(150, XMFLOAT3(0, 200, 0), 3, GenerateRandomFloat4(1));
	this->AddSphere(100, XMFLOAT3(0, -200, 0), 4, GenerateRandomFloat4(1));

	this->AddSphere(100, XMFLOAT3(0, 0, 200), 5, GenerateRandomFloat4(1));
	this->AddSphere(GenerateRandomFloat3(150).x, XMFLOAT3(0, 0, -200), 6, GenerateRandomFloat4(1));
	this->AddSphere(GenerateRandomFloat3(150).x, XMFLOAT3(0, 0, -200), 6, GenerateRandomFloat4(1));*/
	
	//XMFLOAT3 vel1{ 0.0f, 0.0f,  1.5f };
	//XMFLOAT3 vel2{ 0.0f, 0.0f, -1.5f };

	//const float G = 10.0f;
	//const float m1 = 1.0f;
	//const float m2 = 1.0f;

	//Primitive* other = this->primitives[7];
	//Primitive* other2 = this->primitives[6];
	//this->primitives[6]->SetUpdater([other, vel1, G, m2](Primitive& p, float t, float dt) mutable {
	//	if (!other) return;

	//	XMFLOAT3 pos = p.GetPosition();
	//	XMFLOAT3 opos = other->GetPosition();

	//	float dx = opos.x - pos.x;
	//	float dy = opos.y - pos.y;
	//	float dz = opos.z - pos.z;
	//	float dist = sqrtf(dx * dx + dy * dy + dz * dz);
	//	if (dist < 0.5f) return;  // предотвращаем сингулярность

	//	float force = G * m2 / (dist * dist);
	//	float nx = dx / dist, ny = dy / dist, nz = dz / dist;

	//	vel1.x += nx * force * dt;
	//	vel1.y += ny * force * dt;
	//	vel1.z += nz * force * dt;

	//	p.SetPosition({ pos.x + vel1.x * dt,
	//					pos.y + vel1.y * dt,
	//					pos.z + vel1.z * dt });
	//	});

	//this->primitives[7]->SetUpdater([other2, vel2, G, m1](Primitive& p, float t, float dt) mutable {
	//	if (!other2) return;

	//	XMFLOAT3 pos = p.GetPosition();
	//	XMFLOAT3 opos = other2->GetPosition();

	//	float dx = opos.x - pos.x;
	//	float dy = opos.y - pos.y;
	//	float dz = opos.z - pos.z;
	//	float dist = sqrtf(dx * dx + dy * dy + dz * dz);
	//	if (dist < 0.5f) return;

	//	float force = G * m1 / (dist * dist);
	//	float nx = dx / dist, ny = dy / dist, nz = dz / dist;

	//	vel2.x += nx * force * dt;
	//	vel2.y += ny * force * dt;
	//	vel2.z += nz * force * dt;

	//	p.SetPosition({ pos.x + vel2.x * dt,
	//					pos.y + vel2.y * dt,
	//					pos.z + vel2.z * dt });
	//	});

	//std::vector< XMFLOAT3> poses = {
	//	{-400, -400, -400},
	//	{400, -400, -400},
	//	{400, 400, -400},
	//	{-400, 400, -400}
	//};
	//this->AddPolygon(poses, Colors::RED);
	//std::vector< XMFLOAT3> poses2 = {
	//	{-500, -500, -500},
	//	{500, 500, 500}
	//};
	//std::vector< XMFLOAT3> poses3 = {
	//	{-500, 500, -500},
	//	{500, -500, 500}
	//};
	//std::vector< XMFLOAT3> poses4 = {
	//	{500, -500, -500},
	//	{-500, 500, -500}
	//};
	//this->AddLine(poses2, Colors::BLUE);
	//this->AddLine(poses3, Colors::BLUE);
	//this->AddLine(poses4, Colors::BLUE);
	//this->AddPoint(poses2[0], Colors::WHITE);

	/*for (float x = -100; x < 100; x += 4) {
		for (float y = -100; y < 100; y += 4) {
			float z = x * y / 50.f;
			std::vector<XMFLOAT3> poses = { BaseVectors::ORIGIN, XMFLOAT3(x, y, z) };
			this->AddLine(poses, XMFLOAT4(abs(z) / 100.f, 0, 1-abs(z) / 100.f, 1.f));
			this->primitives.back()->SetUpdater([x, y](Primitive& p, float t, float dt) {
				float z = 10 * sinf(t + x / 10.f) * cosf(t + y / 10.f);
				p.SetPosition(XMFLOAT3(x, y, z)); p.SetColor(XMFLOAT4(abs(z) / 10.f, 0, 1 - abs(z) / 10.f, 1.f));
				});
		}
	}*/
	this->AddPoint(BaseVectors::ORIGIN, Colors::RED);
	this->AddPoint(BaseVectors::ORIGIN, Colors::GREEN);
	this->primitives[0]->SetUpdater([](Primitive& p, float t, float dt) {
		p.SetPosition(XMFLOAT3(100 * cosf(t), 0, 100 * sinf(t)));
		});
	this->primitives[1]->SetUpdater([](Primitive& p, float t, float dt) {

		p.SetPosition(XMFLOAT3(0, 100 * cosf(t), 100 * sinf(t)));
		});
		

	this->orientationTransformer.Initialize(device, deviceContext);

	this->UpdateLight();
	this->UpdateSavedScenes();
	this->tpsTimer.Start();
		
	return true;
}

void Scene::OnResize(ID3D11DepthStencilView* dsView, ID3D11DepthStencilView* dsViewNoMSAA, ID3D11RenderTargetView* mainRTV, int newWidth, int newHeight)
{
	this->dsView = dsView;
	this->dsViewNoMSAA = dsViewNoMSAA;
	this->mainRTV = mainRTV;
	this->width = newWidth;
	this->height = newHeight;
	this->camera.SetProjectionValues(this->width, this->height, -1000000, 1000000);

	try {
		HRESULT hr = CreateResources();
		COM_ERROR_IF_FAILED(hr, "Failed to create resources.");
	}
	catch (COMException& exception) {
		ErrorLogger::Log(exception);
	}
}

ID3D11RenderTargetView* Scene::GetMaskRTV() const
{
	return this->maskRTV.Get();
}

void Scene::Draw()
{
	DrawGrid();
	std::vector<Primitive*> primitivesOrdered = GetPrimitivesSorted();

	this->deviceContext->ClearRenderTargetView(this->primitivesIDsRTV.Get(), Colors::clearColor);
	for (Primitive* p : primitivesOrdered) {
		UCHAR dim = p->GetDimension();
		switch (dim) {
		case 0: this->deviceContext->GSSetShader(this->geometryshaderpoints.GetShader(), NULL, 0); break;
		case 1: this->deviceContext->GSSetShader(this->geometryshaderthickness.GetShader(), NULL, 0); break;
		case 2: this->deviceContext->GSSetShader(nullptr, NULL, 0); break;
		}

		XMMATRIX projectionMatrix = this->camera.GetProjectionMatrix();

		this->deviceContext->RSSetState(this->rasterizerSolid.Get());


		this->deviceContext->OMSetRenderTargets(1, this->rtvIDs, this->dsViewNoMSAA);
		this->deviceContext->PSSetShader(this->pixelShaderWriteIDs.GetShader(), NULL, 0);
		this->deviceContext->PSSetConstantBuffers(1, 1, this->cb_ps_id.GetAddressOf());
		this->SetIDToWrite(p->id);

		p->Draw(this->camera.GetViewMatrix(), projectionMatrix);

		this->deviceContext->OMSetRenderTargets(1, this->rtvsMain, this->dsView);
		this->deviceContext->PSSetShader(this->pixelShaderMain.GetShader(), NULL, 0);


		if (dim < 2) {
			XMFLOAT4 col = p->GetColor();
			if (p->selected) {
				p->SetColor(Colors::SelectedColor);
				this->deviceContext->OMSetDepthStencilState(this->dsStateNoDepth.Get(), 0);
			}
			p->Draw(this->camera.GetViewMatrix(), projectionMatrix);

			p->SetColor(col);
			this->deviceContext->OMSetDepthStencilState(this->dsStateDepth.Get(), 0);
			continue;
		}

		if (this->rsSolid) {
			this->deviceContext->ClearRenderTargetView(this->maskRTV.Get(), Colors::clearColor);

			bool transparent = p->GetTransparent();
			if (transparent) this->deviceContext->OMSetDepthStencilState(this->dsStateDepthNoWrite.Get(), 0);
			

			if (this->outlineThroughObjets && p->selected){
				this->deviceContext->OMSetRenderTargets(3, this->rtvsMask, this->dsView);
				this->deviceContext->OMSetDepthStencilState(this->dsStateNoDepth.Get(), 0);
				p->Draw(this->camera.GetViewMatrix(), projectionMatrix);

				this->deviceContext->OMSetRenderTargets(1, this->rtvsMain, this->dsView);

				if(transparent) this->deviceContext->OMSetDepthStencilState(this->dsStateDepthNoWrite.Get(), 0);
				else this->deviceContext->OMSetDepthStencilState(this->dsStateDepth.Get(), 0);
			}
			else {
				this->deviceContext->OMSetRenderTargets(3, this->rtvsMainMask, this->dsView);
			}

			p->Draw(this->camera.GetViewMatrix(), projectionMatrix);

			this->deviceContext->ResolveSubresource(
				this->geometryPresenceMaskResolved.Get(), 0,
				this->geometryPresenceMask.Get(), 0,
				DXGI_FORMAT_R8_UNORM
			);

			SetOutline(p->selected ? Colors::SelectedColor : Colors::BLACK, p->selected ? 0.3f : 0.1f);
			this->RenderOutlineToTexture(this->outlineThroughObjets && p->selected);
		}
		if (this->rsWireframe) {
			this->deviceContext->RSSetState(this->rasterizerWireframe.Get());

			XMFLOAT4 col = p->GetColor();
			bool illumination = p->GetIlluminationCapability();

			p->SetColor(XMFLOAT4(1 - col.x, 1 - col.y, 1 - col.z, 1));
			p->SetIlluminationCapability(false);

			p->Draw(this->camera.GetViewMatrix(), projectionMatrix);

			p->SetColor(col);
			p->SetIlluminationCapability(illumination);
		}
	}
	this->mainRTV->GetResource(this->mainRTVTexture.GetAddressOf());

	this->deviceContext->ResolveSubresource(
		this->mainRTVTextureResolved.Get(), 0,
		this->mainRTVTexture.Get(), 0,
		DXGI_FORMAT_R8G8B8A8_UNORM
	);
	this->mainRTVTexture = nullptr;
	this->deviceContext->ResolveSubresource(
		this->outlinesTextureResolved.Get(), 0,
		this->outlinesTexture.Get(), 0,
		DXGI_FORMAT_R8G8B8A8_UNORM
	);

	this->deviceContext->ClearRenderTargetView(this->outlinesRTV.Get(), Colors::clearColor);
	this->RenderOutline();


	this->deviceContext->OMSetDepthStencilState(this->dsStateNoDepth.Get(), 0);
	this->deviceContext->GSSetShader(nullptr, NULL, 0);
	this->orientationTransformer.Draw(this->camera.GetViewMatrix(), this->camera.GetProjectionMatrix(), this->camera.GetScale());

	this->deviceContext->OMSetRenderTargets(1, this->rtvIDs, this->dsViewNoMSAA);
	this->deviceContext->PSSetShader(this->pixelShaderWriteIDs.GetShader(), NULL, 0);
	this->deviceContext->PSSetConstantBuffers(1, 1, this->cb_ps_id.GetAddressOf());
	std::vector<UINT> idsAux = this->orientationTransformer.GetAuxiliaryObjectsIDs();

	for (UINT id : idsAux) {
		this->SetIDToWrite(id);
		this->orientationTransformer.DrawID(this->camera.GetViewMatrix(), this->camera.GetProjectionMatrix(), this->camera.GetScale(), id);
	}

	this->SetMainResources();
	this->deviceContext->OMSetDepthStencilState(this->dsStateDepth.Get(), 0);

	UpdateTime();
}

void Scene::HandleSelection(Primitive* primitiveClicked)
{
	if (!primitiveClicked) {
		for (Primitive* p : this->primitives) {
			p->selected = false;
		}
		this->orientationTransformer.SetTargetObject(nullptr);
		return;
	}
	primitiveClicked->selected = !primitiveClicked->selected;
	if (primitiveClicked->selected) this->orientationTransformer.SetTargetObject(primitiveClicked);
	else this->orientationTransformer.SetTargetObject(nullptr);

	if (!(GetAsyncKeyState(VK_SHIFT) & 0x8000)) {
		for (Primitive* p : this->primitives) {
			if (p == primitiveClicked) continue;
			p->selected = false;
		}
	}
}

void Scene::HandleLMouse(int px, int py, bool tPressfRelease)
{
	static bool blockNextLMouseRelease = false;

	try {
		if (px < 0 || py < 0 || px >= this->width || py >= this->height || this->blockMousePick) return;

		UINT id = this->GetIdByPixel(px, py);

		if (tPressfRelease) {
			std::vector<UINT> auxIDs = this->orientationTransformer.GetAuxiliaryObjectsIDs();
			for (UINT auxID : auxIDs) {
				if (id == auxID) {
					this->orientationTransformer.HandleObjPress(id);
					blockNextLMouseRelease = true;
					break;
				}
			}
			return;
		}
		this->orientationTransformer.HandleObjRelease();

		if (blockNextLMouseRelease) { blockNextLMouseRelease = false; return; }

		Primitive* primitiveClicked = this->GetPrimitiveByID(id);
		HandleSelection(primitiveClicked);
	}
	catch (COMException& exception) {
		ErrorLogger::Log(exception);
	}
}

void Scene::AddPoint(const XMFLOAT3& pos, const XMFLOAT4& col)
{
	this->primitives.push_back(PrimitiveConstructor::Point(pos, col, this->primitives.size() + 1));
}

void Scene::AddLine(const std::vector<XMFLOAT3>& poses, const XMFLOAT4& col)
{
	this->primitives.push_back(PrimitiveConstructor::Line(poses, col, this->primitives.size() + 1));
}

void Scene::AddPolygon(const std::vector<XMFLOAT3>& poses, const XMFLOAT4& col)
{
	this->primitives.push_back(PrimitiveConstructor::Polygon(poses, col, this->primitives.size() + 1));
}

void Scene::AddSphere(float radius, const XMFLOAT3& pos, const UINT numSubdivides, const XMFLOAT4& col)
{
	this->primitives.push_back(PrimitiveConstructor::Sphere(radius, pos, numSubdivides, col, this->primitives.size() + 1));
}

void Scene::AddLine3d(float radius, std::vector<XMFLOAT3>& poses, const UINT numSubdivides, const XMFLOAT4& col)
{
	this->primitives.push_back(PrimitiveConstructor::Line3d(radius, poses, numSubdivides, col, this->primitives.size() + 1));
}

void Scene::UpdateLight()
{
	for (Primitive* p : this->primitives) {
		if (!p->GetIlluminationCapability()) continue;
		p->SetLighting(this->ambient, this->intensity, this->shininess);
		p->SetSmoothShading(this->smoothShade);
	}
	this->orientationTransformer.UpdateLighting(this->ambient, this->intensity, this->shininess, this->smoothShade);
}

const std::vector<std::string>& Scene::GetSavedScenes() const
{
	return this->savedScenes;
}

void Scene::SaveScene(std::string name)
{
	json scene{};
	for (size_t i = 0; i < this->primitives.size(); ++i) {
		Primitive* p = this->primitives[i];
		jsonSaver::to_json(scene["primitives"][i], *p);
	}
	
	XMFLOAT3 camPos = this->camera.GetPositionFloat3();
	XMMATRIX camRot = this->camera.GetRotMatrix();
	float camScale	= this->camera.GetScale();

	scene["camera"]["camPos"] = { camPos.x, camPos.y, camPos.z };
	scene["camera"]["camRot"] = { 
		camRot.r[0].m128_f32[0], camRot.r[0].m128_f32[1], camRot.r[0].m128_f32[2], camRot.r[0].m128_f32[3],
		camRot.r[1].m128_f32[0], camRot.r[1].m128_f32[1], camRot.r[1].m128_f32[2], camRot.r[1].m128_f32[3],
		camRot.r[2].m128_f32[0], camRot.r[2].m128_f32[1], camRot.r[2].m128_f32[2], camRot.r[2].m128_f32[3],
		camRot.r[3].m128_f32[0], camRot.r[3].m128_f32[1], camRot.r[3].m128_f32[2], camRot.r[3].m128_f32[3]
	};
	scene["camera"]["camScale"] = camScale;
	
	std::ofstream file(this->scenesPath + name);

	if (file.is_open()) {
		file << scene.dump(4);
		file.close();
	}
	else {
		ErrorLogger::Log("Failed to save scene.");
		return;
	}

	this->UpdateSavedScenes();
}

void Scene::ClearScene()
{
	for (Primitive* p : this->primitives) {
		delete p;
	}
	this->primitives.clear();
}

void Scene::LoadScene(std::string name)
{
	std::ifstream file(this->scenesPath + name);

	if (!file.is_open()) {
		ErrorLogger::Log("Failed to open scene.");
		return;
	}

	if(this->primitives.size()){
		ClearScene();
	}

	json data;
	try {
		data = json::parse(file);
	}
	catch (const json::parse_error& e) {
		ErrorLogger::Log("Failed to parse scene JSON: " + std::string(e.what()));
		return;
	}

	for (json j : data["primitives"]) {
		Primitive* p = new Primitive(this->device, this->deviceContext);
		if(!jsonSaver::from_json(j, *p)) continue;

		this->primitives.push_back(p);
	}


	XMFLOAT3 camPos = XMFLOAT3(data["camera"]["camPos"][0], data["camera"]["camPos"][1], data["camera"]["camPos"][2]);
	XMMATRIX camRot = XMMATRIX(data["camera"]["camRot"].get<std::vector<float>>().data());
	float camScale = data["camera"]["camScale"];

	this->camera.SetScale(camScale);
	this->camera.SetRotation(camRot);
	this->camera.SetPosition(camPos);
	this->UpdateLight();
}

void Scene::UpdateTime()
{
	float dtSec = this->tpsTimer.GetMillisecondsElapsed() * 0.001f;
	this->deltaTime = dtSec * this->timeSpeed;
	this->currentTime += this->deltaTime;
	this->tpsTimer.Restart();
	for (auto& prim : this->primitives)
		prim->Update(this->currentTime, this->deltaTime);

	this->orientationTransformer.Update();
}

bool Scene::InitializeDirectX()
{
	if (!this->geometryshaderpoints.Initialize(this->device, this->shadersPath + L"geometryshaderpoints.cso"))
		return false;

	if (!this->geometryshaderthickness.Initialize(this->device, this->shadersPath + L"geometryshaderthickness.cso"))
		return false;

	try {
		HRESULT hr{};
		{ // RASTERIZER STATES
			CD3D11_RASTERIZER_DESC rasterizerDesc(D3D11_DEFAULT);
			rasterizerDesc.CullMode = D3D11_CULL_BACK;
			rasterizerDesc.FillMode = D3D11_FILL_SOLID;
			rasterizerDesc.AntialiasedLineEnable = TRUE;
			rasterizerDesc.MultisampleEnable = TRUE;
			rasterizerDesc.FrontCounterClockwise = TRUE;
			hr = this->device->CreateRasterizerState(&rasterizerDesc, this->rasterizerSolid.GetAddressOf());
			rasterizerDesc.FillMode = D3D11_FILL_WIREFRAME;
			hr = this->device->CreateRasterizerState(&rasterizerDesc, this->rasterizerWireframe.GetAddressOf());
			COM_ERROR_IF_FAILED(hr, "Failed to create rasterizer state.");
		}


		{ // DEPTH STENCIL STATES
			CD3D11_DEPTH_STENCIL_DESC depthStencilStateDesc(D3D11_DEFAULT);
			depthStencilStateDesc.DepthFunc = D3D11_COMPARISON_FUNC::D3D11_COMPARISON_LESS_EQUAL;
			hr = this->device->CreateDepthStencilState(&depthStencilStateDesc, this->dsStateDepth.GetAddressOf());
			COM_ERROR_IF_FAILED(hr, "Failed to create depth stencil state.");

			depthStencilStateDesc.DepthEnable = FALSE;
			hr = this->device->CreateDepthStencilState(&depthStencilStateDesc, this->dsStateNoDepth.GetAddressOf());
			COM_ERROR_IF_FAILED(hr, "Failed to create depth stencil state.");

			depthStencilStateDesc.DepthEnable = TRUE;
			depthStencilStateDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			hr = this->device->CreateDepthStencilState(&depthStencilStateDesc, this->dsStateDepthNoWrite.GetAddressOf());
		}


		{ // SAMPLER STATE
			D3D11_SAMPLER_DESC sampDesc = {};
			sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
			device->CreateSamplerState(&sampDesc, this->samplerState.GetAddressOf());
			this->deviceContext->PSSetSamplers(0, 1, this->samplerState.GetAddressOf());
		}


		hr = CreateResources();
		COM_ERROR_IF_FAILED(hr, "Failed to create resources.");


		{ // SHADERS AND CONSTANT BUFFERS
			if (!this->vsFSQuad.Initialize(this->device, this->shadersPath + L"vertexshaderFSQuad.cso", nullptr, NULL))
				return false;
			if (!this->pixelShaderMain.Initialize(this->device, this->shadersPath + L"pixelshader.cso"))
				return false;
			if (!this->pixelShaderOutlineToTexture.Initialize(this->device, this->shadersPath + L"pixelshaderoutline.cso"))
				return false;
			if (!this->pixelShaderOutlineToScreen.Initialize(this->device, this->shadersPath + L"pixelshaderoutlinemerge.cso"))
				return false;
			if (!this->pixelShaderWriteIDs.Initialize(this->device, this->shadersPath + L"pixelshaderwriteids.cso"))
				return false;


			hr = this->cb_ps_outline.Initialize(this->device, this->deviceContext);
			COM_ERROR_IF_FAILED(hr, "Failed to create cb ps outline.");
			hr = this->cb_ps_id.Initialize(this->device, this->deviceContext);
			COM_ERROR_IF_FAILED(hr, "Failed to create cb ps id.");

			SetOutline(Colors::BLACK, 0.1f);
		}
	}
	catch (COMException& exception) {
		ErrorLogger::Log(exception);
		return false;
	}

	this->SetMainResources();
	return true;
}

HRESULT Scene::CreateResources()
{
	HRESULT hr{};
	try {
		this->geometryPresenceMask.Reset();
		this->geometryPresenceMaskResolved.Reset();
		this->outlinesTexture.Reset();
		this->outlinesTextureResolved.Reset();
		this->mainRTVTextureResolved.Reset();
		this->primitivesIDsTexture.Reset();
		this->primitivesIDsTextureStaging.Reset();

		this->maskRTV.Reset();
		this->outlinesRTV.Reset();
		this->primitivesIDsRTV.Reset();

		this->maskSRV.Reset();
		this->outlinesSRV.Reset();
		this->sceneSRV.Reset();


		CD3D11_TEXTURE2D_DESC maskDesc(
			DXGI_FORMAT_R8_UNORM,
			this->width,
			this->height
		);
		maskDesc.MipLevels = 1;
		maskDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
		maskDesc.SampleDesc.Count = 8;
		maskDesc.SampleDesc.Quality = 0;

		hr = this->device->CreateTexture2D(&maskDesc, nullptr, this->geometryPresenceMask.GetAddressOf());
		COM_ERROR_IF_FAILED(hr, "Failed to create texture for mask.");

		maskDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		maskDesc.SampleDesc.Count = 1;
		hr = this->device->CreateTexture2D(&maskDesc, nullptr, this->geometryPresenceMaskResolved.GetAddressOf());
		COM_ERROR_IF_FAILED(hr, "Failed to create texture for mask.");


		maskDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		maskDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
		maskDesc.SampleDesc.Count = 8;
		hr = this->device->CreateTexture2D(&maskDesc, nullptr, this->outlinesTexture.GetAddressOf());
		COM_ERROR_IF_FAILED(hr, "Failed to create texture for outlines.");

		maskDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		maskDesc.SampleDesc.Count = 1;
		hr = this->device->CreateTexture2D(&maskDesc, nullptr, this->outlinesTextureResolved.GetAddressOf());
		COM_ERROR_IF_FAILED(hr, "Failed to create texture for outlines.");
		hr = this->device->CreateTexture2D(&maskDesc, nullptr, this->mainRTVTextureResolved.GetAddressOf());
		COM_ERROR_IF_FAILED(hr, "Failed to create texture for main RTV.");


		maskDesc.Format = DXGI_FORMAT_R32_UINT;
		maskDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
		maskDesc.SampleDesc.Count = 1;
		hr = this->device->CreateTexture2D(&maskDesc, nullptr, this->primitivesIDsTexture.GetAddressOf());
		COM_ERROR_IF_FAILED(hr, "Failed to create texture for ids RTV.");

		maskDesc.BindFlags = 0;
		maskDesc.SampleDesc.Count = 1;
		maskDesc.Usage = D3D11_USAGE_STAGING;
		maskDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		hr = this->device->CreateTexture2D(&maskDesc, nullptr, this->primitivesIDsTextureStaging.GetAddressOf());
		COM_ERROR_IF_FAILED(hr, "Failed to create texture for staging ids RTV.");


		hr = this->device->CreateRenderTargetView(this->geometryPresenceMask.Get(), nullptr, this->maskRTV.GetAddressOf());
		COM_ERROR_IF_FAILED(hr, "Failed to create mask RTV.");
		hr = this->device->CreateRenderTargetView(this->outlinesTexture.Get(), nullptr, this->outlinesRTV.GetAddressOf());
		COM_ERROR_IF_FAILED(hr, "Failed to create outlines RTV.");
		hr = this->device->CreateRenderTargetView(this->primitivesIDsTexture.Get(), nullptr, this->primitivesIDsRTV.GetAddressOf());
		COM_ERROR_IF_FAILED(hr, "Failed to create ids RTV.");
		this->UpdateRTVs();


		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		hr = this->device->CreateShaderResourceView(this->geometryPresenceMaskResolved.Get(), nullptr, this->maskSRV.GetAddressOf());
		COM_ERROR_IF_FAILED(hr, "Failed to create mask SRV.");
		hr = this->device->CreateShaderResourceView(this->outlinesTextureResolved.Get(), nullptr, this->outlinesSRV.GetAddressOf());
		COM_ERROR_IF_FAILED(hr, "Failed to create outlines SRV.");
		hr = this->device->CreateShaderResourceView(this->mainRTVTextureResolved.Get(), nullptr, this->sceneSRV.GetAddressOf());
		COM_ERROR_IF_FAILED(hr, "Failed to create scene SRV.");

		this->SRVs[0] = this->maskSRV.Get();
		this->SRVs[1] = this->outlinesSRV.Get();
		this->SRVs[2] = this->sceneSRV.Get();
		this->deviceContext->PSSetShaderResources(0, 3, this->SRVs);
	}
	catch (COMException& exception) {
		ErrorLogger::Log(exception);
	}

	return hr;
}

void Scene::UpdateRTVs()
{
	this->rtvsMainMask[0] = this->mainRTV;
	this->rtvsMainMask[1] = this->maskRTV.Get();
	this->rtvsMainMask[2] = nullptr;

	this->rtvsMain[0] = this->mainRTV;
	this->rtvsMain[1] = nullptr;
	this->rtvsMain[2] = nullptr;

	this->rtvsMain3[0] = nullptr;
	this->rtvsMain3[1] = nullptr;
	this->rtvsMain3[2] = this->mainRTV;

	this->rtvsMask[0] = nullptr;
	this->rtvsMask[1] = this->maskRTV.Get();
	this->rtvsMask[2] = nullptr;

	this->rtvsOutlines[0] = nullptr;
	this->rtvsOutlines[1] = nullptr;
	this->rtvsOutlines[2] = this->outlinesRTV.Get();

	this->rtvIDs[0] = this->primitivesIDsRTV.Get();
}

void Scene::SetMainResources()
{
	this->deviceContext->IASetInputLayout(this->vsMain->GetInputLayout());
	this->deviceContext->VSSetShader(this->vsMain->GetShader(), NULL, 0);

	this->deviceContext->PSSetShader(this->pixelShaderMain.GetShader(), NULL, 0);

	this->deviceContext->OMSetDepthStencilState(this->dsStateDepth.Get(), 0);
	this->deviceContext->OMSetRenderTargets(3, this->rtvsMain, this->dsView);
}

void Scene::SetOutlineResources()
{
	this->deviceContext->IASetInputLayout(nullptr);
	this->deviceContext->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	this->deviceContext->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	this->deviceContext->VSSetShader(this->vsFSQuad.GetShader(), NULL, 0);

	this->deviceContext->OMSetDepthStencilState(this->dsStateNoDepth.Get(), 0);
}

void Scene::DrawGrid()
{
	this->deviceContext->GSSetShader(this->geometryshaderthickness.GetShader(), NULL, 0);

	const float baseGridSize = 128.f;
	const int linesNum = 40;
	const float logStep = 2.0f;

	auto scale = this->camera.GetScale();

	float logScale = std::log(scale) / std::log(logStep);
	float levelF = std::floor(logScale);
	float fraction = logScale - levelF;

	float step0 = baseGridSize * std::pow(logStep, levelF);
	float step1 = baseGridSize * std::pow(logStep, levelF + 1.f);

	float alpha0 = 1.0f - fraction;
	float alpha1 = fraction;

	auto drawGrid = [&](float gridSize, float alpha)
		{
			if (alpha <= 0.01f) return;

			XMFLOAT4 color = { Colors::GRAY.x, Colors::GRAY.y, Colors::GRAY.z, Colors::GRAY.w * alpha };

			for (float x = -20; x <= 20; x += 1)
			{
				std::vector<XMFLOAT3> poses = {
					XMFLOAT3(x * gridSize,  linesNum * gridSize / 2.f, 0),
					XMFLOAT3(x * gridSize, -linesNum * gridSize / 2.f, 0)
				};
				auto p1 = PrimitiveConstructor::Line(poses, color, 0);

				poses = {
					XMFLOAT3(linesNum * gridSize / 2.f, x * gridSize, 0),
					XMFLOAT3(-linesNum * gridSize / 2.f, x * gridSize, 0)
				};
				auto p2 = PrimitiveConstructor::Line(poses, color, 0);

				p1->Draw(this->camera.GetViewMatrix(), this->camera.GetProjectionMatrix());
				p2->Draw(this->camera.GetViewMatrix(), this->camera.GetProjectionMatrix());
				delete p1;
				delete p2;
			}
		};

	drawGrid(step0, alpha0);
	drawGrid(step1, alpha1);

	float axisLen = step1;

	struct AxisLine { XMFLOAT3 from; XMFLOAT3 to; XMFLOAT4 color; };
	AxisLine axes[] = {
		{ XMFLOAT3(0, 0, 0), XMFLOAT3(axisLen, 0,       0), Colors::RED   },
		{ XMFLOAT3(0, 0, 0), XMFLOAT3(0,       axisLen, 0), Colors::GREEN },
		{ XMFLOAT3(0, 0, 0), XMFLOAT3(0,       0,       axisLen), Colors::BLUE  },
	};

	for (auto& axis : axes)
	{
		std::vector<XMFLOAT3> poses = { axis.from, axis.to };
		auto line = PrimitiveConstructor::Line(poses, axis.color, 0);
		line->Draw(this->camera.GetViewMatrix(), this->camera.GetProjectionMatrix());
		delete line;
	}
}

std::vector<Primitive*>& Scene::GetPrimitivesSorted()
{
	size_t primitivesCount = this->primitives.size();
	static std::vector<Primitive*> primitivesOrdered;
	primitivesOrdered.clear();
	primitivesOrdered.reserve(primitivesCount);

	std::vector<Primitive*> nonSelOpaque;
	std::vector<Primitive*> selOpaque;
	std::vector<std::pair<Primitive*, float>> nonSelTransparent;
	std::vector<std::pair<Primitive*, float>> selTransparent;

	
	XMVECTOR camPosV = this->camera.GetPositionVector();
	XMVECTOR camForward = this->camera.GetForwardVector();

	for (Primitive* p : this->primitives) {
		bool transparent = p->GetTransparent();
		bool sel = p->selected;

		if (!transparent) {
			if (sel) selOpaque.push_back(p);
			else nonSelOpaque.push_back(p);
		}
		else {
			XMFLOAT3 posF = p->GetPosition();
			XMVECTOR posV = XMLoadFloat3(&posF);
			XMVECTOR rel = XMVectorSubtract(posV, camPosV);
			float proj = XMVectorGetX(XMVector3Dot(rel, camForward));
			if (sel) selTransparent.emplace_back(p, proj);
			else nonSelTransparent.emplace_back(p, proj);
		}
	}

	auto cmp = [](const std::pair<Primitive*, float>& a, const std::pair<Primitive*, float>& b) {
		return a.second > b.second;
		};
	std::sort(nonSelTransparent.begin(), nonSelTransparent.end(), cmp);
	std::sort(selTransparent.begin(), selTransparent.end(), cmp);


	primitivesOrdered.insert(primitivesOrdered.end(), nonSelOpaque.begin(), nonSelOpaque.end());
	primitivesOrdered.insert(primitivesOrdered.end(), selOpaque.begin(), selOpaque.end());
	for (auto& pr : nonSelTransparent) primitivesOrdered.push_back(pr.first);
	for (auto& pr : selTransparent) primitivesOrdered.push_back(pr.first);

	return primitivesOrdered;
}


void Scene::SetOutline(const XMFLOAT4& col, const float outlineScale)
{
	this->cb_ps_outline.data.screenSize[0] = this->width;
	this->cb_ps_outline.data.screenSize[1] = this->height;

	this->cb_ps_outline.data.outlineColor[0] = col.x;
	this->cb_ps_outline.data.outlineColor[1] = col.y;
	this->cb_ps_outline.data.outlineColor[2] = col.z;
	this->cb_ps_outline.data.outlineColor[3] = col.w;

	this->cb_ps_outline.data.outlineScale = outlineScale;

	this->cb_ps_outline.ApplyChanges();
}

void Scene::RenderOutlineToTexture(bool toTexture)
{
	this->SetOutlineResources();

	this->deviceContext->PSSetShader(this->pixelShaderOutlineToTexture.GetShader(), NULL, 0);
	this->deviceContext->PSSetConstantBuffers(0, 1, this->cb_ps_outline.GetAddressOf());
	this->deviceContext->OMSetRenderTargets(3, toTexture ? this->rtvsOutlines : this->rtvsMain3, this->dsView);

	this->deviceContext->Draw(6, 0);
	this->SetMainResources();
}

void Scene::RenderOutline()
{
	this->SetOutlineResources();

	this->deviceContext->PSSetShader(this->pixelShaderOutlineToScreen.GetShader(), NULL, 0);
	this->deviceContext->OMSetRenderTargets(3, this->rtvsMain, this->dsView);

	this->deviceContext->Draw(6, 0);
	this->SetMainResources();
}

void Scene::SetIDToWrite(UINT id)
{
	this->cb_ps_id.data.id = id;
	this->cb_ps_id.ApplyChanges();
}

Primitive* Scene::GetPrimitiveByID(UINT id)
{
	for (Primitive* p : this->primitives) {
		if (p->id == id) return p;
	}
	return nullptr;
}

UINT Scene::GetIdByPixel(int px, int py)
{
	this->deviceContext->Flush();

	this->deviceContext->CopyResource(
		this->primitivesIDsTextureStaging.Get(),
		this->primitivesIDsTexture.Get()
	);

	D3D11_MAPPED_SUBRESOURCE mapped = {};
	HRESULT hr = this->deviceContext->Map(this->primitivesIDsTextureStaging.Get(),
		0, D3D11_MAP_READ, 0, &mapped);
	COM_ERROR_IF_FAILED(hr, "Failed to map primitives IDs texture staging resource.");

	uint8_t* basePtr = (uint8_t*)mapped.pData;
	uint8_t* rowPtr = basePtr + py * mapped.RowPitch;
	UINT* pixel = (UINT*)(rowPtr + px * sizeof(uint32_t));
	UINT id = *pixel;

	this->deviceContext->Unmap(this->primitivesIDsTextureStaging.Get(), 0);

	return id;
}


void Scene::UpdateSavedScenes()
{
	std::vector<std::string> files;
	WIN32_FIND_DATAA file_data;
	HANDLE h_find = INVALID_HANDLE_VALUE;

	std::string search_path = this->scenesPath + "/*.json";
	h_find = FindFirstFileA(search_path.c_str(), &file_data);
	if (h_find == INVALID_HANDLE_VALUE) {
		return;
	}

	do {
		const std::string file_name = file_data.cFileName;
		files.push_back(file_name);
	} while (FindNextFileA(h_find, &file_data) != 0);

	FindClose(h_find);
	
	this->savedScenes = files;
}


