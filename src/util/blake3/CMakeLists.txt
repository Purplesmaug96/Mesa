# Mesa3D - src/util/blake3/ (mirror of src/util/blake3/meson.build)
# SPDX-License-Identifier: MIT

set(files_blake3
  blake3.c
  blake3_dispatch.c
  blake3_portable.c)
set(blake3_defs)
set(is_windows OFF)
set(is_msvc OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(is_windows ON)
endif()
if(MSVC)
  set(is_msvc ON)
endif()

set(blake3_x86_no_simd_defs -DBLAKE3_NO_SSE2 -DBLAKE3_NO_SSE41 -DBLAKE3_NO_AVX2 -DBLAKE3_NO_AVX512)

if(host_cpu_family STREQUAL "x86_64")
  if(is_windows)
    set(blake3_defs ${blake3_x86_no_simd_defs})
  elseif(sizeof_pointer STREQUAL "4")
    set(blake3_defs ${blake3_x86_no_simd_defs})
  else()
    list(APPEND files_blake3
      blake3_sse2_x86-64_unix.S
      blake3_sse41_x86-64_unix.S
      blake3_avx2_x86-64_unix.S
      blake3_avx512_x86-64_unix.S)
  endif()
elseif(host_cpu_family STREQUAL "x86")
  set(blake3_defs ${blake3_x86_no_simd_defs})
elseif(host_cpu_family STREQUAL "aarch64")
  list(APPEND files_blake3 blake3_neon.c)
endif()

set(_srcs)
foreach(_f IN LISTS files_blake3)
  list(APPEND _srcs "${CMAKE_CURRENT_SOURCE_DIR}/${_f}")
endforeach()
add_library(blake3 STATIC ${_srcs})
set_target_properties(blake3 PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden EXCLUDE_FROM_ALL ON)
if(blake3_defs)
  target_compile_definitions(blake3 PRIVATE ${blake3_defs})
endif()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(blake3 PRIVATE -Wno-error)
endif()

add_library(idep_blake3 INTERFACE)
target_link_libraries(idep_blake3 INTERFACE blake3)
set(MESA_LAST_TARGET "idep_blake3" PARENT_SCOPE)
