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
include(${PROJECT_SOURCE_DIR}/cmake/symlink.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/textures.cmake)

# Asset download
FetchContent_Declare(
    assets
    SOURCE_DIR ${PROJECT_SOURCE_DIR}/_data
    URL https://docs.google.com/uc?export=download&id=1bkiqadSEmR0Vohj04z6wvjbD6yRQ4FRT&confirm=t
    URL_HASH MD5=f6b6399aa119f37db90b7d0b1a6d5e6c
)
FetchContent_MakeAvailable(assets)       
FetchContent_GetProperties(assets SOURCE_DIR assets_SOURCE_DIR)

cmake_path(RELATIVE_PATH assets_SOURCE_DIR OUTPUT_VARIABLE assets_SOURCE_DIR_RELATIVE)

add_models(
    TARGET      common_models
    SOURCE_ROOT ${assets_SOURCE_DIR}
    SOURCES     models/cottage.obj
                models/sphere.obj)
                
add_symlink_assets(
    TARGET      sponza_models
    SOURCE_ROOT ${assets_SOURCE_DIR}
    SOURCES     models/NewSponza_Main_glTF_002.gltf
                models/NewSponza_Main_glTF_002.bin)

# add_symlink_assets(
#     TARGET      sponza_textures
#     SOURCE_ROOT ${assets_SOURCE_DIR}/textures
#     TARGET_ROOT assets/models/textures
#     SOURCES     arch_stone_wall_01_BaseColor.png
#                 arch_stone_wall_01_Metalness.png
#                 arch_stone_wall_01_Normal.png
#                 arch_stone_wall_01_Roughness.png
#                 arch_stone_wall_01_Roughnessarch_stone_wall_01_Metalness.png
#                 brickwall_01_BaseColor.png
#                 brickwall_01_Metalness.png
#                 brickwall_01_Normal.png
#                 brickwall_01_Roughness.png
#                 brickwall_01_Roughnessbrickwall_01_Metalness.png
#                 brickwall_02_BaseColor.png
#                 brickwall_02_Metalness.png
#                 brickwall_02_Normal.png
#                 brickwall_02_Roughness.png
#                 brickwall_02_Roughnessbrickwall_02_Metalness.png
#                 ceiling_plaster_01_BaseColor.png
#                 ceiling_plaster_01_Metalness.png
#                 ceiling_plaster_01_Normal.png
#                 ceiling_plaster_01_Roughness.png
#                 ceiling_plaster_01_Roughnessceiling_plaster_01_Metalness.png
#                 ceiling_plaster_02_BaseColor.png
#                 ceiling_plaster_02_Metalness.png
#                 ceiling_plaster_02_Normal.png
#                 ceiling_plaster_02_Roughness.png
#                 ceiling_plaster_02_Roughnessceiling_plaster_01_Metalness.png
#                 col_1stfloor_BaseColor.png
#                 col_1stfloor_Metalness.png
#                 col_1stfloor_Normal.png
#                 col_1stfloor_Roughness.png
#                 col_1stfloor_Roughnesscol_1stfloor_Metalness.png
#                 col_brickwall_01_BaseColor.png
#                 col_brickwall_01_Metalness.png
#                 col_brickwall_01_Normal.png
#                 col_brickwall_01_Roughness.png
#                 col_brickwall_01_Roughnesscol_brickwall_01_Metalness.png
#                 col_brickwall_01_Roughnesscolumn_brickwall_01_Metalness.png
#                 col_head_1stfloor_BaseColor.png
#                 col_head_1stfloor_Metalness.png
#                 col_head_1stfloor_Normal.png
#                 col_head_1stfloor_Roughness.png
#                 col_head_1stfloor_Roughnesscol_head_1stfloor_Metalness.png
#                 col_head_2ndfloor_02_BaseColor.png
#                 col_head_2ndfloor_02_Metalness.png
#                 col_head_2ndfloor_02_Normal.png
#                 col_head_2ndfloor_02_Roughness.png
#                 col_head_2ndfloor_02_Roughnesscol_head_2ndfloor_02_Metalness.png
#                 col_head_2ndfloor_03_BaseColor.png
#                 col_head_2ndfloor_03_Metalness.png
#                 col_head_2ndfloor_03_Normal.png
#                 col_head_2ndfloor_03_Roughness.png
#                 col_head_2ndfloor_03_Roughnesscol_head_2ndfloor_03_Metalness.png
#                 curtain_fabric_blue_BaseColor.png
#                 curtain_fabric_green_BaseColor.png
#                 curtain_fabric_Metalness.png
#                 curtain_fabric_Normal.png
#                 curtain_fabric_red_BaseColor.png
#                 curtain_fabric_Roughness.png
#                 dirt_decal_01.png
#                 dirt_decal_01_alpha.png
#                 dirt_decal_01_dirt_decal_01_mask_alpha_dirt_decal_Opacity.png
#                 dirt_decal_01_dirt_decal_01_mask_gltf_alpha_dirt_decal_Opacity.png
#                 dirt_decal_01_invmask.png
#                 dirt_decal_01_mask.png
#                 dirt_decal_01_mask_gltf.png
#                 dirt_decal_01_mask_usd.png
#                 door_stoneframe_01_BaseColor.png
#                 door_stoneframe_01_Metalness.png
#                 door_stoneframe_01_Normal.png
#                 door_stoneframe_01_Roughness.png
#                 door_stoneframe_01_Roughnessdoor_stoneframe_01_Metalness.png
#                 door_stoneframe_02_BaseColor.png
#                 door_stoneframe_02_Metalness.png
#                 door_stoneframe_02_Normal.png
#                 door_stoneframe_02_Roughness.png
#                 door_stoneframe_02_Roughnessdoor_stoneframe_02_Metalness.png
#                 floor_tiles_01_BaseColor.png
#                 floor_tiles_01_Metalness.png
#                 floor_tiles_01_Normal.png
#                 floor_tiles_01_Roughness.png
#                 floor_tiles_01_Roughnessfloor_tiles_01_Metalness.png
#                 kloppenheim_05_4k.hdr
#                 lionhead_01_BaseColor.png
#                 lionhead_01_Metalness.png
#                 lionhead_01_Normal.png
#                 lionhead_01_Roughness.png
#                 lionhead_01_Roughnesslionhead_01_Metalness.png
#                 metal_door_01_BaseColor.png
#                 metal_door_01_Metalness.png
#                 metal_door_01_Normal.png
#                 metal_door_01_Roughness.png
#                 metal_door_01_Roughnessmetal_door_01_Metalness.png
#                 ornament_01_BaseColor.png
#                 ornament_01_Metalness.png
#                 ornament_01_Normal.png
#                 ornament_01_Roughness.png
#                 ornament_01_Roughnessornament_01_Metalness.png
#                 roof_tiles_01_BaseColor.png
#                 roof_tiles_01_Metalness.png
#                 roof_tiles_01_Normal.png
#                 roof_tiles_01_Roughness.png
#                 roof_tiles_01_Roughnessroof_tiles_01_Metalness.png
#                 stone_01_tile_BaseColor.png
#                 stone_01_tile_Metalness.png
#                 stone_01_tile_Normal.png
#                 stone_01_tile_Roughness.png
#                 stone_01_tile_Roughnessstone_01_tile_Metalness.png
#                 stone_trims_01_BaseColor.png
#                 stone_trims_01_Metalness.png
#                 stone_trims_01_Normal.png
#                 stone_trims_01_Roughness.png
#                 stone_trims_01_Roughnessstone_trims_01_Metalness.png
#                 stone_trims_02_BaseColor.png
#                 stone_trims_02_Metalness.png
#                 stone_trims_02_Normal.png
#                 stone_trims_02_Roughness.png
#                 stone_trims_02_Roughnessstone_trims_02_Metalness.png
#                 stones_2ndfloor_01_BaseColor.png
#                 stones_2ndfloor_01_Metalness.png
#                 stones_2ndfloor_01_Normal.png
#                 stones_2ndfloor_01_Roughness.png
#                 stones_2ndfloor_01_Roughnessstones_2ndfloor_01_Metalness.png
#                 window_frame_01_BaseColor.png
#                 window_frame_01_Metalness.png
#                 window_frame_01_Normal.png
#                 window_frame_01_Roughness.png
#                 window_frame_01_Roughnesswindow_frame_01_Metalness.png
#                 wood_door_01_BaseColor.png
#                 wood_door_01_Metalness.png
#                 wood_door_01_Normal.png
#                 wood_door_01_Roughness.png
#                 wood_door_01_Roughnesswood_door_01_Metalness.png
#                 wood_tile_01_BaseColor.png
#                 wood_tile_01_Metalness.png
#                 wood_tile_01_Normal.png
#                 wood_tile_01_Roughness.png
#                 wood_tile_01_Roughnesswood_tile_01_Metalness.png)


add_custom_target(all_models ALL DEPENDS common_models sponza_models)