# Project Jans

Early development of **Project Jans**, a Vulkan-based game engine. The codebase is organized around a small runtime (`src/app`, `src/vulkan`) and will grow toward authoring tools and real-time gameplay rendering.

## Current status

The project currently boots a GLFW window and renders a simple colored cube at the origin using Vulkan dynamic rendering. This is the foundation for swapchain management, pipelines, and mesh drawing—not yet a full engine loop or asset pipeline.

## Requirements

### Toolchain

| Requirement | Version / notes |
|-------------|-----------------|
| **CMake** | 3.20 or newer |
| **C++ compiler** | C++20 (`clang++` or `g++`) |
| **Vulkan SDK** | 1.3+ with **`glslc`** on `PATH` (or `$VULKAN_SDK/bin`) |
| **GLFW 3** | Headers + library (CMake `find_package(glfw3)`) |
| **GLM** | Header-only math library |

### Platform

- **macOS**: Apple Silicon or Intel; MoltenVK via the Vulkan SDK. Xcode Command Line Tools for the compiler.
- **Linux**: Vulkan loader and validation layers from your distro; GPU driver with Vulkan support.

### macOS install (Homebrew example)

```bash
brew install cmake glfw glm
```

Install the [Vulkan SDK](https://vulkan.lunarg.com/) and add it to your shell profile:

```bash
export VULKAN_SDK="$HOME/VulkanSDK/<version>"
export PATH="$VULKAN_SDK/bin:$PATH"
```

Verify:

```bash
cmake --version
glslc --version
pkg-config --modversion glfw3   # or confirm glfw is found by CMake
```

### IDE / language server (optional)

The project exports `compile_commands.json` (symlink at repo root → `build/compile_commands.json`) for clangd and similar tools. Configure once with CMake before opening the project in the editor.

## Commands

From the repository root:

### First-time configure and build

```bash
cmake -S . -B build
cmake --build build
```

### Run

```bash
./build/engine
```

### Rebuild after code changes

```bash
cmake --build build
```

### Reconfigure (after `CMakeLists.txt` or dependency changes)

```bash
cmake -S . -B build
cmake --build build
```

### Clean build directory

```bash
rm -rf build
cmake -S . -B build
cmake --build build
```

### Compile shaders only

Shaders are built automatically as part of `cmake --build build`. To force shader recompilation:

```bash
cmake --build build --target shaders
```

SPIR-V output: `build/shaders/*.spv`.

### Refresh IDE compile database

```bash
cmake -S . -B build
ln -sf build/compile_commands.json compile_commands.json
```

## Build (summary)

Shader GLSL under `shaders/` is compiled to SPIR-V in `build/shaders` during the build. The runtime executable is **`build/engine`**.

## Phase 1 plan

Phase 1 establishes core rendering and a minimal content workflow. Items below are high-level placeholders; each will be broken down into milestones, APIs, and deliverables in future docs.

1. **Dynamic lighting** — Directional and point lights, shadow mapping, and material response (albedo + basic shading) integrated with the render graph.
2. **Model import** — Load meshes and materials from common interchange formats, with a stable in-engine asset representation and GPU upload path.
3. **Basic editor UI** — Dockable panels for scene hierarchy, transforms, and viewport; enough to place objects and tweak lighting without rebuilding the game.
4. **Real-time rendering** — Stable frame loop, camera control, resize handling, and performance targets suitable for interactive preview in the editor and standalone play mode.

Further phases (physics, scripting, audio, packaging, etc.) are out of scope until Phase 1 is defined in detail.

## Repository layout

```
src/app/          Application window and main loop
src/vulkan/       Vulkan context, swapchain, and renderer
shaders/          GLSL sources (compiled to SPIR-V in build/)
```

## License

TBD.
