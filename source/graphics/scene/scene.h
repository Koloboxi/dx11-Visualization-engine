#pragma once
#include "..\camera\Camera.h"
#include "jsonSaver.h"

#include "../shaders/shaders.h"
#include "..\misc\Colors.h"

#include "..\imgui\imgui.h"
#include "auxiliaryObjects.h"
#include "..\navcube\NavGizmo.h"
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

namespace CSV3DLoader { struct CSV3DData; }

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
	NavGizmo navGizmo;
	void Draw();

	void HandleSelection(Primitive* primitiveClicked);
	void HandleLMouse(int px, int py, bool tPressfRelease);
	// Forward an in-progress left-drag to the corner nav gizmo (arc → camera
	// rotation). No-op unless a gizmo arc is being dragged.
	bool NavGizmoActive() const { return navGizmo.HasActiveObject(); }
	void HandleNavGizmoDrag(int px, int py) { navGizmo.HandleObjMove(px, py, width, height, camera); }
	void DeleteSelected();

	Primitive* stagedPrimitive = nullptr;
	// Staging mode: enabled on CSV3D import, turned off from the SEM window.
	// While off, the tree behaves normally (double-click = rename, not stage).
	bool stagingEnabled = false;
	void SetStaged(Primitive* p);
	void ClearStaged();
	bool blockMousePick = false;
	bool blockMouseWheel = false;
	bool controllerSelected = false;

	bool rsSolid = true;
	bool rsWireframe = false;
	bool rsNoCull = true;

	// Width of geometry-shader-thickened lines (dim==1 primitives), in clip-space
	// half-width units. Pushed into CB_GS_geometryshader each frame by Graphics so
	// the top-strip thickness popup edits it live.
	float lineThickness = 0.001f;

	// Standard-projection orientation for the iso/dim presets driven by the nav
	// gizmo ball: which axis is "up" (0=X,1=Y,2=Z) and its sign (+1/-1). Set from
	// the top-strip projection-params popup.
	int projUpAxis = 2;
	int projUpSign = 1;

	float ambient = 0.4f;
	float intensity = 0.6f;
	float shininess = 25.0f;
	bool smoothShade = true;
	void UpdateLight();

	bool outlineThroughObjets = true;

	bool showGrid = true;
	bool showAxes = true;

	// Visual cross-section: clip meshes (dim==2) against a single plane.
	// axis selects the plane by its normal: 0 = YZ (normal X), 1 = XZ (normal Y),
	// 2 = XY (normal Z). offset slides the plane along that normal; flip swaps
	// which half is kept.
	struct SectionPlane {
		bool  enabled = false;
		int   axis    = 0;
		float offset  = 0.0f;
		bool  flip    = false;
	};
	SectionPlane section;

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
	// overrideColor: when non-null, every node is drawn in this flat colour
	// instead of the per-node T gradient.
	// gradLow/gradHigh: gradient endpoints for the T-value colouring.
	// ensureCCW: consistent winding for the triangle surface (disabled for the
	// untouched SEM source surface).
	// bcView: when true the per-node colour is not the gradLow..gradHigh gradient
	// over T. Instead only boundary-condition nodes are tinted (T==0 -> gradLow,
	// T==1 -> gradHigh) and every interior node is drawn light grey. Used to show
	// the thermal Dirichlet BCs after a solve.
	// registerColorSets: when true the primitive also stores both colourings as
	// named per-vertex colour sets ("tfield" and "bc"), so the caller can later
	// switch between them via Primitive::ActivateColorSet without rebuilding the
	// geometry. bcView only selects which of the two is active on creation.
	Primitive* AddFromCSV3D(const std::string& path, const std::string& name = "", SceneNode* parent = nullptr, const XMFLOAT4* overrideColor = nullptr,
		const XMFLOAT4& gradLow = Colors::BLUE, const XMFLOAT4& gradHigh = Colors::RED, bool ensureCCW = true, bool bcView = false, bool registerColorSets = false);

	// Build a primitive from already-loaded CSV3D mesh data (the SEM_MeshView the
	// SEM core exposes in-memory after a compute, marshalled into a CSV3DData),
	// bypassing the disk round-trip. Same colour/edge/colour-set semantics as
	// AddFromCSV3D; AddFromCSV3D itself loads the file and delegates here.
	Primitive* AddFromCSV3DData(const CSV3DLoader::CSV3DData& data, const std::string& name = "", SceneNode* parent = nullptr, const XMFLOAT4* overrideColor = nullptr,
		const XMFLOAT4& gradLow = Colors::BLUE, const XMFLOAT4& gradHigh = Colors::RED, bool ensureCCW = true, bool bcView = false, bool registerColorSets = false);
	void AddCubeWireframe(float halfSize, const XMFLOAT3& center, const XMFLOAT4& col);
	void AddCubeSolid(float halfSize, const XMFLOAT3& center, const XMFLOAT4& col);
	Primitive* AddRevolutionSurface(const std::vector<XMFLOAT3>& profile, UINT segments, const XMFLOAT4& col, const std::string& name, SceneNode* parent = nullptr);

	// Add a flat triangle-soup surface from explicit per-vertex positions/colours
	// (poses.size() must be a multiple of 3). ensureCCW makes the winding/normals
	// locally consistent; pass false to keep the supplied winding (e.g. a single
	// quad whose normal orientation is already chosen by the caller).
	Primitive* AddColoredTriangles(const std::vector<XMFLOAT3>& poses, const std::vector<XMFLOAT4>& cols,
		const std::string& name, SceneNode* parent = nullptr, bool ensureCCW = true);

	// Add a mirror copy of `src` reflected across `plane` (xyz = normal, w = d).
	// The new primitive is parented under `parent` (or root) and registered like
	// any other. When mirrorEdgeChildren is set, any direct primitive child of
	// `src` whose name marks it as a wireframe ("(edges)") is also mirrored and
	// parented under the clone, so a surface + its edges mirror together. Returns
	// the cloned primitive (null on a degenerate plane or null src).
	Primitive* AddMirroredCopy(Primitive* src, const XMFLOAT4& plane, const std::string& name,
		SceneNode* parent = nullptr, bool mirrorEdgeChildren = true);

	void RemovePrimitive(Primitive* p);
	void RemoveNode(SceneNode* n);
	void RemovePrimitivesByPrefix(const std::string& prefix);
	void SetController(SceneController* ctrl);

	// Empty (non-primitive) grouping node, parented under `parent` (or root).
	// Not tracked in `primitives`; removed via RemoveNode or with its subtree.
	SceneNode* AddGroupNode(const std::string& name, SceneNode* parent = nullptr);

	// Toggle a node's visibility and one-shot-cascade the new state to all
	// descendants; afterwards each child toggles independently.
	void SetNodeVisibleCascade(SceneNode* n, bool show);

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
	Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerSolidNoCull;

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
	ConstantBuffer<CB_VS_section> cb_vs_section{};

	void ApplySectionCB(bool forceDisabled = false);

	// Push the current scene lighting (ambient/intensity/shininess/smooth shading)
	// onto a single primitive, if it is illuminated. Called right after a
	// primitive is created so it never renders with default (zero) lighting — a
	// black surface with a harsh specular highlight — before the next
	// UpdateLight(); UpdateLight() itself fans this out over every primitive.
	void ApplyCurrentLighting(Primitive* p);

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

	void DestroyNodeRecursive(SceneNode* n);

	std::vector<std::string> savedScenes;
	void UpdateSavedScenes();

	mutable std::vector<Primitive*> m_sortedCache;
	mutable bool     m_sortedDirty   = true;
	mutable XMFLOAT3 m_sortedCamPos  = { 1e30f, 1e30f, 1e30f };
};
