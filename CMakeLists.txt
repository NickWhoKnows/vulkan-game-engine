cmake_minimum_required(VERSION 3.20)

project(vlkprogram LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)
find_path(GLM_INCLUDE_DIR glm/glm.hpp
    HINTS /opt/homebrew/include /usr/local/include
    REQUIRED
)
find_program(GLSLC_EXECUTABLE glslc
    HINTS /usr/local/bin $ENV{VULKAN_SDK}/bin
    REQUIRED
)

set(SHADER_SOURCES
    shaders/cube.vert
    shaders/cube.frag
)

set(SHADER_OUTPUTS)
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SHADER_OUTPUT ${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.spv)
    add_custom_command(
        OUTPUT ${SHADER_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
        COMMAND ${GLSLC_EXECUTABLE} ${CMAKE_SOURCE_DIR}/${SHADER} -o ${SHADER_OUTPUT}
        DEPENDS ${CMAKE_SOURCE_DIR}/${SHADER}
        COMMENT "Compiling ${SHADER_NAME}"
        VERBATIM
    )
    list(APPEND SHADER_OUTPUTS ${SHADER_OUTPUT})
endforeach()

add_custom_target(shaders DEPENDS ${SHADER_OUTPUTS})

add_executable(engine
    src/main.cpp
    src/app/Application.cpp
    src/vulkan/Context.cpp
    src/vulkan/Renderer.cpp
)

add_dependencies(engine shaders)

target_include_directories(engine
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${GLM_INCLUDE_DIR}
)

target_compile_definitions(engine
    PRIVATE
    VK_ENABLE_BETA_EXTENSIONS
    SHADER_DIR="${CMAKE_BINARY_DIR}/shaders"
)

target_link_libraries(engine
    PRIVATE
    Vulkan::Vulkan
    glfw
)
