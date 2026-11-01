#include "app/Application.hpp"

#include "vulkan/Context.hpp"
#include "vulkan/Renderer.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <stdexcept>

namespace vlk
{

Application::Application()
{
    initWindow();
    context_ = std::make_unique<Context>(window_);
    renderer_ = std::make_unique<Renderer>(*context_, window_);
    glfwSetFramebufferSizeCallback(window_, framebufferResizeCallback);
}

Application::~Application()
{
    if (context_)
    {
        context_->waitIdle();
    }

    renderer_.reset();
    context_.reset();

    if (window_)
    {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }

    glfwTerminate();
}

void Application::run()
{
    mainLoop();
}

void Application::initWindow()
{
    if (!glfwInit())
    {
        throw std::runtime_error("failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window_ = glfwCreateWindow(1280, 720, "Cube", nullptr, nullptr);
    if (!window_)
    {
        glfwTerminate();
        throw std::runtime_error("failed to create window");
    }

    glfwSetWindowUserPointer(window_, this);
}

void Application::mainLoop()
{
    while (!glfwWindowShouldClose(window_))
    {
        glfwPollEvents();
        renderer_->draw();
    }
}

void Application::framebufferResizeCallback(GLFWwindow* window, int, int)
{
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    if (app && app->renderer_)
    {
        app->renderer_->notifyResized();
    }
}

} // namespace vlk
