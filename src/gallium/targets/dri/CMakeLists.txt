# Mesa3D - src/gallium/targets/dri/ (mirror of meson.build)
# SPDX-License-Identifier: MIT

set(gallium_dri_c_args)
set(gallium_dri_ld_args -Wl,--default-symver)
set(gallium_dri_link_depends)
set(gallium_dri_drivers)
set(gallium_dri_link_with)
set(gallium_dri_link_whole)

if(with_gallium_va)
  list(APPEND gallium_dri_link_with galliumvlwinsys)
  list(APPEND gallium_dri_link_whole libva_st)
endif()

# dri.sym (version script, all sym_config entries are empty here)
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/dri.sym.in"
  "${CMAKE_CURRENT_BINARY_DIR}/dri.sym"
  @ONLY)
set(_dri_sym "${CMAKE_CURRENT_BINARY_DIR}/dri.sym")

if(with_ld_version_script)
  list(APPEND gallium_dri_ld_args -Wl,--version-script "${_dri_sym}")
  list(APPEND gallium_dri_link_depends "${_dri_sym}")
endif()
if(with_ld_dynamic_list)
  list(APPEND gallium_dri_ld_args -Wl,--dynamic-list "${CMAKE_CURRENT_SOURCE_DIR}/../dri.dyn")
  list(APPEND gallium_dri_link_depends "${CMAKE_CURRENT_SOURCE_DIR}/../dri.dyn")
endif()

if(MESA_OP_UNVERSION_LIBGALLIUM OR with_platform_android)
  set(libgallium_name gallium_dri)
else()
  set(libgallium_name "gallium-${project_version}")
endif()

# libgallium_dri (shared)
add_library(${libgallium_name} SHARED "${CMAKE_CURRENT_SOURCE_DIR}/dri_target.c")
set_target_properties(${libgallium_name} PROPERTIES
  C_VISIBILITY_PRESET hidden
  OUTPUT_NAME ${libgallium_name})
target_include_directories(${libgallium_name} PRIVATE
  "${inc_include}" "${inc_src}" "${inc_mesa}" "${inc_gallium}" "${inc_gallium_aux}"
  "${inc_util}" "${inc_gallium_drivers}" "${inc_gallium_winsys}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../frontends/dri")
target_link_options(${libgallium_name} PRIVATE
  ${ld_args_build_id} ${ld_args_gc_sections} ${gallium_dri_ld_args})
target_link_libraries(${libgallium_name} PRIVATE
  mesa galliumvl gallium glapi pipe_loader_static ws_null wsw swdri ${libswkmsdri}
  ${gallium_dri_link_with}
  ${dep_libdrm} ${dep_llvm} ${dep_thread} idep_xmlconfig idep_mesautil
  ${driver_swrast}
  ${gallium_dri_link_whole})
# meson link_whole: libdri must be linked with --whole-archive
target_link_libraries(${libgallium_name} PRIVATE
  -Wl,--whole-archive dri -Wl,--no-whole-archive)
set_property(TARGET ${libgallium_name} APPEND PROPERTY LINK_DEPENDS ${gallium_dri_link_depends})
set_target_properties(${libgallium_name} PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/gallium/targets/dri")
install(TARGETS ${libgallium_name} LIBRARY DESTINATION "${libdir}")

set(shared_glapi_lib "${libgallium_name}")
