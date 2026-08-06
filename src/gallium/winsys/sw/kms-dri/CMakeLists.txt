# Mesa3D - src/gallium/winsys/sw/kms-dri/ (mirror of meson.build)
# SPDX-License-Identifier: MIT

add_library(swkmsdri STATIC "${CMAKE_CURRENT_SOURCE_DIR}/kms_dri_sw_winsys.c" "${CMAKE_CURRENT_SOURCE_DIR}/kms_dri_sw_winsys.h")
set_target_properties(swkmsdri PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden EXCLUDE_FROM_ALL ON)
target_include_directories(swkmsdri PRIVATE "${inc_gallium}" "${inc_include}" "${inc_src}" "${inc_gallium_aux}")
target_link_libraries(swkmsdri PRIVATE ${dep_libdrm} idep_mesautil)
