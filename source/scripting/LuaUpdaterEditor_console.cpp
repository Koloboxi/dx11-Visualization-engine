#include "LuaUpdaterEditor.h"

void LuaUpdaterEditor::ExecuteConsoleCmd(const std::string& raw, Scene& scene)
{
    auto log = [&](const std::string& s, bool err = false) {
        consoleLog.push_back({ s, err });
    };

    std::istringstream ss(raw);
    std::string cmd;
    if (!(ss >> cmd)) return;

    auto parseColor = [&](std::vector<std::string>& toks, int offset, XMFLOAT4& col) {
        if ((int)toks.size() >= offset + 4) {
            col.x = std::stof(toks[offset]);
            col.y = std::stof(toks[offset + 1]);
            col.z = std::stof(toks[offset + 2]);
            col.w = std::stof(toks[offset + 3]);
        }
    };

    std::vector<std::string> args;
    { std::string t; while (ss >> t) args.push_back(t); }

    try {
        if (cmd == "help") {
            std::string sub = args.empty() ? "" : args[0];
            if (sub.empty()) {
                log("Commands: help, add, del");
                log("  help add                  - list add subcommands");
                log("  add <type> [args]         - add a primitive");
                log("  del id <id>               - delete by id");
                log("  del name <name>           - delete by name");
            } else if (sub == "add") {
                log("add point <x> <y> <z> [r g b a]");
                log("add sphere <radius> <x> <y> <z> <subdivs> [r g b a]");
                log("add arc3d <arcR> <tubeR> <deg> <cx> <cy> <cz> <divs> [r g b a]");
                log("add arrow3d <shaftR> <headR> <headLen> <fx> <fy> <fz> <tx> <ty> <tz> <sides> [r g b a]");
                log("Colors default to 0.6 0.6 0.9 1.0 if not provided.");
            } else {
                log("Unknown help topic. Try: help, help add", true);
            }
        }
        else if (cmd == "add") {
            if (args.empty()) { log("add: missing type. Try 'help add'.", true); return; }
            XMFLOAT4 col{ 0.6f, 0.6f, 0.9f, 1.0f };
            std::string type = args[0];
            if (type == "point") {
                if (args.size() < 4) { log("add point: usage: add point <x> <y> <z> [r g b a]", true); return; }
                float x = std::stof(args[1]), y = std::stof(args[2]), z = std::stof(args[3]);
                parseColor(args, 4, col);
                scene.AddPoint({ x,y,z }, col);
                log("Added point at " + args[1] + " " + args[2] + " " + args[3]);
            }
            else if (type == "sphere") {
                if (args.size() < 6) { log("add sphere: usage: add sphere <radius> <x> <y> <z> <subdivs> [r g b a]", true); return; }
                float r = std::stof(args[1]);
                float x = std::stof(args[2]), y = std::stof(args[3]), z = std::stof(args[4]);
                int subdivs = std::stoi(args[5]);
                parseColor(args, 6, col);
                scene.AddSphere(r, { x,y,z }, (UINT)std::max(0, subdivs), col);
                log("Added sphere r=" + args[1]);
            }
            else if (type == "arc3d") {
                if (args.size() < 8) { log("add arc3d: usage: add arc3d <arcR> <tubeR> <deg> <cx> <cy> <cz> <divs> [r g b a]", true); return; }
                float aR = std::stof(args[1]), tR = std::stof(args[2]), deg = std::stof(args[3]);
                float cx = std::stof(args[4]), cy = std::stof(args[5]), cz = std::stof(args[6]);
                int divs = std::stoi(args[7]);
                parseColor(args, 8, col);
                scene.AddArc3d(aR, tR, deg, { cx,cy,cz }, (UINT)std::max(3, divs), col);
                log("Added arc3d");
            }
            else if (type == "arrow3d") {
                if (args.size() < 11) { log("add arrow3d: usage: add arrow3d <shaftR> <headR> <headLen> <fx> <fy> <fz> <tx> <ty> <tz> <sides> [r g b a]", true); return; }
                float shR = std::stof(args[1]), heR = std::stof(args[2]), heL = std::stof(args[3]);
                float fx = std::stof(args[4]), fy = std::stof(args[5]), fz = std::stof(args[6]);
                float tx = std::stof(args[7]), ty = std::stof(args[8]), tz = std::stof(args[9]);
                int sides = std::stoi(args[10]);
                parseColor(args, 11, col);
                scene.AddArrow3d(shR, heR, heL, { fx,fy,fz }, { tx,ty,tz }, (UINT)std::max(3, sides), col);
                log("Added arrow3d");
            }
            else {
                log("Unknown primitive type '" + type + "'. Try 'help add'.", true);
            }
        }
        else if (cmd == "del") {
            if (args.empty()) { log("del: missing mode. Use 'del id <id>' or 'del name <name>'.", true); return; }
            std::string mode = args[0];
            if (mode == "id") {
                if (args.size() < 2) { log("del id: missing id", true); return; }
                UINT id = (UINT)std::stoul(args[1]);
                Primitive* found = nullptr;
                for (Primitive* p : scene.primitives) if (p->id == id) { found = p; break; }
                if (!found) { log("del id: id " + args[1] + " not found", true); return; }
                if (found == activeEditorPrim) activeEditorPrim = nullptr;
                found->ClearUpdater();
                scene.RemovePrimitive(found);
                log("Deleted id " + args[1]);
            }
            else if (mode == "name") {
                if (args.size() < 2) { log("del name: missing name", true); return; }
                std::string name = args[1];
                std::vector<Primitive*> toDelete;
                for (Primitive* p : scene.primitives) if (p->name == name) toDelete.push_back(p);
                if (toDelete.empty()) { log("del name: '" + name + "' not found", true); return; }
                for (Primitive* p : toDelete) {
                    if (p == activeEditorPrim) activeEditorPrim = nullptr;
                    p->ClearUpdater();
                    scene.RemovePrimitive(p);
                }
                log("Deleted " + std::to_string(toDelete.size()) + " primitive(s) named '" + name + "'");
            }
            else {
                log("del: unknown mode '" + mode + "'. Use id or name.", true);
            }
        }
        else {
            log("Unknown command: '" + cmd + "'. Type 'help' for commands.", true);
        }
    }
    catch (const std::exception& e) {
        log(std::string("Parse error: ") + e.what(), true);
    }
}

bool LuaUpdaterEditor::DrawConsole(const std::vector<Primitive*>& selected,
                                           const std::vector<Primitive*>& allPrimitives,
                                           Scene& scene,
                                           bool* outBlockWheel,
                                           bool* inOutOpen,
                                           ImGuiWindowFlags extraFlags)
{
    bool localOpen = true;
    bool* openPtr = inOutOpen ? inOutOpen : &localOpen;
    ImGui::Begin("Console", openPtr, extraFlags);
    bool blockPick = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    if (scene.controllerSelected && scene.controller) {
        DrawControllerScripts(scene, outBlockWheel);
    }
    else if (selected.empty()) {
        float logH = -ImGui::GetFrameHeightWithSpacing() * 2 - 8;
        if (ImGui::BeginChild("##conlog", ImVec2(0, logH), true)) {
            if (outBlockWheel && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
                *outBlockWheel = true;
            for (const auto& entry : consoleLog) {
                if (entry.isError)
                    ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "%s", entry.text.c_str());
                else
                    ImGui::TextUnformatted(entry.text.c_str());
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        bool run = false;
        ImGui::SetNextItemWidth(-70);
        if (ImGui::InputText("##cin", consoleInputBuf, sizeof(consoleInputBuf),
            ImGuiInputTextFlags_EnterReturnsTrue))
            run = true;
        ImGui::SameLine();
        if (ImGui::Button("Run", ImVec2(60, 0))) run = true;

        if (run && consoleInputBuf[0] != '\0') {
            consoleLog.push_back({ std::string("> ") + consoleInputBuf, false });
            ExecuteConsoleCmd(std::string(consoleInputBuf), scene);
            consoleInputBuf[0] = '\0';
        }
    }
    else {
        if (ImGui::BeginTabBar("##primtabs")) {
            for (Primitive* prim : selected) {
                std::string tabLabel = prim->name.empty()
                    ? ("id " + std::to_string(prim->id))
                    : prim->name;

                if (ImGui::BeginTabItem(tabLabel.c_str())) {
                    activeEditorPrim = prim;

#ifndef ENABLE_LUA
                    ImGui::TextColored(ImVec4(1.f, 0.8f, 0.2f, 1.f), "Lua disabled.");
                    ImGui::InputTextMultiline("##script", &prim->luaScript,
                        ImVec2(-1, -ImGui::GetFrameHeightWithSpacing() - 4));
                    ImGui::BeginDisabled(); ImGui::Button("Apply"); ImGui::EndDisabled();
#else
                    if (prim->HasUpdater())
                        ImGui::TextColored(ImVec4(0.3f, 1.f, 0.4f, 1.f), "[active]");
                    else
                        ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.f), "[no updater]");

                    ImGui::SameLine();
                    if (ImGui::SmallButton("Clear##lua")) {
                        prim->ClearUpdater();
                        prim->luaScript.clear();
                        errorMsg.clear();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Pick vec")) {
                        awaitingVectorPick = true;
                        activeEditorPrim = prim;
                    }
                    if (awaitingVectorPick && activeEditorPrim == prim) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(1.f, 0.8f, 0.1f, 1.f), "click a primitive...");
                    }
                    ImGui::Separator();
                    ImGui::TextDisabled("p.{x,y,z,scale,rx,ry,rz,rw,r,g,b,a}  vx,vy,vz(ro)  scene[i]  t  dt");

                    float scriptH = -ImGui::GetFrameHeightWithSpacing() - 4;
                    ImGui::InputTextMultiline("##script", &prim->luaScript,
                        ImVec2(-1, scriptH),
                        ImGuiInputTextFlags_AllowTabInput);
                    if (outBlockWheel && ImGui::IsItemHovered())
                        *outBlockWheel = true;

                    if (ImGui::Button("Apply")) {
                        errorMsg.clear();
                        if (!CompileAndApply(prim, allPrimitives, prim->luaScript, errorMsg))
                            errorMsg = "Error: " + errorMsg;
                    }
                    if (!errorMsg.empty()) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(1.f, 0.3f, 0.3f, 1.f), "%s", errorMsg.c_str());
                    }
#endif
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
    }

    ImGui::End();
    return blockPick;
}
