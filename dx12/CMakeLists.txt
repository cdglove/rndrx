# Copyright 2020 Google Inc. All Rights Reserved.
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
set(SOURCES 
    glfw_main.cpp
    ${dear_imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp 
    ${dear_imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp 
    ${dear_imgui_SOURCE_DIR}/imgui.cpp 
    ${dear_imgui_SOURCE_DIR}/imgui_draw.cpp 
    ${dear_imgui_SOURCE_DIR}/imgui_tables.cpp 
    ${dear_imgui_SOURCE_DIR}/imgui_demo.cpp 
    ${dear_imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${dear_imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp)

add_executable(rndrx-dx12 ${SOURCES})

target_link_libraries(rndrx-dx12 
    PUBLIC d3d12 dxgi glfw glm tinyobjloader dxc
    $<$<CONFIG:DEBUG>:dxguid>)

target_include_directories(rndrx-dx12 PUBLIC 
    ${dear_imgui_SOURCE_DIR} 
    ${dear_imgui_SOURCE_DIR}/backends
    ${dear_imgui_SOURCE_DIR}/misc/cpp
    ${stb_SOURCE_DIR})

target_compile_definitions(rndrx-dx12 PUBLIC
    _CRT_SECURE_NO_DEPRECATE
    _CRT_SECURE_NO_WARNINGS
    IMGUI_USE_STB_SPRINTF
    IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    $<$<CONFIG:DEBUG>:RNDRX_ENABLE_DX12_DEBUG_LAYER=1>
    $<$<CONFIG:DEBUG>:RNDRX_ENABLE_SHADER_DEBUGGING=1>)

add_custom_command(
    TARGET rndrx-dx12 POST_BUILD
    # Create asset layout in project directory for easy debugging
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:rndrx-dx12> ${PROJECT_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:rndrx-dx12> ${PROJECT_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${assets_SOURCE_DIR} ${PROJECT_BINARY_DIR}/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/assets ${PROJECT_BINARY_DIR}/assets
    # Create staging directory for easy deployment.
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:rndrx-dx12> ${PROJECT_BINARY_DIR}/stage
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/assets ${PROJECT_BINARY_DIR}/stage/assets
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:rndrx-dx12> ${PROJECT_BINARY_DIR}/stage
    COMMAND_EXPAND_LISTS)
