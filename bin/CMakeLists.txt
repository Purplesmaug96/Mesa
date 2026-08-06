# Mesa3D - bin/ (mirror of bin/meson.build)
# SPDX-License-Identifier: MIT

# git_sha1_gen.py path is defined in the top-level CMakeLists.txt (sha1_h too)
set(gen_vs_module_defs_py "${CMAKE_CURRENT_SOURCE_DIR}/gen_vs_module_defs.py")
set(symbols_check "${CMAKE_CURRENT_SOURCE_DIR}/symbols-check.py")
set(install_megadrivers_py "${CMAKE_CURRENT_SOURCE_DIR}/install_megadrivers.py")

# configure_file copy: bin/drm-shim.py -> build/bin/drm-shim
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/drm-shim.py" "${CMAKE_BINARY_DIR}/bin/drm-shim" COPYONLY)
