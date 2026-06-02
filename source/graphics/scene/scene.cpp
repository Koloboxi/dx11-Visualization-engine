#include "scene.h"
#include "..\..\loaders\STLLoader.h"
#include <fstream>
#include <algorithm>
#include <numeric>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace {
	std::string ResolveScenesDir()
	{
		char* appdata = nullptr;
		size_t len = 0;
		_dupenv_s(&appdata, &len, "APPDATA");
		std::filesystem::path base = appdata ? std::filesystem::path(appdata) : std::filesystem::current_path();
		if (appdata) free(appdata);
		std::filesystem::path dir = base / "VEngineDX11" / "Scenes";
		std::error_code ec;
		std::filesystem::create_directories(dir, ec);
		return dir.string() + "\\";
	}
}

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

	this->orientationTransformer.Initialize(device, deviceContext);

	this->UpdateLight();
	this->scenesPath = ResolveScenesDir();
	this->UpdateSavedScenes();
	this->tpsTimer.Start();

	this->root.name = "";

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
	const std::vector<Primitive*>& primitivesOrdered = GetPrimitivesSorted();

	const bool doIDPass = !this->blockMousePick && this->idPassNeeded;
	if (doIDPass)
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

		if (doIDPass) {
			this->deviceContext->OMSetRenderTargets(1, this->rtvIDs, this->dsViewNoMSAA);
			this->deviceContext->PSSetShader(this->pixelShaderWriteIDs.GetShader(), NULL, 0);
			this->deviceContext->PSSetConstantBuffers(1, 1, this->cb_ps_id.GetAddressOf());
			this->SetIDToWrite(p->id);
			p->Draw(this->camera.GetViewMatrix(), projectionMatrix);
		}

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

	DrawTrajectories();

	{
		for (Primitive* sel : this->primitives) {
			if (!sel->selected) continue;
			XMFLOAT3 v = sel->velocity;
			float len2 = v.x*v.x + v.y*v.y + v.z*v.z;
			if (len2 <= 0.01f) continue;
			float len = sqrtf(len2);
			float headLen  = (len * 0.15f < 40.0f) ? len * 0.15f : 40.0f;
			float headRad  = headLen * 0.4f;
			float shaftRad = (headRad * 0.3f > 1.0f) ? headRad * 0.3f : 1.0f;
			XMFLOAT3 pos = sel->GetPosition();
			XMFLOAT3 tip = { pos.x + v.x, pos.y + v.y, pos.z + v.z };
			Primitive* arrow = PrimitiveConstructor::Arrow3d(shaftRad, headRad, headLen, pos, tip, 8,
				XMFLOAT4(1.0f, 0.95f, 0.1f, 1.0f), 0);
			if (arrow) {
				arrow->SetLighting(this->ambient, this->intensity, this->shininess);
				this->deviceContext->IASetInputLayout(this->vsMain->GetInputLayout());
				this->deviceContext->VSSetShader(this->vsMain->GetShader(), NULL, 0);
				this->deviceContext->PSSetShader(this->pixelShaderMain.GetShader(), NULL, 0);
				this->deviceContext->GSSetShader(nullptr, NULL, 0);
				this->deviceContext->RSSetState(this->rasterizerSolid.Get());
				this->deviceContext->OMSetDepthStencilState(this->dsStateDepth.Get(), 0);
				this->deviceContext->OMSetRenderTargets(1, this->rtvsMain, this->dsView);
				arrow->Draw(this->camera.GetViewMatrix(), this->camera.GetProjectionMatrix());
				delete arrow;
			}
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

	if (doIDPass) {
		this->deviceContext->OMSetRenderTargets(1, this->rtvIDs, this->dsViewNoMSAA);
		this->deviceContext->PSSetShader(this->pixelShaderWriteIDs.GetShader(), NULL, 0);
		this->deviceContext->PSSetConstantBuffers(1, 1, this->cb_ps_id.GetAddressOf());
		std::vector<UINT> idsAux = this->orientationTransformer.GetAuxiliaryObjectsIDs();
		for (UINT id : idsAux) {
			this->SetIDToWrite(id);
			this->orientationTransformer.DrawID(this->camera.GetViewMatrix(), this->camera.GetProjectionMatrix(), this->camera.GetScale(), id);
		}
		this->idPassNeeded = false;
	}

	this->SetMainResources();
	this->deviceContext->OMSetDepthStencilState(this->dsStateDepth.Get(), 0);

	UpdateTime();
}

void Scene::HandleSelection(Primitive* primitiveClicked)
{
	bool shiftHeld = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
	bool ctrlHeld  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;

	this->controllerSelected = false;

	if (!primitiveClicked) {
		for (Primitive* p : this->primitives) p->selected = false;
		this->orientationTransformer.SetTargetObjects({});
		return;
	}

	if (ctrlHeld) {
		primitiveClicked->selected = !primitiveClicked->selected;
	} else if (shiftHeld) {
		primitiveClicked->selected = true;
	} else {
		for (Primitive* p : this->primitives) p->selected = false;
		primitiveClicked->selected = true;
	}

	std::vector<Primitive*> sel;
	for (Primitive* p : this->primitives) if (p->selected) sel.push_back(p);
	this->orientationTransformer.SetTargetObjects(sel);
}

void Scene::DeleteSelected()
{
	if (this->orientationTransformer.HasActiveObject())
		this->orientationTransformer.HandleObjRelease();
	this->orientationTransformer.SetTargetObjects({});

	auto it = this->primitives.begin();
	while (it != this->primitives.end()) {
		Primitive* p = *it;
		if (p->selected) {
			for (SceneNode* ch : p->children) { ch->parent = &root; root.children.push_back(ch); }
			p->children.clear();
			if (p->parent) p->parent->RemoveChild(p);
			delete p;
			it = this->primitives.erase(it);
		} else {
			++it;
		}
	}
	m_sortedDirty = true;
}

void Scene::HandleLMouse(int px, int py, bool tPressfRelease)
{
	static bool blockNextLMouseRelease = false;

	try {
		if (px < 0 || py < 0 || px >= this->width || py >= this->height || this->blockMousePick) return;

		UINT id = this->GetIdByPixel(px, py);

		if (!tPressfRelease && this->pickModeActive) {
			this->pickModeActive = false;
			this->pickedPrimId = id;
			return;
		}

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

void Scene::SetController(SceneController* ctrl)
{
	if (controller) {
		root.RemoveChild(controller);
		delete controller;
	}
	controller = ctrl;
	if (controller) {
		root.AddChild(controller);
		if (luaCompileControllerCallback) luaCompileControllerCallback(*controller);
		if (controller->compiledTick)  sceneTick  = controller->compiledTick;
		if (controller->compiledReset) sceneReset = controller->compiledReset;
		sceneSliders.clear();
		for (auto& sd : controller->sliderDefs) {
			auto it = sceneFloats.find(sd.luaGlobal);
			if (it == sceneFloats.end()) {
				sceneFloats[sd.luaGlobal] = std::make_shared<float>(0.f);
				it = sceneFloats.find(sd.luaGlobal);
			}
			GlobalSlider sl;
			sl.label         = sd.label;
			sl.luaGlobalName = sd.luaGlobal;
			sl.valuePtr      = it->second.get();
			sl.min           = sd.min;
			sl.max           = sd.max;
			sceneSliders.push_back(sl);
		}
	}
}

void Scene::AddPoint(const XMFLOAT3& pos, const XMFLOAT4& col)
{
	auto* p = PrimitiveConstructor::Point(pos, col, NextId());
	this->primitives.push_back(p);
	root.AddChild(p);
	m_sortedDirty = true;
}

void Scene::AddLine(const std::vector<XMFLOAT3>& poses, const XMFLOAT4& col)
{
	auto* p = PrimitiveConstructor::Line(poses, col, NextId());
	this->primitives.push_back(p);
	root.AddChild(p);
	m_sortedDirty = true;
}

void Scene::AddPolygon(const std::vector<XMFLOAT3>& poses, const XMFLOAT4& col)
{
	auto* p = PrimitiveConstructor::Polygon(poses, col, NextId());
	this->primitives.push_back(p);
	root.AddChild(p);
	m_sortedDirty = true;
}

void Scene::AddSphere(float radius, const XMFLOAT3& pos, const UINT numSubdivides, const XMFLOAT4& col)
{
	auto* p = PrimitiveConstructor::Sphere(radius, pos, numSubdivides, col, NextId());
	this->primitives.push_back(p);
	root.AddChild(p);
	m_sortedDirty = true;
}

void Scene::AddLine3d(float radius, std::vector<XMFLOAT3>& poses, const UINT numSubdivides, const XMFLOAT4& col)
{
	auto* p = PrimitiveConstructor::Line3d(radius, poses, numSubdivides, col, NextId());
	this->primitives.push_back(p);
	root.AddChild(p);
	m_sortedDirty = true;
}

void Scene::AddArc3d(float arcRadius, float lineRadius, float angleDeg, const XMFLOAT3& center, const UINT numSubdivides, const XMFLOAT4& col)
{
	auto* p = PrimitiveConstructor::Arc3d(arcRadius, lineRadius, angleDeg, center, numSubdivides, col, NextId());
	this->primitives.push_back(p);
	root.AddChild(p);
	m_sortedDirty = true;
}

void Scene::AddArrow3d(float shaftRadius, float headRadius, float headLength, const XMFLOAT3& from, const XMFLOAT3& to, UINT sides, const XMFLOAT4& col)
{
	Primitive* p = PrimitiveConstructor::Arrow3d(shaftRadius, headRadius, headLength, from, to, sides, col, NextId());
	if (p) { this->primitives.push_back(p); root.AddChild(p); m_sortedDirty = true; }
}

void Scene::AddFromSTL(const std::string& path, const XMFLOAT4& col, const std::string& name)
{
	Primitive* p = STLLoader::Load(path, col, NextId());
	if (!p) return;
	if (!name.empty()) p->name = name;
	this->primitives.push_back(p);
	root.AddChild(p);
	m_sortedDirty = true;
	this->UpdateLight();
}

void Scene::AddCubeWireframe(float halfSize, const XMFLOAT3& center, const XMFLOAT4& col)
{
	Primitive* p = PrimitiveConstructor::CubeWireframe(halfSize, center, col, NextId());
	if (p) { this->primitives.push_back(p); root.AddChild(p); m_sortedDirty = true; }
}

void Scene::AddCubeSolid(float halfSize, const XMFLOAT3& center, const XMFLOAT4& col)
{
	Primitive* p = PrimitiveConstructor::CubeSolid(halfSize, center, col, NextId());
	if (p) { this->primitives.push_back(p); root.AddChild(p); m_sortedDirty = true; }
}

void Scene::RemovePrimitivesByPrefix(const std::string& prefix)
{
	if (orientationTransformer.HasActiveObject()) orientationTransformer.HandleObjRelease();
	orientationTransformer.SetTargetObjects({});

	auto it = this->primitives.begin();
	while (it != this->primitives.end()) {
		Primitive* p = *it;
		if (p->name.rfind(prefix, 0) == 0) {
			for (SceneNode* ch : p->children) { ch->parent = &root; root.children.push_back(ch); }
			p->children.clear();
			if (p->parent) p->parent->RemoveChild(p);
			delete p;
			it = this->primitives.erase(it);
		} else ++it;
	}
	m_sortedDirty = true;
}

void Scene::RemovePrimitive(Primitive* p)
{
	if (!p) return;
	auto it = std::find(this->primitives.begin(), this->primitives.end(), p);
	if (it == this->primitives.end()) return;

	if (this->orientationTransformer.HasActiveObject())
		this->orientationTransformer.HandleObjRelease();

	this->orientationTransformer.SetTargetObjects({});
	for (SceneNode* ch : p->children) { ch->parent = &root; root.children.push_back(ch); }
	p->children.clear();
	if (p->parent) p->parent->RemoveChild(p);
	this->primitives.erase(it);
	m_sortedDirty = true;
	delete p;
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
		if (p->parent && p->parent->IsPrimitive())
			scene["primitives"][i]["parentId"] = static_cast<Primitive*>(p->parent)->id;
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

	scene["sceneName"]   = this->sceneName;
	scene["sceneData"]   = this->sceneData;
	scene["currentTime"] = this->currentTime;
	{
		json obj = json::object();
		for (const auto& [k, v] : this->sceneFloats)
			if (v) obj[k] = *v;
		scene["sceneFloats"] = obj;
	}
	if (controller)
		scene["controller"] = controller->ToJson();
	if (luaSaveStateCallback)
		scene["luaState"] = luaSaveStateCallback();

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
	if (this->orientationTransformer.HasActiveObject())
		this->orientationTransformer.HandleObjRelease();
	this->orientationTransformer.SetTargetObjects({});

	for (Primitive* p : this->primitives) delete p;
	this->primitives.clear();
	root.children.clear();
	m_sortedDirty = true;
	this->ClearTrajectories();
	this->ClearSceneCustomState();
}

void Scene::ClearSceneCustomState()
{
	this->sceneData    = nlohmann::json::object();
	this->sceneFloats.clear();
	this->sceneSliders.clear();
	this->sceneTick    = nullptr;
	this->sceneReset   = nullptr;
	this->sceneName.clear();
	this->controllerSelected = false;
	if (controller) {
		root.RemoveChild(controller);
		delete controller;
		controller = nullptr;
	}
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
		if (!jsonSaver::from_json(j, *p)) { delete p; continue; }
		if (p->id == 0) p->id = NextId();
		this->primitives.push_back(p);
		root.AddChild(p);
	}
	m_sortedDirty = true;

	for (Primitive* p : this->primitives)
		if (p->id >= this->nextId) this->nextId = p->id + 1;

	XMFLOAT3 camPos = XMFLOAT3(data["camera"]["camPos"][0], data["camera"]["camPos"][1], data["camera"]["camPos"][2]);
	XMMATRIX camRot = XMMATRIX(data["camera"]["camRot"].get<std::vector<float>>().data());
	float camScale = data["camera"]["camScale"];

	this->camera.SetScale(camScale);
	this->camera.SetRotation(camRot);
	this->camera.SetPosition(camPos);
	this->UpdateLight();

	this->ClearSceneCustomState();
	this->currentTime = data.value("currentTime", 0.0f);

	if (data.contains("sceneData")) this->sceneData = data["sceneData"];
	if (data.contains("sceneName")) this->sceneName  = data["sceneName"].get<std::string>();

	if (data.contains("sceneFloats") && data["sceneFloats"].is_object()) {
		for (auto it = data["sceneFloats"].begin(); it != data["sceneFloats"].end(); ++it)
			this->sceneFloats[it.key()] = std::make_shared<float>(it.value().get<float>());
	}

	{
		const auto& primJson = data["primitives"];
		for (size_t i = 0; i < this->primitives.size() && i < primJson.size(); ++i) {
			if (!primJson[i].contains("parentId")) continue;
			UINT pid = primJson[i]["parentId"].get<UINT>();
			for (Primitive* pp : this->primitives) {
				if (pp->id == pid) {
					root.RemoveChild(this->primitives[i]);
					pp->AddChild(this->primitives[i]);
					break;
				}
			}
		}
	}

	root.name = this->sceneName;

	if (data.contains("controller") && data["controller"].is_object()) {
		SceneController* ctrl = SceneController::FromJson(data["controller"]);
		for (auto& sd : ctrl->sliderDefs) {
			auto it = this->sceneFloats.find(sd.luaGlobal);
			if (it == this->sceneFloats.end())
				this->sceneFloats[sd.luaGlobal] = std::make_shared<float>(0.f);
		}
		SetController(ctrl);
	}

	if (data.contains("luaState") && luaRestoreStateCallback)
		luaRestoreStateCallback(data["luaState"]);

	if (luaReApplyCallback) luaReApplyCallback(this->primitives);

	if (this->sceneTick)
		this->sceneTick(*this, this->currentTime, 0.f, true);
	for (auto& p : this->primitives)
		p->Update(this->currentTime, 0.f);

	this->tpsTimer.Stop();
	this->deltaTime  = 0.0f;
	this->timePaused = true;
	this->tpsTimer.Start();
}

void Scene::UpdateTime()
{
	float dtSec = this->tpsTimer.GetMillisecondsElapsed() * 0.001f;
	this->tpsTimer.Restart();

	if (!this->timePaused) {
		this->deltaTime = dtSec * this->timeSpeed;
		this->currentTime += this->deltaTime;

		if (this->timeMax > 0.0f && this->currentTime >= this->timeMax) {
			if (this->timeLoop)
				this->currentTime = fmodf(this->currentTime, this->timeMax);
			else {
				this->currentTime = this->timeMax;
				this->timePaused = true;
			}
		}
	} else {
		this->deltaTime = 0.0f;
	}

	if (this->sceneTick)
		this->sceneTick(*this, this->currentTime, this->deltaTime, this->timePaused);

	if (!this->timePaused) {
		for (auto& prim : this->primitives)
			prim->Update(this->currentTime, this->deltaTime);

		if (this->showTrajectories) {
			for (Primitive* p : this->primitives) {
				XMFLOAT3 v = p->velocity;
				bool moving = (v.x*v.x + v.y*v.y + v.z*v.z) > 1e-4f;
				if (!p->HasUpdater() && !moving) continue;
				auto& traj = this->trajectoryData[p->id];
				traj.push_back(p->GetPosition());
				if (this->trajectoryMaxLen > 0 && (int)traj.size() > this->trajectoryMaxLen)
					traj.pop_front();
			}
		}
	}

	this->orientationTransformer.Update();
}

void Scene::ResetTime()
{
	this->currentTime = 0.0f;
	this->timePaused  = true;
	this->tpsTimer.Restart();
	this->ClearTrajectories();

	if (this->sceneReset) this->sceneReset(*this);
}

void Scene::ClearTrajectories()
{
	this->trajectoryData.clear();
}

void Scene::DrawTrajectories()
{
	if (!showTrajectories || trajectoryData.empty()) return;

	this->deviceContext->GSSetShader(this->geometryshaderthickness.GetShader(), NULL, 0);
	this->deviceContext->RSSetState(this->rasterizerSolid.Get());
	this->deviceContext->OMSetDepthStencilState(this->dsStateDepth.Get(), 0);
	this->deviceContext->OMSetRenderTargets(1, this->rtvsMain, this->dsView);
	this->deviceContext->PSSetShader(this->pixelShaderMain.GetShader(), NULL, 0);

	const int NUM_SEGS = 6;

	for (auto& [id, dq] : this->trajectoryData) {
		if (dq.size() < 2) continue;

		XMFLOAT4 col = { 1.f, 1.f, 1.f, 1.f };
		for (Primitive* p : this->primitives)
			if (p->id == id) { col = p->GetColor(); break; }

		const int N = (int)dq.size();
		const int segPts = (N + NUM_SEGS - 1) / NUM_SEGS;

		for (int seg = 0; seg < NUM_SEGS; seg++) {
			int start = seg * segPts;
			if (start >= N) break;
			int end = (start + segPts + 1 < N) ? start + segPts + 1 : N;

			float alpha = (float)(seg + 1) / NUM_SEGS * 0.9f;
			XMFLOAT4 sc = { col.x, col.y, col.z, alpha };

			std::vector<XMFLOAT3> pts;
			pts.reserve(end - start);
			auto it = dq.begin();
			std::advance(it, start);
			for (int k = start; k < end; k++, ++it)
				pts.push_back(*it);

			Primitive* line = PrimitiveConstructor::Line(pts, sc, 0);
			if (line) {
				line->Draw(this->camera.GetViewMatrix(), this->camera.GetProjectionMatrix());
				delete line;
			}
		}
	}
}

bool Scene::InitializeDirectX()
{
	if (!this->geometryshaderpoints.Initialize(this->device, this->shadersPath + L"geometryshaderpoints.cso"))
		return false;

	if (!this->geometryshaderthickness.Initialize(this->device, this->shadersPath + L"geometryshaderthickness.cso"))
		return false;

	try {
		HRESULT hr{};
		{
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

		{
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

		{
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

		{
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
	this->deviceContext->OMSetRenderTargets(1, this->rtvsMain, this->dsView);

	if (!showGrid && !showAxes) return;

	this->deviceContext->GSSetShader(this->geometryshaderthickness.GetShader(), NULL, 0);

	const float baseGridSize = 128.f;
	const int   linesNum     = 40;
	const float logStep      = 2.0f;

	float camScale = this->camera.GetScale();

	float logScale = std::log(camScale) / std::log(logStep);
	float levelF   = std::floor(logScale);
	float fraction = logScale - levelF;

	float step0 = baseGridSize * std::pow(logStep, levelF);
	float step1 = baseGridSize * std::pow(logStep, levelF + 1.f);

	if (showGrid) {
		float alpha0 = 1.0f - fraction;
		float alpha1 = fraction;
		float extent = linesNum * step1 / 2.f;

		auto drawGrid = [&](float gridSize, float alpha) {
			if (alpha <= 0.01f) return;
			XMFLOAT4 color = { Colors::GRAY.x, Colors::GRAY.y, Colors::GRAY.z, Colors::GRAY.w * alpha };
			int lineCount = (int)(extent / gridSize);
			for (int ix = -lineCount; ix <= lineCount; ix++) {
				float x = (float)ix * gridSize;
				std::vector<XMFLOAT3> poses = {
					XMFLOAT3(x,  extent, 0),
					XMFLOAT3(x, -extent, 0)
				};
				auto p1 = PrimitiveConstructor::Line(poses, color, 0);
				poses = {
					XMFLOAT3( extent, x, 0),
					XMFLOAT3(-extent, x, 0)
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
	}

	if (showAxes) {
		float axisLen = step1;

		struct AxisLine { XMFLOAT3 from; XMFLOAT3 to; XMFLOAT4 color; };
		AxisLine axes[] = {
			{ XMFLOAT3(0, 0, 0), XMFLOAT3(axisLen, 0,       0), Colors::RED   },
			{ XMFLOAT3(0, 0, 0), XMFLOAT3(0,       axisLen, 0), Colors::GREEN },
			{ XMFLOAT3(0, 0, 0), XMFLOAT3(0,       0,       axisLen), Colors::BLUE  },
		};

		for (auto& axis : axes) {
			std::vector<XMFLOAT3> poses = { axis.from, axis.to };
			auto line = PrimitiveConstructor::Line(poses, axis.color, 0);
			line->Draw(this->camera.GetViewMatrix(), this->camera.GetProjectionMatrix());
			delete line;
		}
	}
}

const std::vector<Primitive*>& Scene::GetPrimitivesSorted() const
{
	XMFLOAT3 cp = this->camera.GetPositionFloat3();
	bool cameraMoved = (cp.x != m_sortedCamPos.x || cp.y != m_sortedCamPos.y || cp.z != m_sortedCamPos.z);

	if (!m_sortedDirty && !cameraMoved)
		return m_sortedCache;

	m_sortedDirty  = false;
	m_sortedCamPos = cp;
	m_sortedCache.clear();
	m_sortedCache.reserve(this->primitives.size());

	std::vector<Primitive*> nonSelOpaque;
	std::vector<Primitive*> selOpaque;
	std::vector<std::pair<Primitive*, float>> nonSelTransparent;
	std::vector<std::pair<Primitive*, float>> selTransparent;

	XMVECTOR camPosV    = this->camera.GetPositionVector();
	XMVECTOR camForward = const_cast<Camera&>(this->camera).GetForwardVector();

	for (Primitive* p : this->primitives) {
		bool transparent = p->GetTransparent();
		bool sel = p->selected;

		if (!transparent) {
			if (sel) selOpaque.push_back(p);
			else nonSelOpaque.push_back(p);
		} else {
			XMFLOAT3 posF = p->GetPosition();
			XMVECTOR posV = XMLoadFloat3(&posF);
			XMVECTOR rel  = XMVectorSubtract(posV, camPosV);
			float proj    = XMVectorGetX(XMVector3Dot(rel, camForward));
			if (sel) selTransparent.emplace_back(p, proj);
			else nonSelTransparent.emplace_back(p, proj);
		}
	}

	auto cmp = [](const std::pair<Primitive*, float>& a, const std::pair<Primitive*, float>& b) {
		return a.second > b.second;
	};
	std::sort(nonSelTransparent.begin(), nonSelTransparent.end(), cmp);
	std::sort(selTransparent.begin(), selTransparent.end(), cmp);

	m_sortedCache.insert(m_sortedCache.end(), nonSelOpaque.begin(), nonSelOpaque.end());
	m_sortedCache.insert(m_sortedCache.end(), selOpaque.begin(), selOpaque.end());
	for (auto& pr : nonSelTransparent) m_sortedCache.push_back(pr.first);
	for (auto& pr : selTransparent)    m_sortedCache.push_back(pr.first);

	return m_sortedCache;
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
