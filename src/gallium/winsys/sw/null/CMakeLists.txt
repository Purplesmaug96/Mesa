# Mesa3D - src/gallium/winsys/sw/null/ (mirror of meson.build)
# SPDX-License-Identifier: MIT

add_library(ws_null STATIC "${CMAKE_CURRENT_SOURCE_DIR}/null_sw_winsys.c" "${CMAKE_CURRENT_SOURCE_DIR}/null_sw_winsys.h")
set_target_properties(ws_null PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden EXCLUDE_FROM_ALL ON)
target_include_directories(ws_null PRIVATE "${inc_gallium}" "${inc_src}" "${inc_gallium_aux}" "${inc_include}")
target_link_libraries(ws_null PRIVATE idep_mesautil)
