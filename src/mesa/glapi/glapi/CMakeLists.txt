# Mesa3D - src/mesa/glapi/glapi/ (mirror of src/mesa/glapi/glapi/meson.build)
# SPDX-License-Identifier: MIT

add_subdirectory(gen)

set(inc_glapi "${CMAKE_CURRENT_SOURCE_DIR}")

# libglapi_bridge
set(bridge_glapi_files
  "${CMAKE_CURRENT_SOURCE_DIR}/libgl_public.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/../entry_x86-64_tls.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/../entry_x86_tls.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/../entry_ppc64le_tls.h"
  "${glapi_mapi_tmp_h}")
if(with_dri AND (with_dri_platform STREQUAL "apple" OR with_dri_platform STREQUAL "windows"))
  list(APPEND bridge_glapi_files "${glapi_gentable_c}" "${main_dispatch_h}")
endif()

add_library(glapi_bridge STATIC ${bridge_glapi_files})
set_target_properties(glapi_bridge PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden EXCLUDE_FROM_ALL ON)
target_compile_options(glapi_bridge PRIVATE ${gcc_lto_quirk})
target_include_directories(glapi_bridge PRIVATE
  "${inc_mesa}" "${inc_include}" "${inc_src}" "${inc_gallium}" "${CMAKE_CURRENT_BINARY_DIR}/gen")
target_link_libraries(glapi_bridge PRIVATE ${dep_thread} idep_mesautil)
add_dependencies(glapi_bridge gen_glapi_mapi_tmp_h)
set(inc_glapi "${inc_glapi}" PARENT_SCOPE)

# forward generator outputs up to src/mesa/glapi/ (meson: subdirs share scope)
foreach(_v
    glapi_mapi_tmp_h glapi_gentable_c main_enums_c main_api_exec_c
    main_api_exec_decl_h main_api_save_init_h main_api_save_h
    main_api_beginend_init_h main_api_hw_select_init_h main_dispatch_h
    main_marshal_generated_h main_unmarshal_table_c main_api_trace_c
    main_marshal_generated_c glx_generated glx_indirect_size_h)
  set(${_v} "${${_v}}" PARENT_SCOPE)
endforeach()
