#include "vulkan/Renderer.hpp"

#include "assets/MeshAsset.hpp"
#include "assets/Scene.hpp"
#include "editor/EditorUI.hpp"
#include "vulkan/Check.hpp"
#include "vulkan/ImGuiLayer.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <backends/imgui_impl_vulkan.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace vlk {
namespace {

std::vector<char> readFile(const std::string &path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to open " + path);
    }

    const auto size = static_cast<std::size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(size));
    if (!file) {
        throw std::runtime_error("failed to read " + path);
    }
    return buffer;
}

VkShaderModule createShaderModule(VkDevice device, const std::vector<char> &code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

    VkShaderModule module = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device, &createInfo, nullptr, &module), "failed to create shader module");
    return module;
}

VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &formats) {
    for (const auto &format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats.front();
}

VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR &capabilities, GLFWwindow *window) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);

    VkExtent2D extent{};
    extent.width =
        std::clamp(static_cast<uint32_t>(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    extent.height = std::clamp(static_cast<uint32_t>(height), capabilities.minImageExtent.height,
                               capabilities.maxImageExtent.height);
    return extent;
}

VkFormat findDepthFormat(VkPhysicalDevice physicalDevice) {
    const VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };

    for (VkFormat format : candidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
        if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return format;
        }
    }

    throw std::runtime_error("failed to find a depth format");
}

void transitionImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                     VkImageAspectFlags aspect) {
    if (oldLayout == newLayout) {
        return;
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = 0;
        sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask =
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else {
        throw std::runtime_error("unsupported image layout transition");
    }

    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspect) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspect;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView view = VK_NULL_HANDLE;
    check(vkCreateImageView(device, &viewInfo, nullptr, &view), "failed to create image view");
    return view;
}

} // namespace

Renderer::Renderer(Context &context, GLFWwindow *window, AssetManager &assets)
    : context_(context), assets_(assets), window_(window) {
    depthFormat_ = findDepthFormat(context_.physicalDevice());
    createSwapchain(VK_NULL_HANDLE);
    createPipeline();
    uploadMeshAssets();
    createViewportResources();
    createCommandBuffers();
    createSyncObjects();
    createPresentSemaphores();
    imgui_ = std::make_unique<ImGuiLayer>(context_, window_, swapchainFormat_,
                                          static_cast<uint32_t>(swapchainImages_.size()));
    updateViewportImGuiTexture();
}

Renderer::~Renderer() {
    context_.waitIdle();

    imgui_.reset();

    if (viewportDescriptor_ != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(viewportDescriptor_);
        viewportDescriptor_ = VK_NULL_HANDLE;
    }

    const VkDevice device = context_.device();
    if (viewportSampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device, viewportSampler_, nullptr);
    }

    for (std::size_t frame = 0; frame < imageAvailable_.size(); ++frame) {
        vkDestroySemaphore(device, imageAvailable_[frame], nullptr);
        vkDestroyFence(device, inFlight_[frame], nullptr);
    }
    destroyPresentSemaphores();

    if (!commandBuffers_.empty()) {
        vkFreeCommandBuffers(device, context_.commandPool(), static_cast<uint32_t>(commandBuffers_.size()),
                             commandBuffers_.data());
    }

    destroyMeshBuffers();
    destroyViewportResources();
    vkDestroyPipeline(device, pipeline_, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
    cleanupSwapchain();
}

void Renderer::notifyResized() {
    framebufferResized_ = true;
}

void Renderer::draw() {
    const VkDevice device = context_.device();
    vkWaitForFences(device, 1, &inFlight_[currentFrame_], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    const VkResult acquireResult = vkAcquireNextImageKHR(device, swapchain_, UINT64_MAX, imageAvailable_[currentFrame_],
                                                         VK_NULL_HANDLE, &imageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    }

    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swapchain image");
    }

    vkResetFences(device, 1, &inFlight_[currentFrame_]);

    ensureViewportSize(static_cast<uint32_t>(viewportUiWidth_), static_cast<uint32_t>(viewportUiHeight_));

    imgui_->newFrame();
    EditorViewport editorViewport{};
    editorViewport.texture = viewportTextureId_;
    buildEditorUI(assets_, editorViewport);
    viewportUiWidth_ = editorViewport.width;
    viewportUiHeight_ = editorViewport.height;

    ImGui::Render();

    vkResetCommandBuffer(commandBuffers_[currentFrame_], 0);
    record(commandBuffers_[currentFrame_], imageIndex);

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAvailable_[currentFrame_];
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers_[currentFrame_];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinished_[imageIndex];
    check(vkQueueSubmit(context_.graphicsQueue(), 1, &submitInfo, inFlight_[currentFrame_]), "failed to submit draw");

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished_[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex;

    const VkResult presentResult = vkQueuePresentKHR(context_.presentQueue(), &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || framebufferResized_) {
        framebufferResized_ = false;
        recreateSwapchain();
    } else if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("failed to present swapchain image");
    }

    currentFrame_ = (currentFrame_ + 1) % kMaxFramesInFlight;
}

void Renderer::createSwapchain(VkSwapchainKHR oldSwapchain) {
    const VkPhysicalDevice physicalDevice = context_.physicalDevice();
    const VkSurfaceKHR surface = context_.surface();

    VkSurfaceCapabilitiesKHR capabilities{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());

    const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(formats);
    swapchainFormat_ = surfaceFormat.format;
    swapchainExtent_ = chooseExtent(capabilities, window_);

    uint32_t swapchainImageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && swapchainImageCount > capabilities.maxImageCount) {
        swapchainImageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = swapchainImageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = swapchainExtent_;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = oldSwapchain;

    const uint32_t families[] = {context_.graphicsFamily(), context_.presentFamily()};
    if (families[0] != families[1]) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = families;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    check(vkCreateSwapchainKHR(context_.device(), &createInfo, nullptr, &swapchain_), "failed to create swapchain");

    uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(context_.device(), swapchain_, &imageCount, nullptr);
    swapchainImages_.resize(imageCount);
    vkGetSwapchainImagesKHR(context_.device(), swapchain_, &imageCount, swapchainImages_.data());

    swapchainImageViews_.resize(imageCount);
    for (uint32_t index = 0; index < imageCount; ++index) {
        swapchainImageViews_[index] =
            createImageView(context_.device(), swapchainImages_[index], swapchainFormat_, VK_IMAGE_ASPECT_COLOR_BIT);
    }
}

void Renderer::createViewportResources() {
    const VkDevice device = context_.device();
    viewportExtent_.width = std::max(1u, static_cast<uint32_t>(viewportUiWidth_));
    viewportExtent_.height = std::max(1u, static_cast<uint32_t>(viewportUiHeight_));

    VkImageCreateInfo colorInfo{};
    colorInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    colorInfo.imageType = VK_IMAGE_TYPE_2D;
    colorInfo.extent = {viewportExtent_.width, viewportExtent_.height, 1};
    colorInfo.mipLevels = 1;
    colorInfo.arrayLayers = 1;
    colorInfo.format = swapchainFormat_;
    colorInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    colorInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    colorInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    colorInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateImage(device, &colorInfo, nullptr, &viewportColorImage_), "failed to create viewport color image");

    VkMemoryRequirements colorRequirements{};
    vkGetImageMemoryRequirements(device, viewportColorImage_, &colorRequirements);
    VkMemoryAllocateInfo colorAlloc{};
    colorAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    colorAlloc.allocationSize = colorRequirements.size;
    colorAlloc.memoryTypeIndex =
        context_.findMemoryType(colorRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device, &colorAlloc, nullptr, &viewportColorMemory_), "failed to allocate viewport memory");
    check(vkBindImageMemory(device, viewportColorImage_, viewportColorMemory_, 0), "failed to bind viewport memory");

    viewportColorView_ =
        createImageView(device, viewportColorImage_, swapchainFormat_, VK_IMAGE_ASPECT_COLOR_BIT);

    VkImageCreateInfo depthInfo{};
    depthInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthInfo.imageType = VK_IMAGE_TYPE_2D;
    depthInfo.extent = {viewportExtent_.width, viewportExtent_.height, 1};
    depthInfo.mipLevels = 1;
    depthInfo.arrayLayers = 1;
    depthInfo.format = depthFormat_;
    depthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateImage(device, &depthInfo, nullptr, &viewportDepthImage_), "failed to create viewport depth image");

    VkMemoryRequirements depthRequirements{};
    vkGetImageMemoryRequirements(device, viewportDepthImage_, &depthRequirements);
    VkMemoryAllocateInfo depthAlloc{};
    depthAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    depthAlloc.allocationSize = depthRequirements.size;
    depthAlloc.memoryTypeIndex =
        context_.findMemoryType(depthRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device, &depthAlloc, nullptr, &viewportDepthMemory_), "failed to allocate viewport depth");
    check(vkBindImageMemory(device, viewportDepthImage_, viewportDepthMemory_, 0), "failed to bind viewport depth");

    viewportDepthImageView_ =
        createImageView(device, viewportDepthImage_, depthFormat_, VK_IMAGE_ASPECT_DEPTH_BIT);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    check(vkCreateSampler(device, &samplerInfo, nullptr, &viewportSampler_), "failed to create viewport sampler");

    VkCommandBuffer commandBuffer = context_.beginSingleTimeCommands();
    transitionImage(commandBuffer, viewportDepthImage_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_ASPECT_DEPTH_BIT);
    context_.endSingleTimeCommands(commandBuffer);
}

void Renderer::destroyViewportResources() {
    const VkDevice device = context_.device();
    if (viewportColorView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device, viewportColorView_, nullptr);
        viewportColorView_ = VK_NULL_HANDLE;
    }
    if (viewportColorImage_ != VK_NULL_HANDLE) {
        vkDestroyImage(device, viewportColorImage_, nullptr);
        viewportColorImage_ = VK_NULL_HANDLE;
    }
    if (viewportColorMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, viewportColorMemory_, nullptr);
        viewportColorMemory_ = VK_NULL_HANDLE;
    }
    if (viewportDepthImageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device, viewportDepthImageView_, nullptr);
        viewportDepthImageView_ = VK_NULL_HANDLE;
    }
    if (viewportDepthImage_ != VK_NULL_HANDLE) {
        vkDestroyImage(device, viewportDepthImage_, nullptr);
        viewportDepthImage_ = VK_NULL_HANDLE;
    }
    if (viewportDepthMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, viewportDepthMemory_, nullptr);
        viewportDepthMemory_ = VK_NULL_HANDLE;
    }
}

void Renderer::ensureViewportSize(uint32_t width, uint32_t height) {
    width = std::max(1u, width);
    height = std::max(1u, height);
    if (width == viewportExtent_.width && height == viewportExtent_.height) {
        return;
    }

    context_.waitIdle();
    if (viewportDescriptor_ != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(viewportDescriptor_);
        viewportDescriptor_ = VK_NULL_HANDLE;
        viewportTextureId_ = 0;
    }

    destroyViewportResources();
    viewportColorLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    viewportUiWidth_ = static_cast<float>(width);
    viewportUiHeight_ = static_cast<float>(height);
    createViewportResources();
    updateViewportImGuiTexture();
}

void Renderer::updateViewportImGuiTexture() {
    if (viewportColorView_ == VK_NULL_HANDLE || viewportSampler_ == VK_NULL_HANDLE) {
        return;
    }

    viewportDescriptor_ = ImGui_ImplVulkan_AddTexture(viewportSampler_, viewportColorView_,
                                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    viewportTextureId_ = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(viewportDescriptor_));
}

void Renderer::createPipeline() {
    const VkShaderModule vertexShader =
        createShaderModule(context_.device(), readFile(std::string(SHADER_DIR) + "/cube.vert.spv"));
    const VkShaderModule fragmentShader =
        createShaderModule(context_.device(), readFile(std::string(SHADER_DIR) + "/cube.frag.spv"));

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexShader;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentShader;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributes[2]{};
    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = offsetof(Vertex, position);
    attributes[1].location = 1;
    attributes[1].binding = 0;
    attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset = offsetof(Vertex, color);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &colorBlendAttachment;

    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(glm::mat4);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstant;
    check(vkCreatePipelineLayout(context_.device(), &layoutInfo, nullptr, &pipelineLayout_),
          "failed to create pipeline layout");

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &swapchainFormat_;
    renderingInfo.depthAttachmentFormat = depthFormat_;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;

    check(vkCreateGraphicsPipelines(context_.device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_),
          "failed to create graphics pipeline");

    vkDestroyShaderModule(context_.device(), vertexShader, nullptr);
    vkDestroyShaderModule(context_.device(), fragmentShader, nullptr);
}

void Renderer::uploadMeshAssets() {
    for (const MeshAsset &meshAsset : assets_.meshes()) {
        MeshGpuBuffers gpu{};
        const MeshData &mesh = meshAsset.mesh;
        const VkDeviceSize vertexBytes = static_cast<VkDeviceSize>(mesh.vertices.size() * sizeof(Vertex));
        const VkDeviceSize indexBytes = static_cast<VkDeviceSize>(mesh.indices.size() * sizeof(uint16_t));

        gpu.vertexBuffer = createBuffer(vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        uploadBuffer(gpu.vertexBuffer, mesh.vertices.data(), vertexBytes);

        gpu.indexBuffer = createBuffer(indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        uploadBuffer(gpu.indexBuffer, mesh.indices.data(), indexBytes);
        gpu.indexCount = static_cast<uint32_t>(mesh.indices.size());
        meshBuffers_.emplace(meshAsset.id, gpu);
    }
}

void Renderer::destroyMeshBuffers() {
    for (auto &[id, buffers] : meshBuffers_) {
        (void)id;
        destroyBuffer(buffers.vertexBuffer);
        destroyBuffer(buffers.indexBuffer);
    }
    meshBuffers_.clear();
}

void Renderer::createCommandBuffers() {
    commandBuffers_.resize(kMaxFramesInFlight);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = context_.commandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());
    check(vkAllocateCommandBuffers(context_.device(), &allocInfo, commandBuffers_.data()),
          "failed to allocate command buffers");
}

void Renderer::createSyncObjects() {
    imageAvailable_.resize(kMaxFramesInFlight);
    inFlight_.resize(kMaxFramesInFlight);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int frame = 0; frame < kMaxFramesInFlight; ++frame) {
        check(vkCreateSemaphore(context_.device(), &semaphoreInfo, nullptr, &imageAvailable_[frame]),
              "failed to create semaphore");
        check(vkCreateFence(context_.device(), &fenceInfo, nullptr, &inFlight_[frame]), "failed to create fence");
    }
}

void Renderer::createPresentSemaphores() {
    renderFinished_.resize(swapchainImages_.size());

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (VkSemaphore &semaphore : renderFinished_) {
        check(vkCreateSemaphore(context_.device(), &semaphoreInfo, nullptr, &semaphore),
              "failed to create present semaphore");
    }
}

void Renderer::destroyPresentSemaphores() {
    for (VkSemaphore semaphore : renderFinished_) {
        vkDestroySemaphore(context_.device(), semaphore, nullptr);
    }
    renderFinished_.clear();
}

void Renderer::destroySwapchainViews() {
    const VkDevice device = context_.device();
    for (VkImageView view : swapchainImageViews_) {
        vkDestroyImageView(device, view, nullptr);
    }
    swapchainImageViews_.clear();
    swapchainImages_.clear();
}

void Renderer::cleanupSwapchain() {
    destroySwapchainViews();

    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(context_.device(), swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

void Renderer::recreateSwapchain() {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window_, &width, &height);
        glfwWaitEvents();
    }

    context_.waitIdle();
    destroyPresentSemaphores();
    destroySwapchainViews();

    const VkSwapchainKHR oldSwapchain = swapchain_;
    createSwapchain(oldSwapchain);
    if (oldSwapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(context_.device(), oldSwapchain, nullptr);
    }
    createPresentSemaphores();

    if (imgui_) {
        imgui_->onSwapchainRecreated(swapchainFormat_, static_cast<uint32_t>(swapchainImages_.size()));
        updateViewportImGuiTexture();
    }
}

void Renderer::record(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    check(vkBeginCommandBuffer(commandBuffer, &beginInfo), "failed to begin command buffer");

    transitionImage(commandBuffer, viewportColorImage_, viewportColorLayout_, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_ASPECT_COLOR_BIT);
    viewportColorLayout_ = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = viewportColorView_;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{0.08f, 0.09f, 0.12f, 1.0f}};

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = viewportDepthImageView_;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo sceneRendering{};
    sceneRendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    sceneRendering.renderArea.offset = {0, 0};
    sceneRendering.renderArea.extent = viewportExtent_;
    sceneRendering.layerCount = 1;
    sceneRendering.colorAttachmentCount = 1;
    sceneRendering.pColorAttachments = &colorAttachment;
    sceneRendering.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(commandBuffer, &sceneRendering);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    VkViewport viewport{};
    viewport.width = static_cast<float>(viewportExtent_.width);
    viewport.height = static_cast<float>(viewportExtent_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = viewportExtent_;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    const float aspect = viewportExtent_.height == 0
                             ? 1.0f
                             : static_cast<float>(viewportExtent_.width) / static_cast<float>(viewportExtent_.height);
    const glm::mat4 view =
        glm::lookAt(glm::vec3(2.2f, 2.0f, 2.2f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 20.0f);
    projection[1][1] *= -1.0f;
    const glm::mat4 viewProjection = projection * view;

    if (const Scene *scene = assets_.activeScene()) {
        for (const SceneObject &object : scene->objects) {
            const auto meshIt = meshBuffers_.find(object.meshAssetId);
            if (meshIt == meshBuffers_.end()) {
                continue;
            }

            const MeshGpuBuffers &mesh = meshIt->second;
            const glm::mat4 mvp = viewProjection;
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &mesh.vertexBuffer.handle, &offset);
            vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer.handle, 0, VK_INDEX_TYPE_UINT16);
            vkCmdPushConstants(commandBuffer, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &mvp);
            vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, 0, 0, 0);
        }
    }

    vkCmdEndRendering(commandBuffer);

    transitionImage(commandBuffer, viewportColorImage_, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    viewportColorLayout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    transitionImage(commandBuffer, swapchainImages_[imageIndex], VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

    VkRenderingAttachmentInfo swapColorAttachment{};
    swapColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    swapColorAttachment.imageView = swapchainImageViews_[imageIndex];
    swapColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    swapColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    swapColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    swapColorAttachment.clearValue.color = {{0.05f, 0.05f, 0.06f, 1.0f}};

    VkRenderingInfo uiRendering{};
    uiRendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    uiRendering.renderArea.offset = {0, 0};
    uiRendering.renderArea.extent = swapchainExtent_;
    uiRendering.layerCount = 1;
    uiRendering.colorAttachmentCount = 1;
    uiRendering.pColorAttachments = &swapColorAttachment;

    vkCmdBeginRendering(commandBuffer, &uiRendering);
    imgui_->renderDrawData(commandBuffer, swapchainExtent_);
    vkCmdEndRendering(commandBuffer);

    transitionImage(commandBuffer, swapchainImages_[imageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT);

    check(vkEndCommandBuffer(commandBuffer), "failed to end command buffer");
}

Renderer::Buffer Renderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) {
    Buffer buffer;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(context_.device(), &bufferInfo, nullptr, &buffer.handle), "failed to create buffer");

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(context_.device(), buffer.handle, &requirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = context_.findMemoryType(requirements.memoryTypeBits, properties);
    check(vkAllocateMemory(context_.device(), &allocInfo, nullptr, &buffer.memory), "failed to allocate buffer memory");
    check(vkBindBufferMemory(context_.device(), buffer.handle, buffer.memory, 0), "failed to bind buffer memory");
    return buffer;
}

void Renderer::uploadBuffer(Buffer &buffer, const void *data, VkDeviceSize size) {
    void *mapped = nullptr;
    check(vkMapMemory(context_.device(), buffer.memory, 0, size, 0, &mapped), "failed to map buffer memory");
    std::memcpy(mapped, data, static_cast<std::size_t>(size));
    vkUnmapMemory(context_.device(), buffer.memory);
}

void Renderer::destroyBuffer(Buffer &buffer) {
    vkDestroyBuffer(context_.device(), buffer.handle, nullptr);
    vkFreeMemory(context_.device(), buffer.memory, nullptr);
    buffer.handle = VK_NULL_HANDLE;
    buffer.memory = VK_NULL_HANDLE;
}

} // namespace vlk
