# Mesa3D - src/compiler/spirv/ (mirror of src/compiler/spirv/meson.build)
# SPDX-License-Identifier: MIT

set(_gen "${CMAKE_BINARY_DIR}/src/compiler/spirv")

# vtn_gather_types.c
add_custom_command(
  OUTPUT "${_gen}/vtn_gather_types.c"
  COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/vtn_gather_types_c.py" "${CMAKE_CURRENT_SOURCE_DIR}/spirv.core.grammar.json" "${_gen}/vtn_gather_types.c"
  DEPENDS vtn_gather_types_c.py spirv.core.grammar.json
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "vtn_gather_types.c")
add_custom_target(gen_vtn_gather_types DEPENDS "${_gen}/vtn_gather_types.c")

# spirv_info.h/.c
add_custom_command(
  OUTPUT "${_gen}/spirv_info.h" "${_gen}/spirv_info.c"
  COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/spirv_info_gen.py" --json "${CMAKE_CURRENT_SOURCE_DIR}/spirv.core.grammar.json" --out-h "${_gen}/spirv_info.h" --out-c "${_gen}/spirv_info.c"
  DEPENDS spirv_info_gen.py spirv.core.grammar.json
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "spirv_info")
add_custom_target(gen_spirv_info DEPENDS "${_gen}/spirv_info.h" "${_gen}/spirv_info.c")

# vtn_generator_ids.h
add_custom_command(
  OUTPUT "${_gen}/vtn_generator_ids.h"
  COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/vtn_generator_ids_h.py" "${CMAKE_CURRENT_SOURCE_DIR}/spir-v.xml" "${_gen}/vtn_generator_ids.h"
  DEPENDS vtn_generator_ids_h.py spir-v.xml
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "vtn_generator_ids.h")
add_custom_target(gen_vtn_generator_ids DEPENDS "${_gen}/vtn_generator_ids.h")

set(files_libvtn
  GLSL.ext.AMD.h
  GLSL.std.450.h
  OpenCL.std.h
  gl_spirv.c
  nir_spirv.h
  spirv.h
  spirv_to_nir.c
  vtn_alu.c
  vtn_amd.c
  vtn_cfg.c
  vtn_cmat.c
  vtn_debug.c
  vtn_glsl450.c
  vtn_opencl.c
  vtn_private.h
  vtn_structured_cfg.c
  vtn_subgroup.c
  vtn_variables.c)

set(_vtn_srcs)
foreach(_f IN LISTS files_libvtn)
  list(APPEND _vtn_srcs "${CMAKE_CURRENT_SOURCE_DIR}/${_f}")
endforeach()
list(APPEND _vtn_srcs
  "${_gen}/spirv_info.c"
  "${_gen}/spirv_info.h"
  "${_gen}/vtn_gather_types.c"
  "${_gen}/vtn_generator_ids.h")

# libvtn
add_library(vtn STATIC ${_vtn_srcs})
set_target_properties(vtn PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden EXCLUDE_FROM_ALL ON)
target_include_directories(vtn PRIVATE
  "${inc_include}" "${inc_src}" "${CMAKE_BINARY_DIR}" "${_gen}")
target_link_libraries(vtn PRIVATE ${dep_spirv_tools} idep_nir ${dep_valgrind})
add_dependencies(vtn gen_vtn_gather_types gen_spirv_info gen_vtn_generator_ids)

# idep_vtn
add_library(idep_vtn INTERFACE)
target_link_libraries(idep_vtn INTERFACE vtn ${dep_spirv_tools} idep_nir idep_mesautil)
target_sources(idep_vtn INTERFACE "${_gen}/spirv_info.h")
add_dependencies(idep_vtn gen_spirv_info)

# spirv2nir
set(with_nir_tool OFF)
if(with_tools MATCHES "(^|;)nir(;|$)" OR with_tools STREQUAL "nir")
  set(with_nir_tool ON)
endif()
if(with_nir_tool)
  add_executable(spirv2nir
    "${CMAKE_CURRENT_SOURCE_DIR}/spirv2nir.c"
    "${_gen}/vtn_generator_ids.h")
  set_target_properties(spirv2nir PROPERTIES C_VISIBILITY_PRESET hidden)
  target_include_directories(spirv2nir PRIVATE
    "${inc_include}" "${inc_src}" "${CMAKE_BINARY_DIR}" "${_gen}")
  target_link_libraries(spirv2nir PRIVATE ${dep_m} idep_vtn idep_mesautil)
  add_dependencies(spirv2nir gen_vtn_generator_ids)
  install(TARGETS spirv2nir RUNTIME DESTINATION bin)
endif()
set(MESA_LAST_TARGET "idep_vtn" PARENT_SCOPE)
