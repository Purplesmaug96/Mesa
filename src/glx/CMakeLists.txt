# Mesa3D - src/glx/ (mirror of meson.build)
# SPDX-License-Identifier: MIT

set(inc_st_dri "${CMAKE_SOURCE_DIR}/src/gallium/frontends/dri")
set(dir_mesa_glapi_gen "${CMAKE_BINARY_DIR}/src/mesa/glapi/glapi/gen")

set(files_libglx
  clientattrib.c
  clientinfo.c
  compsize.c
  create_context.c
  dri_common.c
  dri_common.h
  dri_common_query_renderer.c
  drisw_glx.c
  drisw_priv.h
  eval.c
  glxclient.h
  glxcmds.c
  glxconfig.c
  glxconfig.h
  glxcurrent.c
  glx_error.c
  glx_error.h
  glxext.c
  glxextensions.c
  glxextensions.h
  glxhash.c
  glxhash.h
  glx_pbuffer.c
  glx_query.c
  indirect_glx.c
  indirect_init.h
  indirect_texture_compression.c
  indirect_transpose_matrix.c
  indirect_vertex_array.c
  indirect_vertex_array.h
  indirect_vertex_array_priv.h
  indirect_vertex_program.c
  indirect_window_pos.c
  packrender.h
  packsingle.h
  pixel.c
  pixelstore.c
  query_renderer.c
  render2.c
  renderpix.c
  single2.c
  vertarr.c
  xfont.c
)

set(extra_libs_libglx)
set(extra_deps_libgl)
set(extra_ld_args_libgl)
set(c_args_libglx)

if(with_dri_platform STREQUAL "drm")
  list(APPEND files_libglx dri3_glx.c dri3_priv.h)
endif()

if(with_glvnd)
  set(gl_lib_name "GLX_${glvnd_vendor_name}")
  set(gl_lib_version 0.0.0)
  list(APPEND files_libglx
    g_glxglvnddispatchfuncs.c
    g_glxglvnddispatchindices.h
    glxglvnd.c
    glxglvnd.h
    glxglvnddispatchfuncs.h)
else()
  set(gl_lib_name GL)
  set(gl_lib_version 1.2.0)
endif()

set(glx_gallium_link)
if(with_dri)
  list(APPEND glx_gallium_link ${libgallium_dri})
endif()

# libglx (static)
add_library(glx STATIC ${files_libglx} ${glx_generated} "${main_dispatch_h}")
set_target_properties(glx PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  C_VISIBILITY_PRESET hidden
  EXCLUDE_FROM_ALL ON)
target_compile_options(glx PRIVATE ${c_args_libglx})
target_include_directories(glx PRIVATE
  "${inc_include}" "${inc_src}" "${inc_glapi}" "${inc_loader}" "${inc_loader_x11}"
  "${inc_gallium}" "${inc_mesa}" "${inc_st_dri}" "${inc_gallium_aux}"
  "${dir_mesa_glapi_gen}")
target_link_libraries(glx PRIVATE
  loader loader_x11 ${extra_libs_libglx} ${glx_gallium_link}
  idep_mesautil idep_xmlconfig
  ${dep_libdrm} ${dep_glproto} ${dep_x11} ${dep_xext} ${dep_glvnd} ${dep_xxf86vm} ${dep_xshmfence})
add_dependencies(glx
  gen_main_dispatch_h
  gen_glx_indirect.c gen_glx_indirect.h gen_glx_indirect_init.c
  gen_glx_indirect_size.h gen_glx_indirect_size.c)

# libglapi_bridge contains GL functions that we need to export from libGL.so.
# Other than that, we don't need it.
if(with_glvnd)
  set(libgl_link)
else()
  set(libgl_link glapi_bridge)
endif()

set(_libgl_whole)
foreach(_gl IN ITEMS glx ${libgl_link})
  list(APPEND _libgl_whole -Wl,--whole-archive "$<TARGET_LINKER_FILE:${_gl}>" -Wl,--no-whole-archive)
endforeach()

# libGL / libGLX_<vendor> (shared)
add_library(${gl_lib_name} SHARED "${CMAKE_CURRENT_SOURCE_DIR}/libgl_stub.c")
set_target_properties(${gl_lib_name} PROPERTIES
  VERSION ${gl_lib_version}
  SOVERSION 0
  OUTPUT_NAME ${gl_lib_name})
target_link_libraries(${gl_lib_name} PRIVATE ${_libgl_whole})
target_link_options(${gl_lib_name} PRIVATE
  ${ld_args_bsymbolic} ${ld_args_gc_sections} ${extra_ld_args_libgl})
target_link_libraries(${gl_lib_name} PRIVATE
  ${dep_libdrm} ${dep_dl} ${dep_m} ${dep_thread}
  ${dep_x11} ${dep_xcb_glx} ${dep_xcb} ${dep_x11_xcb} ${dep_xext}
  ${dep_xxf86vm} ${dep_xcb_shm} ${dep_glproto} ${dep_xshmfence}
  ${extra_deps_libgl} ${glx_gallium_link}
  loader loader_x11 ${extra_libs_libglx}
  idep_mesautil idep_xmlconfig)
set_target_properties(${gl_lib_name} PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/glx")
install(TARGETS ${gl_lib_name} LIBRARY DESTINATION "${libdir}")

set(libgl "${gl_lib_name}")