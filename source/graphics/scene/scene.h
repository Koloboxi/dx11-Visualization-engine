#pragma once
#include "..\camera\Camera.h"
#include "jsonSaver.h"

#include "../shaders/shaders.h"
#include "..\misc\Colors.h"

#include "..\imgui\imgui.h"
#include "auxiliaryObjects.h"
#include "..\..\utils\Timer.h"
#include "..\..\external\json.hpp"
#include "scene_node.h"
#include "scene_controller.h"

#include <unordered_map>
#include <deque>
#include <memory>
#include <functional>
#include <string>
#include <vector>

class Scene;

struct GlobalSlider {
    std::string label;
    std::string luaGlobalName;
    float* valuePtr = nullptr;
    float min = 0.f;
    float max = 1.f;
};

class Scene {
public:
	bool Initialize(ID3D11Device* device, ID3D11DeviceContext* deviceContext, std::wstring shadersPath, ID3D11DepthStencilView* dsView, ID3D11DepthStencilView* dsViewNoMSAA, ID3D11RenderTargetView* mainRTV, VertexShader* vs, int windowWidth, int windowHeight);
	void OnResize(ID3D11DepthStencilView* dsView, ID3D11DepthStencilView* dsViewNoMSAA, ID3D11RenderTargetView* mainRTV, int newWidth, int newHeight);
	ID3D11RenderTargetView* GetMaskRTV() const;

	Camera camera;
	OrientationTransformer orientationTransformer;
	void Draw();

	void HandleSelection(Primitive* primitiveClicked);
	void HandleLMouse(int px, int py, bool tPressfRelease);
	void DeleteSelected();
	bool blockMousePick = false;
	bool blockMouseWheel = false;
	bool controllerSelected = false;

	bool rsSolid = true;
	bool rsWireframe = false;

	float ambient = 0.4f;
	float intensity = 0.6f;
	float shininess = 25.0f;
	bool smoothShade = true;
	void UpdateLight();

	bool outlineThroughObjets = true;

	bool showGrid = true;
	bool showAxes = true;

	SceneNode       root;
	SceneController* controller = nullptr;

	std::vector<Primitive*> primitives;

	void AddPoint(const XMFLOAT3& pos, const XMFLOAT4& col);
	void AddLine(const std::vector<XMFLOAT3>& poses, const XMFLOAT4& col);
	void AddPolygon(const std::vector<XMFLOAT3>& poses, const XMFLOAT4& col);
	void AddSphere(float radius, const XMFLOAT3& pos, const UINT numSubdivides, const XMFLOAT4& col);
	void AddLine3d(float radius, std::vector<XMFLOAT3>& poses, const UINT numSubdivides, const XMFLOAT4& col);
	void AddArc3d(float arcRadius, float lineRadius, float angleDeg, const XMFLOAT3& center, const UINT numSubdivides, const XMFLOAT4& col);
	void AddArrow3d(float shaftRadius, float headRadius, float headLength, const XMFLOAT3& from, const XMFLOAT3& to, UINT sides, const XMFLOAT4& col);
	void AddFromSTL(const std::string& path, const XMFLOAT4& col, const std::string& name = "");
	void AddFromOBJ(const std::string& path, const XMFLOAT4& lineCol, const XMFLOAT4& pointCol);
	void AddFromCSVMesh(const std::string& path, const XMFLOAT4& lineCol, const XMFLOAT4& pointCol);
	void AddFromCSV3D(const std::string& path, const std::string& name = "");
	void AddCubeWireframe(float halfSize, const XMFLOAT3& center, const XMFLOAT4& col);
	void AddCubeSolid(float halfSize, const XMFLOAT3& center, const XMFLOAT4& col);

	void RemovePrimitive(Primitive* p);
	void RemovePrimitivesByPrefix(const std::string& prefix);
	void SetController(SceneController* ctrl);

	void LoadNewtonDemo();
	void LoadPersistentHomologyScene();
	void LoadGRScene();
	void LoadIdealGasScene();

	nlohmann::json sceneData;
	std::unordered_map<std::string, std::shared_ptr<float>> sceneFloats;
	std::vector<GlobalSlider> sceneSliders;
	std::function<void(Scene&, float t, float dt, bool paused)> sceneTick;
	std::function<void(Scene&)> sceneReset;
	std::string sceneName;
	std::function<void(const std::vector<Primitive*>&)>  luaReApplyCallback;
	std::function<void(SceneController&)>                luaCompileControllerCallback;
	std::function<nlohmann::json()>                      luaSaveStateCallback;
	std::function<void(const nlohmann::json&)>           luaRestoreStateCallback;

	void ClearSceneCustomState();

	std::string scenesPath;
	const std::vector<std::string>& GetSavedScenes() const;
	void SaveScene(std::string name);
	void ClearScene();
	void LoadScene(std::string name);

	float currentTime = 0.0f;
	float deltaTime   = 0.0f;
	float timeSpeed   = 1.0f;
	bool  timePaused  = false;
	float timeMax     = 0.0f;
	bool  timeLoop    = false;
	void  ResetTime();

	bool showTrajectories  = false;
	int  trajectoryMaxLen  = 1000;
	void ClearTrajectories();

	bool pickModeActive = false;
	UINT pickedPrimId   = 0;
	bool idPassNeeded   = true;

private:
	ID3D11Device* device;
	ID3D11DeviceContext* deviceContext;

	int width;
	int height;

	std::wstring shadersPath;
	GeometryShader geometryshaderpoints;
	GeometryShader geometryshaderthickness;

	VertexShader* vsMain;
	VertexShader vsFSQuad;

	Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerSolid;
	Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerWireframe;

	ID3D11DepthStencilView* dsView;
	ID3D11DepthStencilView* dsViewNoMSAA;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilState> dsStateDepth;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilState> dsStateNoDepth;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilState> dsStateDepthNoWrite;

	Microsoft::WRL::ComPtr<ID3D11SamplerState> samplerState;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> geometryPresenceMask;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> geometryPresenceMaskResolved;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> outlinesTexture;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> outlinesTextureResolved;

	Microsoft::WRL::ComPtr<ID3D11Resource> mainRTVTexture;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> mainRTVTextureResolved;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> primitivesIDsTexture;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> primitivesIDsTextureStaging;

	ID3D11RenderTargetView* mainRTV;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> maskRTV;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> outlinesRTV;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> primitivesIDsRTV;

	ID3D11RenderTargetView* rtvsMainMask[3];
	ID3D11RenderTargetView* rtvsMain[3];
	ID3D11RenderTargetView* rtvsMain3[3];
	ID3D11RenderTargetView* rtvsMask[3];
	ID3D11RenderTargetView* rtvsOutlines[3];
	ID3D11RenderTargetView* rtvIDs[1];

	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> maskSRV;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> outlinesSRV;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> sceneSRV;
	ID3D11ShaderResourceView* SRVs[3];

	PixelShader pixelShaderMain;
	PixelShader pixelShaderOutlineToTexture;
	PixelShader pixelShaderOutlineToScreen;
	PixelShader pixelShaderWriteIDs;

	ConstantBuffer<CB_PS_pixelshaderOutline> cb_ps_outline{};
	ConstantBuffer<CB_PS_id> cb_ps_id{};

	Timer tpsTimer;
	void UpdateTime();

	std::unordered_map<UINT, std::deque<XMFLOAT3>> trajectoryData;
	void DrawTrajectories();

	bool InitializeDirectX();
	HRESULT CreateResources();
	void UpdateRTVs();

	void SetMainResources();
	void SetOutlineResources();

	void DrawGrid();

	const std::vector<Primitive*>& GetPrimitivesSorted() const;

	void SetOutline(const XMFLOAT4& col, const float outlineScale);
	void RenderOutlineToTexture(bool toTexture);
	void RenderOutline();

	void SetIDToWrite(UINT id);
	Primitive* GetPrimitiveByID(UINT id);
	UINT GetIdByPixel(int px, int py);

	UINT nextId = 1;
	UINT NextId() { return nextId++; }

	std::vector<std::string> savedScenes;
	void UpdateSavedScenes();

	mutable std::vector<Primitive*> m_sortedCache;
	mutable bool     m_sortedDirty   = true;
	mutable XMFLOAT3 m_sortedCamPos  = { 1e30f, 1e30f, 1e30f };
};
