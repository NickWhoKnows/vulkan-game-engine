#pragma once

#include <cstdint>

namespace vlk {

using AssetId = uint32_t;
inline constexpr AssetId kInvalidAssetId = 0;

enum class AssetKind { Mesh, Scene };

} // namespace vlk
