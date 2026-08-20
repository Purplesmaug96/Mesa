# Mesa3D - src/gallium/drivers/xenos/
# SPDX-License-Identifier: MIT
#
# xenos: hardware (PM4) Gallium driver for the Xbox 360 GPU.  The shader
# compiler translates NIR straight into Xenos microcode (CF + vfetch + ALU
# instructions, see gpu/xenos_ucode.h) and the context turns Gallium state
# into PM4 register writes (gpu/xenos_pm4.h).  The GPU ring/command buffer
# machinery lives in gpu/ and is platform-agnostic except for the ring
# initialisation and MMIO punches, which are provided by the winsys.
#
# This is a static library; the xbox360 target merges it into libxbox360.a
# alongside mesa/gallium like every other driver archive.

add_library(xenos STATIC
  xenos_context.c
  xenos_context.h
  xenos_private.h
  xenos_public.h
  xenos_resource.c
  xenos_resource.h
  xenos_screen.c
  xenos_screen.h
  xenos_shader.c
  xenos_shader.h
  xenos_winsys.h
  gpu/xenos_fetch.h
  gpu/xenos_gpu.c
  gpu/xenos_gpu.h
  gpu/xenos_pm4.h
  gpu/xenos_regs.h
  gpu/xenos_ucode.c
  gpu/xenos_ucode.h
)
set_target_properties(xenos PROPERTIES
  POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden EXCLUDE_FROM_ALL ON)
target_include_directories(xenos PRIVATE
  "${inc_gallium_aux}" "${inc_gallium}" "${inc_include}" "${inc_src}"
  "${CMAKE_CURRENT_SOURCE_DIR}/gpu")
if(DEFINED XECHAIN_SYSROOT AND XECHAIN_SYSROOT)
  # gpu/xenos_gpu.c pokes the CP ring MMIO registers via <xecore/xboxkrnl.h>.
  target_include_directories(xenos PRIVATE "${XECHAIN_SYSROOT}/include")
endif()
target_link_libraries(xenos PRIVATE idep_nir idep_mesautil)

# driver_xenos (meson declare_dependency equivalent)
add_library(driver_xenos INTERFACE)
target_link_libraries(driver_xenos INTERFACE xenos)
target_compile_definitions(driver_xenos INTERFACE -DGALLIUM_XENOS)