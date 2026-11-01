#include "vulkan/Renderer.hpp"

#include "vulkan/Check.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

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

struct Vertex {
    float position[3];
    float color[3];
};

constexpr Vertex kVertices[] = {
    {{-0.5f, -0.5f, 0.5f}, {0.90f, 0.25f, 0.25f}},  {{0.5f, -0.5f, 0.5f}, {0.90f, 0.25f, 0.25f}},
    {{0.5f, 0.5f, 0.5f}, {0.90f, 0.25f, 0.25f}},    {{-0.5f, 0.5f, 0.5f}, {0.90f, 0.25f, 0.25f}},

    {{0.5f, -0.5f, -0.5f}, {0.25f, 0.80f, 0.35f}},  {{-0.5f, -0.5f, -0.5f}, {0.25f, 0.80f, 0.35f}},
    {{-0.5f, 0.5f, -0.5f}, {0.25f, 0.80f, 0.35f}},  {{0.5f, 0.5f, -0.5f}, {0.25f, 0.80f, 0.35f}},

    {{0.5f, -0.5f, 0.5f}, {0.25f, 0.45f, 0.95f}},   {{0.5f, -0.5f, -0.5f}, {0.25f, 0.45f, 0.95f}},
    {{0.5f, 0.5f, -0.5f}, {0.25f, 0.45f, 0.95f}},   {{0.5f, 0.5f, 0.5f}, {0.25f, 0.45f, 0.95f}},

    {{-0.5f, -0.5f, -0.5f}, {0.95f, 0.80f, 0.20f}}, {{-0.5f, -0.5f, 0.5f}, {0.95f, 0.80f, 0.20f}},
    {{-0.5f, 0.5f, 0.5f}, {0.95f, 0.80f, 0.20f}},   {{-0.5f, 0.5f, -0.5f}, {0.95f, 0.80f, 0.20f}},

    {{-0.5f, 0.5f, 0.5f}, {0.20f, 0.85f, 0.90f}},   {{0.5f, 0.5f, 0.5f}, {0.20f, 0.85f, 0.90f}},
    {{0.5f, 0.5f, -0.5f}, {0.20f, 0.85f, 0.90f}},   {{-0.5f, 0.5f, -0.5f}, {0.20f, 0.85f, 0.90f}},

    {{-0.5f, -0.5f, -0.5f}, {0.85f, 0.30f, 0.75f}}, {{0.5f, -0.5f, -0.5f}, {0.85f, 0.30f, 0.75f}},
    {{0.5f, -0.5f, 0.5f}, {0.85f, 0.30f, 0.75f}},   {{-0.5f, -0.5f, 0.5f}, {0.85f, 0.30f, 0.75f}},
};

constexpr uint16_t kIndices[] = {
    0,  1,  2,  2,  3,  0,  4,  5,  6,  6,  7,  4,  8,  9,  10, 10, 11, 8,
    12, 13, 14, 14, 15, 12, 16, 17, 18, 18, 19, 16, 20, 21, 22, 22, 23, 20,
};

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

Renderer::Renderer(Context &context, GLFWwindow *window) : context_(context), window_(window) {
    depthFormat_ = findDepthFormat(context_.physicalDevice());
    createSwapchain(VK_NULL_HANDLE);
    createDepthResources();
    createPipeline();
    createMeshBuffers();
    createCommandBuffers();
    createSyncObjects();
    createPresentSemaphores();
}

Renderer::~Renderer() {
    context_.waitIdle();

    const VkDevice device = context_.device();
    for (std::size_t frame = 0; frame < imageAvailable_.size(); ++frame) {
        vkDestroySemaphore(device, imageAvailable_[frame], nullptr);
        vkDestroyFence(device, inFlight_[frame], nullptr);
    }
    destroyPresentSemaphores();

    if (!commandBuffers_.empty()) {
        vkFreeCommandBuffers(device, context_.commandPool(), static_cast<uint32_t>(commandBuffers_.size()),
                             commandBuffers_.data());
    }

    destroyBuffer(vertexBuffer_);
    destroyBuffer(indexBuffer_);
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

    const float aspect = swapchainExtent_.height == 0
                             ? 1.0f
                             : static_cast<float>(swapchainExtent_.width) / static_cast<float>(swapchainExtent_.height);

    const glm::mat4 view =
        glm::lookAt(glm::vec3(2.2f, 2.0f, 2.2f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 20.0f);
    projection[1][1] *= -1.0f;
    const glm::mat4 mvp = projection * view;

    vkResetCommandBuffer(commandBuffers_[currentFrame_], 0);
    record(commandBuffers_[currentFrame_], imageIndex, &mvp[0][0]);

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

void Renderer::createDepthResources() {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {swapchainExtent_.width, swapchainExtent_.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat_;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    const VkDevice device = context_.device();
    check(vkCreateImage(device, &imageInfo, nullptr, &depthImage_), "failed to create depth image");

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, depthImage_, &requirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex =
        context_.findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device, &allocInfo, nullptr, &depthMemory_), "failed to allocate depth image memory");
    check(vkBindImageMemory(device, depthImage_, depthMemory_, 0), "failed to bind depth image memory");

    depthImageView_ = createImageView(device, depthImage_, depthFormat_, VK_IMAGE_ASPECT_DEPTH_BIT);

    VkCommandBuffer commandBuffer = context_.beginSingleTimeCommands();
    transitionImage(commandBuffer, depthImage_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_ASPECT_DEPTH_BIT);
    context_.endSingleTimeCommands(commandBuffer);
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

void Renderer::createMeshBuffers() {
    vertexBuffer_ = createBuffer(sizeof(kVertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    uploadBuffer(vertexBuffer_, kVertices, sizeof(kVertices));

    indexBuffer_ = createBuffer(sizeof(kIndices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    uploadBuffer(indexBuffer_, kIndices, sizeof(kIndices));
    indexCount_ = static_cast<uint32_t>(std::size(kIndices));
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

void Renderer::destroyDepthResources() {
    const VkDevice device = context_.device();
    if (depthImageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device, depthImageView_, nullptr);
        depthImageView_ = VK_NULL_HANDLE;
    }
    if (depthImage_ != VK_NULL_HANDLE) {
        vkDestroyImage(device, depthImage_, nullptr);
        depthImage_ = VK_NULL_HANDLE;
    }
    if (depthMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, depthMemory_, nullptr);
        depthMemory_ = VK_NULL_HANDLE;
    }
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
    destroyDepthResources();
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
    destroyDepthResources();
    destroySwapchainViews();

    const VkSwapchainKHR oldSwapchain = swapchain_;
    createSwapchain(oldSwapchain);
    vkDestroySwapchainKHR(context_.device(), oldSwapchain, nullptr);
    createDepthResources();
    createPresentSemaphores();
}

void Renderer::record(VkCommandBuffer commandBuffer, uint32_t imageIndex, const float *mvp) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    check(vkBeginCommandBuffer(commandBuffer, &beginInfo), "failed to begin command buffer");

    transitionImage(commandBuffer, swapchainImages_[imageIndex], VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = swapchainImageViews_[imageIndex];
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{0.08f, 0.09f, 0.12f, 1.0f}};

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = depthImageView_;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = swapchainExtent_;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(commandBuffer, &renderingInfo);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    VkViewport viewport{};
    viewport.width = static_cast<float>(swapchainExtent_.width);
    viewport.height = static_cast<float>(swapchainExtent_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = swapchainExtent_;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer_.handle, &offset);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer_.handle, 0, VK_INDEX_TYPE_UINT16);
    vkCmdPushConstants(commandBuffer, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), mvp);
    vkCmdDrawIndexed(commandBuffer, indexCount_, 1, 0, 0, 0);

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
