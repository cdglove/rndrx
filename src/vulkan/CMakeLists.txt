# Copyright 2022 Google Inc. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
find_package(Vulkan REQUIRED)

include(${PROJECT_SOURCE_DIR}/cmake/shaders.cmake)

option(SPIRV_REFLECT_EXECUTABLE    "" OFF)
option(SPIRV_REFLECT_EXAMPLES      "" OFF)
option(SPIRV_REFLECT_STATIC_LIB    "" ON)
FetchContent_Declare(
    spirv_reflect
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Reflect.git
    GIT_TAG sdk-1.3.224.1)
FetchContent_MakeAvailable(spirv_reflect)

add_custom_target(vulkan_shaders ALL)
add_vulkan_vertex_shader(TARGET vulkan_shaders SOURCE ../../assets/shaders/fullscreen_quad.hlsl ENTRY_POINT VSMain)
add_vulkan_vertex_shader(TARGET vulkan_shaders SOURCE ../../assets/shaders/static_model.hlsl ENTRY_POINT VSMain)
add_vulkan_fragment_shader(TARGET vulkan_shaders SOURCE ../../assets/shaders/fullscreen_quad.hlsl ENTRY_POINT CopyImageOpaque)
add_vulkan_fragment_shader(TARGET vulkan_shaders SOURCE ../../assets/shaders/fullscreen_quad.hlsl ENTRY_POINT BlendImage)
add_vulkan_fragment_shader(TARGET vulkan_shaders SOURCE ../../assets/shaders/fullscreen_quad.hlsl ENTRY_POINT BlendImageInv)
add_vulkan_fragment_shader(TARGET vulkan_shaders SOURCE ../../assets/shaders/static_model.hlsl ENTRY_POINT Phong)

set(SOURCES 
    main.cpp
    ${dear_imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp 
    ${dear_imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
    ${dear_imgui_SOURCE_DIR}/imgui.cpp 
    ${dear_imgui_SOURCE_DIR}/imgui_draw.cpp 
    ${dear_imgui_SOURCE_DIR}/imgui_tables.cpp 
    ${dear_imgui_SOURCE_DIR}/imgui_demo.cpp 
    ${dear_imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${dear_imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp)

add_executable(rndrx-vulkan ${SOURCES})

target_link_libraries(rndrx-vulkan 
    PUBLIC glfw glm tinyobjloader Vulkan::Vulkan spirv-reflect-static)

target_include_directories(rndrx-vulkan PUBLIC 
    ${PROJECT_SOURCE_DIR}/include
    ${dear_imgui_SOURCE_DIR} 
    ${dear_imgui_SOURCE_DIR}/backends
    ${dear_imgui_SOURCE_DIR}/misc/cpp
    ${stb_SOURCE_DIR})

target_compile_definitions(rndrx-vulkan PUBLIC
    _CRT_SECURE_NO_DEPRECATE=1
    _CRT_SECURE_NO_WARNINGS=1
    IMGUI_USE_STB_SPRINTF=1
    IMGUI_DISABLE_OBSOLETE_FUNCTIONS=1
    NOMINMAX=1
    WIN32_LEAN_AND_MEAN=1
    GLFW_EXPOSE_NATIVE_WIN32
    GLFW_INCLUDE_VULKAN
    GLM_FORCE_RADIANS=1
    GLM_FORCE_DEPTH_ZERO_TO_ONE=1
    GLM_FORCE_DEFAULT_ALIGNED_GENTYPES=1
    $<$<CONFIG:DEBUG>:RNDRX_ENABLE_VULKAN_DEBUG_LAYER=1>
    $<$<CONFIG:DEBUG>:RNDRX_ENABLE_SHADER_DEBUGGING=1>)

target_precompile_headers(rndrx-vulkan PUBLIC
        <vulkan/vulkan_core.h>
        <algorithm>
        <array>
        <chrono>
        <filesystem>
        <functional>
        <glm/gtc/matrix_transform.hpp>
        <glm/gtx/fast_trigonometry.hpp>
        <glm/mat3x3.hpp>
        <glm/mat4x4.hpp>
        <glm/vec2.hpp>
        <glm/vec3.hpp>
        <glm/vec4.hpp>
        <iostream>
        <iterator>
        <limits>
        <memory>
        <queue>
        <ranges>
        <sstream>
        <stdexcept>
        <string>
        <string_view>
        <unordered_map>
        <vector>
        <vulkan/vulkan.hpp>
        <vulkan/vulkan_enums.hpp>
        <vulkan/vulkan_handles.hpp>
        <vulkan/vulkan_raii.hpp>
        <vulkan/vulkan_structs.hpp>
    )

# add_custom_command(
#     TARGET rndrx-vulkan POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:rndrx-vulkan> ${PROJECT_BINARY_DIR}/stage
#     COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:rndrx-vulkan> ${PROJECT_BINARY_DIR}/stage
#     COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:rndrx-vulkan> ${PROJECT_BINARY_DIR}/vulkan
#     # When debugging, the working directory is set to the project dir, not the 
#     # executable dir, so we need to put the ddls there too for them to be found.
#     COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:rndrx-vulkan> ${PROJECT_BINARY_DIR}
#     COMMAND_EXPAND_LISTS)

# if(MSVC)
#     add_custom_command(
#         TARGET rndrx-vulkan POST_BUILD
#         COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_PDB_FILE:rndrx-vulkan> ${PROJECT_BINARY_DIR}/stage
#         COMMAND_EXPAND_LISTS)
# endif()