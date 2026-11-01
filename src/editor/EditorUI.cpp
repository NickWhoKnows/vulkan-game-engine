#include "editor/EditorUI.hpp"

#include "assets/MeshAsset.hpp"
#include "assets/Scene.hpp"

namespace vlk {

void buildEditorUI(AssetManager &assets, EditorViewport &viewport) {
    ImGuiViewport *mainViewport = ImGui::GetMainViewport();
    const float explorerWidth = 280.0f;

    const ImGuiWindowFlags panelFlags =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

    ImGui::SetNextWindowPos(mainViewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(explorerWidth, mainViewport->WorkSize.y));
    ImGui::Begin("Scene Explorer", nullptr, panelFlags);
    if (const Scene *scene = assets.activeScene()) {
        ImGui::Text("Active scene: %s", scene->name.c_str());
        ImGui::Separator();
        ImGui::Text("Objects");
        if (scene->objects.empty()) {
            ImGui::TextDisabled("(empty)");
        } else {
            for (const SceneObject &object : scene->objects) {
                ImGui::BulletText("%s", object.name.c_str());
                if (const MeshAsset *mesh = assets.findMesh(object.meshAssetId)) {
                    ImGui::Indent();
                    ImGui::TextDisabled("mesh: %s", mesh->name.c_str());
                    ImGui::Unindent();
                }
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Assets");
        ImGui::Text("Meshes");
        for (const MeshAsset &mesh : assets.meshes()) {
            ImGui::BulletText("%s", mesh.name.c_str());
        }
        ImGui::Text("Scenes");
        for (const Scene &sceneAsset : assets.scenes()) {
            const bool active = sceneAsset.id == assets.activeSceneId();
            ImGui::BulletText("%s%s", sceneAsset.name.c_str(), active ? " (active)" : "");
        }
    } else {
        ImGui::TextDisabled("No active scene");
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(mainViewport->WorkPos.x + explorerWidth, mainViewport->WorkPos.y));
    ImGui::SetNextWindowSize(ImVec2(mainViewport->WorkSize.x - explorerWidth, mainViewport->WorkSize.y));
    ImGui::Begin("Scene", nullptr, panelFlags);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    viewport.width = available.x > 1.0f ? available.x : 1.0f;
    viewport.height = available.y > 1.0f ? available.y : 1.0f;
    if (viewport.texture != 0) {
        ImGui::Image(viewport.texture, ImVec2(viewport.width, viewport.height));
    } else {
        ImGui::TextDisabled("Viewport not ready");
    }
    ImGui::End();
}

} // namespace vlk
