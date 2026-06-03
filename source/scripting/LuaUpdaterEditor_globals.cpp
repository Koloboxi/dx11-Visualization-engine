#include "LuaUpdaterEditor.h"

bool LuaUpdaterEditor::DrawGlobals(const std::vector<GlobalSlider>* extraSliders,
                                           std::vector<Primitive*>* allPrims,
                                           float currentTime,
                                           bool* global_changed,
                                           bool* outBlockWheel,
                                           ImGuiWindowFlags extraFlags) {
    ImGui::Begin("Lua Globals", nullptr, extraFlags);
    bool blockPick = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    if (extraSliders && !extraSliders->empty()) {
        for (const auto& slider : *extraSliders) {
            if (!slider.valuePtr) continue;
            ImGui::TextUnformatted(slider.label.c_str());
            ImGui::SameLine();
            ImGui::SetNextItemWidth(130);
            float dragSpeed = (slider.max - slider.min) * 0.005f;
            if (dragSpeed <= 0.f) dragSpeed = 0.1f;
            ImGui::PushID(slider.label.c_str());
            ImGui::DragFloat("##s", slider.valuePtr, dragSpeed, slider.min, slider.max, "%.2f");
            ImGui::PopID();
        }
        ImGui::Separator();
    }

    float childH = -ImGui::GetFrameHeightWithSpacing() - 4;
    if (ImGui::BeginChild("##glist", ImVec2(0, childH))) {
        if (outBlockWheel && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
            *outBlockWheel = true;
        bool anyChanged = false;
        for (int i = 0; i < (int)globals.size(); i++) {
            ImGui::PushID(i);
            ImGui::SetNextItemWidth(130);
            ImGui::InputText("##n", &globals[i].name);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            if (ImGui::DragFloat("##v", &globals[i].value, 0.1f, 0, 0, "%.4g"))
                anyChanged = true;
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) {
                globals.erase(globals.begin() + i);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        if (global_changed) *global_changed = anyChanged && allPrims;
    }
    ImGui::EndChild();

    if (ImGui::Button("+ Add")) globals.push_back({});
    ImGui::End();
    return blockPick;
}
