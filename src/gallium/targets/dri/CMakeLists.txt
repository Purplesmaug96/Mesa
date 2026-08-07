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
# meson link_whole: libdri first so its st_*/loader_* references pull
# members from the following static libs (mesa, gallium, ...). All local
# archives go in one --start-group so mutual references resolve regardless of
# order; explicit paths keep CMake from reordering target items out of it.
set(_gl_ring
  "${CMAKE_BINARY_DIR}/src/gallium/drivers/softpipe/libsoftpipe.a"
  "${CMAKE_BINARY_DIR}/src/mesa/libmesa.a"
  "${CMAKE_BINARY_DIR}/src/gallium/auxiliary/libgalliumvl.a"
  "${CMAKE_BINARY_DIR}/src/gallium/auxiliary/libgallium.a"
  "${CMAKE_BINARY_DIR}/src/mesa/glapi/shared-glapi/libglapi.a"
  "${CMAKE_BINARY_DIR}/src/gallium/auxiliary/pipe-loader/libpipe_loader_static.a"
  "${CMAKE_BINARY_DIR}/src/gallium/winsys/sw/null/libws_null.a"
  "${CMAKE_BINARY_DIR}/src/gallium/winsys/sw/wrapper/libwsw.a"
  "${CMAKE_BINARY_DIR}/src/gallium/winsys/sw/dri/libswdri.a"
  "${CMAKE_BINARY_DIR}/src/util/blake3/libblake3.a"
  "${CMAKE_BINARY_DIR}/src/mesa/libmesa_sse41.a"
  "${CMAKE_BINARY_DIR}/src/compiler/glsl/libglsl.a"
  "${CMAKE_BINARY_DIR}/src/compiler/glsl/glcpp/libglcpp.a"
  "${CMAKE_BINARY_DIR}/src/compiler/spirv/libvtn.a"
  "${CMAKE_BINARY_DIR}/src/util/libxmlconfig.a"
  "${CMAKE_BINARY_DIR}/src/loader/libloader.a"
  "${CMAKE_BINARY_DIR}/src/compiler/nir/libnir.a"
  "${CMAKE_BINARY_DIR}/src/util/libmesa_util.a"
  "${CMAKE_BINARY_DIR}/src/util/libmesa_util_clflush.a"
  "${CMAKE_BINARY_DIR}/src/util/libmesa_util_simd.a"
  "${CMAKE_BINARY_DIR}/src/c11/impl/libmesa_util_c11.a"
  "${CMAKE_BINARY_DIR}/src/compiler/libcompiler.a")
if(with_gallium_drisw_kms)
  list(APPEND _gl_ring "${CMAKE_BINARY_DIR}/src/gallium/winsys/sw/kms-dri/libswkmsdri.a")
endif()
if(with_gallium_va)
  list(APPEND _gl_ring
    "${CMAKE_BINARY_DIR}/src/gallium/auxiliary/vl/libgalliumvlwinsys.a"
    "${CMAKE_BINARY_DIR}/src/gallium/frontends/va/libva_st.a")
endif()

target_link_libraries(${libgallium_name} PRIVATE
  -Wl,--whole-archive dri -Wl,--no-whole-archive
  -Wl,--start-group ${_gl_ring} -Wl,--end-group
  ${gallium_dri_link_whole}
  ${dep_libdrm} ${dep_llvm} ${dep_thread} ${dep_spirv_tools} idep_xmlconfig idep_mesautil)
set_property(TARGET ${libgallium_name} APPEND PROPERTY LINK_DEPENDS ${gallium_dri_link_depends})
set_target_properties(${libgallium_name} PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/gallium/targets/dri")
install(TARGETS ${libgallium_name} LIBRARY DESTINATION "${libdir}")

set(shared_glapi_lib "${libgallium_name}")
set(libgallium_dri "${libgallium_name}" PARENT_SCOPE)
set(shared_glapi_lib "${libgallium_name}" PARENT_SCOPE)
