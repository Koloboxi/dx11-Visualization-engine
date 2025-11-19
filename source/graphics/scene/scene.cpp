#include "scene.h"
#include <fstream>

bool Scene::Initialize(ID3D11Device* device, ID3D11DeviceContext* deviceContext, std::wstring shadersPath, ID3D11DepthStencilView* dsView, ID3D11RenderTargetView* mainRTV, VertexShader* vs, int windowWidth, int windowHeight)
{
	this->device = device;
	this->deviceContext = deviceContext;

	this->dsView = dsView;
	this->mainRTV = mainRTV;

	this->vsMain = vs;
	this->shadersPath = shadersPath;

	this->width = windowWidth;
	this->height = windowHeight;

	InitializeDirectX();

	camera.SetProjectionValues(this->width, this->height, -1000000, 1000000);
	this->camera.SetRotation(Projections::DIM);
	this->camera.SetPosition(BaseVectors::ORIGIN);

	XMFLOAT4 transparent = GenerateRandomFloat4(1);
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
	this->AddSphere(100, XMFLOAT3(0, 0, -200), 6, GenerateRandomFloat4(1));

	std::vector< XMFLOAT3> poses = {
		{-400, -400, -400},
		{400, -400, -400},
		{400, 400, -400},
		{-400, 400, -400}
	};
	this->AddPolygon(poses, RED);
	std::vector< XMFLOAT3> poses2 = {
		{-500, -500, -500},
		{500, 500, 500}
	};
	std::vector< XMFLOAT3> poses3 = {
		{-500, 500, -500},
		{500, -500, 500}
	};
	std::vector< XMFLOAT3> poses4 = {
		{500, -500, -500},
		{-500, 500, -500}
	};
	this->AddLine(poses2, BLUE);
	this->AddLine(poses3, BLUE);
	this->AddLine(poses4, BLUE);
	this->AddPoint(poses2[0], WHITE);

	this->UpdateLight();
	this->UpdateSavedScenes();
		
	return true;
}

void Scene::OnResize(ID3D11DepthStencilView* dsView, ID3D11RenderTargetView* mainRTV, int newWidth, int newHeight)
{
	this->dsView = dsView;
	this->mainRTV = mainRTV;
	this->width = newWidth;
	this->height = newHeight;
	this->camera.SetProjectionValues(this->width, this->height, -1000000, 1000000);

	try {
		CD3D11_TEXTURE2D_DESC maskDesc(
			DXGI_FORMAT_R8_UNORM,
			this->width,
			this->height
		);
		maskDesc.MipLevels = 1;
		maskDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
		maskDesc.SampleDesc.Count = 8;
		maskDesc.SampleDesc.Quality = 0;

		HRESULT hr = this->device->CreateTexture2D(&maskDesc, nullptr, this->geometryPresenceMask.GetAddressOf());
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


		hr = this->device->CreateRenderTargetView(this->geometryPresenceMask.Get(), nullptr, this->maskRTV.GetAddressOf());
		COM_ERROR_IF_FAILED(hr, "Failed to create mask RTV.");
		hr = this->device->CreateRenderTargetView(this->outlinesTexture.Get(), nullptr, this->outlinesRTV.GetAddressOf());
		COM_ERROR_IF_FAILED(hr, "Failed to create outlines RTV.");

		this->UpdateRTVs();


		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = maskDesc.Format;
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
}

ID3D11RenderTargetView* Scene::GetMaskRTV() const
{
	return this->maskRTV.Get();
}

void Scene::Draw()
{
	std::vector<Primitive*> primitivesOrdered = GetPrimitivesSorted();
		
	for (Primitive* p : primitivesOrdered) {
		UCHAR dim = p->GetDimension();
		switch (dim) {
		case 0: this->deviceContext->GSSetShader(this->geometryshaderpoints.GetShader(), NULL, 0); break;
		case 1: this->deviceContext->GSSetShader(this->geometryshaderthickness.GetShader(), NULL, 0); break;
		case 2: this->deviceContext->GSSetShader(nullptr, NULL, 0); break;
		}

		XMMATRIX projectionMatrix = (p->ProjectionScalabe() ? this->camera.GetProjectionMatrix() : this->camera.GetProjectionMatrixNoScale());

		this->deviceContext->RSSetState(this->rasterizerSolid.Get());
		this->deviceContext->OMSetRenderTargets(3, this->rtvsMain, this->dsView);

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

			if (this->outlineThroughObjets && p->selected){
				this->deviceContext->OMSetRenderTargets(3, this->rtvsMask, this->dsView);
				this->deviceContext->OMSetDepthStencilState(this->dsStateNoDepth.Get(), 0);
				p->Draw(this->camera.GetViewMatrix(), projectionMatrix);

				this->deviceContext->OMSetRenderTargets(1, this->rtvsMain, this->dsView);
				this->deviceContext->OMSetDepthStencilState(this->dsStateDepth.Get(), 0);
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

			SetOutline(p->selected ? Colors::SelectedColor : BLACK, p->selected ? 0.3f : 0.1f);
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
}

void Scene::HandleMouseRay(XMVECTOR rayOrigin, XMVECTOR rayDirection)
{
	std::vector<XMFLOAT3> poses(2);

	XMStoreFloat3(&poses[0], rayOrigin);
	XMStoreFloat3(&poses[1], rayDirection);
	this->AddLine(poses, RED);
}

void Scene::AddPoint(const XMFLOAT3& pos, const XMFLOAT4& col)
{
	Primitive* point = new Primitive(this->device, this->deviceContext);
	
	Vertex vertexData[1] = { Vertex(BaseVectors::ORIGIN) };

	point->SetVertexIndexBuffers(vertexData, 1, 0, 0, 0);
	point->SetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_POINTLIST);
	point->SetPosition(pos);
	point->SetColor(col);
	point->SetIlluminationCapability(false);

	this->primitives.push_back(point);
}

void Scene::AddLine(const std::vector<XMFLOAT3>& poses, const XMFLOAT4& col)
{
	Primitive* line = new Primitive(this->device, this->deviceContext);

	std::vector<Vertex> vertexData{};
	for (XMFLOAT3 pos : poses) {
		vertexData.push_back(Vertex(pos));
	}
		
	line->SetVertexIndexBuffers(vertexData.data(), vertexData.size(), 0, 0, 1);
	line->SetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_LINESTRIP);
	line->SetPosition(BaseVectors::ORIGIN);
	line->SetColor(col);
	line->SetIlluminationCapability(false);

	this->primitives.push_back(line);
}

void Scene::AddPolygon(const std::vector<XMFLOAT3>& poses, const XMFLOAT4& col)
{
	Primitive* poly = new Primitive(this->device, this->deviceContext);
	std::vector<Vertex> vertexData{};

	if (poses.size() == 3) {
		vertexData = { Vertex(poses[0]), Vertex(poses[1]), Vertex(poses[2]) };
		poly->SetVertexIndexBuffers(vertexData.data(), vertexData.size(), nullptr, NULL, 2);
		poly->SetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	}
	else {
		DWORD centerIndex = vertexData.size();
		XMFLOAT3 centerOfMass = math::GetCenterOfMass(poses);
		
		for (int i = 0; i < poses.size(); ++i) {
			XMFLOAT3 faceNormal = math::ComputeNormal(centerOfMass, poses[i], poses[(i + 1) % poses.size()]);

			vertexData.push_back(Vertex(centerOfMass, faceNormal));
			vertexData.push_back(Vertex(poses[i], faceNormal));
			vertexData.push_back(Vertex(poses[(i + 1) % poses.size()], faceNormal));
		}


		std::vector<DWORD> indexData{};
		for (DWORD i = 0; i < vertexData.size(); ++i) {
			indexData.push_back(i);
		}
		poly->SetVertexIndexBuffers(vertexData.data(), vertexData.size(), indexData.data(), indexData.size(), 2);
		poly->SetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}

	poly->SetPosition(BaseVectors::ORIGIN);
	poly->SetColor(col);
	poly->SetIlluminationCapability(true);

	this->primitives.push_back(poly);
}

void Scene::AddSphere(float radius, const XMFLOAT3& pos, const UINT numSubdivides, const XMFLOAT4& col)
{
	std::vector<Vertex> vertexData = {
	{ {  0.0f,      0.0f,      1.0f     }, {  0.0f,      0.0f,      1.0f     } },

	{ {  0.894427f, 0.0f,      0.447214f}, {  0.894427f, 0.0f,      0.447214f} },
	{ {  0.276393f, 0.850651f, 0.447214f}, {  0.276393f, 0.850651f, 0.447214f} },
	{ { -0.723607f, 0.525731f, 0.447214f}, { -0.723607f, 0.525731f, 0.447214f} },
	{ { -0.723607f,-0.525731f, 0.447214f}, { -0.723607f,-0.525731f, 0.447214f} },
	{ {  0.276393f,-0.850651f, 0.447214f}, {  0.276393f,-0.850651f, 0.447214f} },

	{ {  0.723607f, 0.525731f,-0.447214f}, {  0.723607f, 0.525731f,-0.447214f} },
	{ { -0.276393f, 0.850651f,-0.447214f}, { -0.276393f, 0.850651f,-0.447214f} },
	{ { -0.894427f, 0.0f,     -0.447214f}, { -0.894427f, 0.0f,     -0.447214f} },
	{ { -0.276393f,-0.850651f,-0.447214f}, { -0.276393f,-0.850651f,-0.447214f} },
	{ {  0.723607f,-0.525731f,-0.447214f}, {  0.723607f,-0.525731f,-0.447214f} },

	{ {  0.0f,      0.0f,     -1.0f     }, {  0.0f,      0.0f,     -1.0f     } }
	};

	std::vector<DWORD> indexData = {
		0, 1, 2,
		0, 2, 3,
		0, 3, 4,
		0, 4, 5,
		0, 5, 1,

		1, 6, 2,
		2, 6, 7,
		2, 7, 3,
		3, 7, 8,
		3, 8, 4,
		4, 8, 9,
		4, 9, 5,
		5, 9, 10,
		5, 10, 1,
		1, 10, 6,

		11, 7, 6,
		11, 8, 7,
		11, 9, 8,
		11, 10, 9,
		11, 6, 10
	};

	for (UINT i = 0; i < numSubdivides; ++i) {
		Subdivide(vertexData, indexData);
	}

	for (Vertex& vertex : vertexData) {
		XMVECTOR posVec = XMLoadFloat3(&vertex.pos);
		posVec = XMVector3Normalize(posVec);
		XMStoreFloat3(&vertex.pos, posVec);

		vertex.pos.x *= radius;
		vertex.pos.y *= radius;
		vertex.pos.z *= radius;
	}

	Primitive* poly = new Primitive(this->device, this->deviceContext);
	poly->SetVertexIndexBuffers(vertexData.data(), vertexData.size(), indexData.data(), indexData.size(), 2);
	poly->SetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	poly->SetPosition(pos);
	poly->SetColor(col);
	poly->SetIlluminationCapability(true);

	this->primitives.push_back(poly);
}

void Scene::UpdateLight()
{
	for (Primitive* p : this->primitives) {
		if (!p->GetIlluminationCapability()) continue;
		p->SetLighting(this->ambient, this->intensity, this->shininess);
		p->SetSmoothShading(this->smoothShade);
	}
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

bool Scene::InitializeDirectX()
{
	if (!this->geometryshaderpoints.Initialize(this->device, this->shadersPath + L"geometryshaderpoints.cso"))
		return false;

	if (!this->geometryshaderthickness.Initialize(this->device, this->shadersPath + L"geometryshaderthickness.cso"))
		return false;

	try {
		CD3D11_RASTERIZER_DESC rasterizerDesc(D3D11_DEFAULT);
		rasterizerDesc.CullMode = D3D11_CULL_BACK;
		rasterizerDesc.FillMode = D3D11_FILL_SOLID;
		rasterizerDesc.AntialiasedLineEnable = TRUE;
		rasterizerDesc.MultisampleEnable = TRUE;
		rasterizerDesc.FrontCounterClockwise = TRUE;
		HRESULT hr = this->device->CreateRasterizerState(&rasterizerDesc, this->rasterizerSolid.GetAddressOf());
		rasterizerDesc.FillMode = D3D11_FILL_WIREFRAME;
		hr = this->device->CreateRasterizerState(&rasterizerDesc, this->rasterizerWireframe.GetAddressOf());
		COM_ERROR_IF_FAILED(hr, "Failed to create rasterizer state.");


		CD3D11_DEPTH_STENCIL_DESC depthStencilStateDesc(D3D11_DEFAULT);
		depthStencilStateDesc.DepthFunc = D3D11_COMPARISON_FUNC::D3D11_COMPARISON_LESS_EQUAL;
		hr = this->device->CreateDepthStencilState(&depthStencilStateDesc, this->dsStateDepth.GetAddressOf());
		COM_ERROR_IF_FAILED(hr, "Failed to create depth stencil state.");

		depthStencilStateDesc.DepthEnable = FALSE;
		hr = this->device->CreateDepthStencilState(&depthStencilStateDesc, this->dsStateNoDepth.GetAddressOf());
		COM_ERROR_IF_FAILED(hr, "Failed to create depth stencil state.");


		D3D11_SAMPLER_DESC sampDesc = {};
		sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
		device->CreateSamplerState(&sampDesc, this->samplerState.GetAddressOf());
		this->deviceContext->PSSetSamplers(0, 1, this->samplerState.GetAddressOf());


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
		

		hr = this->device->CreateRenderTargetView(this->geometryPresenceMask.Get(), nullptr, this->maskRTV.GetAddressOf());
		COM_ERROR_IF_FAILED(hr, "Failed to create mask RTV.");
		hr = this->device->CreateRenderTargetView(this->outlinesTexture.Get(), nullptr, this->outlinesRTV.GetAddressOf());
		COM_ERROR_IF_FAILED(hr, "Failed to create outlines RTV.");
		this->UpdateRTVs();


		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = maskDesc.Format;
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


		if (!this->vsFSQuad.Initialize(this->device, this->shadersPath + L"vertexshaderFSQuad.cso", nullptr, NULL))
			return false;
		if (!this->pixelShaderMain.Initialize(this->device, this->shadersPath + L"pixelshader.cso"))
			return false;
		if (!this->pixelShaderOutlineToTexture.Initialize(this->device, this->shadersPath + L"pixelshaderoutline.cso"))
			return false;
		if (!this->pixelShaderOutlineToScreen.Initialize(this->device, this->shadersPath + L"pixelshaderoutlinemerge.cso"))
			return false;


		hr = this->cb_ps_outline.Initialize(this->device, this->deviceContext);
		COM_ERROR_IF_FAILED(hr, "Failed to create cb ps outline.");

		SetOutline(BLACK, 0.1f);
	}
	catch (COMException& exception) {
		ErrorLogger::Log(exception);
		return false;
	}

	this->SetMainResources();
	return true;
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

std::vector<Primitive*>& Scene::GetPrimitivesSorted()
{
	size_t primitivesCount = this->primitives.size();
	size_t selectedCount = 0;
	static std::vector<Primitive*> primitivesOrdered(primitivesCount);

	for (size_t i = 0; i < primitivesCount; ++i) {
		if (this->primitives[i]->selected && this->primitives[i]->GetDimension() == 2) {
			primitivesOrdered[primitivesCount - 1 - selectedCount++] = this->primitives[i];
		}
		else {
			primitivesOrdered[i - selectedCount] = this->primitives[i];
		}
	}
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


