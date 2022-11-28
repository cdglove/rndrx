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

# Asset download
FetchContent_Declare(
    assets
    URL https://drive.google.com/u/0/uc?id=1XJCf1I9GzAqOXX42Z8YvpnM135vO4a4x&export=download&confirm=t&uuid=e1b175e1-61be-4dd8-92e4-37aa456d459b
    URL_HASH MD5=365e2117463501ebee5d521d90068229
)
FetchContent_MakeAvailable(assets)       
FetchContent_GetProperties(assets SOURCE_DIR assets_SOURCE_DIR)

cmake_path(RELATIVE_PATH assets_SOURCE_DIR OUTPUT_VARIABLE assets_SOURCE_DIR_RELATIVE)

add_models(TARGET      common_models
           SOURCE_ROOT ${assets_SOURCE_DIR}
           SOURCES     models/cottage.obj
                       models/sphere.obj
)

add_custom_target(all_models ALL DEPENDS common_models)