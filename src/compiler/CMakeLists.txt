# Mesa3D - src/compiler/ (mirror of src/compiler/meson.build)
# SPDX-License-Identifier: MIT

set(inc_compiler "${CMAKE_CURRENT_SOURCE_DIR}")
set(inc_glsl "${CMAKE_CURRENT_SOURCE_DIR}/glsl")
set(inc_spirv "${CMAKE_CURRENT_SOURCE_DIR}/spirv")
set(inc_isaspec "${CMAKE_CURRENT_SOURCE_DIR}/isaspec")

set(_gen "${CMAKE_BINARY_DIR}/src/compiler")

# builtin_types.h (depends on u_format_gen_h)
add_custom_command(
  OUTPUT "${_gen}/builtin_types.h"
  COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/builtin_types_h.py" "${_gen}/builtin_types.h"
  DEPENDS builtin_types_h.py builtin_types.py
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "builtin_types.h")
add_custom_target(gen_builtin_types DEPENDS "${_gen}/builtin_types.h")

# builtin_types.c
add_custom_command(
  OUTPUT "${_gen}/builtin_types.c"
  COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/builtin_types_c.py" "${_gen}/builtin_types.c"
  DEPENDS builtin_types_c.py builtin_types.py
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "builtin_types.c")
add_custom_target(gen_builtin_types_c DEPENDS "${_gen}/builtin_types.c")

# ir_expression_operation.h (capture)
add_custom_command(
  OUTPUT "${_gen}/ir_expression_operation.h"
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/glsl/ir_expression_operation.py enum > ${_gen}/ir_expression_operation.h
  DEPENDS glsl/ir_expression_operation.py
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "ir_expression_operation.h")
add_custom_target(gen_ir_expression_operation DEPENDS "${_gen}/ir_expression_operation.h")

# float64/astc/float32 glsl files (meson files() references)
set(float64_glsl_file "${CMAKE_CURRENT_SOURCE_DIR}/glsl/float64.glsl")
set(astc_decoder_glsl_file "${CMAKE_CURRENT_SOURCE_DIR}/glsl/astc_decoder.glsl")
set(float32_glsl_file "${CMAKE_CURRENT_SOURCE_DIR}/glsl/float32.glsl")

set(files_libcompiler
  glsl_types.c
  glsl_types.h
  list.h
  shader_enums.c
  shader_enums.h
  shader_info.h)

add_library(compiler STATIC
  "${CMAKE_CURRENT_SOURCE_DIR}/glsl_types.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/shader_enums.c"
  "${_gen}/ir_expression_operation.h"
  "${_gen}/builtin_types.h"
  "${_gen}/builtin_types.c")
set_target_properties(compiler PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES EXCLUDE_FROM_ALL ON)
target_include_directories(compiler PRIVATE
  "${inc_compiler}" "${inc_include}" "${inc_src}" "${CMAKE_BINARY_DIR}" "${_gen}" "${CMAKE_BINARY_DIR}/src/util/format")
target_link_libraries(compiler PRIVATE ${dep_valgrind})
add_dependencies(compiler gen_ir_expression_operation gen_builtin_types gen_builtin_types_c u_format_gen_h)

# idep_compiler
add_library(idep_compiler INTERFACE)
target_include_directories(idep_compiler INTERFACE "${inc_compiler}" "${CMAKE_BINARY_DIR}" "${_gen}")
target_sources(idep_compiler INTERFACE
  "${_gen}/ir_expression_operation.h" "${_gen}/builtin_types.h")
target_link_libraries(idep_compiler INTERFACE compiler ${dep_valgrind})
add_dependencies(idep_compiler gen_ir_expression_operation gen_builtin_types)

add_subdirectory(nir)
add_subdirectory(spirv)

if(with_gallium)
  add_subdirectory(glsl)
endif()
add_subdirectory(isaspec)

set(inc_compiler "${inc_compiler}" PARENT_SCOPE)
set(inc_glsl "${inc_glsl}" PARENT_SCOPE)
set(inc_spirv "${inc_spirv}" PARENT_SCOPE)
