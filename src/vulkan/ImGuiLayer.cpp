#include "vulkan/ImGuiLayer.hpp"

#include "vulkan/Check.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <stdexcept>

namespace vlk {
namespace {

void checkVkResult(VkResult result) {
    check(result, "ImGui Vulkan call failed");
}

ImGui_ImplVulkan_InitInfo makeInitInfo(Context &context, VkDescriptorPool pool, VkFormat *swapchainFormat,
                                       uint32_t swapchainImageCount) {
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = context.instance();
    initInfo.PhysicalDevice = context.physicalDevice();
    initInfo.Device = context.device();
    initInfo.QueueFamily = context.graphicsFamily();
    initInfo.Queue = context.graphicsQueue();
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.DescriptorPool = pool;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = swapchainImageCount;
    initInfo.Allocator = nullptr;
    initInfo.CheckVkResultFn = checkVkResult;
    initInfo.UseDynamicRendering = true;
#ifdef IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
    initInfo.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    initInfo.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    initInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = swapchainFormat;
#endif
    return initInfo;
}

} // namespace

ImGuiLayer::ImGuiLayer(Context &context, GLFWwindow *window, VkFormat swapchainFormat, uint32_t swapchainImageCount)
    : context_(context), window_(window), swapchainColorFormat_(swapchainFormat) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(window_, true);
    createDescriptorPool();

    ImGui_ImplVulkan_InitInfo initInfo =
        makeInitInfo(context_, descriptorPool_, &swapchainColorFormat_, swapchainImageCount);
    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        throw std::runtime_error("failed to initialize ImGui Vulkan backend");
    }
    if (!ImGui_ImplVulkan_CreateFontsTexture()) {
        throw std::runtime_error("failed to create ImGui font texture");
    }

    initialized_ = true;
}

ImGuiLayer::~ImGuiLayer() {
    shutdownImpl();
}

void ImGuiLayer::createDescriptorPool() {
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.pPoolSizes = poolSizes;
    check(vkCreateDescriptorPool(context_.device(), &poolInfo, nullptr, &descriptorPool_),
          "failed to create ImGui descriptor pool");
}

void ImGuiLayer::shutdownImpl() {
    if (initialized_) {
        context_.waitIdle();
        ImGui_ImplVulkan_DestroyFontsTexture();
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        initialized_ = false;
    }

    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(context_.device(), descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }
}

void ImGuiLayer::newFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::renderDrawData(VkCommandBuffer commandBuffer, VkExtent2D) const {
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
}

void ImGuiLayer::onSwapchainRecreated(VkFormat swapchainFormat, uint32_t swapchainImageCount) {
    swapchainColorFormat_ = swapchainFormat;
    ImGui_ImplVulkan_SetMinImageCount(swapchainImageCount);
    ImGui_ImplVulkan_DestroyFontsTexture();
    ImGui_ImplVulkan_Shutdown();

    ImGui_ImplVulkan_InitInfo initInfo =
        makeInitInfo(context_, descriptorPool_, &swapchainColorFormat_, swapchainImageCount);
    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        throw std::runtime_error("failed to reinitialize ImGui Vulkan backend");
    }
    if (!ImGui_ImplVulkan_CreateFontsTexture()) {
        throw std::runtime_error("failed to recreate ImGui font texture");
    }
}

} // namespace vlk
