#include "graphics.h"

void Graphics::Gui()
{
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	static int fpsCounter = 0;
	static std::string number;
	fpsCounter += 1;
	if (fpsTimer.GetMillisecondsElapsed() >= 1000) {
		fpsTimer.Restart();
		number = std::format("{}", fpsCounter);
		fpsCounter = 0;
	}
	ImGui::GetBackgroundDrawList()->AddText(ImVec2(0, 0), ImColor(0, 255, 0), number.c_str());

	ImGui::Begin("Parameters");
	if (ImGui::BeginTabBar("Panels"))
	{
		if (ImGui::BeginTabItem("Scenes"))
		{
			static std::string name{};
			if (ImGui::Button("Save")) {
				this->scene.SaveScene(name + ".json");
			}
			ImGui::SameLine();
			if (ImGui::Button("Clear")) {
				this->scene.ClearScene();
			}

			ImGui::InputText("Scene", &name);

			const std::vector<std::string>& savedScenes = this->scene.GetSavedScenes();
			if (!savedScenes.empty())
			{
				static int selectedSceneIndex = 0;

				if (ImGui::BeginCombo("Saved Scenes", savedScenes[selectedSceneIndex].c_str()))
				{
					for (int i = 0; i < savedScenes.size(); i++)
					{
						const bool isSelected = (selectedSceneIndex == i);

						if (ImGui::Selectable(savedScenes[i].c_str(), isSelected))
						{
							selectedSceneIndex = i;

							this->scene.LoadScene(savedScenes[i]);
						}
						if (isSelected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
			}
			else
			{
				ImGui::Text("No saved scenes available");
			}

			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("View"))
		{
			if (ImGui::Button("Reset Pos")) {
				this->scene.camera.SetPosition(BaseVectors::ORIGIN);
			}
			if (ImGui::Button("Reset Scale")) {
				this->scene.camera.SetScale(1);
			}

			if (ImGui::Button("Iso")) {
				this->scene.camera.SetRotation(Projections::ISO);
			}
			if (ImGui::Button("Dim")) {
				this->scene.camera.SetRotation(Projections::DIM);
			}
			if (ImGui::Button("XY")) {
				this->scene.camera.SetRotation(Projections::XY);
			}
			ImGui::SameLine();
			if (ImGui::Button("XZ")) {
				this->scene.camera.SetRotation(Projections::XZ);
			}
			ImGui::SameLine();
			if (ImGui::Button("YZ")) {
				this->scene.camera.SetRotation(Projections::YZ);
			}
			ImGui::NewLine();

			ImGui::Checkbox("Fill", &this->scene.rsSolid);
			ImGui::Checkbox("Wirerame", &this->scene.rsWireframe);
			ImGui::Checkbox("Outline through", &this->scene.outlineThroughObjets);

			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Light"))
		{
			if (ImGui::DragFloat("Ambient", &this->scene.ambient, 0.05f, 0.0f, 1.0f, "%.2f")) {
				this->scene.UpdateLight();
			}
			if (ImGui::DragFloat("Intensity", &this->scene.intensity, 0.05f, 0.0f, 1.0f, "%.2f")) {
				this->scene.UpdateLight();
			}
			if (ImGui::DragFloat("Shininess", &this->scene.shininess, 0.1f, 0.0f, 100.0f, "%.1f")) {
				this->scene.UpdateLight();
			}
			if (ImGui::Checkbox("Smooth Shade", &this->scene.smoothShade)) {
				this->scene.UpdateLight();
			}

			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}
	ImGui::End();

	ImGui::Begin("Primitives");
	if (ImGui::TreeNode("Primitives")) {
		std::string ordinal;
		for (UINT i = 0; i < this->scene.primitives.size(); ++i) {
			ordinal = std::to_string(i);
			UCHAR dimension = this->scene.primitives[i]->GetDimension();
			ordinal += dimension == 0 ? "p" : (dimension == 1 ? "l" : "t");
			ImGui::Selectable((ordinal).c_str(), &this->scene.primitives[i]->selected);
		}
		ImGui::TreePop();
	}
	ImGui::End();

	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}