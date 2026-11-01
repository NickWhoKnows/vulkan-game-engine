#include "vulkan/Context.hpp"

#include "vulkan/Check.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>

namespace vlk {
namespace {

const char *kValidationLayer = "VK_LAYER_KHRONOS_validation";

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT *data, void *) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << "validation: " << data->pMessage << '\n';
    }
    return VK_FALSE;
}

bool layerAvailable(const char *name) {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());

    for (const auto &layer : layers) {
        if (std::strcmp(layer.layerName, name) == 0) {
            return true;
        }
    }
    return false;
}

bool deviceExtensionAvailable(VkPhysicalDevice device, const char *name) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());

    for (const auto &extension : extensions) {
        if (std::strcmp(extension.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

struct QueueFamilies {
    uint32_t graphics = 0;
    uint32_t present = 0;
    bool graphicsFound = false;
    bool presentFound = false;

    bool complete() const {
        return graphicsFound && presentFound;
    }
};

QueueFamilies findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) {
    QueueFamilies families;
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> properties(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties.data());

    for (uint32_t index = 0; index < count; ++index) {
        if (properties[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            families.graphics = index;
            families.graphicsFound = true;
        }

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface, &presentSupport);
        if (presentSupport) {
            families.present = index;
            families.presentFound = true;
        }

        if (families.complete()) {
            break;
        }
    }

    return families;
}

bool swapchainAdequate(VkPhysicalDevice device, VkSurfaceKHR surface) {
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    return formatCount > 0 && presentModeCount > 0;
}

} // namespace

Context::Context(GLFWwindow *window) {
    createInstance();
    setupDebugMessenger();
    createSurface(window);
    pickPhysicalDevice();
    createLogicalDevice();
    createCommandPool();
}

Context::~Context() {
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
        vkDestroyDevice(device_, nullptr);
    }

    if (instance_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);

        if (debugMessenger_ != VK_NULL_HANDLE) {
            auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroyMessenger) {
                destroyMessenger(instance_, debugMessenger_, nullptr);
            }
        }

        vkDestroyInstance(instance_, nullptr);
    }
}

void Context::waitIdle() const {
    vkDeviceWaitIdle(device_);
}

uint32_t Context::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);

    for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
        const bool typeMatches = (typeFilter & (1u << index)) != 0;
        const bool propertiesMatch = (memoryProperties.memoryTypes[index].propertyFlags & properties) == properties;
        if (typeMatches && propertiesMatch) {
            return index;
        }
    }

    throw std::runtime_error("failed to find a suitable memory type");
}

VkCommandBuffer Context::beginSingleTimeCommands() const {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    check(vkAllocateCommandBuffers(device_, &allocInfo, &commandBuffer), "failed to allocate command buffer");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(commandBuffer, &beginInfo), "failed to begin command buffer");
    return commandBuffer;
}

void Context::endSingleTimeCommands(VkCommandBuffer commandBuffer) const {
    check(vkEndCommandBuffer(commandBuffer), "failed to end command buffer");

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    check(vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE), "failed to submit command buffer");
    check(vkQueueWaitIdle(graphicsQueue_), "failed to wait for queue");

    vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
}

void Context::createInstance() {
    validationEnabled_ = layerAvailable(kValidationLayer);

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Project Jans";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "Project Jans";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    uint32_t glfwExtensionCount = 0;
    const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    if (!glfwExtensions) {
        throw std::runtime_error("failed to query GLFW Vulkan extensions");
    }

    std::vector<const char *> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    if (validationEnabled_) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (validationEnabled_) {
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = &kValidationLayer;
    }

    check(vkCreateInstance(&createInfo, nullptr, &instance_), "failed to create Vulkan instance");
}

void Context::setupDebugMessenger() {
    if (!validationEnabled_) {
        return;
    }

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;

    auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
    if (!createMessenger) {
        throw std::runtime_error("debug utils messenger is unavailable");
    }

    check(createMessenger(instance_, &createInfo, nullptr, &debugMessenger_), "failed to create debug messenger");
}

void Context::createSurface(GLFWwindow *window) {
    check(glfwCreateWindowSurface(instance_, window, nullptr, &surface_), "failed to create window surface");
}

void Context::pickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) {
        throw std::runtime_error("no Vulkan physical devices found");
    }

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    for (VkPhysicalDevice device : devices) {
        const QueueFamilies families = findQueueFamilies(device, surface_);
        if (!families.complete() || !swapchainAdequate(device, surface_)) {
            continue;
        }
        if (!deviceExtensionAvailable(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            continue;
        }

        VkPhysicalDeviceVulkan13Features vulkan13{};
        vulkan13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        VkPhysicalDeviceFeatures2 features{};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &vulkan13;
        vkGetPhysicalDeviceFeatures2(device, &features);
        if (!vulkan13.dynamicRendering) {
            continue;
        }

        physicalDevice_ = device;
        graphicsFamily_ = families.graphics;
        presentFamily_ = families.present;
        return;
    }

    throw std::runtime_error("failed to find a GPU with swapchain and dynamic rendering support");
}

void Context::createLogicalDevice() {
    const float queuePriority = 1.0f;
    std::set<uint32_t> uniqueFamilies = {graphicsFamily_, presentFamily_};
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    queueInfos.reserve(uniqueFamilies.size());

    for (uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;
        queueInfos.push_back(queueInfo);
    }

    const bool portability = deviceExtensionAvailable(physicalDevice_, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);

    VkPhysicalDevicePortabilitySubsetFeaturesKHR portabilityFeatures{};
    portabilityFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR;

    VkPhysicalDeviceVulkan13Features vulkan13{};
    vulkan13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    if (portability) {
        vulkan13.pNext = &portabilityFeatures;
    }

    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &vulkan13;
    vkGetPhysicalDeviceFeatures2(physicalDevice_, &features);

    if (!vulkan13.dynamicRendering) {
        throw std::runtime_error("selected GPU does not support dynamic rendering");
    }

    std::vector<const char *> extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if (portability) {
        extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
    }

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &features;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
    createInfo.pQueueCreateInfos = queueInfos.data();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    check(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_), "failed to create logical device");

    vkGetDeviceQueue(device_, graphicsFamily_, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, presentFamily_, 0, &presentQueue_);
}

void Context::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = graphicsFamily_;
    check(vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_), "failed to create command pool");
}

} // namespace vlk
