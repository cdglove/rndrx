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

# If building for Windows
if(WIN32)
    FetchContent_Declare(
        dxc
        URL https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.7.2207/dxc_2022_07_18.zip
        URL_HASH MD5=e226cd9fee7f3b9015c0767c9035123c
    )
    FetchContent_MakeAvailable(dxc)
    FetchContent_GetProperties(dxc SOURCE_DIR dxc_SOURCE_DIR)
    add_library(dxc SHARED IMPORTED)
    set_target_properties(
        dxc PROPERTIES
        IMPORTED_LOCATION "${dxc_SOURCE_DIR}/bin/x64/dxcompiler.dll;${dxc_SOURCE_DIR}/bin/x64/dxil.dll"
        IMPORTED_IMPLIB "${dxc_SOURCE_DIR}/lib/x64/dxcompiler.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${dxc_SOURCE_DIR}/inc")
endif()
    
# If building from Windows.
if(CMAKE_HOST_SYSTEM_NAME EQUAL Windows)
  set(DXC_EXECUTABLE ${dxc_SOURCE_DIR}/bin/x64/dxc.exe)
else()
    # Copied from "${dxc_SOURCE_DIR}/cmake/caches/PredefinedParams.cmake"
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "")
    set(LLVM_APPEND_VC_REV ON CACHE BOOL "") 
    set(LLVM_DEFAULT_TARGET_TRIPLE "dxil-ms-dx" CACHE STRING "")
    set(LLVM_ENABLE_EH ON CACHE BOOL "") 
    set(LLVM_ENABLE_RTTI ON CACHE BOOL "") 
    set(LLVM_INCLUDE_DOCS OFF CACHE BOOL "") 
    set(LLVM_INCLUDE_EXAMPLES OFF CACHE BOOL "") 
    set(LLVM_INCLUDE_TESTS OFF CACHE BOOL "") 
    set(LLVM_OPTIMIZED_TABLEGEN OFF CACHE BOOL "") 
    set(LLVM_REQUIRES_EH ON CACHE BOOL "") 
    set(LLVM_REQUIRES_RTTI ON CACHE BOOL "") 
    set(LLVM_TARGETS_TO_BUILD "None" CACHE STRING "")
    set(LIBCLANG_BUILD_STATIC ON CACHE BOOL "") 
    set(CLANG_BUILD_EXAMPLES OFF CACHE BOOL "") 
    set(CLANG_CL OFF CACHE BOOL "") 
    set(CLANG_ENABLE_ARCMT OFF CACHE BOOL "") 
    set(CLANG_ENABLE_STATIC_ANALYZER OFF CACHE BOOL "") 
    set(CLANG_INCLUDE_TESTS OFF CACHE BOOL "") 
    set(HLSL_INCLUDE_TESTS ON CACHE BOOL "") 
    set(ENABLE_SPIRV_CODEGEN ON CACHE BOOL "") 
    set(SPIRV_BUILD_TESTS ON CACHE BOOL "")
    set(CMAKE_CXX_STANDARD 17)

    FetchContent_Declare(
        dxc
        GIT_REPOSITORY https://github.com/microsoft/DirectXShaderCompiler.git
        GIT_TAG        8492f4a
    )
    FetchContent_GetProperties(dxc BINARY_DIR dxc_BINARY_DIR)
    FetchContent_MakeAvailable(dxc)
    set(DXC_EXECUTABLE "${dxc_BINARY_DIR}/bin/dxc")
endif()