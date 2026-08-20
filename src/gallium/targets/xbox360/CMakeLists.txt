# Mesa3D - src/gallium/targets/xbox360/
# SPDX-License-Identifier: MIT
#
# "xbox360" Gallium backend: a headless GL context for homebrew on the
# Xbox 360 (no D3D9/GDI/X/DRM on the console).  The GL frontend glue
# (xbox360_api.c) is renderer-agnostic; the actual screen comes from
# softpipe/ (software, system-memory rendering, the fallback) or xenos/
# (the PM4 hardware driver, src/gallium/drivers/xenos/) selected with
# MESA_OP_XBOX360_XENOS.
#
# The result is a plain static archive libxbox360.a, since XEX images are
# statically linked PE binaries.

option(MESA_OP_XBOX360_XENOS "xbox360: use the xenos hardware (PM4) driver instead of softpipe" OFF)

add_library(xbox360 STATIC
  xbox360_api.c xbox360_api.h xbox360_screen.h
  $<$<BOOL:${MESA_OP_XBOX360_XENOS}>:xenos/xbox360_xenos.c>
  $<$<NOT:$<BOOL:${MESA_OP_XBOX360_XENOS}>>:softpipe/xbox360_softpipe.c>)
set_target_properties(xbox360 PROPERTIES
  OUTPUT_NAME xbox360_core
  C_VISIBILITY_PRESET hidden)
target_include_directories(xbox360 PRIVATE
  "${inc_include}" "${inc_src}" "${inc_mesa}"
  "${inc_gallium}" "${inc_gallium_aux}" "${inc_util}"
  "${inc_gallium_drivers}" "${inc_gallium_winsys}" "${inc_gallium_winsys_sw}"
  "${CMAKE_SOURCE_DIR}/src/mesa/glapi")
if(MESA_OP_XBOX360_XENOS)
  target_include_directories(xbox360 PRIVATE
    "${CMAKE_SOURCE_DIR}/src/gallium/drivers/xenos")
endif()

# Static link ring: mesa, gallium, softpipe, glapi and the supporting
# compiler/util archives (mirrors the dri target).  All local archives go in
# one --start-group so mutually recursive references resolve regardless of
# link-list order.
set(_x360_ring
  "${CMAKE_CURRENT_BINARY_DIR}/libxbox360_core.a"
  "${CMAKE_BINARY_DIR}/src/gallium/drivers/softpipe/libsoftpipe.a"
  "${CMAKE_BINARY_DIR}/src/mesa/libmesa.a"
  "${CMAKE_BINARY_DIR}/src/gallium/auxiliary/libgalliumvl.a"
  "${CMAKE_BINARY_DIR}/src/gallium/auxiliary/libgallium.a"
  "${CMAKE_BINARY_DIR}/src/mesa/glapi/shared-glapi/libglapi.a"
  "${CMAKE_BINARY_DIR}/src/util/blake3/libblake3.a"
  "${CMAKE_BINARY_DIR}/src/compiler/glsl/libglsl.a"
  "${CMAKE_BINARY_DIR}/src/compiler/glsl/glcpp/libglcpp.a"
  "${CMAKE_BINARY_DIR}/src/compiler/spirv/libvtn.a"
  "${CMAKE_BINARY_DIR}/src/compiler/nir/libnir.a"
  "${CMAKE_BINARY_DIR}/src/compiler/libcompiler.a"
  "${CMAKE_BINARY_DIR}/src/util/libmesa_util.a"
  "${CMAKE_BINARY_DIR}/src/util/libmesa_util_simd.a"
  "${CMAKE_BINARY_DIR}/src/c11/impl/libmesa_util_c11.a"
  "${CMAKE_BINARY_DIR}/src/gallium/winsys/sw/null/libws_null.a")
if(MESA_OP_XBOX360_XENOS)
  list(APPEND _x360_ring
    "${CMAKE_BINARY_DIR}/src/gallium/drivers/xenos/libxenos.a")
endif()
if(host_cpu_family STREQUAL "x86" OR host_cpu_family STREQUAL "x86_64")
  list(APPEND _x360_ring
    "${CMAKE_BINARY_DIR}/src/mesa/libmesa_sse41.a"
    "${CMAKE_BINARY_DIR}/src/util/libmesa_util_clflush.a")
endif()

target_link_libraries(xbox360 PRIVATE
  -Wl,--start-group ${_x360_ring} -Wl,--end-group
  ${dep_thread} ${dep_m} ${dep_dl} idep_mesautil)

# The samples link a single libxbox360.a, so the final archive must contain
# the whole GL stack: merge every archive in the ring into the output archive
# (replacing the API-glue-only archive) using an MRI script, which both
# llvm-ar and GNU ar can read.  This is a real output file that ninja
# rebuilds whenever any ring archive changes, which a POST_BUILD command on
# the target itself cannot express.
set(_x360_mri "${CMAKE_CURRENT_BINARY_DIR}/libxbox360.mri")
macro(x360_write_mri)
  set(_ring "${_x360_ring}")
  if(DEFINED X360_RING_REV AND X360_RING_REV)
    list(REVERSE _ring)
  endif()
  set(_rot 0)
  if(DEFINED X360_RING_ROT)
    set(_rot "${X360_RING_ROT}")
  endif()
  while(_rot GREATER 0)
    list(POP_FRONT _ring _pop)
    list(APPEND _ring "${_pop}")
    math(EXPR _rot "${_rot} - 1")
  endwhile()
  file(WRITE "${_x360_mri}" "create ${CMAKE_CURRENT_BINARY_DIR}/libxbox360.a\n")
  foreach(_a IN LISTS _ring)
    file(APPEND "${_x360_mri}" "addlib ${_a}\n")
  endforeach()
  file(APPEND "${_x360_mri}" "save\nend\n")
endmacro()
x360_write_mri()
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/libxbox360.a"
  COMMAND "${CMAKE_AR}" -M < "${_x360_mri}"
  DEPENDS xbox360 ${_x360_ring} "${_x360_mri}"
  COMMENT "Merging the xbox360 GL stack into libxbox360.a")
add_custom_target(xbox360_merged ALL
  DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/libxbox360.a")
set(_x360_ring_targets
  softpipe mesa gallium galliumvl glapi blake3 glsl glcpp vtn nir compiler
  mesa_util mesa_util_simd mesa_util_c11 ws_null)
if(MESA_OP_XBOX360_XENOS)
  list(APPEND _x360_ring_targets xenos)
endif()
if(host_cpu_family STREQUAL "x86" OR host_cpu_family STREQUAL "x86_64")
  list(APPEND _x360_ring_targets mesa_sse41 mesa_util_clflush)
endif()
add_dependencies(xbox360 ${_x360_ring_targets})

# The archive itself (libxbox360_core.a) is only the API glue; install the
# merged stack instead.
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libxbox360.a" DESTINATION "${libdir}")
install(FILES xbox360_api.h DESTINATION include/xbox360)
install(FILES "${CMAKE_SOURCE_DIR}/src/gallium/drivers/xenos/gpu/xenos_gpu.h"
  DESTINATION include/xbox360)

# Real GL entry points (glClear, glBegin, ...) for the legacy API.
# The shared-glapi dispatch library only provides the table plumbing and
# "_dispatch_stub_*" helpers; the public gl* symbols must be generated for
# clients that link statically (as the samples/XEX do).
set(_gls "${CMAKE_CURRENT_BINARY_DIR}/glapi")
file(MAKE_DIRECTORY "${_gls}")
add_custom_command(
  OUTPUT "${_gls}/glapi_public.h"
  COMMAND ${Python3_EXECUTABLE}
          "${CMAKE_SOURCE_DIR}/src/mesa/glapi/mapi_abi.py"
          --printer glapi
          --gl_symbols "${CMAKE_SOURCE_DIR}/src/glx/libgl-symbols.txt"
          "${CMAKE_SOURCE_DIR}/src/mesa/glapi/glapi/gen/gl_and_es_API.xml"
          > "${_gls}/glapi_public.h"
  DEPENDS "${CMAKE_SOURCE_DIR}/src/mesa/glapi/mapi_abi.py"
          "${CMAKE_SOURCE_DIR}/src/glx/libgl-symbols.txt"
          "${CMAKE_SOURCE_DIR}/src/mesa/glapi/glapi/gen/gl_and_es_API.xml"
  COMMENT "Generating xbox360 GL public entries")
add_custom_target(gen_glapi_public_h
  DEPENDS "${_gls}/glapi_public.h")

# Host-only smoke test for development: renders with the real GL entry
# points and checks the colour buffer.  Requires a video/dri-free build
# (softpipe + xbox360), e.g. -DMESA_OP_EGL=OFF -DMESA_OP_GLX=disabled.
option(MESA_OP_XBOX360_TEST "Build the xbox360 backend host smoke test" OFF)
if(MESA_OP_XBOX360_TEST AND NOT CMAKE_CROSSCOMPILING)
  add_library(xbox360_glpublic STATIC xbox360_glpublic.c)
  target_include_directories(xbox360_glpublic PRIVATE
    "${inc_mesa}" "${inc_include}" "${inc_src}" "${_gls}")
  add_dependencies(xbox360_glpublic gen_glapi_public_h)
  set_target_properties(xbox360_glpublic PROPERTIES POSITION_INDEPENDENT_CODE
    ON C_VISIBILITY_PRESET hidden EXCLUDE_FROM_ALL ON)

  add_executable(xbox360_gltest xbox360_gltest.c)
  target_include_directories(xbox360_gltest PRIVATE
    "${inc_include}" "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(xbox360_gltest PRIVATE xbox360_glpublic xbox360
    ${dep_thread} ${dep_m} ${dep_spirv_tools})
  add_test(NAME xbox360_gltest COMMAND xbox360_gltest)
elseif(CMAKE_CROSSCOMPILING)
  # Console builds fold the public gl* entry points into the merged archive.
  add_library(xbox360_glpublic STATIC xbox360_glpublic.c)
  target_include_directories(xbox360_glpublic PRIVATE
    "${inc_mesa}" "${inc_include}" "${inc_src}" "${_gls}")
  add_dependencies(xbox360_glpublic gen_glapi_public_h)
  set_target_properties(xbox360_glpublic PROPERTIES POSITION_INDEPENDENT_CODE
    ON C_VISIBILITY_PRESET hidden)
  list(APPEND _x360_ring_targets xbox360_glpublic)
  list(APPEND _x360_ring "${CMAKE_CURRENT_BINARY_DIR}/libxbox360_glpublic.a")
  target_link_libraries(xbox360 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/libxbox360_glpublic.a")
  add_dependencies(xbox360 xbox360_glpublic)
  x360_write_mri()
endif()