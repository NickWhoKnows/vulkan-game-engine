#pragma once

#include "assets/AssetId.hpp"
#include "assets/MeshData.hpp"

#include <string>

namespace vlk {

struct MeshAsset {
    AssetId id = kInvalidAssetId;
    std::string name;
    MeshData mesh;
};

} // namespace vlk
