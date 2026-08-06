# Mesa3D - src/x11/ (mirror of src/x11/meson.build)
# SPDX-License-Identifier: MIT

set(inc_loader_x11 "${CMAKE_CURRENT_SOURCE_DIR}")

set(loader_x11_files x11_display.c)
if(with_dri_platform STREQUAL "drm")
  list(APPEND loader_x11_files x11_dri3.c)
endif()

mesa_static(loader_x11
  SOURCES ${loader_x11_files}
  INC "${inc_include};${inc_src}"
  DEP idep_mesautil ${dep_libdrm} ${dep_xcb_dri3} ${dep_xcb_present} ${dep_xcb_sync} ${dep_xshmfence} ${dep_xcb_xfixes} ${dep_x11})
set(inc_loader_x11 "${inc_loader_x11}" PARENT_SCOPE)
