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

include(FetchContent)
FetchContent_Declare(
    dear_imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.89.1
)
FetchContent_MakeAvailable(dear_imgui)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        8b5f1f3
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
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG        v2.7.0
)
FetchContent_GetProperties(tinygltf)
if(NOT tinygltf_POPULATED)
    FetchContent_Populate(tinygltf)
    set(TINYGLTF_HEADER_ONLY ON CACHE INTERNAL "" FORCE)
    set(TINYGLTF_INSTALL OFF CACHE INTERNAL "" FORCE)
    add_subdirectory(${tinygltf_SOURCE_DIR} ${tinygltf_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

add_library(rndrx-common 
    bounding_box.cpp
    config.cpp
    frame_graph_description.cpp
    tiny_gltf_impl.cpp)

target_include_directories(rndrx-common
    PUBLIC
    ${PROJECT_SOURCE_DIR}/include)

# When loading large files this file need to be optimised otherwise
# debugging time is too long. 
set_source_files_properties(tiny_gltf_impl.cpp 
    PROPERTIES 
    COMPILE_FLAGS -O2)

target_link_libraries(rndrx-common 
    PUBLIC 
    tinygltf
    glm
    Vulkan::Vulkan # Temporary: Should go away once we abstract image types, etc.
)

target_compile_definitions(rndrx-common
    PUBLIC
    STBI_MSC_SECURE_CRT=1
    TINYGLTF_NO_STB_IMAGE_WRITE=1
    TINYGLTF_USE_CPP14=1
    GLM_FORCE_DEFAULT_ALIGNED_GENTYPES=1
    GLM_FORCE_DEPTH_ZERO_TO_ONE=1
    GLM_FORCE_RADIANS=1)

if(WIN32)
    add_subdirectory(dx12)
endif()
add_subdirectory(vulkan)
