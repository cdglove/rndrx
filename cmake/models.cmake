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
function(_compile_model MODEL_FILE_IN MODEL_FILE_OUT COMPILER_FLAGS)
  add_custom_command(OUTPUT "${MODEL_FILE_OUT}"
                     MAIN_DEPENDENCY ${MODEL_FILE_IN}
                     COMMAND ${CMAKE_COMMAND} -E copy ${MODEL_FILE_IN} ${MODEL_FILE_OUT}
                     COMMENT "Building model ${MODEL_FILE_OUT} from ${MODEL_FILE_IN}"
                     VERBATIM)
endfunction()

function(_compile_model_and_add_to_target MODEL_FILE_IN TARGET COMPILER_FLAGS)
  get_filename_component(SOURCE_FILE_PATH ${SHADER_FILE_IN} DIRECTORY)
  get_filename_component(SOURCE_FILE_NAME ${SHADER_FILE_IN} NAME_WE)
  set(OUTPUT_FILE_NAME ${SOURCE_FILE_NAME}.model)
  set(OUTPUT_FULL_PATH ${CMAKE_CURRENT_BINARY_DIR}/${SOURCE_FILE_PATH}/${OUTPUT_FILE_NAME})
  _compile_model(${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE_IN} ${OUTPUT_FULL_PATH} "${COMPILER_FLAGS}")
  add_custom_target(${OUTPUT_FILE_NAME} DEPENDS ${OUTPUT_FULL_PATH})
  add_dependencies(${TARGET} ${OUTPUT_FILE_NAME})
endfunction()

function(add_model)
  cmake_parse_arguments(ADD_MODEL "" "SOURCE;TARGET" "" ${ARGN})
  _compile_model_and_add_to_target(${ADD_MODEL_SOURCE} ${ADD_MODEL_TARGET})
endfunction()

function(add_models)
  cmake_parse_arguments(ADD_MODELS "" "TARGET" "SOURCES" ${ARGN})
  add_custom_target(${ADD_MODELS_TARGET})
  foreach(FILE IN LISTS ${ADD_MODELS_SOURCES})
    add_vulkan_fragment_shader(TARGET ${ADD_MODELS_TARGET} SOURCE ${FILE})
  endforeach()
endfunction()