#include "../scene.h"
#include <array>
#include <cmath>

void BindNewtonScene(Scene& s)
{
    auto ensureFloat = [&](const char* key, float def) {
        if (!s.sceneFloats.count(key))
            s.sceneFloats[key] = std::make_shared<float>(def);
    };
    ensureFloat("nb_G",  1000.f);
    ensureFloat("nb_m0", 1000.f);
    ensureFloat("nb_m1",    5.f);
    ensureFloat("nb_m2",    3.f);
    ensureFloat("nb_m3",    8.f);
    ensureFloat("nb_m4",    1.f);

    s.sceneSliders.clear();

    struct SliderDef { const char* label; const char* key; const char* lua; float mn, mx; };
    const SliderDef defs[] = {
        {"G",       "nb_G",  "G",       1.f,   5000.f},
        {"m_star",  "nb_m0", "m_star",  10.f, 10000.f},
        {"m_p1",    "nb_m1", "m_p1",   0.1f,   100.f},
        {"m_p2",    "nb_m2", "m_p2",   0.1f,   100.f},
        {"m_p3",    "nb_m3", "m_p3",   0.1f,   100.f},
        {"m_comet", "nb_m4", "m_comet",0.1f,    50.f},
    };
    for (auto& d : defs) {
        GlobalSlider sl;
        sl.label         = d.label;
        sl.luaGlobalName = d.lua;
        sl.valuePtr      = s.sceneFloats[d.key].get();
        sl.min           = d.mn;
        sl.max           = d.mx;
        s.sceneSliders.push_back(sl);
    }

    s.sceneWindows.clear();
    SceneWindow infoWin;
    infoWin.id    = "newton_info";
    infoWin.title = "N-body Info";
    infoWin.pos[0]  = 50.f;  infoWin.pos[1]  = 380.f;
    infoWin.size[0] = 340.f; infoWin.size[1] = 220.f;
    infoWin.drawContent = [&s](SceneWindow&) {
        auto sf = [&](const char* k) -> float {
            auto it = s.sceneFloats.find(k);
            return it != s.sceneFloats.end() ? *it->second : 0.f;
        };
        float G     = sf("nb_G");
        float ms[5] = { sf("nb_m0"), sf("nb_m1"), sf("nb_m2"), sf("nb_m3"), sf("nb_m4") };
        float Mtot  = ms[0] + ms[1] + ms[2] + ms[3] + ms[4];

        ImGui::Text("G = %.1f   M_total = %.1f", G, Mtot);
        ImGui::Separator();

        XMFLOAT3 starPos = { 0,0,0 };
        for (Primitive* p : s.primitives)
            if (p->name == "Star") { starPos = p->GetPosition(); break; }

        float Etot = 0.f;
        int n = (int)s.primitives.size();
        for (int i = 0; i < n && i < 5; i++) {
            XMFLOAT3 v = s.primitives[i]->velocity;
            float v2 = v.x*v.x + v.y*v.y + v.z*v.z;
            Etot += 0.5f * ms[i] * v2;
            for (int j = i+1; j < n && j < 5; j++) {
                XMFLOAT3 pi = s.primitives[i]->GetPosition();
                XMFLOAT3 pj = s.primitives[j]->GetPosition();
                float dx = pi.x-pj.x, dy = pi.y-pj.y, dz = pi.z-pj.z;
                float r = sqrtf(dx*dx+dy*dy+dz*dz);
                if (r > 0.001f) Etot -= G * ms[i] * ms[j] / r;
            }
        }
        ImGui::Text("Total energy: %.2f", Etot);
        ImGui::Separator();

        if (ImGui::BeginTable("##nbt", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Body",    ImGuiTableColumnFlags_WidthFixed, 70.f);
            ImGui::TableSetupColumn("r",       ImGuiTableColumnFlags_WidthFixed, 60.f);
            ImGui::TableSetupColumn("|v|",     ImGuiTableColumnFlags_WidthFixed, 52.f);
            ImGui::TableSetupColumn("eps",     ImGuiTableColumnFlags_WidthFixed, 70.f);
            ImGui::TableHeadersRow();

            for (int i = 0; i < (int)s.primitives.size() && i < 5; i++) {
                Primitive* prim = s.primitives[i];
                XMFLOAT3 pos = prim->GetPosition();
                XMFLOAT3 vel = prim->velocity;
                float dx = pos.x-starPos.x, dy = pos.y-starPos.y, dz = pos.z-starPos.z;
                float r  = (i == 0) ? 0.f : sqrtf(dx*dx+dy*dy+dz*dz);
                float v2 = vel.x*vel.x+vel.y*vel.y+vel.z*vel.z;
                float v  = sqrtf(v2);
                float eps = (i > 0 && r > 0.001f) ? (0.5f*v2 - G*ms[0]/r) : 0.f;

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(prim->name.c_str());
                ImGui::TableSetColumnIndex(1); ImGui::Text("%.1f", r);
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.2f", v);
                ImGui::TableSetColumnIndex(3); ImGui::Text("%.1f", eps);
            }
            ImGui::EndTable();
        }
    };
    s.sceneWindows.push_back(infoWin);
}

void Scene::LoadNewtonDemo()
{
    for (Primitive* p : this->primitives) delete p;
    this->primitives.clear();
    this->orientationTransformer.SetTargetObjects({});
    this->ClearTrajectories();
    this->ClearSceneCustomState();
    this->currentSceneId = "newton";

    this->sceneFloats["nb_G"]  = std::make_shared<float>(1000.f);
    this->sceneFloats["nb_m0"] = std::make_shared<float>(1000.f);
    this->sceneFloats["nb_m1"] = std::make_shared<float>(5.f);
    this->sceneFloats["nb_m2"] = std::make_shared<float>(3.f);
    this->sceneFloats["nb_m3"] = std::make_shared<float>(8.f);
    this->sceneFloats["nb_m4"] = std::make_shared<float>(1.f);

    struct BodyDef { XMFLOAT3 pos; XMFLOAT3 vel; float radius; XMFLOAT4 col; const char* name; };
    BodyDef bodies[5] = {
        { {  0,   0,   0}, {  0,   0,   0}, 30.f, {1.0f,0.8f,0.2f,1.0f}, "Star"     },
        { {200,   0,   0}, {  0,  72,   0}, 12.f, {0.2f,0.5f,1.0f,1.0f}, "Planet_1" },
        { {  0, 280,   0}, {-62,   0,  10},  9.f, {0.3f,0.9f,0.3f,1.0f}, "Planet_2" },
        { {-160,  0,  60}, {  0, -79,  15}, 11.f, {0.9f,0.3f,0.3f,1.0f}, "Planet_3" },
        { {100,-100,  80}, { 40,  40, -30},  7.f, {0.8f,0.5f,1.0f,1.0f}, "Comet"    },
    };

    for (auto& b : bodies) {
        this->AddSphere(b.radius, b.pos, 2, b.col);
        this->primitives.back()->name = b.name;
    }

    struct NBodyShared {
        std::array<XMFLOAT3, 5> vel;
        std::array<XMFLOAT3, 5> curPos;
        std::array<XMFLOAT3, 5> initVel;
        std::array<XMFLOAT3, 5> initPos;
        std::array<std::shared_ptr<float>, 5> massPtr;
        std::shared_ptr<float> Gptr;
        float prevT = -1.0f;
    };
    auto nbody = std::make_shared<NBodyShared>();
    const char* mkeys[] = {"nb_m0","nb_m1","nb_m2","nb_m3","nb_m4"};
    for (int i = 0; i < 5; i++) {
        nbody->vel[i]     = nbody->initVel[i] = bodies[i].vel;
        nbody->initPos[i] = nbody->curPos[i]  = bodies[i].pos;
        nbody->massPtr[i] = this->sceneFloats[mkeys[i]];
    }
    nbody->Gptr = this->sceneFloats["nb_G"];

    Primitive* prims[5];
    for (int i = 0; i < 5; i++)
        prims[i] = this->primitives[this->primitives.size() - 5 + i];

    prims[0]->SetUpdater([nbody](Primitive& self, float t, float dt) {
        if (t < nbody->prevT - 0.001f) {
            for (int i = 0; i < 5; i++) {
                nbody->curPos[i] = nbody->initPos[i];
                nbody->vel[i]    = nbody->initVel[i];
            }
        }
        nbody->prevT = t;

        float G = *nbody->Gptr;
        float soft2 = 15.f * 15.f;
        XMFLOAT3 forces[5] = {};
        for (int i = 0; i < 5; i++) {
            float mi = *nbody->massPtr[i];
            for (int j = i + 1; j < 5; j++) {
                float mj = *nbody->massPtr[j];
                float dx = nbody->curPos[j].x - nbody->curPos[i].x;
                float dy = nbody->curPos[j].y - nbody->curPos[i].y;
                float dz = nbody->curPos[j].z - nbody->curPos[i].z;
                float r2 = dx*dx + dy*dy + dz*dz + soft2;
                float r  = sqrtf(r2);
                float f  = G * mi * mj / r2;
                float fx = f*dx/r, fy = f*dy/r, fz = f*dz/r;
                forces[i].x += fx; forces[i].y += fy; forces[i].z += fz;
                forces[j].x -= fx; forces[j].y -= fy; forces[j].z -= fz;
            }
        }
        for (int i = 0; i < 5; i++) {
            float mi = *nbody->massPtr[i];
            nbody->vel[i].x    += forces[i].x / mi * dt;
            nbody->vel[i].y    += forces[i].y / mi * dt;
            nbody->vel[i].z    += forces[i].z / mi * dt;
            nbody->curPos[i].x += nbody->vel[i].x * dt;
            nbody->curPos[i].y += nbody->vel[i].y * dt;
            nbody->curPos[i].z += nbody->vel[i].z * dt;
        }
        self.SetPosition(nbody->curPos[0]);
    });

    for (int i = 1; i < 5; i++) {
        prims[i]->SetUpdater([nbody, i](Primitive& self, float t, float dt) {
            self.SetPosition(nbody->curPos[i]);
        });
    }

    prims[0]->luaScript = R"(
if _nv_t and t < _nv_t - 0.001 then _nv = nil end
_nv_t = t
local _ms = {m_star or 1000, m_p1 or 5, m_p2 or 3, m_p3 or 8, m_comet or 1}
if not _nv then
  _nv = {
    {vx=0,   vy=0,  vz=0,  px=0,    py=0,   pz=0,   m=_ms[1]},
    {vx=0,   vy=72, vz=0,  px=200,  py=0,   pz=0,   m=_ms[2]},
    {vx=-62, vy=0,  vz=10, px=0,    py=280, pz=0,   m=_ms[3]},
    {vx=0,   vy=-79,vz=15, px=-160, py=0,   pz=60,  m=_ms[4]},
    {vx=40,  vy=40, vz=-30,px=100,  py=-100,pz=80,  m=_ms[5]}
  }
end
for i=1,5 do _nv[i].m=_ms[i] end
local Nv, soft, Gv = 5, 15, G or 1000
local fx,fy,fz = {},{},{}
for i=1,Nv do fx[i]=0;fy[i]=0;fz[i]=0 end
for i=1,Nv do
  for j=i+1,Nv do
    local dx=_nv[j].px-_nv[i].px
    local dy=_nv[j].py-_nv[i].py
    local dz=_nv[j].pz-_nv[i].pz
    local r2=dx*dx+dy*dy+dz*dz+soft*soft
    local r=math.sqrt(r2)
    local f=Gv*_nv[i].m*_nv[j].m/r2
    local ex,ey,ez=f*dx/r,f*dy/r,f*dz/r
    fx[i]=fx[i]+ex;fy[i]=fy[i]+ey;fz[i]=fz[i]+ez
    fx[j]=fx[j]-ex;fy[j]=fy[j]-ey;fz[j]=fz[j]-ez
  end
end
for i=1,Nv do
  local mi=_nv[i].m
  _nv[i].vx=_nv[i].vx+fx[i]/mi*dt
  _nv[i].vy=_nv[i].vy+fy[i]/mi*dt
  _nv[i].vz=_nv[i].vz+fz[i]/mi*dt
  _nv[i].px=_nv[i].px+_nv[i].vx*dt
  _nv[i].py=_nv[i].py+_nv[i].vy*dt
  _nv[i].pz=_nv[i].pz+_nv[i].vz*dt
end
p.x=_nv[1].px; p.y=_nv[1].py; p.z=_nv[1].pz)";

    for (int i = 1; i < 5; i++) {
        int li = i + 1;
        prims[i]->luaScript =
            "if _nv and _nv[" + std::to_string(li) + "] then "
            "p.x=_nv[" + std::to_string(li) + "].px; "
            "p.y=_nv[" + std::to_string(li) + "].py; "
            "p.z=_nv[" + std::to_string(li) + "].pz end";
    }

    BindNewtonScene(*this);

    this->UpdateLight();
    this->ResetTime();
}
