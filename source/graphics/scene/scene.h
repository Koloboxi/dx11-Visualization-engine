#pragma once
#include "..\camera\Camera.h"
#include "jsonSaver.h"
#include "..\misc\DataVisualizer.h"

#include "../shaders/shaders.h"
#include "..\misc\Colors.h"
#include "..\misc\inlines.h"


class Scene {
public:
	bool Initialize(ID3D11Device* device, ID3D11DeviceContext* deviceContext, std::wstring shadersPath, ID3D11DepthStencilView* dsView, ID3D11DepthStencilView* dsViewNoMSAA, ID3D11RenderTargetView* mainRTV, VertexShader* vs, int windowWidth, int windowHeight);
	void OnResize(ID3D11DepthStencilView* dsView, ID3D11DepthStencilView* dsViewNoMSAA, ID3D11RenderTargetView* mainRTV, int newWidth, int newHeight);
	ID3D11RenderTargetView* GetMaskRTV() const;

	Camera camera;
	void Draw();

	void HandleMouseInteraction(int px, int py);


	bool rsSolid = true;
	bool rsWireframe = false;

	float ambient = 0.4f;
	float intensity = 0.6f;
	float shininess = 25.0f;
	bool smoothShade = true;
	void UpdateLight();

	bool outlineThroughObjets = true;


	std::vector<Primitive*> primitives;

	void AddPoint(const XMFLOAT3& pos, const XMFLOAT4& col);
	void AddLine(const std::vector<XMFLOAT3>& poses, const XMFLOAT4& col);
	void AddPolygon(const std::vector<XMFLOAT3>& poses, const XMFLOAT4& col);

	void AddSphere(float radius, const XMFLOAT3& pos, const UINT numSubdivides, const XMFLOAT4& col);


	std::string scenesPath = "Data/Scenes/";
	const std::vector<std::string>& GetSavedScenes() const;
	void SaveScene(std::string name);
	void ClearScene();
	void LoadScene(std::string name);

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

	bool InitializeDirectX();
	HRESULT CreateResources();
	void UpdateRTVs();

	void SetMainResources();
	void SetOutlineResources();

	std::vector<Primitive*>& GetPrimitivesSorted();

	void SetOutline(const XMFLOAT4& col, const float outlineScale);
	void RenderOutlineToTexture(bool toTexture);
	void RenderOutline();

	void SetIDToWrite(UINT id);
	Primitive* GetPrimitiveByID(UINT id);

	std::vector<std::string> savedScenes;
	void UpdateSavedScenes();
};

