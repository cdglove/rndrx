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

option(SPIRV_REFLECT_EXECUTABLE    "" OFF)
option(SPIRV_REFLECT_EXAMPLES      "" OFF)
option(SPIRV_REFLECT_STATIC_LIB    "" ON)
FetchContent_Declare(
    spirv_reflect
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Reflect.git
    GIT_TAG sdk-1.3.224.1)
FetchContent_MakeAvailable(spirv_reflect)

include(${PROJECT_SOURCE_DIR}/cmake/shaders.cmake)

add_custom_target(vulkan_shaders)
macro(add_vertex_shader SHADER_FILE ENTRY_POINT)
  get_filename_component(FILE_WE ${SHADER_FILE} NAME_WE)
  set(TARGET_NAME ${FILE_WE}.${ENTRY_POINT}.spirv)
  add_vulkan_vertex_shader(${TARGET_NAME} ${SHADER_FILE} ${ENTRY_POINT})
  add_dependencies(vulkan_shaders ${TARGET_NAME})
endmacro()

macro(add_fragment_shader SHADER_FILE ENTRY_POINT)
  get_filename_component(FILE_WE ${SHADER_FILE} NAME_WE)
  set(TARGET_NAME ${FILE_WE}.${ENTRY_POINT}.spirv)
  add_vulkan_fragment_shader(${TARGET_NAME} ${SHADER_FILE} ${ENTRY_POINT})
  add_dependencies(vulkan_shaders ${TARGET_NAME})
endmacro()

add_vertex_shader(assets/shaders/fullscreen_quad.hlsl VSMain)
add_vertex_shader(assets/shaders/static_model.hlsl VSMain)
add_fragment_shader(assets/shaders/fullscreen_quad.hlsl CopyImageInv)
add_fragment_shader(assets/shaders/static_model.hlsl Phong)

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
add_dependencies(rndrx-vulkan vulkan_shaders)

target_link_libraries(rndrx-vulkan 
    PUBLIC glfw glm tinyobjloader Vulkan::Vulkan spirv-reflect-static)

target_include_directories(rndrx-vulkan PUBLIC 
    ${PROJECT_SOURCE_DIR}/include
    ${dear_imgui_SOURCE_DIR} 
    ${dear_imgui_SOURCE_DIR}/backends
    ${dear_imgui_SOURCE_DIR}/misc/cpp
    ${stb_SOURCE_DIR})

target_compile_definitions(rndrx-vulkan PUBLIC
    _CRT_SECURE_NO_DEPRECATE
    _CRT_SECURE_NO_WARNINGS
    IMGUI_USE_STB_SPRINTF
    IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    $<$<CONFIG:DEBUG>:RNDRX_ENABLE_VULKAN_DEBUG_LAYER=1>
    $<$<CONFIG:DEBUG>:RNDRX_ENABLE_SHADER_DEBUGGING=1>)

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