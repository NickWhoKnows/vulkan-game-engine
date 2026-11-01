#pragma once

#include "assets/AssetManager.hpp"

#include <imgui.h>

namespace vlk {

struct EditorViewport {
    ImTextureID texture = 0;
    float width = 0.0f;
    float height = 0.0f;
};

void buildEditorUI(AssetManager &assets, EditorViewport &viewport);

} // namespace vlk
