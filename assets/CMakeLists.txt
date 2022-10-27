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
include(${PROJECT_SOURCE_DIR}/cmake/models.cmake)

# Build step for common assets is just a copy for now.
file(COPY ${assets_SOURCE_DIR}/models 
          ${assets_SOURCE_DIR}/textures
     DESTINATION ${PROJECT_BINARY_DIR}/common/assets
) 

add_models(TARGET  common_models 
           SOURCES ${assets_SOURCE_DIR}/models/cottage.obj
                   ${assets_SOURCE_DIR}/models/sphere.obj
)