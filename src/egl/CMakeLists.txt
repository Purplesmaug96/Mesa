# Mesa3D - src/egl/ (mirror of meson.build)
# SPDX-License-Identifier: MIT

set(inc_egl "${CMAKE_CURRENT_SOURCE_DIR};${CMAKE_CURRENT_SOURCE_DIR}/main")
set(dir_egl_gen "${CMAKE_BINARY_DIR}/src/egl")

set(c_args_for_egl)
set(cpp_args_for_egl)
set(link_args_for_egl)
set(link_deps_for_egl)
set(link_for_egl)
if(with_dri)
  list(APPEND link_for_egl ${libgallium_dri})
endif()
set(deps_for_egl)
set(incs_for_egl "${inc_include};${inc_src};${inc_egl}")

# gen_egl_dispatch args (--bind-wl-display)
set(_gen_egl_args)
if(with_wayland_bind_display)
  list(APPEND _gen_egl_args --bind-wl-display)
endif()

# egl_native_platform (meson: first of x11/haiku/android/surfaceless)
set(_egl_native_platform)
if(with_platform_x11)
  set(_egl_native_platform x11)
elseif(with_platform_haiku)
  set(_egl_native_platform haiku)
elseif(with_platform_android)
  set(_egl_native_platform android)
else()
  set(_egl_native_platform surfaceless)
endif()
if(MESA_OP_EGL_NATIVE_PLATFORM AND NOT MESA_OP_EGL_NATIVE_PLATFORM STREQUAL "auto")
  set(_egl_native_platform "${MESA_OP_EGL_NATIVE_PLATFORM}")
endif()

set(files_egl
  main/eglapi.c
  main/eglarray.c
  main/eglarray.h
  main/eglconfigdebug.c
  main/eglconfigdebug.h
  main/eglconfig.c
  main/eglconfig.h
  main/eglcontext.c
  main/eglcontext.h
  main/eglcurrent.c
  main/eglcurrent.h
  main/egldefines.h
  main/egldevice.c
  main/egldevice.h
  main/egldisplay.c
  main/egldisplay.h
  main/egldriver.h
  main/eglglobals.c
  main/eglglobals.h
  main/eglimage.c
  main/eglimage.h
  main/egllog.c
  main/egllog.h
  main/eglsurface.c
  main/eglsurface.h
  main/eglsync.c
  main/eglsync.h
  main/eglentrypoint.h
  main/egltypedefs.h
)

if(with_dri)
  list(APPEND files_egl
    drivers/dri2/egl_dri2.c
    drivers/dri2/egl_dri2.h
    "${sha1_h}"
    "${main_dispatch_h}")
  list(APPEND deps_for_egl idep_xmlconfig)
  list(APPEND link_for_egl loader)
  list(APPEND incs_for_egl ${inc_loader} ${inc_gallium} ${inc_gallium_aux} ${inc_mesa})
  list(APPEND incs_for_egl "${CMAKE_SOURCE_DIR}/src/gallium/frontends/dri")
  list(APPEND incs_for_egl "${CMAKE_BINARY_DIR}/src/mesa/glapi/glapi/gen")

  list(APPEND files_egl
    drivers/dri2/platform_device.c
    drivers/dri2/platform_surfaceless.c)

  if(with_platform_x11)
    list(APPEND files_egl drivers/dri2/platform_x11.c)
    list(APPEND incs_for_egl ${inc_loader_x11})
    list(APPEND link_for_egl loader_x11)
    if(with_dri_platform STREQUAL "drm")
      list(APPEND files_egl drivers/dri2/platform_x11_dri3.c)
    endif()
    list(APPEND deps_for_egl ${dep_x11_xcb} ${dep_xcb_xrandr} ${dep_xcb_xfixes} ${dep_xcb_shm})
  endif()
  if(with_gbm AND NOT with_platform_android)
    list(APPEND files_egl drivers/dri2/platform_drm.c)
    list(APPEND incs_for_egl "${CMAKE_SOURCE_DIR}/src/gbm/backends/dri")
    list(APPEND deps_for_egl ${dep_libdrm} ${dep_gbm})
  endif()
  if(with_platform_wayland)
    list(APPEND deps_for_egl ${dep_wayland_client} ${dep_wayland_server} ${dep_wayland_egl})
    list(APPEND link_for_egl loader_wayland_helper)
    list(APPEND incs_for_egl "${CMAKE_BINARY_DIR}/src/loader")
    list(APPEND files_egl drivers/dri2/platform_wayland.c
      "${CMAKE_BINARY_DIR}/src/loader/linux-dmabuf-unstable-v1-client-protocol.h"
      "${CMAKE_BINARY_DIR}/src/loader/linux-dmabuf-unstable-v1-protocol.c"
      "${CMAKE_BINARY_DIR}/src/loader/presentation-time-client-protocol.h"
      "${CMAKE_BINARY_DIR}/src/loader/presentation-time-protocol.c")
    if(with_wayland_bind_display)
      list(APPEND files_egl "${CMAKE_BINARY_DIR}/src/egl/wayland-drm-client-protocol.h")
      list(APPEND link_for_egl wayland_drm)
      list(APPEND incs_for_egl "${CMAKE_CURRENT_SOURCE_DIR}/wayland/wayland-drm")
      list(APPEND c_args_for_egl -DHAVE_BIND_WL_DISPLAY)
    endif()
  endif()
  if(with_platform_android)
    list(APPEND deps_for_egl ${dep_android} idep_u_gralloc)
    list(APPEND files_egl drivers/dri2/platform_android.c)
  endif()
endif()

check_symbol_exists(mincore "sys/mman.h" HAVE_MINCORE)
if(HAVE_MINCORE)
  list(APPEND c_args_for_egl -DHAVE_MINCORE)
endif()

# g_egldispatchstubs (only used with glvnd)
set(_egl_dispatch_c "${dir_egl_gen}/g_egldispatchstubs.c")
set(_egl_dispatch_h "${dir_egl_gen}/g_egldispatchstubs.h")
add_custom_command(
  OUTPUT "${_egl_dispatch_c}" "${_egl_dispatch_h}"
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/generate/gen_egl_dispatch.py
          ${_gen_egl_args}
          --source "${_egl_dispatch_c}" --header "${_egl_dispatch_h}"
          ${CMAKE_CURRENT_SOURCE_DIR}/generate/egl.xml ${CMAKE_CURRENT_SOURCE_DIR}/generate/egl_other.xml
  DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/generate/gen_egl_dispatch.py
    ${CMAKE_CURRENT_SOURCE_DIR}/generate/egl.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/generate/egl_other.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/generate/eglFunctionList.py
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "g_egldispatchstubs")
add_custom_target(gen_egldispatchstubs DEPENDS "${_egl_dispatch_c}" "${_egl_dispatch_h}")

if(with_glvnd)
  set(egl_lib_name "EGL_${glvnd_vendor_name}")
  set(egl_lib_version 0.0.0)
  set(egl_lib_soversion 0)
  list(APPEND deps_for_egl ${dep_glvnd})
  list(APPEND files_egl
    "${_egl_dispatch_c}" "${_egl_dispatch_h}"
    main/eglglvnd.c main/egldispatchstubs.c)
else()
  set(egl_lib_name "EGL${MESA_OP_EGL_LIB_SUFFIX}")
  set(egl_lib_version 1.0.0)
  set(egl_lib_soversion 1)
endif()

if(with_ld_version_script)
  if(with_glvnd)
    list(APPEND link_args_for_egl
      -Wl,--version-script "${CMAKE_CURRENT_SOURCE_DIR}/egl-glvnd.sym")
    list(APPEND link_deps_for_egl "${CMAKE_CURRENT_SOURCE_DIR}/egl-glvnd.sym")
  else()
    list(APPEND link_args_for_egl
      -Wl,--version-script "${CMAKE_CURRENT_SOURCE_DIR}/egl.sym")
    list(APPEND link_deps_for_egl "${CMAKE_CURRENT_SOURCE_DIR}/egl.sym")
  endif()
endif()

# -D_EGL_NATIVE_PLATFORM
if(_egl_native_platform)
  string(TOUPPER "${_egl_native_platform}" _egl_np_upper)
  list(APPEND c_args_for_egl "-D_EGL_NATIVE_PLATFORM=_EGL_PLATFORM_${_egl_np_upper}")
endif()

# libEGL / libEGL_<vendor> (shared)
add_library(egl SHARED ${files_egl})
set_target_properties(egl PROPERTIES
  VERSION ${egl_lib_version}
  SOVERSION ${egl_lib_soversion}
  C_VISIBILITY_PRESET hidden
  OUTPUT_NAME ${egl_lib_name})
target_include_directories(egl PRIVATE ${incs_for_egl})
target_compile_options(egl PRIVATE ${c_args_for_egl})
target_link_options(egl PRIVATE
  ${ld_args_bsymbolic} ${ld_args_gc_sections} ${link_args_for_egl})
set_property(TARGET egl APPEND PROPERTY LINK_DEPENDS ${link_deps_for_egl})
target_link_libraries(egl PRIVATE
  ${link_for_egl}
  ${deps_for_egl} ${dep_dl} ${dep_libdrm} ${dep_thread} idep_mesautil)
add_dependencies(egl gen_git_sha1 gen_main_dispatch_h gen_egldispatchstubs)
if(with_platform_wayland)
  add_dependencies(egl gen_wp_linux-dmabuf-unstable-v1 gen_wp_presentation-time)
endif()
set_target_properties(egl PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/egl")
install(TARGETS egl LIBRARY DESTINATION "${libdir}")