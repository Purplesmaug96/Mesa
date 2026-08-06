# Mesa3D - src/android_stub/ (mirror of src/android_stub/meson.build)
# SPDX-License-Identifier: MIT

if(with_android_stub)
  set(stub_libs)
  set(lib_names hardware log nativewindow sync)
  if(MESA_OP_ANDROID_LIBBACKTRACE STREQUAL "enabled")
    list(APPEND lib_names backtrace)
  endif()
  foreach(_l IN LISTS lib_names)
    add_library(${_l} SHARED "${CMAKE_CURRENT_SOURCE_DIR}/${_l}_stub.cpp")
    target_include_directories(${_l} PRIVATE "${inc_include}")
  endforeach()
endif()
