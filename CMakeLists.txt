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
cmake_minimum_required(VERSION 3.19)
project(rndrx LANGUAGES CXX VERSION 0.0.1)
set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
FetchContent_Declare(
    dear_imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        3573195
)
FetchContent_MakeAvailable(dear_imgui)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        b42009b
)
FetchContent_MakeAvailable(stb)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.3.2
)
FetchContent_GetProperties(glfw)
if(NOT glfw_POPULATED)
    FetchContent_Populate(glfw)
    option(GLFW_BUILD_EXAMPLES "Build the GLFW example programs" OFF)
    option(GLFW_BUILD_TESTS "Build the GLFW test programs" OFF)
    option(GLFW_BUILD_DOCS "Build the GLFW documentation" OFF)
    option(GLFW_INSTALL "Generate installation target" OFF)
    option(GLFW_DOCUMENT_INTERNALS "Include internals in documentation" OFF)
    add_subdirectory(${glfw_SOURCE_DIR} ${glfw_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        0.9.9.8
)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(
    tinyobjloader
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
    GIT_TAG        9173980
)
FetchContent_MakeAvailable(tinyobjloader)

FetchContent_Declare(
    dxc
    URL https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.6.2112/dxc_2021_12_08.zip
    URL_HASH MD5=e9e899c387f29ecaa1bfdeb1252775e3
)
FetchContent_MakeAvailable(dxc)
FetchContent_GetProperties(dxc SOURCE_DIR dxc_SOURCE_DIR)
add_library(dxc SHARED IMPORTED)
set_target_properties(
             dxc PROPERTIES
             IMPORTED_LOCATION "${dxc_SOURCE_DIR}/bin/x64/dxcompiler.dll;${dxc_SOURCE_DIR}/bin/x64/dxil.dll"
             IMPORTED_IMPLIB "${dxc_SOURCE_DIR}/lib/x64/dxcompiler.lib"
             INTERFACE_INCLUDE_DIRECTORIES "${dxc_SOURCE_DIR}/inc")

FetchContent_Declare(
    assets
    URL https://drive.google.com/u/0/uc?id=1XJCf1I9GzAqOXX42Z8YvpnM135vO4a4x&export=download&confirm=t&uuid=e1b175e1-61be-4dd8-92e4-37aa456d459b
    URL_HASH MD5=365e2117463501ebee5d521d90068229
)
FetchContent_MakeAvailable(assets)       
FetchContent_GetProperties(assets SOURCE_DIR assets_SOURCE_DIR)     

# Prepare a common staging directory for all targets
file(COPY ${assets_SOURCE_DIR}/models ${assets_SOURCE_DIR}/textures
     DESTINATION ${PROJECT_BINARY_DIR}/stage/assets)

file(COPY ${PROJECT_SOURCE_DIR}/assets/shaders
     DESTINATION ${PROJECT_BINARY_DIR}/stage/assets)

if(MSVC)
    add_compile_options(/EHsc)
endif()

add_subdirectory(dx12)