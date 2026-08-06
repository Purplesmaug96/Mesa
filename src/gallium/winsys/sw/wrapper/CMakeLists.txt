# Mesa3D - src/gallium/winsys/sw/wrapper/ (mirror of meson.build)
# SPDX-License-Identifier: MIT

add_library(wsw STATIC "${CMAKE_CURRENT_SOURCE_DIR}/wrapper_sw_winsys.c" "${CMAKE_CURRENT_SOURCE_DIR}/wrapper_sw_winsys.h")
set_target_properties(wsw PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden EXCLUDE_FROM_ALL ON)
target_include_directories(wsw PRIVATE "${inc_gallium}" "${inc_include}" "${inc_src}" "${inc_gallium_aux}")
target_link_libraries(wsw PRIVATE idep_mesautil)
