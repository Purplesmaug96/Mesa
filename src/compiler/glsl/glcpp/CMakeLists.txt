# Mesa3D - src/compiler/glsl/glcpp/ (mirror of src/compiler/glsl/glcpp/meson.build)
# SPDX-License-Identifier: MIT

set(_gen "${CMAKE_BINARY_DIR}/src/compiler/glsl/glcpp")

# glcpp-parse (bison)
add_custom_command(
  OUTPUT "${_gen}/glcpp-parse.c" "${_gen}/glcpp-parse.h"
  COMMAND ${MESA_BISON} -o "${_gen}/glcpp-parse.c" -p glcpp_parser_ --defines="${_gen}/glcpp-parse.h" "${CMAKE_CURRENT_SOURCE_DIR}/glcpp-parse.y"
  DEPENDS glcpp-parse.y
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "bison: glcpp-parse.y")
add_custom_target(gen_glcpp_parse DEPENDS "${_gen}/glcpp-parse.c" "${_gen}/glcpp-parse.h")

# glcpp-lex (flex)
add_custom_command(
  OUTPUT "${_gen}/glcpp-lex.c"
  COMMAND ${MESA_FLEX} -o "${_gen}/glcpp-lex.c" "${CMAKE_CURRENT_SOURCE_DIR}/glcpp-lex.l"
  DEPENDS glcpp-lex.l
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "flex: glcpp-lex.l")
add_custom_target(gen_glcpp_lex DEPENDS "${_gen}/glcpp-lex.c")

# libglcpp
add_library(glcpp STATIC
  "${_gen}/glcpp-lex.c"
  "${_gen}/glcpp-parse.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/glcpp.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/pp.c")
set_target_properties(glcpp PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES EXCLUDE_FROM_ALL ON)
target_include_directories(glcpp PRIVATE
  "${inc_include}" "${inc_src}" "${inc_mesa}" "${inc_gallium}" "${inc_gallium_aux}" "${CMAKE_BINARY_DIR}" "${_gen}")
target_link_libraries(glcpp PRIVATE idep_mesautil)
add_dependencies(glcpp gen_glcpp_parse gen_glcpp_lex)

# libglcpp_standalone
add_library(glcpp_standalone STATIC "${CMAKE_CURRENT_SOURCE_DIR}/pp_standalone_scaffolding.c")
set_target_properties(glcpp_standalone PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden EXCLUDE_FROM_ALL ON)
target_include_directories(glcpp_standalone PRIVATE
  "${inc_include}" "${inc_src}" "${inc_mesa}" "${inc_gallium}" "${inc_gallium_aux}" "${CMAKE_BINARY_DIR}" "${_gen}")
target_link_libraries(glcpp_standalone PRIVATE glcpp idep_mesautil)

# glcpp executable (meson names it glcpp; renamed to avoid CMake target clash)
add_executable(glcpp_exe "${CMAKE_CURRENT_SOURCE_DIR}/glcpp.c")
set_target_properties(glcpp_exe PROPERTIES C_VISIBILITY_PRESET hidden EXCLUDE_FROM_ALL ON)
target_include_directories(glcpp_exe PRIVATE
  "${inc_include}" "${inc_src}" "${inc_mesa}" "${inc_gallium}" "${inc_gallium_aux}" "${CMAKE_BINARY_DIR}" "${_gen}")
target_link_libraries(glcpp_exe PRIVATE ${dep_m} idep_mesautil glcpp_standalone glsl_util)
