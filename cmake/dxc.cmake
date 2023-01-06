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
  
# If building from Windows we can use prebuilt dxc
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    FetchContent_Declare(
        dxc
        URL https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.7.2207/dxc_2022_07_18.zip
        URL_HASH MD5=e226cd9fee7f3b9015c0767c9035123c
    )
    FetchContent_MakeAvailable(dxc)
    FetchContent_GetProperties(dxc SOURCE_DIR dxc_SOURCE_DIR)
    add_executable(dxc IMPORTED )
    set_property(TARGET dxc 
                 PROPERTY IMPORTED_LOCATION "${dxc_SOURCE_DIR}/bin/x64/dxc.exe")
    add_library(dxcompiler SHARED IMPORTED)
    set_target_properties(
        dxcompiler PROPERTIES
        IMPORTED_LOCATION "${dxc_SOURCE_DIR}/bin/x64/dxcompiler.dll;${dxc_SOURCE_DIR}/bin/x64/dxil.dll"
        IMPORTED_IMPLIB "${dxc_SOURCE_DIR}/lib/x64/dxcompiler.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${dxc_SOURCE_DIR}/inc")
else()
    include(ExternalProject)
    set(dxc_SOURCE_DIR ${CMAKE_BINARY_DIR}/dxc-prefix/src/dxc) 
    set(dxc_BINARY_DIR ${CMAKE_BINARY_DIR}/dxc-prefix/src/dxc-build)
    ExternalProject_Add(dxc
        GIT_REPOSITORY      https://github.com/microsoft/DirectXShaderCompiler.git
        GIT_TAG             2168dcb
        CONFIGURE_COMMAND   ${CMAKE_COMMAND} -G Ninja ${dxc_SOURCE_DIR}
                                             -DCMAKE_BUILD_TYPE=Release
                                             -C ${dxc_SOURCE_DIR}/cmake/caches/PredefinedParams.cmake    
        INSTALL_COMMAND     "")
    add_executable(dxc IMPORTED )
    set_property(TARGET dxc 
                 PROPERTY IMPORTED_LOCATION "${dxc_BINARY_DIR}/bin/dxc")
endif()