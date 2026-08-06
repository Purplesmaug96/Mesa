# Mesa3D - src/tool/ (mirror of src/tool/meson.build)
# SPDX-License-Identifier: MIT

if(MESA_OP_PERFETTO AND dep_libdrm)
  add_subdirectory(pps)
endif()
if(with_tools MATCHES "(^|;)dlclose-skip(;|$)" OR with_tools STREQUAL "dlclose-skip")
  add_subdirectory(dlclose-skip)
endif()
if(with_tools MATCHES "(^|;)gamma(;|$)" OR with_tools STREQUAL "gamma")
  add_subdirectory(gamma)
endif()
