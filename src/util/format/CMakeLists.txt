# Mesa3D - src/util/format/ (mirror of src/util/format/meson.build)
# SPDX-License-Identifier: MIT

set(files_mesa_format
  "${CMAKE_CURRENT_SOURCE_DIR}/u_format.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/u_format_bptc.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/u_format_etc.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/u_format_fxt1.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/u_format_latc.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/u_format_other.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/u_format_rgtc.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/u_format_s3tc.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/u_format_tests.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/u_format_unpack_neon.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/u_format_yuv.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/u_format_zs.c")

# u_format_gen.h
add_custom_command(
  OUTPUT "${CMAKE_BINARY_DIR}/src/util/format/u_format_gen.h"
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/u_format_table.py ${CMAKE_CURRENT_SOURCE_DIR}/u_format.yaml --enums > ${CMAKE_BINARY_DIR}/src/util/format/u_format_gen.h
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/u_format_table.py" "${CMAKE_CURRENT_SOURCE_DIR}/u_format.yaml" "${CMAKE_CURRENT_SOURCE_DIR}/u_format_pack.py" "${CMAKE_CURRENT_SOURCE_DIR}/u_format_parse.py"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "generating u_format_gen.h")
add_custom_target(u_format_gen_h DEPENDS "${CMAKE_BINARY_DIR}/src/util/format/u_format_gen.h")

# u_format_pack.h
add_custom_command(
  OUTPUT "${CMAKE_BINARY_DIR}/src/util/format/u_format_pack.h"
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/u_format_table.py ${CMAKE_CURRENT_SOURCE_DIR}/u_format.yaml --header > ${CMAKE_BINARY_DIR}/src/util/format/u_format_pack.h
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/u_format_table.py" "${CMAKE_CURRENT_SOURCE_DIR}/u_format.yaml" "${CMAKE_CURRENT_SOURCE_DIR}/u_format_pack.py" "${CMAKE_CURRENT_SOURCE_DIR}/u_format_parse.py"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "generating u_format_pack.h")
add_custom_target(u_format_pack_h DEPENDS "${CMAKE_BINARY_DIR}/src/util/format/u_format_pack.h")

# u_format_table.c
add_custom_command(
  OUTPUT "${CMAKE_BINARY_DIR}/src/util/format/u_format_table.c"
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/u_format_table.py ${CMAKE_CURRENT_SOURCE_DIR}/u_format.yaml > ${CMAKE_BINARY_DIR}/src/util/format/u_format_table.c
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/u_format_table.py" "${CMAKE_CURRENT_SOURCE_DIR}/u_format.yaml" "${CMAKE_CURRENT_SOURCE_DIR}/u_format_pack.py" "${CMAKE_CURRENT_SOURCE_DIR}/u_format_parse.py"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "generating u_format_table.c")
add_custom_target(u_format_table_c DEPENDS "${CMAKE_BINARY_DIR}/src/util/format/u_format_table.c")

# idep_mesautilformat
add_library(idep_mesautilformat INTERFACE)
target_sources(idep_mesautilformat INTERFACE "${CMAKE_BINARY_DIR}/src/util/format/u_format_gen.h")
add_dependencies(idep_mesautilformat u_format_gen_h)

set(files_mesa_format "${files_mesa_format}"
    "${CMAKE_BINARY_DIR}/src/util/format/u_format_gen.h"
    "${CMAKE_BINARY_DIR}/src/util/format/u_format_pack.h"
    "${CMAKE_BINARY_DIR}/src/util/format/u_format_table.c"
  PARENT_SCOPE)