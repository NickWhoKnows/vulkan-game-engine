#pragma once

#include "vulkan/Context.hpp"

#include <vulkan/vulkan.h>

struct GLFWwindow;

namespace vlk {

class ImGuiLayer {
  public:
    ImGuiLayer(Context &context, GLFWwindow *window, VkFormat swapchainFormat, uint32_t swapchainImageCount);
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer &) = delete;
    ImGuiLayer &operator=(const ImGuiLayer &) = delete;

    void newFrame();
    void renderDrawData(VkCommandBuffer commandBuffer, VkExtent2D extent) const;
    void onSwapchainRecreated(VkFormat swapchainFormat, uint32_t swapchainImageCount);

    VkDescriptorPool descriptorPool() const { return descriptorPool_; }

  private:
    void createDescriptorPool();
    void shutdownImpl();

    Context &context_;
    GLFWwindow *window_ = nullptr;
    VkFormat swapchainColorFormat_ = VK_FORMAT_UNDEFINED;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    bool initialized_ = false;
};

} // namespace vlk
