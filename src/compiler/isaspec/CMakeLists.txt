# Mesa3D - src/compiler/isaspec/ (mirror of src/compiler/isaspec/meson.build)
# SPDX-License-Identifier: MIT

set(prog_isaspec_decode "${CMAKE_CURRENT_SOURCE_DIR}/decode.py")
set(prog_isaspec_encode "${CMAKE_CURRENT_SOURCE_DIR}/encode.py")
set(isaspec_py_deps "${CMAKE_CURRENT_SOURCE_DIR}/isa.py")

add_library(idep_isaspec_decode INTERFACE)
target_include_directories(idep_isaspec_decode INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

mesa_static(isaspec SOURCES isaspec.c DEP idep_mesautil)
set(MESA_LAST_TARGET "isaspec" PARENT_SCOPE)
