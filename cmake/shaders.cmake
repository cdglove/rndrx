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
function(_compile_shader_with_dxc SHADER_FILE_IN IL_FILE_OUT ENTRY_POINT SHADER_TYPE DXC_FLAGS)
  add_custom_command(OUTPUT "${IL_FILE_OUT}"
                    MAIN_DEPENDENCY ${SHADER_FILE_IN}
                    COMMAND ${DXC_EXECUTABLE} -nologo -E${ENTRY_POINT} -T${SHADER_TYPE}_6_0 
                                                              $<IF:$<CONFIG:DEBUG>,-Od,-O3> -Fo${IL_FILE_OUT}
                                                              ${DXC_FLAGS}
                                                              ${SHADER_FILE_IN}
                    COMMENT "Building shader ${IL_FILE_OUT} from ${SHADER_FILE_IN}"
                    VERBATIM)
endfunction()

function(_compile_shader_and_add_to_target SHADER_FILE_IN TARGET OUTPUT_EXTENSION ENTRY_POINT SHADER_TYPE DXC_FLAGS)
  get_filename_component(SOURCE_FILE_PATH ${SHADER_FILE_IN} DIRECTORY)
  get_filename_component(SOURCE_FILE_NAME ${SHADER_FILE_IN} NAME_WE)
  string(TOLOWER ${ENTRY_POINT} ENTRY_POINT_LOWER)
  set(OUTPUT_FILE_NAME ${SOURCE_FILE_NAME}.${ENTRY_POINT_LOWER}.${OUTPUT_EXTENSION})
  set(OUTPUT_FULL_PATH ${CMAKE_CURRENT_BINARY_DIR}/${SOURCE_FILE_PATH}/${OUTPUT_FILE_NAME})
  _compile_shader_with_dxc(${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE_IN} ${OUTPUT_FULL_PATH} ${ENTRY_POINT} ${SHADER_TYPE} "${DXC_FLAGS}")
  add_custom_target(${OUTPUT_FILE_NAME} DEPENDS ${OUTPUT_FULL_PATH})
  add_dependencies(${TARGET} ${OUTPUT_FILE_NAME})
endfunction()

function(add_vulkan_vertex_shader)
  cmake_parse_arguments(ADD_SHADER "" "SOURCE;ENTRY_POINT;TARGET" "" ${ARGN})
  _compile_shader_and_add_to_target(${ADD_SHADER_SOURCE} ${ADD_SHADER_TARGET} "spv" ${ADD_SHADER_ENTRY_POINT} "vs" "-spirv;-fspv-entrypoint-name=main")
endfunction()

function(add_vulkan_fragment_shader)
  cmake_parse_arguments(ADD_SHADER "" "SOURCE;ENTRY_POINT;TARGET" "" ${ARGN})
  _compile_shader_and_add_to_target(${ADD_SHADER_SOURCE} ${ADD_SHADER_TARGET} "spv" ${ADD_SHADER_ENTRY_POINT} "ps" "-spirv;-fspv-entrypoint-name=main")
endfunction()

function(add_vulkan_vertex_shaders)
  cmake_parse_arguments(ADD_SHADERS "" "TARGET" "SOURCES" ${ARGN})
  add_custom_target(${ADD_SHADERS_TARGET})
  foreach(FILE IN ITEMS ${ADD_SHADERS_SOURCES})
    add_vulkan_fragment_shader(TARGET ${ADD_SHADERS_TARGET} SOURCE ${FILE} ENTRY_POINT "VSMain")
  endforeach()
endfunction()

function(add_vulkan_fragment_shaders)
  cmake_parse_arguments(ADD_SHADERS "" "TARGET" "SOURCES" ${ARGN})
  add_custom_target(${ADD_SHADERS_TARGET})
  foreach(FILE IN ITEMS ${ADD_SHADERS_SOURCES})
    add_vulkan_fragment_shader(TARGET ${ADD_SHADERS_TARGET} SOURCE ${FILE} ENTRY_POINT "PSMain")
  endforeach()
endfunction()

function(add_d3d12_vertex_shader)
  cmake_parse_arguments(ADD_SHADER "" "SOURCE;ENTRY_POINT;TARGET" "" ${ARGN})
  _compile_shader_and_add_to_target(${ADD_SHADER_SOURCE} ${ADD_SHADER_TARGET} "dxil" ${ADD_SHADER_ENTRY_POINT} "vs" "")
endfunction()

function(add_d3d12_fragment_shader)
  cmake_parse_arguments(ADD_SHADER "" "SOURCE;ENTRY_POINT;TARGET" "" ${ARGN})
  _compile_shader_and_add_to_target(${ADD_SHADER_SOURCE} ${ADD_SHADER_TARGET} "dxil" ${ADD_SHADER_ENTRY_POINT} "ps" "")
endfunction()

function(add_d3d12_vertex_shaders)
  cmake_parse_arguments(ADD_SHADERS "" "TARGET" "SOURCES" ${ARGN})
  add_custom_target(${ADD_SHADERS_TARGET})
  foreach(FILE IN ITEMS ${ADD_SHADERS_SOURCES})
    add_vulkan_fragment_shader(TARGET ${ADD_SHADERS_TARGET} SOURCE ${FILE} ENTRY_POINT "VSMain")
  endforeach()
endfunction()

function(add_d3d12_fragment_shaders)
  cmake_parse_arguments(ADD_SHADERS "" "TARGET" "SOURCES" ${ARGN})
  add_custom_target(${ADD_SHADERS_TARGET})
  foreach(FILE IN ITEMS ${ADD_SHADERS_SOURCES})
    add_vulkan_fragment_shader(TARGET ${ADD_SHADERS_TARGET} SOURCE ${FILE} ENTRY_POINT "PSMain")
  endforeach()
endfunction()

