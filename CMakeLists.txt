cmake_minimum_required(VERSION 3.10)
project(SceneDebugger)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set policy for MSVC runtime library selection
if (MSVC)
    cmake_policy(SET CMP0091 NEW)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Include directories for ImGui and project headers
include_directories(
    ${PROJECT_SOURCE_DIR}/imgui
    ${PROJECT_SOURCE_DIR}/imgui/backends
    ${PROJECT_SOURCE_DIR}
)

# Source files
set(SOURCES
    main.cpp
    Camera.h
    Shader.h
    # ImGui source files
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_demo.cpp
    imgui/backends/imgui_impl_glfw.cpp
    imgui/backends/imgui_impl_opengl3.cpp
)

# Find OpenGL
find_package(OpenGL REQUIRED)
if (WIN32)
    # On Windows, set OpenGL library explicitly
    set(OPENGL_LIBRARIES opengl32.lib)
endif()

# Set paths to libraries
if (WIN32)
    # Modify these paths to where you've installed the libraries
    set(GLFW_ROOT "C:/Devel/glfw-3.3.8.bin.WIN64")
    set(GLEW_ROOT "C:/Devel/glew-2.1.0")
    set(GLM_ROOT "C:/Devel/glm")
    
    # GLFW
    set(GLFW_INCLUDE_DIR "${GLFW_ROOT}/include")
    set(GLFW_LIBRARY "${GLFW_ROOT}/lib-vc2019/glfw3.lib")
    
    # GLEW
    set(GLEW_INCLUDE_DIR "${GLEW_ROOT}/include")
    set(GLEW_LIBRARY "${GLEW_ROOT}/lib/Release/x64/glew32.lib")
    
    # GLM
    set(GLM_INCLUDE_DIR "${GLM_ROOT}")
    
    # Include directories
    include_directories(
        ${GLFW_INCLUDE_DIR}
        ${GLEW_INCLUDE_DIR}
        ${GLM_INCLUDE_DIR}
    )
    
    # Link directories
    link_directories(
        ${GLFW_ROOT}/lib-vc2019
        ${GLEW_ROOT}/lib/Release/x64
    )
    
    # Add executable
    add_executable(SceneDebugger ${SOURCES})
    
    # Link libraries
    target_link_libraries(SceneDebugger
        ${GLFW_LIBRARY}
        ${GLEW_LIBRARY}
        ${OPENGL_LIBRARIES}
    )
    
    # Include GLM
    target_include_directories(SceneDebugger PRIVATE ${GLM_INCLUDE_DIR})
else()
    # Unix-like systems
    # Find GLFW
    find_package(PkgConfig REQUIRED)
    pkg_search_module(GLFW REQUIRED glfw3)
    
    # Find GLEW
    find_package(GLEW REQUIRED)
    
    # Find GLM
    find_package(glm REQUIRED)
    
    # Include directories
    include_directories(
        ${GLFW_INCLUDE_DIRS}
        ${GLEW_INCLUDE_DIRS}
        ${GLM_INCLUDE_DIRS}
    )
    
    # Add executable
    add_executable(SceneDebugger ${SOURCES})
    
    # Link libraries
    target_link_libraries(SceneDebugger
        ${GLFW_LIBRARIES}
        ${GLEW_LIBRARIES}
        ${OPENGL_LIBRARIES}
    )
    
    # Include GLM
    target_include_directories(SceneDebugger PRIVATE ${GLM_INCLUDE_DIRS})
endif()

# Copy shader files to the output directory
add_custom_command(TARGET SceneDebugger POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${PROJECT_SOURCE_DIR}/vertex_shader.glsl"
    "${PROJECT_SOURCE_DIR}/fragment_shader.glsl"
    $<TARGET_FILE_DIR:SceneDebugger>
)
