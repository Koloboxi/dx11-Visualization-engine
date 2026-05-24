#include "graphics.h"
#include <fstream>

// ─── helpers ──────────────────────────────────────────────────────────────────

static Primitive* FindSelected(const std::vector<Primitive*>& prims) {
    for (Primitive* p : prims)
        if (p->selected) return p;
    return nullptr;
}

// ─── "Add primitive" popup state ──────────────────────────────────────────────

struct AddPrimState {
    int type = 0;  // 0=Point 1=Line 2=Polygon 3=Sphere 4=Line3d 5=Arc3d 6=STL 7=Arrow3d

    // shared
    float color[4] = { 0.6f, 0.6f, 0.9f, 1.0f };

    // Point / Line / Polygon – up to 16 points
    float pts[16][3] = {};
    int   ptCount = 1;

    // Sphere
    float sRadius = 100.0f;
    int   sSubdiv = 2;
    float sPos[3] = {};

    // Line3d / Arc3d
    float l3Radius   = 5.0f;
    int   l3Subdiv   = 8;

    // Arc3d
    float arcRadius  = 75.0f;
    float arcAngle   = 90.0f;
    float arcCenter[3] = {};

    // STL
    char  stlPath[512] = {};

    // Arrow3d
    float arrShaftR  = 3.0f;
    float arrHeadR   = 8.0f;
    float arrHeadLen = 20.0f;
    float arrFrom[3] = {};
    float arrTo[3]   = { 0, 100, 0 };
    int   arrSides   = 8;
};

static AddPrimState s_add;

static void AddPointsEditor(int& count, float pts[][3], int maxPts,
                             const char* btnLabel = "+ Point")
{
    for (int i = 0; i < count; ++i) {
        std::string lbl = "##pt" + std::to_string(i);
        ImGui::PushItemWidth(180);
        ImGui::InputFloat3(lbl.c_str(), pts[i]);
        ImGui::PopItemWidth();
        if (count > 1) {
            ImGui::SameLine();
            if (ImGui::SmallButton(("x##" + std::to_string(i)).c_str())) {
                for (int j = i; j < count - 1; ++j)
                    memcpy(pts[j], pts[j + 1], sizeof(float) * 3);
                --count; --i;
            }
        }
    }
    if (count < maxPts && ImGui::SmallButton(btnLabel))
        ++count;
}

// ─── GUI main ─────────────────────────────────────────────────────────────────

void Graphics::Gui()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // FPS counter
    static int fpsCounter = 0;
    static std::string fpsStr;
    ++fpsCounter;
    if (fpsTimer.GetMillisecondsElapsed() >= 1000) {
        fpsTimer.Restart();
        fpsStr = std::format("{}", fpsCounter);
        fpsCounter = 0;
    }
    ImGui::GetBackgroundDrawList()->AddText(ImVec2(0, 0), ImColor(0, 255, 0), fpsStr.c_str());

    bool blockMousePick = false;

    // ── Parameters panel ──────────────────────────────────────────────────────
    ImGui::Begin("Parameters");
    if (ImGui::BeginTabBar("Panels")) {

        // ── View tab ──────────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("View")) {
            if (ImGui::Button("Reset Pos"))
                this->scene.camera.SetPosition(BaseVectors::ORIGIN);
            ImGui::SameLine();
            if (ImGui::Button("Reset Scale"))
                this->scene.camera.SetScale(1);

            ImGui::Separator();
            ImGui::Checkbox("Fill",            &this->scene.rsSolid);
            ImGui::Checkbox("Wireframe",       &this->scene.rsWireframe);
            ImGui::Checkbox("Outline through", &this->scene.outlineThroughObjets);
            ImGui::EndTabItem();
        }

        // ── Scenes tab ────────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Scenes")) {
            static std::string name;
            if (ImGui::Button("Save"))
                this->scene.SaveScene(name + ".json");
            ImGui::SameLine();
            if (ImGui::Button("Clear")) {
                for (Primitive* p : this->scene.primitives)
                    this->luaEditor.OnPrimitiveRemoved(p);
                this->scene.ClearScene();
            }
            ImGui::SameLine();
            if (ImGui::Button("Newton demo")) {
                for (Primitive* p : this->scene.primitives)
                    this->luaEditor.OnPrimitiveRemoved(p);
                this->scene.LoadNewtonDemo();
                this->luaEditor.globals = { { "G", 1000.f } };
            }
            if (ImGui::Button("PH Demo")) {
                for (Primitive* p : this->scene.primitives)
                    this->luaEditor.OnPrimitiveRemoved(p);
                this->scene.LoadPersistentHomologyScene();
            }
            ImGui::InputText("Scene name", &name);

            const auto& saved = this->scene.GetSavedScenes();
            if (!saved.empty()) {
                static int selIdx = 0;
                if (selIdx >= (int)saved.size()) selIdx = 0;
                if (ImGui::BeginCombo("Saved scenes", saved[selIdx].c_str())) {
                    for (int i = 0; i < (int)saved.size(); ++i) {
                        bool isSel = (selIdx == i);
                        if (ImGui::Selectable(saved[i].c_str(), isSel)) {
                            selIdx = i;
                            for (Primitive* p : this->scene.primitives)
                                this->luaEditor.OnPrimitiveRemoved(p);
                            this->scene.LoadScene(saved[i]);
                            // Re-apply Lua scripts that were stored in primitives
                            this->luaEditor.ReApplyAll(this->scene.primitives);
                        }
                        if (isSel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }
            else {
                ImGui::TextDisabled("No saved scenes");
            }
            ImGui::EndTabItem();
        }

        // ── Light tab ─────────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Light")) {
            bool changed = false;
            changed |= ImGui::DragFloat("Ambient",   &this->scene.ambient,   0.05f, 0.0f, 1.0f,  "%.2f");
            changed |= ImGui::DragFloat("Intensity", &this->scene.intensity, 0.05f, 0.0f, 1.0f,  "%.2f");
            changed |= ImGui::DragFloat("Shininess", &this->scene.shininess, 0.1f,  0.0f, 100.0f,"%.1f");
            changed |= ImGui::Checkbox("Smooth shade", &this->scene.smoothShade);
            if (changed) 
                this->scene.UpdateLight();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
        blockMousePick = true;
    ImGui::End();

    // ── Primitives panel ──────────────────────────────────────────────────────
    ImGui::Begin("Primitives");

    // Add (+) and Remove (-) buttons
    if (ImGui::Button("+")) ImGui::OpenPopup("AddPrimitive");
    ImGui::SameLine();
    if (ImGui::Button("-")) {
        Primitive* sel = FindSelected(this->scene.primitives);
        if (sel) {
            this->luaEditor.OnPrimitiveRemoved(sel);
            this->scene.RemovePrimitive(sel);
        }
    }
    ImGui::Separator();

    // ── "Add Primitive" popup ─────────────────────────────────────────────
    ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_Always);
    if (ImGui::BeginPopupModal("AddPrimitive", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static const char* types[] = { "Point","Line","Polygon","Sphere","Line3d","Arc3d","STL","Arrow3d" };
        ImGui::Combo("Type", &s_add.type, types, IM_ARRAYSIZE(types));
        ImGui::Separator();

        switch (s_add.type) {
        case 0: // Point
            ImGui::InputFloat3("Position", s_add.pts[0]);
            break;
        case 1: // Line
            AddPointsEditor(s_add.ptCount, s_add.pts, 16);
            break;
        case 2: // Polygon
            AddPointsEditor(s_add.ptCount, s_add.pts, 16);
            break;
        case 3: // Sphere
            ImGui::DragFloat("Radius",       &s_add.sRadius, 1.0f, 0.1f, 5000.0f);
            ImGui::InputFloat3("Centre",     s_add.sPos);
            ImGui::SliderInt("Subdivisions", &s_add.sSubdiv, 0, 5);
            break;
        case 4: // Line3d
            ImGui::DragFloat("Tube radius",  &s_add.l3Radius, 0.1f, 0.1f, 500.0f);
            ImGui::SliderInt("Sides",        &s_add.l3Subdiv, 3, 24);
            AddPointsEditor(s_add.ptCount, s_add.pts, 16);
            break;
        case 5: // Arc3d
            ImGui::DragFloat("Arc radius",   &s_add.arcRadius, 1.0f, 0.1f, 5000.0f);
            ImGui::DragFloat("Tube radius",  &s_add.l3Radius,  0.1f, 0.1f, 500.0f);
            ImGui::DragFloat("Angle (deg)",  &s_add.arcAngle,  1.0f, 1.0f, 360.0f);
            ImGui::InputFloat3("Centre",     s_add.arcCenter);
            ImGui::SliderInt("Subdivisions", &s_add.l3Subdiv, 3, 64);
            break;
        case 6: // STL
            ImGui::InputText("File path", s_add.stlPath, sizeof(s_add.stlPath));
            break;
        case 7: // Arrow3d
            ImGui::DragFloat("Shaft radius",  &s_add.arrShaftR,  0.1f, 0.1f, 500.f);
            ImGui::DragFloat("Head radius",   &s_add.arrHeadR,   0.1f, 0.1f, 500.f);
            ImGui::DragFloat("Head length",   &s_add.arrHeadLen, 0.5f, 0.5f, 1000.f);
            ImGui::SliderInt("Sides",         &s_add.arrSides, 3, 24);
            ImGui::InputFloat3("From",        s_add.arrFrom);
            ImGui::InputFloat3("To",          s_add.arrTo);
            break;
        }

        ImGui::Separator();
        ImGui::ColorEdit4("Color", s_add.color, ImGuiColorEditFlags_AlphaBar);
        ImGui::Separator();

        XMFLOAT4 col(s_add.color[0], s_add.color[1], s_add.color[2], s_add.color[3]);

        if (ImGui::Button("Add", ImVec2(80, 0))) {
            switch (s_add.type) {
            case 0:
                this->scene.AddPoint({ s_add.pts[0][0], s_add.pts[0][1], s_add.pts[0][2] }, col);
                break;
            case 1: {
                std::vector<XMFLOAT3> poses(s_add.ptCount);
                for (int i = 0; i < s_add.ptCount; ++i)
                    poses[i] = { s_add.pts[i][0], s_add.pts[i][1], s_add.pts[i][2] };
                this->scene.AddLine(poses, col);
                break;
            }
            case 2: {
                std::vector<XMFLOAT3> poses(s_add.ptCount);
                for (int i = 0; i < s_add.ptCount; ++i)
                    poses[i] = { s_add.pts[i][0], s_add.pts[i][1], s_add.pts[i][2] };
                this->scene.AddPolygon(poses, col);
                break;
            }
            case 3:
                this->scene.AddSphere(
                    s_add.sRadius,
                    { s_add.sPos[0], s_add.sPos[1], s_add.sPos[2] },
                    s_add.sSubdiv, col);
                break;
            case 4: {
                std::vector<XMFLOAT3> poses(s_add.ptCount);
                for (int i = 0; i < s_add.ptCount; ++i)
                    poses[i] = { s_add.pts[i][0], s_add.pts[i][1], s_add.pts[i][2] };
                this->scene.AddLine3d(s_add.l3Radius, poses, s_add.l3Subdiv, col);
                break;
            }
            case 5:
                this->scene.AddArc3d(
                    s_add.arcRadius, s_add.l3Radius, s_add.arcAngle,
                    { s_add.arcCenter[0], s_add.arcCenter[1], s_add.arcCenter[2] },
                    s_add.l3Subdiv, col);
                break;
            case 6:
                this->scene.AddFromSTL(s_add.stlPath, col);
                break;
            case 7:
                this->scene.AddArrow3d(
                    s_add.arrShaftR, s_add.arrHeadR, s_add.arrHeadLen,
                    { s_add.arrFrom[0], s_add.arrFrom[1], s_add.arrFrom[2] },
                    { s_add.arrTo[0],   s_add.arrTo[1],   s_add.arrTo[2]   },
                    s_add.arrSides, col);
                break;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0)))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    // ── Primitive list ────────────────────────────────────────────────────
    if (ImGui::BeginChild("PrimList", ImVec2(0, 0), false)) {
        for (UINT i = 0; i < this->scene.primitives.size(); ++i) {
            Primitive* prim = this->scene.primitives[i];
            UCHAR dim = prim->GetDimension();
            const char* dimSuffix = (dim == 0) ? "p" : (dim == 1 ? "l" : "t");

            std::string label;
            if (!prim->name.empty())
                label = prim->name;
            else
                label = std::to_string(i) + dimSuffix;

            if (prim->HasUpdater())
                label += " *";

            if (ImGui::Selectable(label.c_str(), prim->selected))
                this->scene.HandleSelection(prim);

        }
    }
    ImGui::EndChild();

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
        blockMousePick = true;
    ImGui::End();

    // ── Time Control window ───────────────────────────────────────────────
    {
        ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);
        ImGui::Begin("Time Control");
        ImGui::Text("t = %.3f", this->scene.currentTime);
        if (this->scene.timePaused) {
            if (ImGui::Button("Play "))  this->scene.timePaused = false;
        } else {
            if (ImGui::Button("Pause")) this->scene.timePaused = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset")) this->scene.ResetTime();
        ImGui::DragFloat("Speed",    &this->scene.timeSpeed, 0.01f, 0.0f, 20.0f, "%.2f");
        ImGui::DragFloat("Max time", &this->scene.timeMax,   0.5f,  0.0f, 10000.f,
            this->scene.timeMax < 0.01f ? "unlimited" : "%.1f s");
        ImGui::Checkbox("Loop", &this->scene.timeLoop);

        ImGui::Separator();
        ImGui::Text("Trajectories");
        ImGui::Checkbox("Show##traj", &this->scene.showTrajectories);
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear##traj")) this->scene.ClearTrajectories();
        ImGui::SetNextItemWidth(140);
        ImGui::DragInt("Max len##traj", &this->scene.trajectoryMaxLen, 10, 0, 1000000,
            this->scene.trajectoryMaxLen <= 0 ? "unlimited" : "%d pts");
        if (this->scene.trajectoryMaxLen < 0) this->scene.trajectoryMaxLen = 0;

        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
            blockMousePick = true;
        ImGui::End();
    }

    // ── Lua updater editor window (shows when a primitive is selected) ─────
    Primitive* sel = FindSelected(this->scene.primitives);
    if (sel) {
        bool luaBlock = this->luaEditor.Draw(sel, this->scene.primitives);
        if (luaBlock) blockMousePick = true;
    }

    // ── Pick-vector handler ────────────────────────────────────────────────
    if (this->luaEditor.awaitingVectorPick)
        this->scene.pickModeActive = true;

    if (this->luaEditor.awaitingVectorPick && this->scene.pickedPrimId != 0) {
        Primitive* from = sel;
        UINT to_id = this->scene.pickedPrimId;

        // Find 1-based vector index (what Lua scene[] uses), NOT the primitive .id field
        int toIdx = -1;
        std::string tname;
        for (int k = 0; k < (int)this->scene.primitives.size(); k++) {
            if (this->scene.primitives[k]->id == to_id) {
                toIdx = k + 1;  // 1-based Lua index
                tname = this->scene.primitives[k]->name.empty()
                    ? ("p" + std::to_string(to_id))
                    : this->scene.primitives[k]->name;
                break;
            }
        }

        if (from && toIdx > 0) {
            std::string idx = std::to_string(toIdx);
            std::string snip =
                "local vec_to_" + tname +
                " = {x=scene[" + idx + "].x-p.x" +
                ", y=scene[" + idx + "].y-p.y" +
                ", z=scene[" + idx + "].z-p.z}\n";
            from->luaScript.insert(0, snip);
        }
        this->luaEditor.awaitingVectorPick = false;
        this->scene.pickedPrimId = 0;
    }

    // ── Lua Globals window ────────────────────────────────────────────────
    {
        float* phR      = nullptr;
        float  phMax    = 500.f;
        bool   phPaused = true;
        if (this->scene.sceneType == SceneType::PersistentHomology && this->scene.phState.radiusShared) {
            phR      = &this->scene.phState.currentRadius;
            phMax    = this->scene.phState.coverageR * 3.f + 60.f;
            phPaused = this->scene.timePaused;
        }
        if (this->luaEditor.DrawGlobals(phR, phMax, phPaused)) blockMousePick = true;
    }

    // ── PH scene windows ──────────────────────────────────────────────────
    if (this->scene.sceneType == SceneType::PersistentHomology) {

        // ── PH Generator window ───────────────────────────────────────────
        if (this->scene.phState.windowsNeedRestore) {
            ImGui::SetNextWindowPos(
                ImVec2(scene.phState.genWindowPos[0], scene.phState.genWindowPos[1]),
                ImGuiCond_Always);
            ImGui::SetNextWindowSize(
                ImVec2(scene.phState.genWindowSize[0], scene.phState.genWindowSize[1]),
                ImGuiCond_Always);
        } else {
            ImGui::SetNextWindowPos(
                ImVec2(scene.phState.genWindowPos[0], scene.phState.genWindowPos[1]),
                ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(
                ImVec2(scene.phState.genWindowSize[0], scene.phState.genWindowSize[1]),
                ImGuiCond_FirstUseEver);
        }

        if (ImGui::Begin("PH Generator", &scene.phState.genWindowOpen)) {
            scene.phState.genWindowPos[0]  = ImGui::GetWindowPos().x;
            scene.phState.genWindowPos[1]  = ImGui::GetWindowPos().y;
            scene.phState.genWindowSize[0] = ImGui::GetWindowSize().x;
            scene.phState.genWindowSize[1] = ImGui::GetWindowSize().y;

            ImGui::SetNextItemWidth(55);
            ImGui::DragInt("##phN", &scene.phState.pointCount, 1, 3, 300);
            ImGui::SameLine(); ImGui::TextUnformatted("pts");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(75);
            ImGui::DragFloat("##phB", &scene.phState.bounds, 2.f, 10.f, 5000.f, "b=%.0f");

            static const char* distribNames[] = { "Uniform", "Gaussian", "Sphere surf" };
            ImGui::SetNextItemWidth(110);
            ImGui::Combo("Distrib##ph", &scene.phState.distribType, distribNames, 3);
            if (scene.phState.distribType == 1) {
                ImGui::SameLine(); ImGui::SetNextItemWidth(65);
                ImGui::DragFloat("σ##phG", &scene.phState.gaussSigma, 1.f, 1.f, 3000.f, "%.0f");
            }
            if (scene.phState.distribType == 2) {
                ImGui::SameLine(); ImGui::SetNextItemWidth(65);
                ImGui::DragFloat("R##phS", &scene.phState.sphereR, 1.f, 10.f, 3000.f, "%.0f");
            }

            if (ImGui::Button("Generate Random##ph")) {
                scene.phState.GenerateRandom();
                scene.RegeneratePHCloud();
                scene.ResetTime();
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Import cloud (x,y,z per line):");
            static char importPath[512] = {};
            ImGui::SetNextItemWidth(160);
            ImGui::InputText("##phpath", importPath, sizeof(importPath));
            ImGui::SameLine();
            if (ImGui::Button("Import##ph")) {
                if (scene.ImportPHCloud(importPath)) {
                    scene.RegeneratePHCloud();
                    scene.ResetTime();
                }
            }

            ImGui::Separator();
            ImGui::SetNextItemWidth(100);
            ImGui::DragFloat("Radius speed##ph", &scene.phState.radiusSpeed,
                0.5f, 0.1f, 2000.f, "%.1f u/s");
            ImGui::Text("Coverage r: %.2f", scene.phState.coverageR);

            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
                blockMousePick = true;
        }
        ImGui::End();

        // ── PH Topology window ─────────────────────────────────────────────
        if (this->scene.phState.windowsNeedRestore) {
            ImGui::SetNextWindowPos(
                ImVec2(scene.phState.topoWindowPos[0], scene.phState.topoWindowPos[1]),
                ImGuiCond_Always);
            ImGui::SetNextWindowSize(
                ImVec2(scene.phState.topoWindowSize[0], scene.phState.topoWindowSize[1]),
                ImGuiCond_Always);
        } else {
            ImGui::SetNextWindowPos(
                ImVec2(scene.phState.topoWindowPos[0], scene.phState.topoWindowPos[1]),
                ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(
                ImVec2(scene.phState.topoWindowSize[0], scene.phState.topoWindowSize[1]),
                ImGuiCond_FirstUseEver);
        }

        if (ImGui::Begin("PH Topology", &scene.phState.topoWindowOpen)) {
            scene.phState.topoWindowPos[0]  = ImGui::GetWindowPos().x;
            scene.phState.topoWindowPos[1]  = ImGui::GetWindowPos().y;
            scene.phState.topoWindowSize[0] = ImGui::GetWindowSize().x;
            scene.phState.topoWindowSize[1] = ImGui::GetWindowSize().y;

            int N = (int)scene.phState.cloudPoints.size();
            ImGui::Text("Points: %d     r = %.2f", N, scene.phState.currentRadius);
            ImGui::Separator();
            ImGui::Text("Beta-0  (components): %d", scene.phState.beta0);
            ImGui::Text("Beta-1  (1-cycles):   %d", scene.phState.beta1);
            ImGui::Separator();
            ImGui::Text("Edges:     %d", scene.phState.numEdges);
            ImGui::Text("Triangles: %d", scene.phState.numTriangles);
            ImGui::Separator();

            float covR = scene.phState.coverageR;
            ImGui::Text("Coverage r:  %.2f", covR);

            if (covR > 0.001f) {
                float progress = scene.phState.currentRadius / covR;
                if (progress > 1.f) progress = 1.f;
                char buf[32];
                snprintf(buf, sizeof(buf), "%.1f%%", progress * 100.f);
                ImGui::ProgressBar(progress, ImVec2(-1.f, 0.f), buf);
            }

            ImGui::Separator();
            if (scene.phState.allConnected) {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f),
                    "Connected! (beta0=1)");
            } else if (!scene.timePaused) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Animating...");
            } else {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Paused");
            }

            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
                blockMousePick = true;
        }
        ImGui::End();

        // ── Sync radius to spheres when paused (e.g. slider drag) ─────────
        if (scene.timePaused) {
            float r = scene.phState.currentRadius;
            if (scene.phState.radiusShared) *scene.phState.radiusShared = r;
            // Sync currentTime so playback resumes smoothly from slider position
            if (scene.phState.radiusSpeed > 0.f)
                scene.currentTime = r / scene.phState.radiusSpeed;
            // Update sphere scales directly
            float scale = r < 0.001f ? 0.001f : r;
            for (UINT id : scene.phState.sphereIds)
                for (Primitive* p : scene.primitives)
                    if (p->id == id) { p->SetScale(scale); break; }
            scene.phState.ComputeTopology(r);
        }

        if (scene.phState.windowsNeedRestore)
            scene.phState.windowsNeedRestore = false;
    }

    // ── Navigation cube (rendered over the 3D viewport) ───────────────────
    bool navCubeHover = NavCube::Draw(
        ImVec2((float)this->windowWidth, (float)this->windowHeight),
        this->scene.camera
    );
    if (navCubeHover) blockMousePick = true;

    this->scene.blockMousePick = blockMousePick;

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}
