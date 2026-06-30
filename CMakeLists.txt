cmake_minimum_required(VERSION 3.16)
project(HorrorEngine VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─────────────────────────────────────────────
#  Output directories
# ─────────────────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ─────────────────────────────────────────────
#  GLAD (vendored — no install needed)
# ─────────────────────────────────────────────
add_library(glad STATIC
    external/glad/src/glad.c
)
target_include_directories(glad PUBLIC external/glad/include)

# ─────────────────────────────────────────────
#  Find system packages
# ─────────────────────────────────────────────
find_package(glfw3  REQUIRED)
find_package(glm    REQUIRED)
find_package(assimp REQUIRED)

# OpenAL — name differs slightly per platform
find_package(OpenAL QUIET)
if (NOT OpenAL_FOUND)
    find_package(openal-soft QUIET)
endif()

# Intel Open Image Denoise — optional CPU denoiser for the ray tracer path
find_package(OpenImageDenoise QUIET)

# ─────────────────────────────────────────────
#  ImGui (vendored — copy files from imgui.io)
# ─────────────────────────────────────────────
# Place imgui source files in external/imgui/
# Download from: https://github.com/ocornut/imgui
if(EXISTS ${CMAKE_SOURCE_DIR}/external/imgui/imgui.cpp)
    add_library(imgui STATIC
        external/imgui/imgui.cpp
        external/imgui/imgui_draw.cpp
        external/imgui/imgui_tables.cpp
        external/imgui/imgui_widgets.cpp
        external/imgui/backends/imgui_impl_glfw.cpp
        external/imgui/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(imgui PUBLIC
        external/imgui
        external/imgui/backends
    )
    target_link_libraries(imgui PUBLIC glfw glad)
    set(IMGUI_AVAILABLE TRUE)
else()
    message(WARNING "ImGui not found in external/imgui/ — interactive UI disabled. "
                    "Download from https://github.com/ocornut/imgui and place there.")
    set(IMGUI_AVAILABLE FALSE)
endif()

# ─────────────────────────────────────────────
#  stb_image (single header, already vendored)
# ─────────────────────────────────────────────
# external/stb/stb_image.h  — download from:
# https://github.com/nothings/stb/blob/master/stb_image.h

# ─────────────────────────────────────────────
#  Engine source files
# ─────────────────────────────────────────────
set(ENGINE_SOURCES
    src/main.cpp
    src/Shader.cpp
    src/Camera.cpp
    src/Model.cpp
    src/Renderer.cpp
    src/ShadowMap.cpp
    src/PostProcess.cpp
    src/Scene.cpp
    src/CinematicEngine.cpp
    src/AudioManager.cpp
    src/RayTracer.cpp
)

add_executable(HorrorEngine ${ENGINE_SOURCES})

# ─────────────────────────────────────────────
#  Include paths
# ─────────────────────────────────────────────
target_include_directories(HorrorEngine PRIVATE
    include
    external/glad/include
    external/stb
    external/nanort
)

# ─────────────────────────────────────────────
#  Link libraries
# ─────────────────────────────────────────────
set(LINK_LIBS
    glad
    glfw
    glm::glm
    assimp::assimp
)

if(OpenAL_FOUND OR openal-soft_FOUND)
    list(APPEND LINK_LIBS OpenAL::OpenAL)
    target_compile_definitions(HorrorEngine PRIVATE HAS_OPENAL)
endif()

if(IMGUI_AVAILABLE)
    list(APPEND LINK_LIBS imgui)
    target_compile_definitions(HorrorEngine PRIVATE HAS_IMGUI)
endif()

if(OpenImageDenoise_FOUND)
    list(APPEND LINK_LIBS OpenImageDenoise)
    target_compile_definitions(HorrorEngine PRIVATE HAS_OIDN)
endif()

target_link_libraries(HorrorEngine PRIVATE ${LINK_LIBS})

# ─────────────────────────────────────────────
#  macOS: OpenGL + suppress deprecation warnings
# ─────────────────────────────────────────────
if(APPLE)
    target_link_libraries(HorrorEngine PRIVATE
        "-framework OpenGL"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
    )
    target_compile_definitions(HorrorEngine PRIVATE GL_SILENCE_DEPRECATION)
    message(STATUS "macOS detected — OpenGL 4.1 core profile will be used")
endif()

# ─────────────────────────────────────────────
#  Linux: link OpenGL directly
# ─────────────────────────────────────────────
if(UNIX AND NOT APPLE)
    find_package(OpenGL REQUIRED)
    target_link_libraries(HorrorEngine PRIVATE OpenGL::GL dl pthread)
endif()

# ─────────────────────────────────────────────
#  Copy shaders and assets next to the binary
# ─────────────────────────────────────────────
add_custom_command(TARGET HorrorEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:HorrorEngine>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:HorrorEngine>/assets
    COMMENT "Copying shaders and assets to build directory"
)

# ─────────────────────────────────────────────
#  Print config summary
# ─────────────────────────────────────────────
message(STATUS "")
message(STATUS "=== HorrorEngine Configuration ===")
message(STATUS "  OpenAL sound:      ${OpenAL_FOUND}")
message(STATUS "  ImGui UI:          ${IMGUI_AVAILABLE}")
message(STATUS "  OIDN denoiser:     ${OpenImageDenoise_FOUND}  (brew install openimagedenoise)")
message(STATUS "  Build type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "  Output:            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "==================================")
message(STATUS "")
