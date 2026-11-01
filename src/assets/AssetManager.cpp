#include "assets/AssetManager.hpp"

namespace vlk {

AssetManager::AssetManager() {
    bootstrapDefaultContent();
}

AssetId AssetManager::allocateId() {
    return nextId_++;
}

void AssetManager::bootstrapDefaultContent() {
    MeshAsset cubeMesh;
    cubeMesh.id = allocateId();
    cubeMesh.name = "Cube";
    cubeMesh.mesh = makeUnitCubeMesh();
    meshes_.push_back(std::move(cubeMesh));

    Scene mainScene;
    mainScene.id = allocateId();
    mainScene.name = "Main Scene";

    SceneObject cubeInstance;
    cubeInstance.id = allocateId();
    cubeInstance.name = "Cube";
    cubeInstance.meshAssetId = meshes_.front().id;
    mainScene.objects.push_back(std::move(cubeInstance));

    activeSceneId_ = mainScene.id;
    scenes_.push_back(std::move(mainScene));
}

const Scene *AssetManager::activeScene() const {
    return findScene(activeSceneId_);
}

const MeshAsset *AssetManager::findMesh(AssetId id) const {
    for (const MeshAsset &mesh : meshes_) {
        if (mesh.id == id) {
            return &mesh;
        }
    }
    return nullptr;
}

const Scene *AssetManager::findScene(AssetId id) const {
    for (const Scene &scene : scenes_) {
        if (scene.id == id) {
            return &scene;
        }
    }
    return nullptr;
}

} // namespace vlk
