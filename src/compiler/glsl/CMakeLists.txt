# Mesa3D - src/compiler/glsl/ (mirror of src/compiler/glsl/meson.build)
# SPDX-License-Identifier: MIT

set(_gen "${CMAKE_BINARY_DIR}/src/compiler/glsl")
set(_gen_compiler "${CMAKE_BINARY_DIR}/src/compiler")

# glsl_util
set(_glsu_srcs)
foreach(_f IN LISTS glsl_util_files)
  list(APPEND _glsu_srcs "${_f}")
endforeach()
add_library(glsl_util STATIC ${_glsu_srcs})
set_target_properties(glsl_util PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden EXCLUDE_FROM_ALL ON)
target_include_directories(glsl_util PRIVATE
  "${inc_include}" "${inc_src}" "${inc_mesa}" "${inc_gallium}" "${inc_gallium_aux}" "${CMAKE_BINARY_DIR}")
target_link_libraries(glsl_util PRIVATE ${dep_valgrind} idep_mesautil)

add_subdirectory(glcpp)

# glsl_parser (bison)
add_custom_command(
  OUTPUT "${_gen}/glsl_parser.cpp" "${_gen}/glsl_parser.h"
  COMMAND ${MESA_BISON} -o "${_gen}/glsl_parser.cpp" -p _mesa_glsl_ --defines="${_gen}/glsl_parser.h" "${CMAKE_CURRENT_SOURCE_DIR}/glsl_parser.yy"
  DEPENDS glsl_parser.yy
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "bison: glsl_parser.yy")
add_custom_target(gen_glsl_parser DEPENDS "${_gen}/glsl_parser.cpp" "${_gen}/glsl_parser.h")

# glsl_lexer (flex, c++)
add_custom_command(
  OUTPUT "${_gen}/glsl_lexer.cpp"
  COMMAND ${MESA_FLEX} -o "${_gen}/glsl_lexer.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/glsl_lexer.ll"
  DEPENDS glsl_lexer.ll
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "flex: glsl_lexer.ll")
add_custom_target(gen_glsl_lexer DEPENDS "${_gen}/glsl_lexer.cpp")

# ir_expression_operation_constant.h / strings.h (capture)
add_custom_command(
  OUTPUT "${_gen}/ir_expression_operation_constant.h"
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/ir_expression_operation.py constant > ${_gen}/ir_expression_operation_constant.h
  DEPENDS ir_expression_operation.py
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "ir_expression_operation_constant.h")
add_custom_target(gen_ir_expr_constant DEPENDS "${_gen}/ir_expression_operation_constant.h")

add_custom_command(
  OUTPUT "${_gen}/ir_expression_operation_strings.h"
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/ir_expression_operation.py strings > ${_gen}/ir_expression_operation_strings.h
  DEPENDS ir_expression_operation.py
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "ir_expression_operation_strings.h")
add_custom_target(gen_ir_expr_strings DEPENDS "${_gen}/ir_expression_operation_strings.h")

# xxd-generated headers
set(_xxd_list
  "float64.glsl|float64_glsl.h|float64_source"
  "CrossPlatformSettings_piece_all.glsl|cross_platform_settings_piece_all.h|cross_platform_settings_piece_all_header"
  "bc1.glsl|bc1_glsl.h|bc1_source"
  "bc4.glsl|bc4_glsl.h|bc4_source"
  "etc2_rgba_stitch.glsl|etc2_rgba_stitch_glsl.h|etc2_rgba_stitch_source"
  "astc_decoder.glsl|astc_glsl.h|astc_source")
set(_xxd_targets)
foreach(_e IN LISTS _xxd_list)
  string(REPLACE "|" ";" _parts "${_e}")
  list(GET _parts 0 _in)
  list(GET _parts 1 _out)
  list(GET _parts 2 _sym)
  string(REPLACE "." "_" _tname "${_out}")
  string(REPLACE "-" "_" _tname "${_tname}")
  add_custom_command(
    OUTPUT "${_gen}/${_out}"
    COMMAND ${Python3_EXECUTABLE} "${files_xxd}" "${CMAKE_CURRENT_SOURCE_DIR}/${_in}" "${_gen}/${_out}" -n ${_sym}
    DEPENDS "${files_xxd}" "${CMAKE_CURRENT_SOURCE_DIR}/${_in}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "xxd: ${_out}")
  add_custom_target(gen_xxd_${_tname} DEPENDS "${_gen}/${_out}")
  list(APPEND _xxd_targets gen_xxd_${_tname})
endforeach()

set(files_libglsl
  ast.h
  ast_array_index.cpp
  ast_expr.cpp
  ast_function.cpp
  ast_to_hir.cpp
  ast_type.cpp
  builtin_functions.cpp
  builtin_functions.h
  builtin_types.cpp
  builtin_variables.cpp
  gl_nir_detect_function_recursion.c
  gl_nir_lower_atomics.c
  gl_nir_lower_images.c
  gl_nir_lower_blend_equation_advanced.c
  gl_nir_lower_buffers.c
  gl_nir_lower_discard_flow.c
  gl_nir_lower_named_interface_blocks.c
  gl_nir_lower_packed_varyings.c
  gl_nir_lower_samplers.c
  gl_nir_lower_samplers_as_deref.c
  gl_nir_lower_xfb_varying.c
  gl_nir_link_atomics.c
  gl_nir_link_functions.c
  gl_nir_link_interface_blocks.c
  gl_nir_link_uniform_blocks.c
  gl_nir_link_uniform_initializers.c
  gl_nir_link_uniforms.c
  gl_nir_link_varyings.c
  gl_nir_link_varyings.h
  gl_nir_link_xfb.c
  gl_nir_linker.c
  gl_nir_linker.h
  gl_nir.h
  glsl_parser_extras.cpp
  glsl_parser_extras.h
  glsl_symbol_table.cpp
  glsl_symbol_table.h
  glsl_to_nir.cpp
  glsl_to_nir.h
  hir_field_selection.cpp
  ir_basic_block.cpp
  ir_basic_block.h
  ir_builder.cpp
  ir_builder.h
  ir_clone.cpp
  ir_constant_expression.cpp
  ir.cpp
  ir.h
  ir_expression_flattening.cpp
  ir_expression_flattening.h
  ir_function_detect_recursion.cpp
  ir_function.cpp
  ir_hierarchical_visitor.cpp
  ir_hierarchical_visitor.h
  ir_hv_accept.cpp
  ir_list.h
  ir_optimization.h
  ir_print_visitor.cpp
  ir_print_visitor.h
  ir_rvalue_visitor.cpp
  ir_rvalue_visitor.h
  ir_validate.cpp
  ir_variable_refcount.cpp
  ir_variable_refcount.h
  ir_visitor.h
  linker_util.h
  linker_util.cpp
  lower_builtins.cpp
  lower_instructions.cpp
  lower_jumps.cpp
  lower_mat_op_to_vec.cpp
  lower_packing_builtins.cpp
  lower_precision.cpp
  lower_subroutine.cpp
  lower_vec_index_to_cond_assign.cpp
  lower_vector_derefs.cpp
  opt_algebraic.cpp
  opt_dead_builtin_variables.cpp
  opt_dead_code.cpp
  opt_flatten_nested_if_blocks.cpp
  opt_function_inlining.cpp
  opt_if_simplification.cpp
  opt_minmax.cpp
  opt_rebalance_tree.cpp
  opt_tree_grafting.cpp
  propagate_invariance.cpp
  string_to_uint_map.cpp
  string_to_uint_map.h
  serialize.cpp
  serialize.h
  shader_cache.cpp
  shader_cache.h)

set(files_libglsl_standalone
  standalone_scaffolding.cpp
  standalone_scaffolding.h
  standalone.cpp
  standalone.h)

set(libglsl_headers
  "${_gen_compiler}/ir_expression_operation.h"
  "${_gen}/cross_platform_settings_piece_all.h"
  "${_gen}/bc1_glsl.h"
  "${_gen}/bc4_glsl.h"
  "${_gen}/etc2_rgba_stitch_glsl.h"
  "${_gen}/astc_glsl.h")

set(_glsl_srcs)
foreach(_f IN LISTS files_libglsl)
  list(APPEND _glsl_srcs "${CMAKE_CURRENT_SOURCE_DIR}/${_f}")
endforeach()
list(APPEND _glsl_srcs
  "${_gen}/glsl_parser.cpp"
  "${_gen}/glsl_lexer.cpp"
  ${libglsl_headers}
  "${_gen}/ir_expression_operation_strings.h"
  "${_gen}/ir_expression_operation_constant.h"
  "${_gen}/float64_glsl.h")

add_library(glsl STATIC ${_glsl_srcs})
set_target_properties(glsl PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES EXCLUDE_FROM_ALL ON)
target_include_directories(glsl PRIVATE
  "${inc_include}" "${inc_src}" "${inc_mesa}" "${inc_gallium}" "${inc_gallium_aux}" "${CMAKE_BINARY_DIR}" "${_gen}")
target_link_libraries(glsl PRIVATE glcpp idep_nir idep_mesautil)
add_dependencies(glsl gen_glsl_parser gen_glsl_lexer gen_ir_expr_constant gen_ir_expr_strings ${_xxd_targets})

# idep_libglsl
add_library(idep_libglsl INTERFACE)
target_link_libraries(idep_libglsl INTERFACE glsl)
target_sources(idep_libglsl INTERFACE ${libglsl_headers})
set(MESA_LAST_TARGET "idep_libglsl" PARENT_SCOPE)

# libglsl_standalone
set(_glslsa_srcs)
foreach(_f IN LISTS files_libglsl_standalone)
  list(APPEND _glslsa_srcs "${CMAKE_CURRENT_SOURCE_DIR}/${_f}")
endforeach()
list(APPEND _glslsa_srcs "${_gen_compiler}/ir_expression_operation.h")
add_library(glsl_standalone STATIC ${_glslsa_srcs})
set_target_properties(glsl_standalone PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES EXCLUDE_FROM_ALL ON)
target_include_directories(glsl_standalone PRIVATE
  "${inc_include}" "${inc_src}" "${inc_mesa}" "${inc_gallium}" "${inc_gallium_aux}" "${CMAKE_BINARY_DIR}" "${_gen}")
target_link_libraries(glsl_standalone PRIVATE glsl glsl_util glcpp_standalone idep_mesautil idep_compiler idep_nir)

# glsl_compiler executable
set(with_glsl_tool OFF)
if(with_tools MATCHES "(^|;)glsl(;|$)" OR with_tools STREQUAL "glsl")
  set(with_glsl_tool ON)
endif()
if(with_glsl_tool)
  add_executable(glsl_compiler "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
  set_target_properties(glsl_compiler PROPERTIES C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
  target_include_directories(glsl_compiler PRIVATE
    "${inc_include}" "${inc_src}" "${inc_mesa}" "${inc_gallium}" "${inc_gallium_aux}" "${CMAKE_BINARY_DIR}" "${_gen}")
  target_link_libraries(glsl_compiler PRIVATE
    ${dep_clock} ${dep_thread} idep_mesautil glsl_standalone)
  install(TARGETS glsl_compiler RUNTIME DESTINATION bin)
endif()
set(MESA_LAST_TARGET "idep_libglsl" PARENT_SCOPE)
