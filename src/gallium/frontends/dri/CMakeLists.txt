# Mesa3D - src/gallium/frontends/dri/ (mirror of meson.build)
# SPDX-License-Identifier: MIT

set(deps_for_libdri)

set(files_libdri
  dri_context.c
  dri_context.h
  dri_drawable.c
  dri_drawable.h
  dri_helpers.c
  dri_helpers.h
  dri_query_renderer.c
  dri_query_renderer.h
  dri_screen.c
  dri_screen.h
  dri_util.c
  dri_util.h)

if(with_platform_x11)
  list(APPEND deps_for_libdri ${dep_xcb})
  if(with_dri_platform STREQUAL "drm" OR with_dri_platform STREQUAL "pseudo-drm")
    list(APPEND deps_for_libdri ${dep_xcb_dri3} ${dep_xcb_present} ${dep_xcb_sync} ${dep_xshmfence} ${dep_xcb_xfixes})
    list(APPEND files_libdri loader_dri3_helper.c)
  endif()
endif()

if(with_dri)
  list(APPEND files_libdri drisw.c)
endif()

if(with_dri2)
  list(APPEND files_libdri dri2.c)
endif()

set(libdri_c_args)
if(with_gallium_zink AND NOT with_platform_android)
  if(with_glvnd)
    list(APPEND libdri_c_args "-DKOPPER_LIB_NAMES=\"libEGL_${glvnd_vendor_name} and libGLX_${glvnd_vendor_name}\"")
  else()
    list(APPEND libdri_c_args "-DKOPPER_LIB_NAMES=\"libEGL and libGLX\"")
  endif()
  list(APPEND files_libdri kopper.c)
else()
  list(APPEND files_libdri kopper_stubs.c)
endif()

# libdri
add_library(dri STATIC ${files_libdri} "${sha1_h}")
set_target_properties(dri PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden EXCLUDE_FROM_ALL ON)
target_compile_options(dri PRIVATE ${libdri_c_args})
target_include_directories(dri PRIVATE
  "${inc_include}" "${inc_util}" "${inc_mesa}" "${inc_src}" "${inc_gallium}"
  "${inc_gallium_aux}" "${inc_gallium_drivers}" "${inc_loader}")
target_link_libraries(dri PRIVATE ${dep_libdrm} idep_mesautil ${deps_for_libdri} loader)
add_dependencies(dri gen_git_sha1)
