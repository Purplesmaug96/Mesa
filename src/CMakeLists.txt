# Mesa3D - src/ (mirror of src/meson.build)
# SPDX-License-Identifier: MIT

set(dir_source_root "${CMAKE_SOURCE_DIR}")
set(dir_compiler_nir "${CMAKE_SOURCE_DIR}/src/compiler/nir/")

# include dirs used across the tree (meson inc_* variables)
set(inc_mesa "${CMAKE_CURRENT_SOURCE_DIR}/mesa")
set(inc_src "${CMAKE_CURRENT_SOURCE_DIR}")
set(inc_gallium "${CMAKE_CURRENT_SOURCE_DIR}/gallium/include")
set(inc_gallium_aux "${CMAKE_CURRENT_SOURCE_DIR}/gallium/auxiliary")
set(inc_amd_common "${CMAKE_CURRENT_SOURCE_DIR}/amd/common;${CMAKE_CURRENT_SOURCE_DIR}/amd/common/nir")
set(inc_llvmpipe "${CMAKE_CURRENT_SOURCE_DIR}/gallium/drivers/llvmpipe")
set(inc_tool "${CMAKE_CURRENT_SOURCE_DIR}/tool")
set(inc_virtio "${CMAKE_CURRENT_SOURCE_DIR}/virtio")
set(inc_virtio_gpu "${CMAKE_CURRENT_SOURCE_DIR}/virtio/virtio-gpu")
set(inc_frontends "${CMAKE_CURRENT_SOURCE_DIR}/gallium/frontends")
set(inc_amd_common_llvm "")
set(inc_radv_bvh "${CMAKE_CURRENT_SOURCE_DIR}/amd/vulkan/bvh")
set(inc_radv_tools "${CMAKE_CURRENT_SOURCE_DIR}/amd/vulkan/tools")
set(inc_vulkan_bvh "${CMAKE_CURRENT_SOURCE_DIR}/vulkan/runtime/bvh")
set(inc_etnaviv "${CMAKE_CURRENT_SOURCE_DIR}/etnaviv")
set(inc_panfrost "${CMAKE_CURRENT_SOURCE_DIR}/panfrost")
set(inc_kosmickrisp "${CMAKE_CURRENT_SOURCE_DIR}/kosmickrisp")

# glsl_util_files (meson: used by glsl compiler)
set(glsl_util_files
  "${CMAKE_CURRENT_SOURCE_DIR}/mesa/main/extensions_table.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/mesa/program/prog_parameter.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/mesa/program/symbol_table.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/mesa/program/dummy_errors.c")

# git_sha1.h (custom_target, build always stale)
add_custom_command(
  OUTPUT "${CMAKE_BINARY_DIR}/src/git_sha1.h"
  COMMAND ${Python3_EXECUTABLE} "${git_sha1_gen_py}" --output "${CMAKE_BINARY_DIR}/src/git_sha1.h"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMENT "generating git_sha1.h")
add_custom_target(gen_git_sha1 ALL DEPENDS "${CMAKE_BINARY_DIR}/src/git_sha1.h")

add_subdirectory(gtest)
add_subdirectory(c11)
add_subdirectory(android_stub)
add_subdirectory(util)
# helper scripts shared across the src/ tree (meson: files() entries from src/meson.build)
set(files_xxd "${CMAKE_CURRENT_SOURCE_DIR}/util/xxd.py")
set(glsl2spirv "${CMAKE_CURRENT_SOURCE_DIR}/util/glsl2spirv.py")

# loader is needed by gallium/lvp, gbm, wayland, x11
set(with_loader OFF)
if(with_gallium OR with_swrast_vk OR with_gbm OR with_platform_wayland OR with_platform_x11)
  set(with_loader ON)
endif()

if(with_platform_x11)
  add_subdirectory(x11)
endif()
if(with_loader)
  add_subdirectory(loader)
endif()
add_subdirectory(compiler)

if(with_gallium_radeonsi OR with_amd_vk OR with_gallium_r300 OR with_gallium_r600)
  add_subdirectory(amd)
endif()
if(with_any_broadcom)
  add_subdirectory(broadcom)
endif()
if(with_gallium_etnaviv)
  add_subdirectory(etnaviv)
endif()
if(with_gallium_freedreno OR with_freedreno_vk)
  add_subdirectory(freedreno)
endif()
if(with_gallium_panfrost OR with_gallium_lima OR with_panfrost_vk)
  add_subdirectory(panfrost)
endif()
if(with_any_intel)
  add_subdirectory(intel)
endif()
if(with_gallium)
  add_subdirectory(mesa)
endif()
if(with_gallium OR with_swrast_vk)
  add_subdirectory(gallium)
  if(with_egl)
    add_subdirectory(egl)
  endif()
else()
  if(with_egl)
    message(WARNING "EGL disabled because no gallium drivers are enabled")
  endif()
endif()

if(with_glx STREQUAL "dri")
  add_subdirectory(glx)
endif()
if(with_gbm)
  add_subdirectory(gbm)
endif()
add_subdirectory(tool)

set(libgl "")  # set by glx/egl