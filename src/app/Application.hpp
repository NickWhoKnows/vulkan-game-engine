#pragma once

#include <memory>

struct GLFWwindow;

namespace vlk {

class AssetManager;
class Context;
class Renderer;

class Application {
  public:
    Application();
    ~Application();
    Application(const Application &) = delete;
    Application &operator=(const Application &) = delete;
    void run();

  private:
    void initWindow();
    static void framebufferResizeCallback(GLFWwindow *window, int width, int height);

    GLFWwindow *window_ = nullptr;
    std::unique_ptr<AssetManager> assets_;
    std::unique_ptr<Context> context_;
    std::unique_ptr<Renderer> renderer_;
};

} // namespace vlk
