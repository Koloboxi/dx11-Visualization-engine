#pragma once
#include "GuiIcons.h"
#include "../scene/scene.h"
#include "../../scripting/LuaUpdaterEditor.h"
#include "../../external/sem_exports.h"
#include "../../utils/errorLogger.h"
#include "SEMWindow.h"
#include <unordered_set>
#include <unordered_map>
#include <functional>
#include <string>
#include <algorithm>
#include <commdlg.h>

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

// Returns the directory of the running executable.
inline std::string GetExeDir() {
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string s(buf);
    auto pos = s.find_last_of("\\/");
    return (pos != std::string::npos) ? s.substr(0, pos) : s;
}

// Returns the file name without directory or extension (the "stem").
inline std::string FileStem(const std::string& path) {
    auto slash = path.find_last_of("\\/");
    std::string file = (slash != std::string::npos) ? path.substr(slash + 1) : path;
    auto dot = file.find_last_of('.');
    return (dot != std::string::npos) ? file.substr(0, dot) : file;
}

// Opens Win32 file-open dialog rooted in <exeDir>\Data.
// filter / title are caller-supplied. Returns selected path or empty string.
inline std::string BrowseForMeshFile(const char* filter, const char* title) {
    std::string dataDir = GetExeDir() + "\\Data";

    char fileBuf[MAX_PATH] = {};
    OPENFILENAMEA ofn   = {};
    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = nullptr;
    ofn.lpstrFilter     = filter;
    ofn.lpstrFile       = fileBuf;
    ofn.nMaxFile        = MAX_PATH;
    ofn.lpstrInitialDir = dataDir.c_str();
    ofn.lpstrTitle      = title;
    ofn.Flags           = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn)) return std::string(fileBuf);
    return {};
}

// Scene-management controls (save/clear, demo & saved scene pickers). Rendered
// as the body of the "Scene" collapsible section in the merged Scene window —
// no window Begin/End of its own.
inline void DrawSceneManagementBody(Scene& scene, LuaUpdaterEditor& lua) {
    static int s_savedSelIdx = 0;
    static int s_demoSelIdx  = 0;

    if (ImGui::Button("Save"))
        scene.SaveScene(scene.sceneName.empty() ? "scene.json" : scene.sceneName + ".json");
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
        ClearAndLoad(scene, lua, [&]{ scene.ClearScene(); });

    static const char* demoNames[] = { "Newton", "PH Demo", "GR Demo", "Ideal Gas" };
    if (ImGui::BeginCombo("Demo scenes", demoNames[s_demoSelIdx])) {
        for (int i = 0; i < 4; ++i) {
            bool sel = (s_demoSelIdx == i);
            if (ImGui::Selectable(demoNames[i], sel)) {
                s_demoSelIdx = i;
                if (i == 0)      ClearAndLoad(scene, lua, [&]{ scene.LoadNewtonDemo();              lua.ReApplyAll(scene.primitives); });
                else if (i == 1) ClearAndLoad(scene, lua, [&]{ scene.LoadPersistentHomologyScene(); lua.ReApplyAll(scene.primitives); });
                else if (i == 2) ClearAndLoad(scene, lua, [&]{ scene.LoadGRScene();                 lua.ReApplyAll(scene.primitives); });
                else             ClearAndLoad(scene, lua, [&]{ scene.LoadIdealGasScene();           lua.ReApplyAll(scene.primitives); });
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
}

// The add/delete controls and the primitive tree. Rendered as the body of the
// "Primitives" collapsible section in the merged Scene window (no Begin/End).
inline void DrawTreeBody(Scene& scene, LuaUpdaterEditor& lua,
                         bool& blockMousePick, bool& blockMouseWheel) {
    static AddPrimState s_add;
    static std::unordered_set<const SceneNode*> s_expanded;
    static int          s_lastClickedIdx = -1;
    static SceneNode*   s_lastClickedNode = nullptr;
    static SceneNode*   s_renameNode     = nullptr;
    static bool         s_renameFocus    = false;
    static char         s_renameBuffer[256] = {};

    auto isRenamable = [](SceneNode* n) { return n && !n->IsController(); };
    auto beginRename = [&](SceneNode* n) {
        if (!isRenamable(n)) return;
        s_renameNode  = n;
        s_renameFocus = true;
        const std::string& cur = (n == &scene.root) ? scene.sceneName : n->name;
        strncpy_s(s_renameBuffer, cur.c_str(), sizeof(s_renameBuffer) - 1);
    };
    auto commitRename = [&]() {
        if (!s_renameNode) return;
        if (s_renameNode == &scene.root) {
            scene.sceneName = s_renameBuffer;
            scene.root.name = s_renameBuffer;
        } else {
            s_renameNode->name = s_renameBuffer;
        }
        s_renameNode = nullptr;
    };

    if (ImGui::Button("+")) ImGui::OpenPopup("AddPrimitive");
    ImGui::SameLine();
    if (ImGui::Button("-")) {
        for (Primitive* p : scene.primitives)
            if (p->selected) lua.OnPrimitiveRemoved(p);
        scene.DeleteSelected();
        s_lastClickedIdx = -1;
    }

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

    if (ImGui::BeginChild("##primlist", {0,0}, false)) {
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
            blockMouseWheel = true;

        float rowH = ImGui::GetFrameHeight();
        float avW  = ImGui::GetContentRegionAvail().x;
        auto* dl   = ImGui::GetWindowDrawList();
        ImVec2 mp  = ImGui::GetMousePos();

        struct FlatItem { SceneNode* node; int depth; };
        static std::vector<FlatItem> s_flat;
        s_flat.clear();

        // Auto-expand any node the first time it is seen with children, so a
        // freshly created primitive shows its children expanded by default.
        // Pure grouping nodes (the SEM "offsets" group) are left collapsed so a
        // freshly created offsets shell set does not flood the tree with rows —
        // the user can expand it manually. Once a node is known, the user's
        // manual collapse/expand sticks.
        static std::unordered_set<const SceneNode*> s_known;
        std::function<void(SceneNode*)> seedExpand = [&](SceneNode* n) {
            if (!s_known.count(n)) {
                s_known.insert(n);
                bool isGroup = n != &scene.root && !n->IsPrimitive() &&
                               !n->IsController() && !n->IsService();
                if (!n->children.empty() && !isGroup)
                    s_expanded.insert(n);
            }
            for (SceneNode* ch : n->children) seedExpand(ch);
        };
        seedExpand(&scene.root);

        std::function<void(SceneNode*, int)> buildFlat = [&](SceneNode* n, int d) {
            s_flat.push_back({n, d});
            bool exp = s_expanded.count(n) > 0;
            if (exp)
                for (SceneNode* ch : n->children) buildFlat(ch, d+1);
        };
        buildFlat(&scene.root, 0);

        for (int i = 0; i < (int)s_flat.size(); ++i) {
            SceneNode* node  = s_flat[i].node;
            int        depth = s_flat[i].depth;
            bool isRoot  = (node == &scene.root);
            bool isPrim  = node->IsPrimitive();
            bool isCtrl  = node->IsController();
            bool hasCh   = !node->children.empty();
            bool isExp   = s_expanded.count(node) > 0;

            Primitive* prim = isPrim ? static_cast<Primitive*>(node) : nullptr;

            ImVec2 rp = ImGui::GetCursorScreenPos();
            ImVec2 re = {rp.x + avW, rp.y + rowH};
            bool hovered = (mp.x >= rp.x && mp.x < re.x && mp.y >= rp.y && mp.y < re.y);

            if (prim && prim->selected)
                dl->AddRectFilled(rp, re, IM_COL32(70,110,180,180));
            else if (isCtrl)
                dl->AddRectFilled(rp, re, scene.controllerSelected
                    ? IM_COL32(70,130,70,190) : IM_COL32(60,75,60,120));
            else if (isRoot)
                dl->AddRectFilled(rp, re, IM_COL32(45,50,70,140));
            else if (hovered)
                dl->AddRectFilled(rp, re, IM_COL32(58,63,80,150));

            float triX  = rp.x + 4.f + depth * 14.f;
            float triMid = rp.y + rowH * .5f;
            if (hasCh) {
                if (isExp)
                    dl->AddTriangleFilled({triX,triMid-4},{triX+8,triMid-4},{triX+4,triMid+3}, IM_COL32(200,210,230,200));
                else
                    dl->AddTriangleFilled({triX,triMid-5},{triX,triMid+5},{triX+6,triMid}, IM_COL32(200,210,230,200));
            }

            float textX = triX + 14.f;

            if (node == s_renameNode) {
                ImGui::SetCursorScreenPos({textX, rp.y + 1.f});
                ImGui::SetNextItemWidth(avW - textX + rp.x - 4.f);
                if (s_renameFocus) { ImGui::SetKeyboardFocusHere(); s_renameFocus = false; }
                bool entered = ImGui::InputText(("##rename" + std::to_string((uintptr_t)node)).c_str(),
                                                s_renameBuffer, sizeof(s_renameBuffer),
                                                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                if (entered || ImGui::IsItemDeactivated()) commitRename();
                ImGui::SetCursorScreenPos({rp.x, rp.y + rowH});
                continue;
            }

            std::string lbl;
            ImU32 textCol;

            // The light theme needs dark (near-black) tree text; the row highlight
            // bars are translucent, so black stays legible over them too.
            const bool light = scene.lightTheme;
            if (isRoot) {
                lbl      = node->name.empty() ? "(unnamed scene)" : node->name;
                textCol  = light ? IM_COL32(15,20,35,255) : IM_COL32(200,220,255,230);
            } else if (isCtrl) {
                lbl     = "[controller]";
                textCol = light ? IM_COL32(20,90,20,255) : IM_COL32(140,210,140,200);
            } else if (isPrim) {
                std::string dimS = prim->GetDimension()==0?"p":(prim->GetDimension()==1?"l":"t");
                lbl = prim->name.empty() ? (std::to_string(prim->id) + dimS) : prim->name;
                if (prim->HasUpdater()) lbl += " *";
                textCol = light ? IM_COL32(0,0,0,255)
                                : (prim->selected ? IM_COL32(255,255,255,230) : IM_COL32(200,210,228,200));
            } else {
                lbl     = node->name.empty() ? "(node)" : node->name;
                textCol = light ? IM_COL32(40,45,55,255) : IM_COL32(180,190,210,180);
            }

            dl->AddText({textX, rp.y + (rowH - ImGui::GetTextLineHeight()) * .5f}, textCol, lbl.c_str());

            if (prim && prim->staging)
                dl->AddRect(rp, re, IM_COL32(90,205,165,220), 3.f, 0, 1.5f);

            // Visibility "eye" toggle, drawn at the right edge. Shown for
            // primitives and for grouping nodes (e.g. the SEM "offsets" group).
            bool showEye = (isPrim || hasCh) && !isRoot && !isCtrl;
            float eyeR  = rowH * 0.24f;
            float eyeCx = rp.x + avW - eyeR - 8.f;
            float eyeCy = rp.y + rowH * .5f;
            float eyeHalf = eyeR + 4.f;
            if (showEye) {
                ImU32 eyeCol = node->visible
                    ? (hovered ? IM_COL32(225,235,255,235) : IM_COL32(180,195,225,180))
                    : IM_COL32(120,128,150,170);
                GuiIcons::EyeIcon(dl, {eyeCx, eyeCy}, eyeR, node->visible, eyeCol);
            }

            ImGui::SetCursorScreenPos(rp);
            ImGui::InvisibleButton(("##r" + std::to_string((uintptr_t)node)).c_str(), {avW, rowH});

            if (ImGui::IsItemClicked()) {
                bool ctrl  = ImGui::GetIO().KeyCtrl;
                bool shift = ImGui::GetIO().KeyShift;
                bool clickedTri = hasCh && mp.x >= triX && mp.x <= triX + 12.f;
                bool clickedEye = showEye && mp.x >= eyeCx - eyeHalf && mp.x <= eyeCx + eyeHalf &&
                                  mp.y >= eyeCy - eyeHalf && mp.y <= eyeCy + eyeHalf;

                if (clickedEye) {
                    // One-shot cascade: parent's new state is pushed to all
                    // descendants; afterwards each child toggles independently.
                    scene.SetNodeVisibleCascade(node, !node->visible);
                } else if (clickedTri) {
                    if (isExp) s_expanded.erase(node);
                    else       s_expanded.insert(node);
                } else if (isCtrl) {
                    for (Primitive* p2 : scene.primitives) p2->selected = false;
                    scene.orientationTransformer.SetTargetObjects({});
                    scene.controllerSelected = true;
                    s_lastClickedNode = node;
                } else if (isRoot) {
                    scene.controllerSelected = false;
                    s_lastClickedNode = &scene.root;
                } else if (isPrim) {
                    scene.controllerSelected = false;
                    s_lastClickedNode = prim;
                    // Selecting a parent selects its whole primitive subtree.
                    auto selectSubtree = [](SceneNode* n, bool v) {
                        std::function<void(SceneNode*)> rec = [&](SceneNode* m) {
                            if (m->IsPrimitive()) static_cast<Primitive*>(m)->selected = v;
                            for (SceneNode* ch : m->children) rec(ch);
                        };
                        rec(n);
                    };
                    if (shift && s_lastClickedIdx >= 0) {
                        int lo = std::min(i, s_lastClickedIdx);
                        int hi = std::max(i, s_lastClickedIdx);
                        for (Primitive* p2 : scene.primitives) p2->selected = false;
                        for (int j = lo; j <= hi; ++j)
                            if (s_flat[j].node->IsPrimitive())
                                static_cast<Primitive*>(s_flat[j].node)->selected = true;
                    } else if (ctrl) {
                        selectSubtree(prim, !prim->selected);
                        s_lastClickedIdx = i;
                    } else {
                        for (Primitive* p2 : scene.primitives) p2->selected = false;
                        selectSubtree(prim, true);
                        s_lastClickedIdx = i;
                    }
                    std::vector<Primitive*> sel;
                    for (Primitive* p2 : scene.primitives) if (p2->selected) sel.push_back(p2);
                    scene.orientationTransformer.SetTargetObjects(sel);
                }
            }

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                // Staging only hijacks double-click while staging mode is on
                // (enabled on CSV3D import, disabled from the SEM window).
                // Otherwise double-click keeps its normal rename behavior.
                if (isPrim && scene.stagingEnabled) {
                    if (prim->staging) scene.ClearStaged();
                    else               scene.SetStaged(prim);
                    for (Primitive* p2 : scene.primitives) p2->selected = false;
                    scene.orientationTransformer.SetTargetObjects({});
                    scene.controllerSelected = false;
                } else if (isRenamable(node)) {
                    beginRename(node);
                }
            }

            ImGui::SetCursorScreenPos({rp.x, rp.y + rowH});
        }

        bool lastClickedAlive = false, renameAlive = false;
        for (const FlatItem& fi : s_flat) {
            if (fi.node == s_lastClickedNode) lastClickedAlive = true;
            if (fi.node == s_renameNode)      renameAlive      = true;
        }
        if (!lastClickedAlive) s_lastClickedNode = nullptr;
        if (!renameAlive)      s_renameNode      = nullptr;
        if (s_renameNode == nullptr && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
            ImGui::IsKeyPressed(ImGuiKey_F2) && isRenamable(s_lastClickedNode))
            beginRename(s_lastClickedNode);

        if (s_flat.size() > 0 && !s_expanded.count(&scene.root))
            s_expanded.insert(&scene.root);
    }
    ImGui::EndChild();

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows))
        blockMousePick = true;
}

}
