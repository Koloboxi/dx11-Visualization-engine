#include "../scene.h"
#include <fstream>
#include <cmath>
#include <cfloat>
#include <ctime>
#include <random>
#include <algorithm>
#include <numeric>
#include <cstdio>

namespace {

struct TopologyStats {
	int   beta0 = 0;
	int   beta1 = 0;
	int   numEdges = 0;
	int   numTriangles = 0;
	bool  allConnected = false;
};

std::vector<XMFLOAT3> GenerateCloud(int distribType, int pointCount, float bounds, float gaussSigma, float sphereR, unsigned seed)
{
	if (seed == 0) seed = (unsigned)time(nullptr);
	std::vector<XMFLOAT3> pts;
	pts.reserve(pointCount);
	std::mt19937 rng(seed);

	switch (distribType) {
	case 0: {
		std::uniform_real_distribution<float> d(-bounds, bounds);
		for (int i = 0; i < pointCount; ++i)
			pts.push_back({ d(rng), d(rng), d(rng) });
		break;
	}
	case 1: {
		std::normal_distribution<float> d(0.f, gaussSigma);
		for (int i = 0; i < pointCount; ++i)
			pts.push_back({ d(rng), d(rng), d(rng) });
		break;
	}
	case 2: {
		std::normal_distribution<float> d(0.f, 1.f);
		for (int i = 0; i < pointCount; ++i) {
			float x = d(rng), y = d(rng), z = d(rng);
			float len = sqrtf(x*x + y*y + z*z);
			if (len < 1e-6f) len = 1.f;
			pts.push_back({ x/len*sphereR, y/len*sphereR, z/len*sphereR });
		}
		break;
	}
	}
	return pts;
}

float CoverageRadius(const std::vector<XMFLOAT3>& pts)
{
	int N = (int)pts.size();
	if (N < 2) return 0.f;
	std::vector<float> key(N, FLT_MAX);
	std::vector<bool>  inMST(N, false);
	float maxEdge = 0.f;
	key[0] = 0.f;
	for (int iter = 0; iter < N; ++iter) {
		int u = -1;
		for (int i = 0; i < N; ++i)
			if (!inMST[i] && (u < 0 || key[i] < key[u])) u = i;
		inMST[u] = true;
		if (key[u] < FLT_MAX) maxEdge = std::max(maxEdge, key[u]);
		for (int v = 0; v < N; ++v) {
			if (inMST[v]) continue;
			float dx = pts[u].x - pts[v].x;
			float dy = pts[u].y - pts[v].y;
			float dz = pts[u].z - pts[v].z;
			float d  = sqrtf(dx*dx + dy*dy + dz*dz);
			if (d < key[v]) key[v] = d;
		}
	}
	return maxEdge / 2.f;
}

TopologyStats ComputeTopology(const std::vector<XMFLOAT3>& pts, float r)
{
	TopologyStats st;
	int N = (int)pts.size();
	if (N == 0) return st;

	std::vector<int> parent(N);
	std::iota(parent.begin(), parent.end(), 0);
	auto find = [&](int x) -> int {
		while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
		return x;
	};

	const float r2 = 4.f * r * r;

	for (int i = 0; i < N; ++i)
		for (int j = i + 1; j < N; ++j) {
			float dx = pts[i].x - pts[j].x;
			float dy = pts[i].y - pts[j].y;
			float dz = pts[i].z - pts[j].z;
			if (dx*dx + dy*dy + dz*dz <= r2) {
				int pi = find(i), pj = find(j);
				if (pi != pj) parent[pi] = pj;
				st.numEdges++;
			}
		}

	for (int i = 0; i < N; ++i)
		for (int j = i + 1; j < N; ++j)
			for (int k = j + 1; k < N; ++k) {
				auto d2 = [&](int a, int b) -> float {
					float dx = pts[a].x - pts[b].x;
					float dy = pts[a].y - pts[b].y;
					float dz = pts[a].z - pts[b].z;
					return dx*dx + dy*dy + dz*dz;
				};
				if (d2(i,j) <= r2 && d2(i,k) <= r2 && d2(j,k) <= r2)
					st.numTriangles++;
			}

	st.beta0 = 0;
	for (int i = 0; i < N; ++i) if (find(i) == i) st.beta0++;
	st.beta1 = st.numEdges - N + st.beta0;
	if (st.beta1 < 0) st.beta1 = 0;
	st.allConnected = (st.beta0 <= 1);
	return st;
}

std::vector<XMFLOAT3> ReadPHCloud(const nlohmann::json& sd)
{
	std::vector<XMFLOAT3> pts;
	if (sd.contains("cloud") && sd["cloud"].is_array())
		for (const auto& p : sd["cloud"])
			pts.push_back({ p[0].get<float>(), p[1].get<float>(), p[2].get<float>() });
	return pts;
}

void WritePHCloud(nlohmann::json& sd, const std::vector<XMFLOAT3>& pts)
{
	auto& arr = sd["cloud"];
	arr = nlohmann::json::array();
	for (const auto& p : pts) arr.push_back({ p.x, p.y, p.z });
}

void RebuildPHPrimitives(Scene& s)
{
	for (auto it = s.primitives.begin(); it != s.primitives.end(); ) {
		const std::string& n = (*it)->name;
		if (n.rfind("ph_pt_", 0) == 0 || n.rfind("ph_sph_", 0) == 0) {
			delete *it; it = s.primitives.erase(it);
		} else ++it;
	}

	auto rPtr = s.sceneFloats["ph_radius"];
	auto pts  = ReadPHCloud(s.sceneData);
	for (int i = 0; i < (int)pts.size(); ++i) {
		s.AddPoint(pts[i], XMFLOAT4(1.0f, 0.82f, 0.22f, 1.0f));
		s.primitives.back()->name = "ph_pt_" + std::to_string(i);

		s.AddSphere(1.0f, pts[i], 2, XMFLOAT4(0.28f, 0.62f, 1.0f, 0.20f));
		s.primitives.back()->name = "ph_sph_" + std::to_string(i);
		s.primitives.back()->SetUpdater([rPtr](Primitive& p, float t, float dt) {
			float r = (rPtr ? *rPtr : 0.f);
			p.SetScale(r < 0.001f ? 0.001f : r);
		});
	}
	s.UpdateLight();
}

void SyncPHSphereScales(Scene& s, float r)
{
	float scale = r < 0.001f ? 0.001f : r;
	for (Primitive* p : s.primitives)
		if (p->name.rfind("ph_sph_", 0) == 0) p->SetScale(scale);
}

void BindPHScene(Scene& s)
{
	if (!s.sceneData.contains("pointCount"))   s.sceneData["pointCount"]   = 20;
	if (!s.sceneData.contains("bounds"))       s.sceneData["bounds"]       = 200.0f;
	if (!s.sceneData.contains("distribType"))  s.sceneData["distribType"]  = 0;
	if (!s.sceneData.contains("gaussSigma"))   s.sceneData["gaussSigma"]   = 80.0f;
	if (!s.sceneData.contains("sphereR"))      s.sceneData["sphereR"]      = 120.0f;

	if (!s.sceneFloats.count("ph_radius"))
		s.sceneFloats["ph_radius"] = std::make_shared<float>(0.0f);
	if (!s.sceneFloats.count("ph_radius_speed"))
		s.sceneFloats["ph_radius_speed"] = std::make_shared<float>(
			s.sceneData.value("radiusSpeed", 25.0f));

	auto pts = ReadPHCloud(s.sceneData);
	s.sceneData["coverageR"] = CoverageRadius(pts);

	auto rPtr = s.sceneFloats["ph_radius"];
	for (Primitive* p : s.primitives) {
		if (p->name.rfind("ph_sph_", 0) == 0) {
			p->SetUpdater([rPtr](Primitive& pp, float t, float dt) {
				float r = (rPtr ? *rPtr : 0.f);
				pp.SetScale(r < 0.001f ? 0.001f : r);
			});
		}
	}

	s.sceneTick = [](Scene& sc, float t, float dt, bool paused) {
		auto rp    = sc.sceneFloats["ph_radius"];
		auto sp    = sc.sceneFloats["ph_radius_speed"];
		if (!rp || !sp) return;
		float speed = *sp;

		if (!paused) {
			*rp = t * speed;
		} else {
			if (speed > 0.f) sc.currentTime = *rp / speed;
			SyncPHSphereScales(sc, *rp);
		}

		auto cloud = ReadPHCloud(sc.sceneData);
		auto st = ComputeTopology(cloud, *rp);
		sc.sceneData["beta0"]     = st.beta0;
		sc.sceneData["beta1"]     = st.beta1;
		sc.sceneData["edges"]     = st.numEdges;
		sc.sceneData["triangles"] = st.numTriangles;
		sc.sceneData["connected"] = st.allConnected;

		if (!paused && st.allConnected) {
			sc.timePaused = true;
			sc.timeMax = sc.currentTime;
		}

		float covR = sc.sceneData.value("coverageR", 100.0f);
		for (auto& sl : sc.sceneSliders)
			if (sl.label == "PH Radius") { sl.max = covR * 3.f + 60.f; break; }
	};

	s.sceneReset = [](Scene& sc) {
		auto rp = sc.sceneFloats["ph_radius"];
		if (rp) *rp = 0.0f;
		SyncPHSphereScales(sc, 0.0f);
		auto cloud = ReadPHCloud(sc.sceneData);
		auto st = ComputeTopology(cloud, 0.0f);
		sc.sceneData["beta0"]     = st.beta0;
		sc.sceneData["beta1"]     = st.beta1;
		sc.sceneData["edges"]     = st.numEdges;
		sc.sceneData["triangles"] = st.numTriangles;
		sc.sceneData["connected"] = false;
	};

	GlobalSlider phSlider;
	phSlider.label    = "PH Radius";
	phSlider.valuePtr = rPtr.get();
	phSlider.min      = 0.f;
	phSlider.max      = (float)s.sceneData.value("coverageR", 100.0f) * 3.f + 60.f;
	phSlider.readOnly = [&s]() { return !s.timePaused; };
	phSlider.onChange = [&s]() {
		auto rp = s.sceneFloats["ph_radius"];
		auto sp = s.sceneFloats["ph_radius_speed"];
		if (!rp || !sp) return;
		float speed = *sp;
		if (speed > 0.f) s.currentTime = *rp / speed;
		SyncPHSphereScales(s, *rp);
	};
	s.sceneSliders.clear();
	s.sceneSliders.push_back(phSlider);

	{
		auto spPtr = s.sceneFloats["ph_radius_speed"];
		GlobalSlider speedSlider;
		speedSlider.label    = "PH Speed";
		speedSlider.valuePtr = spPtr.get();
		speedSlider.min      = 0.1f;
		speedSlider.max      = 2000.f;
		s.sceneSliders.push_back(speedSlider);
	}

	s.sceneWindows.clear();

	SceneWindow genWin;
	genWin.id = "ph_gen";  genWin.title = "PH Generator";
	genWin.pos[0] = 50.f;  genWin.pos[1] = 370.f;
	genWin.size[0] = 300.f; genWin.size[1] = 235.f;
	genWin.drawContent = [&s](SceneWindow& w) {
		auto& sd = s.sceneData;
		int   pc = sd.value("pointCount", 20);
		float bd = sd.value("bounds", 200.0f);
		int   dt = sd.value("distribType", 0);
		float gs = sd.value("gaussSigma", 80.0f);
		float sr = sd.value("sphereR", 120.0f);

		ImGui::SetNextItemWidth(55);
		if (ImGui::DragInt("##phN", &pc, 1, 3, 300))    sd["pointCount"] = pc;
		ImGui::SameLine(); ImGui::TextUnformatted("pts");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(75);
		if (ImGui::DragFloat("##phB", &bd, 2.f, 10.f, 5000.f, "b=%.0f")) sd["bounds"] = bd;

		static const char* distribNames[] = { "Uniform", "Gaussian", "Sphere surf" };
		ImGui::SetNextItemWidth(110);
		if (ImGui::Combo("Distrib##ph", &dt, distribNames, 3)) sd["distribType"] = dt;
		if (dt == 1) {
			ImGui::SameLine(); ImGui::SetNextItemWidth(65);
			if (ImGui::DragFloat("sigma##phG", &gs, 1.f, 1.f, 3000.f, "%.0f")) sd["gaussSigma"] = gs;
		}
		if (dt == 2) {
			ImGui::SameLine(); ImGui::SetNextItemWidth(65);
			if (ImGui::DragFloat("R##phS", &sr, 1.f, 10.f, 3000.f, "%.0f")) sd["sphereR"] = sr;
		}

		if (ImGui::Button("Generate Random##ph")) {
			auto pts = GenerateCloud(dt, pc, bd, gs, sr, 0);
			WritePHCloud(sd, pts);
			sd["coverageR"] = CoverageRadius(pts);
			RebuildPHPrimitives(s);
			s.ResetTime();
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Import cloud (x,y,z per line):");
		static char importPath[512] = {};
		ImGui::SetNextItemWidth(160);
		ImGui::InputText("##phpath", importPath, sizeof(importPath));
		ImGui::SameLine();
		if (ImGui::Button("Import##ph")) {
			std::ifstream f(importPath);
			if (f.is_open()) {
				std::vector<XMFLOAT3> pts;
				std::string line; float x,y,z;
				while (std::getline(f, line)) {
					if (line.empty() || line[0] == '#') continue;
					if (sscanf_s(line.c_str(), "%f,%f,%f", &x, &y, &z) == 3 ||
					    sscanf_s(line.c_str(), "%f %f %f",  &x, &y, &z) == 3)
						pts.push_back({ x, y, z });
				}
				if (!pts.empty()) {
					WritePHCloud(sd, pts);
					sd["pointCount"] = (int)pts.size();
					sd["coverageR"]  = CoverageRadius(pts);
					RebuildPHPrimitives(s);
					s.ResetTime();
				}
			}
		}

		ImGui::Separator();
		ImGui::Text("Coverage r: %.2f", sd.value("coverageR", 0.0f));
	};
	s.sceneWindows.push_back(genWin);

	SceneWindow topoWin;
	topoWin.id = "ph_topo"; topoWin.title = "PH Topology";
	topoWin.pos[0] = 365.f; topoWin.pos[1] = 370.f;
	topoWin.size[0] = 300.f; topoWin.size[1] = 235.f;
	topoWin.drawContent = [&s](SceneWindow& w) {
		auto& sd = s.sceneData;
		auto  rp = s.sceneFloats["ph_radius"];
		float r  = rp ? *rp : 0.f;
		int N = sd.contains("cloud") ? (int)sd["cloud"].size() : 0;
		ImGui::Text("Points: %d     r = %.2f", N, r);
		ImGui::Separator();
		ImGui::Text("Beta-0  (components): %d", sd.value("beta0", 0));
		ImGui::Text("Beta-1  (1-cycles):   %d", sd.value("beta1", 0));
		ImGui::Separator();
		ImGui::Text("Edges:     %d", sd.value("edges", 0));
		ImGui::Text("Triangles: %d", sd.value("triangles", 0));
		ImGui::Separator();
		float covR = sd.value("coverageR", 0.0f);
		ImGui::Text("Coverage r:  %.2f", covR);
		if (covR > 0.001f) {
			float progress = r / covR; if (progress > 1.f) progress = 1.f;
			char buf[32]; snprintf(buf, sizeof(buf), "%.1f%%", progress * 100.f);
			ImGui::ProgressBar(progress, ImVec2(-1.f, 0.f), buf);
		}
		ImGui::Separator();
		if (sd.value("connected", false))
			ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "Connected! (beta0=1)");
		else if (!s.timePaused)
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Animating...");
		else
			ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Paused");
	};
	s.sceneWindows.push_back(topoWin);
}

} // anonymous namespace

void Scene::LoadPersistentHomologyScene()
{
	for (Primitive* p : this->primitives) delete p;
	this->primitives.clear();
	this->orientationTransformer.SetTargetObjects({});
	this->ClearTrajectories();
	this->ClearSceneCustomState();
	this->currentSceneId = "ph";

	this->sceneData["pointCount"]  = 20;
	this->sceneData["bounds"]      = 200.0f;
	this->sceneData["distribType"] = 0;
	this->sceneData["gaussSigma"]  = 80.0f;
	this->sceneData["sphereR"]     = 120.0f;
	auto pts = GenerateCloud(0, 20, 200.0f, 80.0f, 120.0f, 0);
	WritePHCloud(this->sceneData, pts);
	this->sceneData["coverageR"] = CoverageRadius(pts);

	this->sceneFloats["ph_radius"]       = std::make_shared<float>(0.0f);
	this->sceneFloats["ph_radius_speed"] = std::make_shared<float>(25.0f);

	RebuildPHPrimitives(*this);

	BindPHScene(*this);

	this->ResetTime();
}

void BindNewtonScene(Scene& s);
void BindGRScene(Scene& s);

void Scene::RegisterSceneRebinders()
{
	this->sceneRebinders["ph"]     = [](Scene& s) { BindPHScene(s); };
	this->sceneRebinders["newton"] = [](Scene& s) { BindNewtonScene(s); };
	this->sceneRebinders["gr"]     = [](Scene& s) { BindGRScene(s); };
}
