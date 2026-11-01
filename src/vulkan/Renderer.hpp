#pragma once

#include "assets/AssetManager.hpp"
#include "vulkan/Context.hpp"

#include <imgui.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace vlk {

class ImGuiLayer;

class Renderer {
  public:
    static constexpr int kMaxFramesInFlight = 2;

    Renderer(Context &context, GLFWwindow *window, AssetManager &assets);
    ~Renderer();

    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    void draw();
    void notifyResized();

  private:
    struct Buffer {
        VkBuffer handle = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
    };

    struct MeshGpuBuffers {
        Buffer vertexBuffer{};
        Buffer indexBuffer{};
        uint32_t indexCount = 0;
    };

    void createSwapchain(VkSwapchainKHR oldSwapchain);
    void createViewportResources();
    void destroyViewportResources();
    void ensureViewportSize(uint32_t width, uint32_t height);
    void createPipeline();
    void uploadMeshAssets();
    void createCommandBuffers();
    void createSyncObjects();
    void createPresentSemaphores();
    void destroyPresentSemaphores();
    void destroySwapchainViews();
    void cleanupSwapchain();
    void recreateSwapchain();
    void updateViewportImGuiTexture();
    void record(VkCommandBuffer commandBuffer, uint32_t imageIndex);

    Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
    void uploadBuffer(Buffer &buffer, const void *data, VkDeviceSize size);
    void destroyBuffer(Buffer &buffer);
    void destroyMeshBuffers();

    Context &context_;
    AssetManager &assets_;
    GLFWwindow *window_ = nullptr;
    std::unique_ptr<ImGuiLayer> imgui_;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent_{};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;

    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D viewportExtent_{};
    VkImage viewportColorImage_ = VK_NULL_HANDLE;
    VkDeviceMemory viewportColorMemory_ = VK_NULL_HANDLE;
    VkImageView viewportColorView_ = VK_NULL_HANDLE;
    VkImage viewportDepthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory viewportDepthMemory_ = VK_NULL_HANDLE;
    VkImageView viewportDepthImageView_ = VK_NULL_HANDLE;
    VkSampler viewportSampler_ = VK_NULL_HANDLE;
    VkDescriptorSet viewportDescriptor_ = VK_NULL_HANDLE;
    ImTextureID viewportTextureId_ = 0;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    std::unordered_map<AssetId, MeshGpuBuffers> meshBuffers_;

    std::vector<VkCommandBuffer> commandBuffers_;
    std::vector<VkSemaphore> imageAvailable_;
    std::vector<VkSemaphore> renderFinished_;
    std::vector<VkFence> inFlight_;
    uint32_t currentFrame_ = 0;
    bool framebufferResized_ = false;
    float viewportUiWidth_ = 640.0f;
    float viewportUiHeight_ = 480.0f;
    VkImageLayout viewportColorLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
};

} // namespace vlk
