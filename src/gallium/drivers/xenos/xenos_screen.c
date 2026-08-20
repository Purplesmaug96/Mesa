/*
 * xenos - hardware (PM4) Gallium driver for the Xbox 360 GPU.
 *
 * Screen creation and capability reporting.
 *
 * The cap table is inherited from softpipe for now: it is the known-good
 * baseline for the GL 3.3 compatibility context that the xbox360 target
 * requests.  Once the real state conversion and shader compiler land, the
 * caps that matter (max texture sizes, max varyings, format support, ...)
 * are tightened to actual Xenos limits.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include "compiler/nir/nir.h"

#include "util/u_helpers.h"
#include "util/u_memory.h"
#include "util/u_screen.h"
#include "util/format/u_format.h"

#include "pipe/p_defines.h"
#include "pipe/p_screen.h"

#include "tgsi/tgsi_exec.h"

#include "xenos_private.h"
#include "xenos_context.h"
#include "xenos_resource.h"
#include "xenos_screen.h"
#include "xenos_shader.h"

static const struct nir_shader_compiler_options xenos_compiler_options = {
   .fdot_replicates = true,
   .float_mul_add32 =
      nir_float_muladd_support_has_fmad |
      nir_float_muladd_support_fuse,
   .lower_extract_byte = true,
   .lower_extract_word = true,
   .lower_insert_byte = true,
   .lower_insert_word = true,
   .lower_fdph = true,
   .lower_flrp64 = true,
   .lower_fmod = true,
   .lower_uniforms_to_ubo = true,
   .lower_int64_options = nir_lower_imul_2x32_64,
   .max_unroll_iterations = 32,
   .support_indirect_inputs = (uint8_t)BITFIELD_MASK(MESA_SHADER_STAGES),
   .support_indirect_outputs = (uint8_t)BITFIELD_MASK(MESA_SHADER_STAGES),
};

static const char *
xenos_get_name(struct pipe_screen *screen)
{
   return "xenos";
}

static const char *
xenos_get_vendor(struct pipe_screen *screen)
{
   return "Mesa";
}

static const char *
xenos_get_device_vendor(struct pipe_screen *screen)
{
   return "ATI";
}

static int
xenos_screen_get_fd(struct pipe_screen *screen)
{
   return -1;
}

static bool
xenos_is_format_supported(struct pipe_screen *screen,
                          enum pipe_format format,
                          enum pipe_texture_target target,
                          unsigned sample_count,
                          unsigned storage_sample_count,
                          unsigned bind)
{
   const struct util_format_description *format_desc;

   if (MAX2(1, sample_count) != MAX2(1, storage_sample_count))
      return false;

   format_desc = util_format_description(format);
   if (!format_desc)
      return false;

   if (sample_count > 1)
      return false;

   if (bind & PIPE_BIND_RENDER_TARGET) {
      if (format_desc->colorspace == UTIL_FORMAT_COLORSPACE_ZS)
         return false;

      if (format_desc->block.width != 1 ||
          format_desc->block.height != 1)
         return false;
   }

   if (bind & PIPE_BIND_DEPTH_STENCIL) {
      if (format_desc->colorspace != UTIL_FORMAT_COLORSPACE_ZS)
         return false;
   }

   return true;
}

static void
xenos_init_shader_caps(struct xenos_screen *screen)
{
   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
      struct pipe_shader_caps *caps =
         (struct pipe_shader_caps *)&screen->base.shader_caps[i];

      switch (i) {
      case MESA_SHADER_VERTEX:
      case MESA_SHADER_FRAGMENT:
         tgsi_exec_init_shader_caps(caps);
         caps->supported_irs = 1 << PIPE_SHADER_IR_NIR;
         break;
      default:
         /* No geometry/tessellation/compute on the console. */
         break;
      }
   }
}

static void
xenos_init_screen_caps(struct xenos_screen *screen)
{
   struct pipe_caps *caps = (struct pipe_caps *)&screen->base.caps;

   u_init_pipe_screen_caps(&screen->base, 0);

   caps->npot_textures = true;
   caps->mixed_framebuffer_sizes = true;
   caps->mixed_color_depth_bits = true;
   caps->fragment_shader_texture_lod = true;
   caps->fragment_shader_derivatives = true;
   caps->anisotropic_filter = true;
   caps->max_render_targets = 4;    /* Xenos has 4 colour render targets */
   caps->occlusion_query = false;   /* no query objects yet */
   caps->texture_mirror_clamp = true;
   caps->texture_mirror_clamp_to_edge = true;
   caps->texture_swizzle = true;
   caps->max_texture_2d_size = 8192;
   caps->max_texture_3d_levels = 11;
   caps->max_texture_cube_levels = 13;
   caps->blend_equation_separate = true;
   caps->indep_blend_enable = true;
   caps->indep_blend_func = true;
   caps->fs_coord_origin_upper_left = true;
   caps->depth_clip_disable = true;
   caps->depth_bounds_test = true;
   caps->max_vertex_attrib_stride = 2048;
   caps->primitive_restart = true;
   caps->primitive_restart_fixed_index = true;
   caps->vs_instanceid = true;
   caps->vertex_element_instance_divisor = true;
   caps->start_instance = true;
   caps->seamless_cube_map = true;
   caps->seamless_cube_map_per_texture = true;
   caps->max_texture_array_layers = 256;
   caps->min_texel_offset = -8;
   caps->max_texel_offset = 7;
   caps->conditional_render = false;
   caps->fragment_color_clamped = true;
   caps->vertex_color_unclamped = true;
   caps->vertex_color_clamped = true;
   caps->glsl_feature_level =
   caps->glsl_feature_level_compatibility = 330;
   caps->compute = false;
   caps->user_vertex_buffers = true;
   caps->vs_layer_viewport = true;
   caps->constant_buffer_offset_alignment = 16;
   caps->min_map_buffer_alignment = 64;
   caps->cube_map_array = true;
   caps->texture_buffer_objects = true;
   caps->max_texel_buffer_elements = 65536;
   caps->texture_buffer_offset_alignment = 16;
   caps->texture_transfer_modes = 0;
   caps->max_viewports = PIPE_MAX_VIEWPORTS;
   caps->endianness = PIPE_ENDIAN_NATIVE;
   caps->max_texture_gather_components = 4;
   caps->sampler_view_target = true;
   caps->fake_sw_msaa = true;
   caps->draw_indirect = false;
   caps->shareable_shaders = false;

   caps->vendor_id = 0x1002; /* ATI */
   caps->device_id = 0x00FF; /* R500-family */

   caps->uma = true; /* unified memory: EDRAM + system RAM */
   caps->clip_halfz = true;
   caps->texture_float_linear = true;
   caps->texture_half_float_linear = true;
   caps->framebuffer_no_attachment = true;
   caps->cull_distance = true;
   caps->shader_array_components = true;
   caps->tgsi_texcoord = true;
   caps->max_varyings = 16; /* Xenos: 16 interpolated registers */
   caps->max_gs_invocations = 0;
   caps->max_shader_buffer_size = 1 << 27;
   caps->shader_buffer_offset_alignment = 4;
   caps->image_store_formatted = false;

   caps->min_line_width =
   caps->min_line_width_aa =
   caps->min_point_size =
   caps->min_point_size_aa = 1;
   caps->point_size_granularity =
   caps->line_width_granularity = 0.1;
   caps->max_line_width =
   caps->max_line_width_aa = 255.0;
   caps->max_point_size =
   caps->max_point_size_aa = 255.0;
   caps->max_texture_anisotropy = 16.0;
   caps->max_texture_lod_bias = 16.0;
}

static void
xenos_destroy_screen(struct pipe_screen *screen)
{
   FREE(screen);
}

static void
xenos_flush_frontbuffer(struct pipe_screen *screen,
                        struct pipe_context *pipe,
                        struct pipe_resource *resource,
                        unsigned level, unsigned layer,
                        void *context_private,
                        unsigned nboxes,
                        struct pipe_box *sub_box)
{
   /* The xbox360 target presents through its own scanout path. */
}

struct pipe_screen *
xenos_screen_create(struct xenos_winsys *ws)
{
   struct xenos_screen *screen;

   if (!ws || !ws->alloc || !ws->free)
      return NULL;

   screen = CALLOC_STRUCT(xenos_screen);
   if (!screen)
      return NULL;

   screen->ws = ws;
   screen->base.winsys_priv = ws;

   screen->base.destroy = xenos_destroy_screen;
   screen->base.get_name = xenos_get_name;
   screen->base.get_vendor = xenos_get_vendor;
   screen->base.get_device_vendor = xenos_get_device_vendor;
   screen->base.get_screen_fd = xenos_screen_get_fd;
   screen->base.get_timestamp = u_default_get_timestamp;
   screen->base.is_format_supported = xenos_is_format_supported;
   screen->base.context_create = xenos_create_context;
   screen->base.flush_frontbuffer = xenos_flush_frontbuffer;

   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++)
      screen->base.nir_options[i] = &xenos_compiler_options;

   xenos_init_screen_caps(screen);
   xenos_init_shader_caps(screen);
   xenos_init_screen_resource_funcs(&screen->base);

   return &screen->base;
}