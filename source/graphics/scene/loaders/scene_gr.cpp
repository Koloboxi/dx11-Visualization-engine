#include "../scene.h"
#include <cmath>
#include <array>
#include <algorithm>

namespace {

struct GRShared {
    struct Body {
        XMFLOAT3 pos{};
        XMFLOAT3 vel{};
        XMFLOAT3 initPos{};
        XMFLOAT3 initVel{};
        double   tauAccum = 0.0;
        float    tauRate  = 1.0f;
        const char* name  = "";
    };
    static constexpr int N = 3;
    std::array<Body, N> bodies{};
    std::shared_ptr<float> GMptr;
    std::shared_ptr<float> c2ptr;
    float prevT = -1.0f;
};

inline float Len(const XMFLOAT3& v) {
    return sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
}

} // anonymous namespace

void BindGRScene(Scene& s)
{
    auto ensureFloat = [&](const char* key, float def) {
        if (!s.sceneFloats.count(key))
            s.sceneFloats[key] = std::make_shared<float>(def);
    };
    ensureFloat("gr_GM", 10000.f);
    ensureFloat("gr_c2",  2000.f);

    s.sceneSliders.clear();
    {
        struct SD { const char* label; const char* key; const char* lua; float mn, mx; };
        const SD defs[] = {
            {"GM", "gr_GM", "gr_GM", 1000.f, 50000.f},
            {"c²", "gr_c2", "gr_c2",  100.f, 20000.f},
        };
        for (auto& d : defs) {
            GlobalSlider sl;
            sl.label         = d.label;
            sl.luaGlobalName = d.lua;
            sl.valuePtr      = s.sceneFloats[d.key].get();
            sl.min = d.mn; sl.max = d.mx;
            s.sceneSliders.push_back(sl);
        }
    }

    auto gr = std::make_shared<GRShared>();
    gr->GMptr = s.sceneFloats["gr_GM"];
    gr->c2ptr = s.sceneFloats["gr_c2"];

    if (s.sceneData.contains("gr_bodies") && s.sceneData["gr_bodies"].is_array()) {
        auto& bd = s.sceneData["gr_bodies"];
        for (int i = 0; i < GRShared::N && i < (int)bd.size(); i++) {
            auto& b = gr->bodies[i];
            b.initPos = { bd[i]["ip"][0], bd[i]["ip"][1], bd[i]["ip"][2] };
            b.initVel = { bd[i]["iv"][0], bd[i]["iv"][1], bd[i]["iv"][2] };
        }
    }
    gr->bodies[0].name = "Mercury";
    gr->bodies[1].name = "Venus";
    gr->bodies[2].name = "Comet";
    for (auto& b : gr->bodies) { b.pos = b.initPos; b.vel = b.initVel; }

    Primitive* grbody[GRShared::N] = {};
    Primitive* grbh = nullptr;
    for (Primitive* p : s.primitives) {
        if (p->name == "gr_bh") { grbh = p; continue; }
        for (int i = 0; i < GRShared::N; i++)
            if (p->name == "gr_body_" + std::to_string(i))
                grbody[i] = p;
    }

    if (grbody[0]) {
        grbody[0]->SetUpdater([gr](Primitive& self, float t, float dt) {
            if (gr->prevT > 0.f && t < gr->prevT - 0.001f) {
                for (auto& b : gr->bodies) {
                    b.pos = b.initPos; b.vel = b.initVel;
                    b.tauAccum = 0.0;
                }
                gr->prevT = -1.f;
            }
            gr->prevT = t;

            float GM = *gr->GMptr;
            float c2 = *gr->c2ptr;
            float Rs = 2.f * GM / c2;
            float softR = Rs * 0.5f;
            float soft2 = softR * softR;

            for (int i = 0; i < GRShared::N; i++) {
                auto& b = gr->bodies[i];
                float rx = b.pos.x, ry = b.pos.y, rz = b.pos.z;
                float vx = b.vel.x, vy = b.vel.y, vz = b.vel.z;
                float r2     = rx*rx + ry*ry + rz*rz + soft2;
                float r      = sqrtf(r2);
                float r3     = r2 * r;
                float v2     = vx*vx + vy*vy + vz*vz;
                float rdotv  = rx*vx + ry*vy + rz*vz;

                float newt   = GM / r3;
                float pnCoef = GM / (c2 * r3);
                float pnA    = 4.f * GM / r - v2;

                float ax = -newt*rx + pnCoef*(pnA*rx + 4.f*rdotv*vx);
                float ay = -newt*ry + pnCoef*(pnA*ry + 4.f*rdotv*vy);
                float az = -newt*rz + pnCoef*(pnA*rz + 4.f*rdotv*vz);

                b.vel.x += ax * dt; b.vel.y += ay * dt; b.vel.z += az * dt;
                b.pos.x += b.vel.x * dt; b.pos.y += b.vel.y * dt; b.pos.z += b.vel.z * dt;

                float rTrue  = sqrtf(b.pos.x*b.pos.x + b.pos.y*b.pos.y + b.pos.z*b.pos.z);
                float v2new  = b.vel.x*b.vel.x + b.vel.y*b.vel.y + b.vel.z*b.vel.z;
                float dilArg = 1.f - Rs / (rTrue > 0.001f ? rTrue : 0.001f) - v2new / c2;
                if (dilArg < 0.f) dilArg = 0.f;
                b.tauRate   = sqrtf(dilArg);
                b.tauAccum += (double)b.tauRate * dt;
            }
            self.SetPosition(gr->bodies[0].pos);
        });
    }

    for (int i = 1; i < GRShared::N; i++) {
        if (grbody[i]) {
            grbody[i]->SetUpdater([gr, i](Primitive& self, float t, float dt) {
                self.SetPosition(gr->bodies[i].pos);
            });
        }
    }

    s.sceneTick = [gr](Scene& sc, float t, float dt, bool paused) {
        float GM = *gr->GMptr, c2 = *gr->c2ptr;
        float Rs = 2.f * GM / c2;
        for (Primitive* p : sc.primitives) {
            if (p->name == "gr_bh")          p->SetScale(Rs > 0.001f ? Rs : 0.001f);
            else if (p->name == "gr_photon") p->SetScale(1.5f * Rs > 0.001f ? 1.5f * Rs : 0.001f);
            else if (p->name == "gr_isco")   p->SetScale(3.f  * Rs > 0.001f ? 3.f  * Rs : 0.001f);
        }
    };

    s.sceneReset = [gr](Scene& sc) {
        for (auto& b : gr->bodies) {
            b.pos = b.initPos; b.vel = b.initVel;
            b.tauAccum = 0.0;
        }
        gr->prevT = -1.f;
    };

    s.sceneWindows.clear();

    SceneWindow orbWin;
    orbWin.id    = "gr_orbits";
    orbWin.title = "GR Orbits";
    orbWin.pos[0]  = 50.f;   orbWin.pos[1]  = 380.f;
    orbWin.size[0] = 430.f;  orbWin.size[1] = 175.f;
    orbWin.drawContent = [gr, &s](SceneWindow&) {
        float GM = *gr->GMptr, c2 = *gr->c2ptr;
        float Rs   = 2.f * GM / c2;
        float ISCO = 3.f * Rs;

        if (ImGui::BeginTable("##grorb", 5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("Body",   ImGuiTableColumnFlags_WidthFixed, 64.f);
            ImGui::TableSetupColumn("r",      ImGuiTableColumnFlags_WidthFixed, 56.f);
            ImGui::TableSetupColumn("|v|",    ImGuiTableColumnFlags_WidthFixed, 52.f);
            ImGui::TableSetupColumn("d\xCF\x84/dt", ImGuiTableColumnFlags_WidthFixed, 68.f);
            ImGui::TableSetupColumn("\xCF\x84 acc.", ImGuiTableColumnFlags_WidthFixed, 72.f);
            ImGui::TableHeadersRow();

            for (int i = 0; i < GRShared::N; i++) {
                auto& b  = gr->bodies[i];
                float r  = Len(b.pos);
                float v  = Len(b.vel);
                bool inside = r < ISCO;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (inside)
                    ImGui::TextColored({1.f,0.35f,0.35f,1.f}, "%s*", b.name);
                else
                    ImGui::TextUnformatted(b.name);
                ImGui::TableSetColumnIndex(1); ImGui::Text("%.1f", r);
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.2f", v);
                ImGui::TableSetColumnIndex(3); ImGui::Text("%.5f", b.tauRate);
                ImGui::TableSetColumnIndex(4); ImGui::Text("%.3f", (float)b.tauAccum);
            }
            ImGui::EndTable();
        }
        bool anyInside = false;
        for (auto& b : gr->bodies) if (Len(b.pos) < ISCO) { anyInside = true; break; }
        if (anyInside)
            ImGui::TextColored({1.f,0.35f,0.35f,1.f}, "* r < ISCO (%.1f)", ISCO);
        ImGui::Text("Coord. time: %.3f", s.currentTime);
    };
    s.sceneWindows.push_back(orbWin);

    SceneWindow infoWin;
    infoWin.id    = "gr_info";
    infoWin.title = "GR Parameters";
    infoWin.pos[0]  = 490.f;  infoWin.pos[1]  = 380.f;
    infoWin.size[0] = 320.f;  infoWin.size[1] = 270.f;
    infoWin.drawContent = [gr, &s](SceneWindow&) {
        float GM = *gr->GMptr, c2 = *gr->c2ptr;
        float c  = sqrtf(c2);
        float Rs = 2.f * GM / c2;

        ImGui::Text("GM:                    %.1f", GM);
        ImGui::Text("c (speed of light):    %.2f u/s", c);
        ImGui::Separator();
        ImGui::Text("Schwarzschild radius Rs: %.2f", Rs);
        ImGui::Text("Photon sphere:           %.2f", 1.5f * Rs);
        ImGui::Text("ISCO (innermost stable): %.2f", 3.f  * Rs);
        ImGui::Separator();

        ImGui::TextUnformatted("1PN precession estimate:");
        for (int i = 0; i < GRShared::N; i++) {
            auto& b  = gr->bodies[i];
            float r  = Len(b.pos);
            if (r < 0.001f) continue;
            float vcirc = sqrtf(GM / r);
            float T_approx = 2.f * 3.14159265f * r / (vcirc > 0.001f ? vcirc : 0.001f);
            float precRad  = 6.f * 3.14159265f * GM / (c2 * r);
            float precDeg  = precRad * 180.f / 3.14159265f;
            ImGui::Text("  %-7s ~%.1f deg/orbit  T~%.1f", b.name, precDeg, T_approx);
        }
        ImGui::Separator();

        float minTau = 1e10f, maxTau = 0.f;
        for (auto& b : gr->bodies) {
            float tau = (float)b.tauAccum;
            minTau = std::min(minTau, tau);
            maxTau = std::max(maxTau, tau);
        }
        if (s.currentTime > 0.01f)
            ImGui::Text("Clock desync (orbits): %.4f", maxTau - minTau);
        else
            ImGui::TextDisabled("Clock desync: n/a (t=0)");
    };
    s.sceneWindows.push_back(infoWin);
}

void Scene::LoadGRScene()
{
    for (Primitive* p : this->primitives) delete p;
    this->primitives.clear();
    this->orientationTransformer.SetTargetObjects({});
    this->ClearTrajectories();
    this->ClearSceneCustomState();
    this->currentSceneId = "gr";

    this->sceneFloats["gr_GM"] = std::make_shared<float>(10000.f);
    this->sceneFloats["gr_c2"] = std::make_shared<float>(2000.f);

    float GM = 10000.f, c2 = 2000.f;
    float Rs = 2.f * GM / c2;

    struct BodyDef { XMFLOAT3 pos; XMFLOAT3 vel; float radius; XMFLOAT4 col; const char* name; };
    BodyDef bodyDefs[GRShared::N] = {
        { {200.f,  0.f,  0.f}, {0.f,  7.0f,  0.f}, 8.f,  {0.5f,0.7f,1.0f,1.0f}, "gr_body_0" },
        { {120.f,  0.f,  0.f}, {0.f, 10.5f,  0.f}, 6.f,  {1.0f,0.7f,0.3f,1.0f}, "gr_body_1" },
        { {350.f,  0.f, 50.f}, {0.f,  5.0f,  2.0f},7.f,  {0.6f,1.0f,0.6f,1.0f}, "gr_body_2" },
    };

    this->sceneData["gr_bodies"] = nlohmann::json::array();
    for (int i = 0; i < GRShared::N; i++) {
        auto& d = bodyDefs[i];
        this->sceneData["gr_bodies"].push_back({
            {"ip", {d.pos.x, d.pos.y, d.pos.z}},
            {"iv", {d.vel.x, d.vel.y, d.vel.z}}
        });
    }

    this->AddSphere(1.0f, {0,0,0}, 2, {0.08f, 0.04f, 0.02f, 1.0f});
    this->primitives.back()->name = "gr_bh";
    this->primitives.back()->SetScale(Rs);

    this->AddArc3d(1.0f, 0.05f, 360.f, {0,0,0}, 32, {0.9f, 0.9f, 0.3f, 0.55f});
    this->primitives.back()->name = "gr_photon";
    this->primitives.back()->SetScale(1.5f * Rs);

    this->AddArc3d(1.0f, 0.05f, 360.f, {0,0,0}, 32, {1.0f, 0.3f, 0.3f, 0.45f});
    this->primitives.back()->name = "gr_isco";
    this->primitives.back()->SetScale(3.f * Rs);

    for (int i = 0; i < GRShared::N; i++) {
        auto& d = bodyDefs[i];
        this->AddSphere(d.radius, d.pos, 2, d.col);
        this->primitives.back()->name = d.name;
    }

    BindGRScene(*this);

    this->UpdateLight();
    this->ResetTime();
}
