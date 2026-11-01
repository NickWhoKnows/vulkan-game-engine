#include "app/Application.hpp"

#include "assets/AssetManager.hpp"
#include "vulkan/Context.hpp"
#include "vulkan/Renderer.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <stdexcept>

namespace vlk {

Application::Application() {
    initWindow();
    assets_ = std::make_unique<AssetManager>();
    context_ = std::make_unique<Context>(window_);
    renderer_ = std::make_unique<Renderer>(*context_, window_, *assets_);
    glfwSetFramebufferSizeCallback(window_, framebufferResizeCallback);
}

Application::~Application() {
    renderer_.reset();
    context_.reset();
    assets_.reset();

    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }

    glfwTerminate();
}

void Application::run() {
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
        renderer_->draw();
    }
}

void Application::initWindow() {
    if (!glfwInit()) {
        throw std::runtime_error("failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    if (!monitor) {
        throw std::runtime_error("failed to query primary monitor");
    }

    int workX = 0;
    int workY = 0;
    int workWidth = 1280;
    int workHeight = 720;
    glfwGetMonitorWorkarea(monitor, &workX, &workY, &workWidth, &workHeight);

    window_ = glfwCreateWindow(workWidth, workHeight, "Project Jans", nullptr, nullptr);
    if (window_) {
        glfwSetWindowPos(window_, workX, workY);
        glfwMaximizeWindow(window_);
    }
    if (!window_) {
        glfwTerminate();
        throw std::runtime_error("failed to create window");
    }

    glfwSetWindowUserPointer(window_, this);
}

void Application::framebufferResizeCallback(GLFWwindow *window, int, int) {
    auto *app = static_cast<Application *>(glfwGetWindowUserPointer(window));
    if (app && app->renderer_) {
        app->renderer_->notifyResized();
    }
}

} // namespace vlk
