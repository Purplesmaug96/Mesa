# Mesa3D - src/gbm/ (mirror of meson.build)
# SPDX-License-Identifier: MIT

set(inc_gbm
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/main"
  "${CMAKE_CURRENT_SOURCE_DIR}/backends/dri")

set(files_gbm
  main/backend.c
  main/gbm.c
  ${CMAKE_CURRENT_SOURCE_DIR}/main/gbm.h
  ${CMAKE_CURRENT_SOURCE_DIR}/main/gbmint.h
  ${CMAKE_CURRENT_SOURCE_DIR}/main/backend.h)

set(gbm_backends_path "${CMAKE_INSTALL_PREFIX}/${libdir}/gbm")
set(args_gbm "-DDEFAULT_BACKENDS_PATH=\"${gbm_backends_path}\"")

set(libgbm_name "gbm")

# libgbm
add_library(gbm SHARED ${files_gbm})
set_target_properties(gbm PROPERTIES
  VERSION 1.0.0
  SOVERSION 1
  C_VISIBILITY_PRESET hidden
  OUTPUT_NAME ${libgbm_name})
target_include_directories(gbm PRIVATE
  ${inc_gbm} ${inc_loader} ${inc_gallium} ${inc_util} ${inc_include} ${inc_src})
target_compile_definitions(gbm PRIVATE ${args_gbm})
target_link_options(gbm PRIVATE ${ld_args_gc_sections})
target_link_libraries(gbm PRIVATE loader ${dep_libdrm} idep_xmlconfig)
install(TARGETS gbm LIBRARY DESTINATION "${libdir}")

# gbm_dri backend
set(deps_gbm_dri)
set(flags_gbm_dri)
if(with_platform_wayland)
  list(APPEND deps_gbm_dri ${dep_wayland_server})
  if(with_wayland_bind_display)
    list(APPEND flags_gbm_dri -DHAVE_BIND_WL_DISPLAY)
  endif()
endif()

add_library(dri_gbm SHARED
  backends/dri/gbm_dri.c
  backends/dri/gbm_driint.h)
set_target_properties(dri_gbm PROPERTIES
  C_VISIBILITY_PRESET hidden
  PREFIX ""
  OUTPUT_NAME "dri_gbm")
target_include_directories(dri_gbm PRIVATE
  ${inc_gallium} ${inc_mesa} ${inc_loader}
  "${CMAKE_SOURCE_DIR}/src/gallium/frontends/dri"
  ${inc_gallium_aux} ${inc_gbm})
target_compile_definitions(dri_gbm PRIVATE ${flags_gbm_dri})
target_link_options(dri_gbm PRIVATE ${ld_args_gc_sections})
target_link_libraries(dri_gbm PRIVATE
  loader ${libgallium_dri} ${deps_gbm_dri} ${dep_dl} gbm ${dep_libdrm} idep_mesautil idep_xmlconfig)
install(TARGETS dri_gbm LIBRARY DESTINATION "${libdir}/gbm")

# dep_gbm (mirror of meson declare_dependency)
set(dep_gbm gbm)
set(dep_gbm "${dep_gbm}" PARENT_SCOPE)
set(inc_gbm "${inc_gbm}" PARENT_SCOPE)