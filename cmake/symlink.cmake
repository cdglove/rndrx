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
function(_symlink_file FILE_IN FILE_OUT)
  add_custom_command(OUTPUT "${FILE_OUT}"
                     MAIN_DEPENDENCY ${FILE_IN}
                     COMMAND ${CMAKE_COMMAND} -E create_symlink ${FILE_IN} ${FILE_OUT}
                     COMMENT "Symlinking model ${FILE_OUT} from ${FILE_IN}"
                     VERBATIM)
endfunction()

function(_symlink_file_and_add_to_target SOURCE_ROOT FILE_IN TARGET)
  get_filename_component(SOURCE_FILE_PATH ${FILE_IN} DIRECTORY)
  get_filename_component(SOURCE_FILE_NAME ${FILE_IN} NAME)
  set(OUTPUT_FILE_NAME ${SOURCE_FILE_NAME})
  set(OUTPUT_FULL_PATH ${CMAKE_CURRENT_BINARY_DIR}/${SOURCE_FILE_PATH}/${OUTPUT_FILE_NAME})
  set(INPUT_FULL_PATH  ${SOURCE_ROOT}/${FILE_IN})
  _symlink_file(${INPUT_FULL_PATH} ${OUTPUT_FULL_PATH})
  add_custom_target(${OUTPUT_FILE_NAME} DEPENDS ${OUTPUT_FULL_PATH})
  add_dependencies(${TARGET} ${OUTPUT_FILE_NAME})
endfunction()

function(add_symlink_asset)
  cmake_parse_arguments(SYMLINK_FILE "" "TARGET;SOURCE_ROOT;SOURCE" "" ${ARGN})
  _symlink_file_and_add_to_target(${SYMLINK_FILE_SOURCE_ROOT} ${SYMLINK_FILE_SOURCE} ${SYMLINK_FILE_TARGET})
endfunction()

function(add_symlink_assets)
  cmake_parse_arguments(SYMLINK_FILES "" "TARGET;SOURCE_ROOT" "SOURCES" ${ARGN})
  add_custom_target(${SYMLINK_FILES_TARGET})
  foreach(FILE IN ITEMS ${SYMLINK_FILES_SOURCES})
    add_symlink_asset(TARGET      ${SYMLINK_FILES_TARGET} 
                      SOURCE_ROOT ${SYMLINK_FILES_SOURCE_ROOT} 
                      SOURCE      ${FILE})
  endforeach()
endfunction()