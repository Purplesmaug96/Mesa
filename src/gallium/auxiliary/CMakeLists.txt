# Mesa3D - src/gallium/auxiliary/ (mirror of src/gallium/auxiliary/meson.build)
# SPDX-License-Identifier: MIT

set(_gen "${CMAKE_BINARY_DIR}/src/gallium/auxiliary")

# files_libgallium (base)
set(files_libgallium
  driver_ddebug/dd_draw.c
  driver_ddebug/dd_util.h
  pipebuffer/pb_buffer_fenced.c
  pipebuffer/pb_buffer_fenced.h
  pipebuffer/pb_buffer.h
  pipebuffer/pb_bufmgr_cache.c
  pipebuffer/pb_bufmgr_debug.c
  pipebuffer/pb_bufmgr.h
  pipebuffer/pb_bufmgr_mm.c
  pipebuffer/pb_bufmgr_slab.c
  pipebuffer/pb_cache.c
  pipebuffer/pb_cache.h
  pipebuffer/pb_validate.c
  pipebuffer/pb_validate.h
  util/u_async_debug.h
  util/u_async_debug.c
  util/u_bitcast.h
  util/u_bitmask.c
  util/u_bitmask.h
  util/u_blend.h
  util/u_blitter.c
  util/u_blitter.h
  util/u_debug_cb.h
  util/u_debug_describe.c
  util/u_debug_describe.h
  util/u_debug_flush.c
  util/u_debug_flush.h
  util/u_debug_image.c
  util/u_debug_image.h
  util/u_debug_refcnt.c
  util/u_debug_refcnt.h
  util/u_draw.c
  util/u_draw.h
  util/u_draw_quad.c
  util/u_draw_quad.h
  util/u_driconf.c
  util/u_driconf.h
  util/u_dual_blend.h
  util/u_dump_defines.c
  util/u_dump.h
  util/u_dump_state.c
  util/u_framebuffer.c
  util/u_framebuffer.h
  util/u_gen_mipmap.c
  util/u_gen_mipmap.h
  util/u_handle_table.c
  util/u_handle_table.h
  util/u_helpers.c
  util/u_helpers.h
  util/u_index_modify.c
  util/u_index_modify.h
  util/u_inlines.h
  util/u_live_shader_cache.c
  util/u_live_shader_cache.h
  util/u_log.c
  util/u_log.h
  util/u_prim.h
  util/u_prim.c
  util/u_prim_restart.c
  util/u_prim_restart.h
  util/u_pstipple.c
  util/u_pstipple.h
  util/u_pwr8.h
  util/u_range.h
  util/u_rect.h
  util/u_resource.c
  util/u_resource.h
  util/u_sample_positions.c
  util/u_sample_positions.h
  util/u_sampler.c
  util/u_sampler.h
  util/u_screen.c
  util/u_screen.h
  util/u_simple_shaders.h
  util/u_split_draw.c
  util/u_split_draw.h
  util/u_split_prim.h
  util/u_sse.h
  util/u_suballoc.c
  util/u_suballoc.h
  util/u_surface.c
  util/u_surface.h
  util/u_tests.h
  util/u_texture.c
  util/u_texture.h
  util/u_tile.c
  util/u_tile.h
  util/u_transfer.c
  util/u_transfer.h
  util/u_transfer_helper.c
  util/u_transfer_helper.h
  util/u_threaded_context.c
  util/u_threaded_context.h
  util/u_threaded_context_calls.h
  util/u_trace_gallium.c
  util/u_trace_gallium.h
  util/u_upload_mgr.c
  util/u_upload_mgr.h
  util/u_vbuf.c
  util/u_vbuf.h
  util/u_vertex_state_cache.c
  util/u_vertex_state_cache.h
  util/u_video.h
  util/u_viewport.h
)

if(with_gfx_compute)
  list(APPEND files_libgallium
  cso_cache/cso_cache.c
  cso_cache/cso_cache.h
  cso_cache/cso_context.c
  cso_cache/cso_context.h
  cso_cache/cso_hash.c
  cso_cache/cso_hash.h
  draw/draw_cliptest_tmp.h
  draw/draw_context.c
  draw/draw_context.h
  draw/draw_decompose_tmp.h
  draw/draw_fs.c
  draw/draw_fs.h
  draw/draw_gs.c
  draw/draw_gs.h
  draw/draw_gs_tmp.h
  draw/draw_mesh.c
  draw/draw_mesh.h
  draw/draw_mesh_prim.c
  draw/draw_mesh_prim.h
  draw/draw_mesh_prim_tmp.h
  draw/draw_pipe_aaline.c
  draw/draw_pipe_aapoint.c
  draw/draw_pipe.c
  draw/draw_pipe.h
  draw/draw_pipe_clip.c
  draw/draw_pipe_cull.c
  draw/draw_pipe_flatshade.c
  draw/draw_pipe_offset.c
  draw/draw_pipe_stipple.c
  draw/draw_pipe_twoside.c
  draw/draw_pipe_unfilled.c
  draw/draw_pipe_user_cull.c
  draw/draw_pipe_util.c
  draw/draw_pipe_validate.c
  draw/draw_pipe_vbuf.c
  draw/draw_pipe_wide_line.c
  draw/draw_pipe_wide_point.c
  draw/draw_prim_assembler.c
  draw/draw_prim_assembler.h
  draw/draw_prim_assembler_tmp.h
  draw/draw_private.h
  draw/draw_pt.c
  draw/draw_pt_decompose.h
  draw/draw_pt_emit.c
  draw/draw_pt_fetch.c
  draw/draw_pt_fetch_shade_emit.c
  draw/draw_pt_fetch_shade_pipeline.c
  draw/draw_pt_mesh_pipeline.c
  draw/draw_pt.h
  draw/draw_pt_post_vs.c
  draw/draw_pt_so_emit.c
  draw/draw_pt_util.c
  draw/draw_pt_vsplit.c
  draw/draw_pt_vsplit_tmp.h
  draw/draw_so_emit_tmp.h
  draw/draw_split_tmp.h
  draw/draw_tess.c
  draw/draw_tess.h
  draw/draw_vbuf.h
  draw/draw_vertex.c
  draw/draw_vertex.h
  draw/draw_vertex_header.h
  draw/draw_vs.c
  draw/draw_vs_exec.c
  draw/draw_vs.h
  draw/draw_vs_variant.c
  driver_ddebug/dd_context.c
  driver_ddebug/dd_pipe.h
  driver_ddebug/dd_public.h
  driver_ddebug/dd_screen.c
  driver_noop/noop_pipe.c
  driver_noop/noop_public.h
  driver_noop/noop_state.c
  driver_trace/tr_context.c
  driver_trace/tr_context.h
  driver_trace/tr_dump.c
  driver_trace/tr_dump_defines.h
  driver_trace/tr_dump.h
  driver_trace/tr_dump_state.c
  driver_trace/tr_dump_state.h
  driver_trace/tr_public.h
  driver_trace/tr_screen.c
  driver_trace/tr_screen.h
  driver_trace/tr_texture.c
  driver_trace/tr_texture.h
  driver_trace/tr_video.c
  driver_trace/tr_video.h
  hud/font.c
  hud/font.h
  hud/hud_context.c
  hud/hud_context.h
  hud/hud_cpu.c
  hud/hud_nic.c
  hud/hud_cpufreq.c
  hud/hud_diskstat.c
  hud/hud_sensors_temp.c
  hud/hud_driver_query.c
  hud/hud_fps.c
  hud/hud_private.h
  indices/u_indices.h
  indices/u_indices_priv.h
  indices/u_primconvert.c
  indices/u_primconvert.h
  rtasm/rtasm_execmem.c
  rtasm/rtasm_execmem.h
  rtasm/rtasm_x86sse.c
  rtasm/rtasm_x86sse.h
  tgsi/tgsi_aa_point.c
  tgsi/tgsi_aa_point.h
  tgsi/tgsi_build.c
  tgsi/tgsi_build.h
  tgsi/tgsi_dump.c
  tgsi/tgsi_dump.h
  tgsi/tgsi_dynamic_indexing.c
  tgsi/tgsi_exec.c
  tgsi/tgsi_exec.h
  tgsi/tgsi_from_mesa.c
  tgsi/tgsi_from_mesa.h
  tgsi/tgsi_info.c
  tgsi/tgsi_info.h
  tgsi/tgsi_info_opcodes.h
  tgsi/tgsi_iterate.c
  tgsi/tgsi_iterate.h
  tgsi/tgsi_opcode_tmp.h
  tgsi/tgsi_parse.c
  tgsi/tgsi_parse.h
  tgsi/tgsi_point_sprite.c
  tgsi/tgsi_point_sprite.h
  tgsi/tgsi_sanity.c
  tgsi/tgsi_sanity.h
  tgsi/tgsi_scan.c
  tgsi/tgsi_scan.h
  tgsi/tgsi_strings.c
  tgsi/tgsi_strings.h
  tgsi/tgsi_text.c
  tgsi/tgsi_text.h
  tgsi/tgsi_transform.c
  tgsi/tgsi_transform.h
  tgsi/tgsi_two_side.c
  tgsi/tgsi_two_side.h
  tgsi/tgsi_ureg.c
  tgsi/tgsi_ureg.h
  tgsi/tgsi_util.c
  tgsi/tgsi_util.h
  tgsi/tgsi_vpos.c
  translate/translate.c
  translate/translate.h
  translate/translate_cache.c
  translate/translate_cache.h
  translate/translate_generic.c
  translate/translate_sse.c
  nir/tgsi_to_nir.c
  nir/tgsi_to_nir.h
  nir/nir_to_tgsi.c
  nir/nir_to_tgsi.h
  nir/nir_draw_helpers.c
  nir/nir_draw_helpers.h
  util/u_simple_shaders.c
  util/u_tests.c
  )
  # draw_nir_lower_opcodes.c
  add_custom_command(
    OUTPUT "${_gen}/draw_nir_lower_opcodes.c"
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/draw/draw_nir_lower_opcodes.py -p ${dir_compiler_nir} > ${_gen}/draw_nir_lower_opcodes.c
    DEPENDS draw/draw_nir_lower_opcodes.py ${nir_algebraic_depends}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "draw_nir_lower_opcodes.c")
  add_custom_target(gen_draw_nir_lower_opcodes DEPENDS "${_gen}/draw_nir_lower_opcodes.c")
  list(APPEND files_libgallium "${_gen}/draw_nir_lower_opcodes.c")
endif()

if(dep_libdrm)
  list(APPEND files_libgallium
  renderonly/renderonly.c
  renderonly/renderonly.h
  )
endif()

if(draw_with_llvm)
  list(APPEND files_libgallium
  gallivm/lp_bld_arit.c
  gallivm/lp_bld_arit.h
  gallivm/lp_bld_arit_overflow.c
  gallivm/lp_bld_arit_overflow.h
  gallivm/lp_bld_assert.c
  gallivm/lp_bld_assert.h
  gallivm/lp_bld_bitarit.c
  gallivm/lp_bld_bitarit.h
  gallivm/lp_bld_const.c
  gallivm/lp_bld_const.h
  gallivm/lp_bld_conv.c
  gallivm/lp_bld_conv.h
  gallivm/lp_bld_coro.c
  gallivm/lp_bld_coro.h
  gallivm/lp_bld_debug.cpp
  gallivm/lp_bld_debug.h
  gallivm/lp_bld_flow.c
  gallivm/lp_bld_flow.h
  gallivm/lp_bld_format_aos_array.c
  gallivm/lp_bld_format_aos.c
  gallivm/lp_bld_format_float.c
  gallivm/lp_bld_format_s3tc.c
  gallivm/lp_bld_format.c
  gallivm/lp_bld_format.h
  gallivm/lp_bld_format_soa.c
  gallivm/lp_bld_format_srgb.c
  gallivm/lp_bld_format_yuv.c
  gallivm/lp_bld_gather.c
  gallivm/lp_bld_gather.h
  gallivm/lp_bld.h
  gallivm/lp_bld_init_common.c
  gallivm/lp_bld_init.h
  gallivm/lp_bld_intr.c
  gallivm/lp_bld_intr.h
  gallivm/lp_bld_ir_common.c
  gallivm/lp_bld_ir_common.h
  gallivm/lp_bld_jit_sample.c
  gallivm/lp_bld_jit_types.h
  gallivm/lp_bld_jit_types.c
  gallivm/lp_bld_limits.h
  gallivm/lp_bld_logic.c
  gallivm/lp_bld_logic.h
  gallivm/lp_bld_misc.cpp
  gallivm/lp_bld_misc.h
  gallivm/lp_bld_nir.h
  gallivm/lp_bld_nir.c
  gallivm/lp_bld_nir_aos.c
  gallivm/lp_bld_nir_lower.c
  gallivm/lp_bld_nir_soa.c
  gallivm/lp_bld_pack.c
  gallivm/lp_bld_pack.h
  gallivm/lp_bld_passmgr.h
  gallivm/lp_bld_passmgr.c
  gallivm/lp_bld_printf.c
  gallivm/lp_bld_printf.h
  gallivm/lp_bld_quad.c
  gallivm/lp_bld_quad.h
  gallivm/lp_bld_sample_aos.c
  gallivm/lp_bld_sample_aos.h
  gallivm/lp_bld_sample.c
  gallivm/lp_bld_sample.h
  gallivm/lp_bld_sample_soa.c
  gallivm/lp_bld_struct.c
  gallivm/lp_bld_struct.h
  gallivm/lp_bld_swizzle.c
  gallivm/lp_bld_swizzle.h
  gallivm/lp_bld_tgsi_action.c
  gallivm/lp_bld_tgsi_action.h
  gallivm/lp_bld_tgsi.c
  gallivm/lp_bld_tgsi.h
  gallivm/lp_bld_tgsi_info.c
  gallivm/lp_bld_tgsi_soa.c
  gallivm/lp_bld_type.c
  gallivm/lp_bld_type.h
  draw/draw_llvm.c
  draw/draw_llvm.h
  draw/draw_pt_fetch_shade_pipeline_llvm.c
  draw/draw_vs_llvm.c
  tessellator/tessellator.cpp
  tessellator/tessellator.hpp
  tessellator/p_tessellator.cpp
  tessellator/p_tessellator.h
  nir/nir_to_tgsi_info.c
  nir/nir_to_tgsi_info.h
  )
  # lp_bld_nir_no_integer_algebraic.c
  add_custom_command(
    OUTPUT "${_gen}/lp_bld_nir_no_integer_algebraic.c"
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/gallivm/lp_bld_nir_no_integer_algebraic.py -p ${dir_compiler_nir} > ${_gen}/lp_bld_nir_no_integer_algebraic.c
    DEPENDS gallivm/lp_bld_nir_no_integer_algebraic.py ${nir_algebraic_depends}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "lp_bld_nir_no_integer_algebraic.c")
  add_custom_target(gen_lp_bld_nir_no_integer_algebraic DEPENDS "${_gen}/lp_bld_nir_no_integer_algebraic.c")
  list(APPEND files_libgallium "${_gen}/lp_bld_nir_no_integer_algebraic.c")
  if(llvm_with_orcjit)
    list(APPEND files_libgallium gallivm/lp_bld_init_orc.cpp)
  else()
    list(APPEND files_libgallium gallivm/lp_bld_init.c)
  endif()
endif()

# files_libgalliumvl
set(files_libgalliumvl
  vl/vl_codec.c
  vl/vl_codec.h
  vl/vl_compositor.c
  vl/vl_compositor.h
  vl/vl_compositor_gfx.c
  vl/vl_compositor_gfx.h
  vl/vl_compositor_cs.c
  vl/vl_compositor_cs.h
  vl/vl_compositor_proc.c
  vl/vl_compositor_proc.h
  vl/vl_csc.c
  vl/vl_csc.h
  vl/vl_defines.h
  vl/vl_deint_filter.c
  vl/vl_deint_filter.h
  vl/vl_deint_filter_cs.c
  vl/vl_deint_filter_cs.h
  vl/vl_mpeg12_bitstream.c
  vl/vl_mpeg12_bitstream.h
  vl/vl_proc.c
  vl/vl_proc.h
  vl/vl_types.h
  vl/vl_vertex_buffers.c
  vl/vl_vertex_buffers.h
  vl/vl_video_buffer.c
  vl/vl_video_buffer.h
)

# files_libgalliumvlwinsys
set(vlwinsys_deps)
set(files_libgalliumvlwinsys vl/vl_winsys.h)
set(links_libgalliumvlwinsys)
if(NOT with_platform_windows)
  list(APPEND files_libgalliumvlwinsys vl/vl_winsys_drm.c)
endif()
if(with_platform_x11)
  list(APPEND files_libgalliumvlwinsys vl/vl_winsys_dri.c)
  list(APPEND links_libgalliumvlwinsys loader_x11)
  if(with_dri_platform STREQUAL "drm" OR with_dri_platform STREQUAL "pseudo-drm")
    list(APPEND vlwinsys_deps ${dep_xcb_sync} ${dep_xcb_present} ${dep_xshmfence} ${dep_xcb_xfixes} ${dep_xcb_dri3})
    list(APPEND files_libgalliumvlwinsys vl/vl_winsys_dri3.c)
  endif()
endif()
if(with_gallium_zink)
  list(APPEND files_libgalliumvlwinsys vl/vl_winsys_kopper.c)
endif()
if(with_glx STREQUAL "xlib")
  list(APPEND files_libgalliumvlwinsys vl/vl_winsys_xlib_swrast.c)
elseif(with_gallium_drisw_kms)
  list(APPEND files_libgalliumvlwinsys vl/vl_winsys_dri_vgem.c)
endif()

# tr_util.[ch] (enums2names.py)
add_custom_command(
  OUTPUT "${_gen}/tr_util.c" "${_gen}/tr_util.h"
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/driver_trace/enums2names.py ${CMAKE_CURRENT_SOURCE_DIR}/../include/pipe/p_defines.h ${CMAKE_CURRENT_SOURCE_DIR}/../include/pipe/p_video_enums.h ${CMAKE_SOURCE_DIR}/src/util/blend.h -C "${_gen}/tr_util.c" -H "${_gen}/tr_util.h"
  DEPENDS driver_trace/enums2names.py
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "tr_util.c/h")
add_custom_target(gen_tr_util DEPENDS "${_gen}/tr_util.c" "${_gen}/tr_util.h")
list(APPEND files_libgallium "${_gen}/tr_util.c")

# u_tracepoints.c
add_custom_command(
  OUTPUT "${_gen}/u_tracepoints.c"
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/util/u_tracepoints.py -p ${CMAKE_SOURCE_DIR}/src/util/perf/ -C "${_gen}/u_tracepoints.c"
  DEPENDS util/u_tracepoints.py ${u_trace_py}
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "u_tracepoints.c")
add_custom_target(gen_u_tracepoints_c DEPENDS "${_gen}/u_tracepoints.c")
list(APPEND files_libgallium "${_gen}/u_tracepoints.c")

# u_tracepoints.h
add_custom_command(
  OUTPUT "${_gen}/u_tracepoints.h"
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/util/u_tracepoints.py -p ${CMAKE_SOURCE_DIR}/src/util/perf/ -H "${_gen}/u_tracepoints.h"
  DEPENDS util/u_tracepoints.py ${u_trace_py}
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "u_tracepoints.h")
add_custom_target(gen_u_tracepoints_h DEPENDS "${_gen}/u_tracepoints.h")
set(files_u_tracepoints "${_gen}/u_tracepoints.h")
list(APPEND files_libgallium "${files_u_tracepoints}")

add_library(idep_u_tracepoints INTERFACE)
target_sources(idep_u_tracepoints INTERFACE "${files_u_tracepoints}")
add_dependencies(idep_u_tracepoints gen_u_tracepoints_h)

# u_indices_gen.c
add_custom_command(
  OUTPUT "${_gen}/u_indices_gen.c"
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/indices/u_indices_gen.py "${_gen}/u_indices_gen.c"
  DEPENDS indices/u_indices_gen.py
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "u_indices_gen.c")
add_custom_target(gen_u_indices_gen_c DEPENDS "${_gen}/u_indices_gen.c")

# u_unfilled_gen.c
add_custom_command(
  OUTPUT "${_gen}/u_unfilled_gen.c"
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/indices/u_unfilled_gen.py "${_gen}/u_unfilled_gen.c"
  DEPENDS indices/u_unfilled_gen.py
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "u_unfilled_gen.c")
add_custom_target(gen_u_unfilled_gen_c DEPENDS "${_gen}/u_unfilled_gen.c")

# libgallium
add_library(gallium STATIC ${files_libgallium} "${_gen}/u_indices_gen.c" "${_gen}/u_unfilled_gen.c")
set_target_properties(gallium PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES EXCLUDE_FROM_ALL ON)
target_include_directories(gallium PRIVATE
  "${inc_loader}" "${inc_gallium}" "${inc_src}" "${inc_include}" "${CMAKE_CURRENT_SOURCE_DIR}/util")
target_link_libraries(gallium PRIVATE
  ${dep_libdrm} ${dep_llvm} ${dep_dl} ${dep_m} ${dep_thread} ${dep_lmsensors} ${dep_ws2_32}
  idep_nir idep_nir_headers idep_mesautil)
set(_gallium_gen_targets gen_tr_util gen_u_tracepoints_c gen_u_tracepoints_h gen_u_indices_gen_c gen_u_unfilled_gen_c)
if(with_gfx_compute)
  list(APPEND _gallium_gen_targets gen_draw_nir_lower_opcodes)
endif()
if(draw_with_llvm)
  list(APPEND _gallium_gen_targets gen_lp_bld_nir_no_integer_algebraic)
endif()
add_dependencies(gallium ${_gallium_gen_targets})

# _libgalliumvl_stub
add_library(galliumvl_stub STATIC "${CMAKE_CURRENT_SOURCE_DIR}/vl/vl_stubs.c")
set_target_properties(galliumvl_stub PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden EXCLUDE_FROM_ALL ON)
target_include_directories(galliumvl_stub PRIVATE "${inc_gallium}" "${inc_include}" "${inc_src}")
target_link_libraries(galliumvl_stub PRIVATE idep_mesautil)

# libgalliumvl
add_library(galliumvl STATIC ${files_libgalliumvl})
set_target_properties(galliumvl PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden EXCLUDE_FROM_ALL ON)
target_include_directories(galliumvl PRIVATE "${inc_gallium}" "${inc_include}" "${inc_src}")
target_link_libraries(galliumvl PRIVATE idep_nir idep_mesautil)

# some drivers export their screen creation function globally...
if(with_gallium_mediafoundation OR with_gallium_va OR with_dri OR with_gallium_radeonsi)
  set(libgalliumvl_stub galliumvl)
else()
  set(libgalliumvl_stub galliumvl_stub)
endif()

# libgalliumvlwinsys
add_library(galliumvlwinsys STATIC ${files_libgalliumvlwinsys})
set_target_properties(galliumvlwinsys PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden EXCLUDE_FROM_ALL ON)
target_include_directories(galliumvlwinsys PRIVATE
  "${inc_gallium}" "${inc_include}" "${inc_loader}" "${inc_src}")
target_link_libraries(galliumvlwinsys PRIVATE ${dep_libdrm} ${vlwinsys_deps} idep_mesautil ${links_libgalliumvlwinsys})

