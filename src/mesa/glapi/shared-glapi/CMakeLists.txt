# Mesa3D - src/mesa/glapi/shared-glapi/ (mirror of src/mesa/glapi/shared-glapi/meson.build)
# SPDX-License-Identifier: MIT

set(_gen "${CMAKE_BINARY_DIR}/src/mesa/glapi/shared-glapi")

# shared_glapi_mapi_tmp.h
add_custom_command(
  OUTPUT "${_gen}/shared_glapi_mapi_tmp.h"
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../mapi_abi.py --printer shared-glapi --gl_symbols ${CMAKE_SOURCE_DIR}/src/glx/libgl-symbols.txt ${CMAKE_CURRENT_SOURCE_DIR}/../glapi/gen/gl_and_es_API.xml > ${_gen}/shared_glapi_mapi_tmp.h
  DEPENDS ${glapi_xml_py_deps} ${CMAKE_CURRENT_SOURCE_DIR}/../mapi_abi.py
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "shared_glapi_mapi_tmp.h")
add_custom_target(gen_shared_glapi_mapi_tmp_h DEPENDS "${_gen}/shared_glapi_mapi_tmp.h")

# libglapi
add_library(glapi STATIC
  "${CMAKE_CURRENT_SOURCE_DIR}/core.c"
  "${_gen}/shared_glapi_mapi_tmp.h")
set_target_properties(glapi PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden EXCLUDE_FROM_ALL ON)
target_compile_definitions(glapi PRIVATE -DMAPI_MODE_SHARED_GLAPI)
target_compile_options(glapi PRIVATE ${gcc_lto_quirk})
target_include_directories(glapi PRIVATE
  "${inc_mesa}" "${inc_src}" "${inc_include}")
target_link_libraries(glapi PRIVATE ${dep_thread} idep_mesautil)
add_dependencies(glapi gen_shared_glapi_mapi_tmp_h)
set(libglapi_build_dir "${CMAKE_CURRENT_BINARY_DIR}" PARENT_SCOPE)
