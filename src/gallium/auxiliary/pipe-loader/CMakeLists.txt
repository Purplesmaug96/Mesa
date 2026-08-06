# Mesa3D - src/gallium/auxiliary/pipe-loader/ (mirror of meson.build)
# SPDX-License-Identifier: MIT

set(files_pipe_loader
  pipe_loader.c
  pipe_loader.h
  pipe_loader_priv.h
  pipe_loader_sw.c
  driinfo_gallium.h)

set(libpipe_loader_defines)
set(libpipe_loader_links)

if(dep_libdrm)
  list(APPEND files_pipe_loader pipe_loader_drm.c)
  list(APPEND libpipe_loader_links loader)
endif()

set(renderonly_drivers_c_args)

# libpipe_loader_static
add_library(pipe_loader_static STATIC ${files_pipe_loader})
set_target_properties(pipe_loader_static PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden EXCLUDE_FROM_ALL ON)
target_compile_options(pipe_loader_static PRIVATE ${libpipe_loader_defines} ${renderonly_drivers_c_args})
target_include_directories(pipe_loader_static PRIVATE
  "${inc_util}" "${inc_loader}" "${inc_gallium}" "${inc_include}" "${inc_src}"
  "${inc_gallium_aux}" "${inc_gallium_winsys}" "${inc_gallium_drivers}")
target_link_libraries(pipe_loader_static PRIVATE ${libpipe_loader_links} ${dep_libdrm} idep_xmlconfig idep_mesautil)

set(files_pipe_loader "${files_pipe_loader}" PARENT_SCOPE)
set(libpipe_loader_static pipe_loader_static PARENT_SCOPE)
set(libpipe_loader_links "${libpipe_loader_links}" PARENT_SCOPE)
