# Mesa3D - src/util/ (mirror of src/util/meson.build)
# SPDX-License-Identifier: MIT

# inc_util depends only on inc_include and this folder
set(inc_util "${inc_include};${inc_src};${CMAKE_CURRENT_SOURCE_DIR}")

if(with_platform_android)
  add_subdirectory(u_gralloc)
endif()
add_subdirectory(blake3)

set(files_mesa_util
  anon_file.h
  anon_file.c
  bigmath.h
  bitscan.c
  bitscan.h
  bitset.h
  blend.h
  blob.c
  blob.h
  box.h
  build_id.c
  build_id.h
  cache_ops.h
  cnd_monotonic.c
  cnd_monotonic.h
  compiler.h
  compress.c
  compress.h
  thread_sched.c
  thread_sched.h
  crc32.c
  crc32.h
  dag.c
  disk_cache.c
  disk_cache.h
  disk_cache_os.c
  disk_cache_os.h
  double.c
  double.h
  enum_operators.h
  fast_idiv_by_const.c
  fast_idiv_by_const.h
  float8.c
  format_r11g11b10f.h
  format_rgb9e5.h
  format_srgb.h
  fossilize_db.c
  fossilize_db.h
  futex.c
  futex.h
  glheader.h
  half_float.c
  half_float.h
  hash_table.c
  hash_table.h
  helpers.c
  helpers.h
  hex.h
  u_idalloc.c
  u_idalloc.h
  list.h
  log.c
  lut.c
  macros.h
  memstream.c
  memstream.h
  mesa-blake3.c
  mesa-blake3.h
  os_drm.h
  os_time.c
  os_time.h
  os_file.c
  os_file_notify.c
  os_memory_fd.c
  os_misc.c
  os_misc.h
  os_socket.c
  os_socket.h
  pb_slab.c
  pb_slab.h
  ptralloc.h
  perf/u_trace.h
  perf/u_trace.c
  perf/u_trace_priv.h
  u_process.c
  u_process.h
  u_qsort.cpp
  u_qsort.h
  rwlock.c
  rwlock.h
  ralloc.c
  ralloc.h
  rand_xor.c
  rand_xor.h
  range_minimum_query.c
  range_minimum_query.h
  rb_tree.c
  rb_tree.h
  register_allocate.c
  register_allocate.h
  rgtc.c
  rgtc.h
  rounding.h
  set.c
  set.h
  simple_mtx.c
  simple_mtx.h
  slab.c
  slab.h
  softfloat.c
  softfloat.h
  sparse_array.c
  sparse_array.h
  stack_array.h
  string_buffer.c
  string_buffer.h
  strndup.c
  strndup.h
  strtod.c
  strtod.h
  texcompress_astc_luts.cpp
  texcompress_astc_luts.h
  texcompress_astc_luts_wrap.cpp
  texcompress_astc_luts_wrap.h
  texcompress_astc.cpp
  texcompress_astc.h
  timespec.h
  u_atomic.c
  u_atomic.h
  u_call_once.c
  u_call_once.h
  u_dl.c
  u_dl.h
  u_dynarray.c
  u_dynarray.h
  u_endian.h
  u_hash_table.c
  u_hash_table.h
  u_pointer.h
  u_queue.c
  u_queue.h
  u_range_remap.c
  u_range_remap.h
  u_shader_variant_cache.c
  u_shader_variant_cache.h
  u_string.c
  u_string.h
  u_thread.c
  u_thread.h
  u_vector.c
  u_vector.h
  u_math.c
  u_math.h
  u_memset.h
  u_mm.c
  u_mm.h
  u_pack_color.h
  u_debug.c
  u_debug.h
  u_debug_memory.c
  u_cpu_detect.c
  u_cpu_detect.h
  u_printf.c
  u_printf.h
  u_overflow.h
  u_tristate.h
  u_worklist.c
  u_worklist.h
  vl_vlc.h
  vl_rbsp.h
  vl_zscan_data.h
  vl_zscan_data.c
  vma.c
  vma.h
  xxhash.h
  mesa_cache_db.c
  mesa_cache_db.h
  mesa_cache_db_multipart.c
  mesa_cache_db_multipart.h)

set(libmesa_util_links)

# cache_ops per-arch (meson: separate static libs with arch flags)
if(host_cpu_family STREQUAL "aarch64")
  list(APPEND files_mesa_util cache_ops_aarch64.c)
elseif(host_cpu_family STREQUAL "x86" OR host_cpu_family STREQUAL "x86_64")
  set(_x86_sse2_args)
  if(host_cpu_family STREQUAL "x86" AND MESA_OP_SSE2)
    set(_x86_sse2_args "-msse2;-mfpmath=sse;-mstackrealign")
  endif()
  mesa_static(mesa_util_clflush
    SOURCES cache_ops_x86.c
    INC "${inc_util}"
    C_ARGS ${_x86_sse2_args})
  list(APPEND libmesa_util_links mesa_util_clflush)
  check_symbol_exists(__builtin_ia32_clflushopt "stdint.h" __mesa_clflushopt2)
  if(__mesa_clflushopt2)
    mesa_static(mesa_util_clflushopt
      SOURCES cache_ops_x86_clflushopt.c
      INC "${inc_util}"
      C_ARGS -mclflushopt)
    list(APPEND libmesa_util_links mesa_util_clflushopt)
  endif()
else()
  list(APPEND files_mesa_util cache_ops_null.c)
endif()

# drirc files
set(files_drirc "${CMAKE_CURRENT_SOURCE_DIR}/00-mesa-defaults.conf")
set(_drirc_list
  "with_amd_vk|src/amd/vulkan|00-radv-defaults.conf"
  "with_intel_vk|src/intel/vulkan|00-anv-defaults.conf"
  "with_intel_hasvk|src/intel/vulkan_hasvk|00-hasvk-defaults.conf"
  "with_nouveau_vk|src/nouveau/vulkan|00-nvk-defaults.conf"
  "with_microsoft_vk|src/microsoft/vulkan|00-dzn-defaults.conf"
  "with_imagination_vk|src/imagination/vulkan|00-pvr-defaults.conf"
  "with_broadcom_vk|src/broadcom/vulkan|00-v3dv-defaults.conf"
  "with_freedreno_vk|src/freedreno/vulkan|00-turnip-defaults.conf"
  "with_virtio_vk|src/virtio/vulkan|00-venus-defaults.conf"
  "with_swrast_vk|src/gallium/frontends/lavapipe|00-lavapipe-defaults.conf"
  "with_gallium_r300|src/gallium/drivers/r300|00-r300-defaults.conf"
  "with_gallium_iris|src/gallium/drivers/iris|00-iris-defaults.conf"
  "with_gallium_crocus|src/gallium/drivers/crocus|00-crocus-defaults.conf"
  "with_gallium_asahi|src/gallium/drivers/asahi|00-asahi-defaults.conf"
  "with_gallium_v3d|src/gallium/drivers/v3d|00-v3d-defaults.conf"
  "with_asahi_vk|src/asahi/vulkan|00-hk-defaults.conf"
  "with_panfrost_vk|src/panfrost/vulkan|00-panvk-defaults.conf"
  "with_gallium_virgl|src/gallium/drivers/virgl|00-virtio_gpu-defaults.conf"
  "with_gallium_panfrost|src/gallium/drivers/panfrost|00-panfrost-defaults.conf"
  "with_gallium_radeonsi|src/gallium/drivers/radeonsi|00-radeonsi-defaults.conf"
  "with_gallium_zink|src/gallium/drivers/zink|00-zink-defaults.conf"
  "with_gallium_r600|src/gallium/drivers/r600|00-r600-defaults.conf"
  "with_gallium_freedreno|src/gallium/drivers/freedreno|00-msm-defaults.conf"
  "with_gallium_d3d12|src/gallium/drivers/d3d12|00-d3d12-defaults.conf"
  "with_gallium_svga|src/gallium/drivers/svga|00-vmwgfx-defaults.conf")
foreach(_e IN LISTS _drirc_list)
  string(REPLACE "|" ";" _parts "${_e}")
  list(GET _parts 0 _cond)
  list(GET _parts 1 _path)
  list(GET _parts 2 _file)
  if(${_cond})
    list(APPEND files_drirc "${CMAKE_SOURCE_DIR}/${_path}/${_file}")
  endif()
endforeach()

# driconf_static.h (custom_target)
set(_drirc_rel)
foreach(_f IN LISTS files_drirc)
  list(APPEND _drirc_rel "${_f}")
endforeach()
add_custom_command(
  OUTPUT "${CMAKE_BINARY_DIR}/src/util/driconf_static.h"
  COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/driconf_static.py" ${files_drirc} "${CMAKE_BINARY_DIR}/src/util/driconf_static.h"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/driconf_static.py" ${files_drirc}
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "generating driconf_static.h")
add_custom_target(gen_driconf_static DEPENDS "${CMAKE_BINARY_DIR}/src/util/driconf_static.h")

# format_srgb.c (capture)
add_custom_command(
  OUTPUT "${CMAKE_BINARY_DIR}/src/util/format_srgb.c"
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/format_srgb.py > ${CMAKE_BINARY_DIR}/src/util/format_srgb.c
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/format_srgb.py"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "generating format_srgb.c")
add_custom_target(gen_format_srgb DEPENDS "${CMAKE_BINARY_DIR}/src/util/format_srgb.c")

# shader_stats.h (capture)
add_custom_command(
  OUTPUT "${CMAKE_BINARY_DIR}/src/util/shader_stats.h"
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/process_shader_stats.py ${CMAKE_CURRENT_SOURCE_DIR}/shader_stats.rnc ${CMAKE_CURRENT_SOURCE_DIR}/shader_stats.xml > ${CMAKE_BINARY_DIR}/src/util/shader_stats.h
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/process_shader_stats.py" "${CMAKE_CURRENT_SOURCE_DIR}/shader_stats.rnc" "${CMAKE_CURRENT_SOURCE_DIR}/shader_stats.xml"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "generating shader_stats.h")
add_custom_target(gen_shader_stats DEPENDS "${CMAKE_BINARY_DIR}/src/util/shader_stats.h")

# format subdir provides files_mesa_format
add_subdirectory(format)
list(APPEND files_mesa_util ${files_mesa_format})

# deps_for_libmesa_util
set(deps_for_libmesa_util
  ${dep_zlib} ${dep_clock} ${dep_thread} ${dep_atomic} ${dep_m}
  ${dep_valgrind} ${dep_zstd} ${dep_dl} ${dep_unwind} ${dep_futex}
  idep_blake3 idep_mesautilc11 idep_mesautilformat)

if(dep_libdrm)
  list(APPEND files_mesa_util u_sync_provider.c u_sync_provider.h)
  list(APPEND deps_for_libmesa_util ${dep_libdrm})
endif()
if(with_platform_android)
  set(files_debug_stack u_debug_stack_android.cpp)
else()
  set(files_debug_stack u_debug_stack.c u_debug_symbol.c u_debug_symbol.h)
endif()
if(with_platform_haiku)
  list(APPEND deps_for_libmesa_util ${dep_network})
endif()
if(MESA_OP_PERFETTO)
  list(APPEND files_mesa_util perf/u_perfetto.cc perf/u_perfetto.h)
  list(APPEND deps_for_libmesa_util ${dep_perfetto})
endif()
if(MESA_OP_GPUVIS)
  list(APPEND files_mesa_util perf/u_gpuvis.c perf/u_gpuvis.h perf/gpuvis_trace_utils.h)
endif()
if(MESA_OP_SYSPROF)
  list(APPEND files_mesa_util perf/u_sysprof.c perf/u_sysprof.h)
  list(APPEND deps_for_libmesa_util ${dep_sysprof})
endif()

set(u_trace_py "${CMAKE_CURRENT_SOURCE_DIR}/perf/u_trace.py")

# streaming-load-memcpy
mesa_static(mesa_util_simd
  SOURCES streaming-load-memcpy.c
  INC "${inc_util}"
  C_ARGS ${sse41_args})
list(APPEND libmesa_util_links mesa_util_simd)

# _libmesa_util
set(_util_srcs)
foreach(_f IN LISTS files_mesa_util)
  if(IS_ABSOLUTE "${_f}")
    list(APPEND _util_srcs "${_f}")
  else()
    list(APPEND _util_srcs "${CMAKE_CURRENT_SOURCE_DIR}/${_f}")
  endif()
endforeach()
foreach(_f IN LISTS files_debug_stack)
  list(APPEND _util_srcs "${CMAKE_CURRENT_SOURCE_DIR}/${_f}")
endforeach()
list(APPEND _util_srcs "${CMAKE_BINARY_DIR}/src/util/format_srgb.c")
set(_util_incs "${inc_util};${CMAKE_CURRENT_SOURCE_DIR}/format;${CMAKE_CURRENT_BINARY_DIR}/format")
add_library(mesa_util STATIC ${_util_srcs})
set_target_properties(mesa_util PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES EXCLUDE_FROM_ALL ON)
target_include_directories(mesa_util PRIVATE ${_util_incs} "${CMAKE_BINARY_DIR}" "${CMAKE_BINARY_DIR}/src" ${PC_VALGRIND_INCLUDE_DIRS})
target_link_libraries(mesa_util PRIVATE ${deps_for_libmesa_util} ${libmesa_util_links})
if(M_LIB)
  target_link_libraries(mesa_util PRIVATE ${M_LIB})
endif()
add_dependencies(mesa_util gen_format_srgb gen_driconf_static gen_shader_stats u_format_gen_h u_format_pack_h u_format_table_c)

# idep_mesautil
add_library(idep_mesautil INTERFACE)
target_link_libraries(idep_mesautil INTERFACE mesa_util ${deps_for_libmesa_util})
target_include_directories(idep_mesautil INTERFACE "${inc_util}")
target_compile_definitions(idep_mesautil INTERFACE -DXXH_FORCE_ALIGN_CHECK=0 -DXXH_FORCE_MEMORY_ACCESS=0)
target_sources(idep_mesautil INTERFACE "${CMAKE_BINARY_DIR}/src/util/shader_stats.h")
add_dependencies(idep_mesautil gen_shader_stats)
set(MESA_LAST_TARGET "idep_mesautil" PARENT_SCOPE)

# xmlconfig
mesa_feature_found(with_xmlconfig)
if(with_xmlconfig)
  set(xmlconfig_deps ${dep_expat})
  set(c_xmlconfig_arg "-DWITH_XMLCONFIG=1")
else()
  set(xmlconfig_deps "")
  set(c_xmlconfig_arg "-DWITH_XMLCONFIG=0")
endif()
mesa_static(xmlconfig
  SOURCES xmlconfig.c xmlconfig.h
  INC "${inc_util}"
  DEP idep_mesautil ${dep_m} ${xmlconfig_deps}
  C_ARGS ${c_xmlconfig_arg}
          "-DSYSCONFDIR=\"${prefix}/${sysconfdir}\""
          "-DDATADIR=\"${prefix}/${datadir}\"")
mesa_idep(idep_xmlconfig
  LINK xmlconfig ${xmlconfig_deps}
  GEN gen_driconf_static)
set(MESA_LAST_TARGET "idep_xmlconfig" PARENT_SCOPE)

set(glsl2spirv "${CMAKE_CURRENT_SOURCE_DIR}/glsl2spirv.py")

# parson
mesa_static(parson SOURCES parson.c parson.h)
mesa_idep(idep_parson LINK parson)

if(MESA_OP_BUILD_TESTS)
  add_subdirectory(tests)
endif()

set(MESA_LAST_TARGET "idep_mesautil" PARENT_SCOPE)
