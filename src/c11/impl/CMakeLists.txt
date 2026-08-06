# Mesa3D - src/c11/impl/ (mirror of src/c11/impl/meson.build)
# SPDX-License-Identifier: MIT

set(inc_util_c11 "${CMAKE_CURRENT_SOURCE_DIR}/../..")

set(files_mesa_util_c11
  "${CMAKE_CURRENT_SOURCE_DIR}/time.c")
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  list(APPEND files_mesa_util_c11 "${CMAKE_CURRENT_SOURCE_DIR}/threads_win32.c")
else()
  list(APPEND files_mesa_util_c11 "${CMAKE_CURRENT_SOURCE_DIR}/threads_posix.c")
endif()

mesa_static(mesa_util_c11
  SOURCES ${files_mesa_util_c11}
  INC "${inc_util_c11}"
  LINK ${dep_clock} ${dep_thread} ${dep_atomic} ${dep_m} ${dep_valgrind})
mesa_idep(idep_mesautilc11
  LINK mesa_util_c11 ${dep_clock} ${dep_thread} ${dep_atomic} ${dep_m} ${dep_valgrind}
  INC "${inc_util_c11}")
