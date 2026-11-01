#pragma once

#include <vulkan/vulkan.h>

#include <stdexcept>
#include <string>

namespace vlk
{

inline void check(VkResult result, const char* message)
{
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error(std::string(message) + " (VkResult " + std::to_string(result) + ")");
    }
}

} // namespace vlk
