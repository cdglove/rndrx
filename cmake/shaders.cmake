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
function(_compile_shader_with_dxc TARGET_NAME SHADER_FILE ENTRY_POINT SHADER_TYPE FLAGS)
endfunction()

macro(add_d3d12_shader TARGET_NAME SHADER_FILE ENTRY_POINT SHADER_TYPE)
  get_filename_component(FILE_WE ${SHADER_FILE} NAME_WE)
  get_filename_component(FILE_PATH ${SHADER_FILE} DIRECTORY)
  file(MAKE_DIRECTORY "${FILE_PATH}")
  string(TOLOWER ${ENTRY_POINT} ENTRY_POINT_LOWER)
  set(HLSL_IN   ${PROJECT_SOURCE_DIR}/${SHADER_FILE})
  set(DXIL_OUT ${FILE_PATH}/${FILE_WE}.${ENTRY_POINT_LOWER}.dxil)
  add_custom_command(OUTPUT "${DXIL_OUT}"
                     MAIN_DEPENDENCY ${HLSL_IN}
                     COMMAND ${dxc_SOURCE_DIR}/bin/x64/dxc.exe -nologo -E${ENTRY_POINT} -T${SHADER_TYPE}_6_0 
                                                               $<IF:$<CONFIG:DEBUG>,-Od,-O3> -Fo${DXIL_OUT}
                                                               ${HLSL_IN}
                     COMMENT "Building ${DXIL_OUT} from ${SHADER_FILE}"
                     WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                     VERBATIM)
  add_custom_target(${TARGET_NAME} DEPENDS "${DXIL_OUT}")
endmacro()

macro(add_d3d12_vertex_shader TARGET_NAME SHADER_FILE ENTRY_POINT)
  add_d3d12_shader(${TARGET_NAME} ${SHADER_FILE} ${ENTRY_POINT}  "vs")
endmacro()

macro(add_d3d12_fragment_shader TARGET_NAME SHADER_FILE ENTRY_POINT)
  add_d3d12_shader(${TARGET_NAME} ${SHADER_FILE} ${ENTRY_POINT}  "ps")
endmacro()

macro(add_vulkan_shader TARGET_NAME SHADER_FILE ENTRY_POINT SHADER_TYPE)
  get_filename_component(FILE_WE ${SHADER_FILE} NAME_WE)
  get_filename_component(FILE_PATH ${SHADER_FILE} DIRECTORY)
  file(MAKE_DIRECTORY "${FILE_PATH}")
  string(TOLOWER ${ENTRY_POINT} ENTRY_POINT_LOWER)
  set(HLSL_IN   ${PROJECT_SOURCE_DIR}/${SHADER_FILE})
  set(SPIRV_OUT ${FILE_PATH}/${FILE_WE}.${ENTRY_POINT_LOWER}.spv)
  add_custom_command(OUTPUT "${SPIRV_OUT}"
                     MAIN_DEPENDENCY ${HLSL_IN}
                     COMMAND ${dxc_SOURCE_DIR}/bin/x64/dxc.exe -spirv -nologo -E${ENTRY_POINT} -T${SHADER_TYPE}_6_0 
                                                               $<IF:$<CONFIG:DEBUG>,-Od,-O3> -Fo${SPIRV_OUT}
                                                               ${HLSL_IN}
                     COMMENT "Building ${SPIRV_OUT} from ${SHADER_FILE}"
                     WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                     VERBATIM)
  add_custom_target(${TARGET_NAME} DEPENDS "${SPIRV_OUT}")
endmacro()

macro(add_vulkan_vertex_shader TARGET_NAME SHADER_FILE ENTRY_POINT)
  add_vulkan_shader(${TARGET_NAME} ${SHADER_FILE} ${ENTRY_POINT}  "vs")
endmacro()

macro(add_vulkan_fragment_shader TARGET_NAME SHADER_FILE ENTRY_POINT)
  add_vulkan_shader(${TARGET_NAME} ${SHADER_FILE} ${ENTRY_POINT}  "ps")
endmacro()

