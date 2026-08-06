# Mesa3D - src/gallium/drivers/softpipe/ (mirror of meson.build)
# SPDX-License-Identifier: MIT

add_library(softpipe STATIC
  sp_buffer.c
  sp_buffer.h
  sp_clear.c
  sp_clear.h
  sp_context.c
  sp_context.h
  sp_compute.c
  sp_draw_arrays.c
  sp_fence.c
  sp_fence.h
  sp_flush.c
  sp_flush.h
  sp_fs_exec.c
  sp_fs.h
  sp_image.c
  sp_image.h
  sp_limits.h
  sp_prim_vbuf.c
  sp_prim_vbuf.h
  sp_public.h
  sp_quad_blend.c
  sp_quad_depth_test.c
  sp_quad_depth_test_tmp.h
  sp_quad_fs.c
  sp_quad.h
  sp_quad_pipe.c
  sp_quad_pipe.h
  sp_query.c
  sp_query.h
  sp_screen.c
  sp_screen.h
  sp_setup.c
  sp_setup.h
  sp_state_blend.c
  sp_state_clip.c
  sp_state_derived.c
  sp_state_image.c
  sp_state.h
  sp_state_rasterizer.c
  sp_state_sampler.c
  sp_state_shader.c
  sp_state_so.c
  sp_state_surface.c
  sp_state_vertex.c
  sp_surface.c
  sp_surface.h
  sp_tex_sample.c
  sp_tex_sample.h
  sp_tex_tile_cache.c
  sp_tex_tile_cache.h
  sp_texture.c
  sp_texture.h
  sp_tile_cache.c
  sp_tile_cache.h
)
set_target_properties(softpipe PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden EXCLUDE_FROM_ALL ON)
target_include_directories(softpipe PRIVATE
  "${inc_gallium_aux}" "${inc_gallium}" "${inc_include}" "${inc_src}")
target_link_libraries(softpipe PRIVATE idep_nir idep_mesautil)

# driver_softpipe (meson declare_dependency)
add_library(driver_softpipe INTERFACE)
target_link_libraries(driver_softpipe INTERFACE softpipe)
target_compile_definitions(driver_softpipe INTERFACE -DGALLIUM_SOFTPIPE)
