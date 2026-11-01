#pragma once

#include "vulkan/Context.hpp"

#include <cstdint>
#include <vector>

struct GLFWwindow;

namespace vlk {

class Renderer {
  public:
    static constexpr int kMaxFramesInFlight = 2;

    Renderer(Context &context, GLFWwindow *window);
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

    void createSwapchain(VkSwapchainKHR oldSwapchain);
    void createDepthResources();
    void createPipeline();
    void createMeshBuffers();
    void createCommandBuffers();
    void createSyncObjects();
    void createPresentSemaphores();
    void destroyPresentSemaphores();
    void destroyDepthResources();
    void destroySwapchainViews();
    void cleanupSwapchain();
    void recreateSwapchain();
    void record(VkCommandBuffer commandBuffer, uint32_t imageIndex, const float *mvp);

    Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
    void uploadBuffer(Buffer &buffer, const void *data, VkDeviceSize size);
    void destroyBuffer(Buffer &buffer);

    Context &context_;
    GLFWwindow *window_ = nullptr;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent_{};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;

    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkImageView depthImageView_ = VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    Buffer vertexBuffer_{};
    Buffer indexBuffer_{};
    uint32_t indexCount_ = 0;

    std::vector<VkCommandBuffer> commandBuffers_;
    std::vector<VkSemaphore> imageAvailable_;
    std::vector<VkSemaphore> renderFinished_;
    std::vector<VkFence> inFlight_;
    uint32_t currentFrame_ = 0;
    bool framebufferResized_ = false;
};

} // namespace vlk
