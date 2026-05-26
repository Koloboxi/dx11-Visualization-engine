#pragma once
#include "LayoutState.h"
#include "../scene/scene.h"
#include "../../scripting/LuaUpdaterEditor.h"
#include <unordered_set>
#include <unordered_map>
#include <functional>
#include <string>
#include <algorithm>

namespace PrimitivesWindow {

struct AddPrimState {
    int type = 0;
    float color[4] = { 0.6f, 0.6f, 0.9f, 1.0f };
    float pts[16][3] = {};
    int   ptCount = 1;
    float sRadius = 100.0f;
    int   sSubdiv = 2;
    float sPos[3] = {};
    float l3Radius   = 5.0f;
    int   l3Subdiv   = 8;
    float arcRadius  = 75.0f;
    float arcAngle   = 90.0f;
    float arcCenter[3] = {};
    char  stlPath[512] = {};
    float arrShaftR  = 3.0f;
    float arrHeadR   = 8.0f;
    float arrHeadLen = 20.0f;
    float arrFrom[3] = {};
    float arrTo[3]   = { 0, 100, 0 };
    int   arrSides   = 8;
};

inline void AddPointsEditor(int& count, float pts[][3], int maxPts, const char* btnLabel = "+ Point") {
    for (int i = 0; i < count; ++i) {
        std::string lbl = "##pt" + std::to_string(i);
        ImGui::PushItemWidth(180);
        ImGui::InputFloat3(lbl.c_str(), pts[i]);
        ImGui::PopItemWidth();
        if (count > 1) {
            ImGui::SameLine();
            if (ImGui::SmallButton(("x##" + std::to_string(i)).c_str())) {
                for (int j = i; j < count - 1; ++j)
                    memcpy(pts[j], pts[j+1], sizeof(float)*3);
                --count; --i;
            }
        }
    }
    if (count < maxPts && ImGui::SmallButton(btnLabel))
        ++count;
}

inline void ClearAndLoad(Scene& scene, LuaUpdaterEditor& lua, std::function<void()> loader) {
    for (Primitive* p : scene.primitives) lua.OnPrimitiveRemoved(p);
    loader();
    lua.sceneSliders = &scene.sceneSliders;
}

inline void Draw(const LayoutState& lay, float windowH, Scene& scene,
                 LuaUpdaterEditor& lua, bool& blockMousePick, bool& blockMouseWheel,
                 ImGuiWindowFlags extraFlags) {
    static AddPrimState s_add;
    static std::string  s_sceneName;
    static int          s_savedSelIdx = 0;
    static int          s_demoSelIdx  = 0;
    static std::unordered_set<UINT> s_expanded;
    static int          s_lastClickedIdx = -1;

    float panelH = windowH - LayoutState::STRIP_H - lay.consoleH;
    float timeH  = lay.timeH > 0.f ? lay.timeH : panelH * .33f;
    float primH  = panelH - timeH;

    ImGui::SetNextWindowPos({0.f, LayoutState::STRIP_H + timeH}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({lay.colW, primH}, ImGuiCond_Always);
    ImGui::Begin("Primitives", nullptr, extraFlags);

    // ---- Scenes section ----
    if (ImGui::Button("Save"))
        scene.SaveScene(s_sceneName + ".json");
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
        ClearAndLoad(scene, lua, [&]{ scene.ClearScene(); });

    static const char* demoNames[] = { "Newton", "PH Demo", "GR Demo" };
    if (ImGui::BeginCombo("Demo scenes", demoNames[s_demoSelIdx])) {
        for (int i = 0; i < 3; ++i) {
            bool sel = (s_demoSelIdx == i);
            if (ImGui::Selectable(demoNames[i], sel)) {
                s_demoSelIdx = i;
                if (i == 0)      ClearAndLoad(scene, lua, [&]{ scene.LoadNewtonDemo(); });
                else if (i == 1) ClearAndLoad(scene, lua, [&]{ scene.LoadPersistentHomologyScene(); });
                else             ClearAndLoad(scene, lua, [&]{ scene.LoadGRScene(); });
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    const auto& saved = scene.GetSavedScenes();
    if (!saved.empty()) {
        if (s_savedSelIdx >= (int)saved.size()) s_savedSelIdx = 0;
        if (ImGui::BeginCombo("Saved scenes", saved[s_savedSelIdx].c_str())) {
            for (int i = 0; i < (int)saved.size(); ++i) {
                bool isSel = (s_savedSelIdx == i);
                if (ImGui::Selectable(saved[i].c_str(), isSel)) {
                    s_savedSelIdx = i;
                    ClearAndLoad(scene, lua, [&]{ scene.LoadScene(saved[i]); lua.ReApplyAll(scene.primitives); });
                }
                if (isSel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::TextDisabled("No saved scenes");
    }

    ImGui::SetNextItemWidth(lay.colW - 90.f);
    ImGui::InputText("Name##sn", &s_sceneName);

    ImGui::Separator();

    // ---- + / - buttons ----
    if (ImGui::Button("+")) ImGui::OpenPopup("AddPrimitive");
    ImGui::SameLine();
    if (ImGui::Button("-")) {
        for (Primitive* p : scene.primitives)
            if (p->selected) lua.OnPrimitiveRemoved(p);
        scene.DeleteSelected();
        s_lastClickedIdx = -1;
    }

    // ---- AddPrimitive modal ----
    ImGui::SetNextWindowSize({380, 0}, ImGuiCond_Always);
    if (ImGui::BeginPopupModal("AddPrimitive", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static const char* types[] = { "Point","Line","Polygon","Sphere","Line3d","Arc3d","STL","Arrow3d" };
        ImGui::Combo("Type", &s_add.type, types, IM_ARRAYSIZE(types));
        ImGui::Separator();
        switch (s_add.type) {
        case 0: ImGui::InputFloat3("Position", s_add.pts[0]); break;
        case 1: AddPointsEditor(s_add.ptCount, s_add.pts, 16); break;
        case 2: AddPointsEditor(s_add.ptCount, s_add.pts, 16); break;
        case 3:
            ImGui::DragFloat("Radius",       &s_add.sRadius, 1.f, 0.1f, 5000.f);
            ImGui::InputFloat3("Centre",     s_add.sPos);
            ImGui::SliderInt("Subdivisions", &s_add.sSubdiv, 0, 5);
            break;
        case 4:
            ImGui::DragFloat("Tube radius",  &s_add.l3Radius, .1f, .1f, 500.f);
            ImGui::SliderInt("Sides",        &s_add.l3Subdiv, 3, 24);
            AddPointsEditor(s_add.ptCount, s_add.pts, 16);
            break;
        case 5:
            ImGui::DragFloat("Arc radius",   &s_add.arcRadius, 1.f, .1f, 5000.f);
            ImGui::DragFloat("Tube radius",  &s_add.l3Radius,  .1f, .1f, 500.f);
            ImGui::DragFloat("Angle (deg)",  &s_add.arcAngle,  1.f, 1.f, 360.f);
            ImGui::InputFloat3("Centre",     s_add.arcCenter);
            ImGui::SliderInt("Subdivisions", &s_add.l3Subdiv, 3, 64);
            break;
        case 6: ImGui::InputText("File path", s_add.stlPath, sizeof(s_add.stlPath)); break;
        case 7:
            ImGui::DragFloat("Shaft radius",  &s_add.arrShaftR,  .1f, .1f, 500.f);
            ImGui::DragFloat("Head radius",   &s_add.arrHeadR,   .1f, .1f, 500.f);
            ImGui::DragFloat("Head length",   &s_add.arrHeadLen, .5f, .5f, 1000.f);
            ImGui::SliderInt("Sides",         &s_add.arrSides, 3, 24);
            ImGui::InputFloat3("From",        s_add.arrFrom);
            ImGui::InputFloat3("To",          s_add.arrTo);
            break;
        }
        ImGui::Separator();
        ImGui::ColorEdit4("Color", s_add.color, ImGuiColorEditFlags_AlphaBar);
        ImGui::Separator();
        XMFLOAT4 col(s_add.color[0], s_add.color[1], s_add.color[2], s_add.color[3]);
        if (ImGui::Button("Add", {80,0})) {
            switch (s_add.type) {
            case 0: scene.AddPoint({s_add.pts[0][0],s_add.pts[0][1],s_add.pts[0][2]}, col); break;
            case 1: { std::vector<XMFLOAT3> ps(s_add.ptCount); for (int i=0;i<s_add.ptCount;++i) ps[i]={s_add.pts[i][0],s_add.pts[i][1],s_add.pts[i][2]}; scene.AddLine(ps,col); break; }
            case 2: { std::vector<XMFLOAT3> ps(s_add.ptCount); for (int i=0;i<s_add.ptCount;++i) ps[i]={s_add.pts[i][0],s_add.pts[i][1],s_add.pts[i][2]}; scene.AddPolygon(ps,col); break; }
            case 3: scene.AddSphere(s_add.sRadius,{s_add.sPos[0],s_add.sPos[1],s_add.sPos[2]},s_add.sSubdiv,col); break;
            case 4: { std::vector<XMFLOAT3> ps(s_add.ptCount); for (int i=0;i<s_add.ptCount;++i) ps[i]={s_add.pts[i][0],s_add.pts[i][1],s_add.pts[i][2]}; scene.AddLine3d(s_add.l3Radius,ps,s_add.l3Subdiv,col); break; }
            case 5: scene.AddArc3d(s_add.arcRadius,s_add.l3Radius,s_add.arcAngle,{s_add.arcCenter[0],s_add.arcCenter[1],s_add.arcCenter[2]},s_add.l3Subdiv,col); break;
            case 6: scene.AddFromSTL(s_add.stlPath,col); break;
            case 7: scene.AddArrow3d(s_add.arrShaftR,s_add.arrHeadR,s_add.arrHeadLen,{s_add.arrFrom[0],s_add.arrFrom[1],s_add.arrFrom[2]},{s_add.arrTo[0],s_add.arrTo[1],s_add.arrTo[2]},s_add.arrSides,col); break;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {80,0})) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ---- Primitives list (tree, custom rendering) ----
    ImGui::Separator();

    if (ImGui::BeginChild("##primlist", {0,0}, false)) {
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
            blockMouseWheel = true;

        // Build scene-index map
        std::unordered_map<UINT, int> sceneIdx;
        sceneIdx.reserve(scene.primitives.size());
        for (int k = 0; k < (int)scene.primitives.size(); ++k)
            sceneIdx[scene.primitives[k]->id] = k;

        // Build flat ordered list (depth-first, respecting expand state)
        struct FlatItem { Primitive* prim; int depth; };
        static std::vector<FlatItem> s_flat;
        s_flat.clear();
        std::function<void(Primitive*, int)> build = [&](Primitive* p, int d) {
            s_flat.push_back({p, d});
            if (!p->children.empty() && s_expanded.count(p->id))
                for (Primitive* ch : p->children) build(ch, d+1);
        };
        for (Primitive* p : scene.primitives)
            if (!p->parent) build(p, 0);

        float rowH = ImGui::GetFrameHeight();
        float avW  = ImGui::GetContentRegionAvail().x;
        auto* dl   = ImGui::GetWindowDrawList();
        ImVec2 mp  = ImGui::GetMousePos();

        for (int i = 0; i < (int)s_flat.size(); ++i) {
            Primitive* prim  = s_flat[i].prim;
            int        depth = s_flat[i].depth;
            bool hasCh = !prim->children.empty();
            bool isExp = s_expanded.count(prim->id) > 0;

            ImVec2 rp = ImGui::GetCursorScreenPos();
            ImVec2 re = {rp.x + avW, rp.y + rowH};

            bool hovered = (mp.x >= rp.x && mp.x < re.x && mp.y >= rp.y && mp.y < re.y);
            if (prim->selected)
                dl->AddRectFilled(rp, re, IM_COL32(70,110,180,180));
            else if (hovered)
                dl->AddRectFilled(rp, re, IM_COL32(58,63,80,150));

            float triX = rp.x + 4.f + depth * 14.f;
            float triMid = rp.y + rowH * .5f;

            if (hasCh) {
                if (isExp)
                    dl->AddTriangleFilled({triX,triMid-4},{triX+8,triMid-4},{triX+4,triMid+3}, IM_COL32(200,210,230,200));
                else
                    dl->AddTriangleFilled({triX,triMid-5},{triX,triMid+5},{triX+6,triMid}, IM_COL32(200,210,230,200));
            }

            float textX = triX + 14.f;
            const char* dimS = prim->GetDimension()==0 ? "p" : (prim->GetDimension()==1 ? "l" : "t");
            std::string lbl = prim->name.empty()
                ? (std::to_string(sceneIdx[prim->id]) + dimS)
                : prim->name;
            if (prim->HasUpdater()) lbl += " *";

            ImU32 textCol = prim->selected ? IM_COL32(255,255,255,230) : IM_COL32(200,210,228,200);
            dl->AddText({textX, rp.y + (rowH - ImGui::GetTextLineHeight()) * .5f}, textCol, lbl.c_str());

            // Hit area
            ImGui::SetCursorScreenPos(rp);
            ImGui::InvisibleButton(("##r" + std::to_string(prim->id)).c_str(), {avW, rowH});

            if (ImGui::IsItemClicked()) {
                bool ctrl  = ImGui::GetIO().KeyCtrl;
                bool shift = ImGui::GetIO().KeyShift;
                bool clickedTri = hasCh && mp.x >= triX && mp.x <= triX + 12.f;

                if (clickedTri) {
                    if (isExp) s_expanded.erase(prim->id);
                    else       s_expanded.insert(prim->id);
                } else {
                    if (shift && s_lastClickedIdx >= 0) {
                        int lo = std::min(i, s_lastClickedIdx);
                        int hi = std::max(i, s_lastClickedIdx);
                        for (Primitive* p2 : scene.primitives) p2->selected = false;
                        for (int j = lo; j <= hi; ++j) s_flat[j].prim->selected = true;
                    } else if (ctrl) {
                        prim->selected = !prim->selected;
                        s_lastClickedIdx = i;
                    } else {
                        for (Primitive* p2 : scene.primitives) p2->selected = false;
                        prim->selected = true;
                        s_lastClickedIdx = i;
                    }
                    std::vector<Primitive*> sel;
                    for (Primitive* p2 : scene.primitives) if (p2->selected) sel.push_back(p2);
                    scene.orientationTransformer.SetTargetObjects(sel);
                }
            }

            ImGui::SetCursorScreenPos({rp.x, rp.y + rowH});
        }
    }
    ImGui::EndChild();

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
        blockMousePick = true;
    ImGui::End();
}

} // namespace PrimitivesWindow
