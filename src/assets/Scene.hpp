#pragma once

#include "assets/AssetId.hpp"

#include <string>
#include <vector>

namespace vlk {

struct SceneObject {
    AssetId id = kInvalidAssetId;
    std::string name;
    AssetId meshAssetId = kInvalidAssetId;
};

struct Scene {
    AssetId id = kInvalidAssetId;
    std::string name;
    std::vector<SceneObject> objects;
};

} // namespace vlk
