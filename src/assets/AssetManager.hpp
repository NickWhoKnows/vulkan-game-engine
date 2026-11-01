#pragma once

#include "assets/MeshAsset.hpp"
#include "assets/Scene.hpp"

#include <optional>
#include <string>
#include <vector>

namespace vlk {

class AssetManager {
  public:
    AssetManager();

    const std::vector<MeshAsset> &meshes() const { return meshes_; }
    const std::vector<Scene> &scenes() const { return scenes_; }

    AssetId activeSceneId() const { return activeSceneId_; }
    const Scene *activeScene() const;
    const MeshAsset *findMesh(AssetId id) const;
    const Scene *findScene(AssetId id) const;

  private:
    AssetId nextId_ = 1;
    std::vector<MeshAsset> meshes_;
    std::vector<Scene> scenes_;
    AssetId activeSceneId_ = kInvalidAssetId;

    AssetId allocateId();
    void bootstrapDefaultContent();
};

} // namespace vlk
