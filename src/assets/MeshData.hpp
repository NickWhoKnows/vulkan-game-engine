#pragma once

#include <cstdint>
#include <vector>

namespace vlk {

struct Vertex {
    float position[3];
    float color[3];
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
};

MeshData makeUnitCubeMesh();

} // namespace vlk
