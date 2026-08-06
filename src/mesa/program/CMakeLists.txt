# Mesa3D - src/mesa/program/ (mirror of src/mesa/program/meson.build)
# SPDX-License-Identifier: MIT

set(_gen "${CMAKE_BINARY_DIR}/src/mesa/program")

# mesa_lex (flex)
add_custom_command(
  OUTPUT "${_gen}/lex.yy.c"
  COMMAND ${MESA_FLEX} -o "${_gen}/lex.yy.c" "${CMAKE_CURRENT_SOURCE_DIR}/program_lexer.l"
  DEPENDS program_lexer.l
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "flex: program_lexer.l")
add_custom_target(gen_mesa_lex DEPENDS "${_gen}/lex.yy.c")
set(mesa_lex "${_gen}/lex.yy.c" PARENT_SCOPE)

# program_parse_tab (bison)
add_custom_command(
  OUTPUT "${_gen}/program_parse.tab.c" "${_gen}/program_parse.tab.h"
  COMMAND ${MESA_BISON} -o "${_gen}/program_parse.tab.c" --defines="${_gen}/program_parse.tab.h" "${CMAKE_CURRENT_SOURCE_DIR}/program_parse.y"
  DEPENDS program_parse.y
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "bison: program_parse.y")
add_custom_target(gen_program_parse_tab DEPENDS "${_gen}/program_parse.tab.c" "${_gen}/program_parse.tab.h")
set(program_parse_tab "${_gen}/program_parse.tab.c" PARENT_SCOPE)
