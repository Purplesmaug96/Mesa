/*
 * xenos - hardware (PM4) Gallium driver for the Xbox 360 GPU.
 *
 * Context: binds Gallium state and turns it into Xenos PM4 register writes
 * (RB/PA/SC/SQ state, fetch constants, IM_LOAD shaders and the draw
 * packets).  The state conversion itself is the next milestone; for now the
 * context stores all bound state so the conversion has something to chew on
 * and the classic GL path (glBegin/glVertex via the uploader, glClear, ...)
 * can run end-to-end without crashing.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include "util/u_debug_cb.h"
#include "util/u_helpers.h"
#include "util/u_inlines.h"
#include "util/u_framebuffer.h"
#include "util/u_memory.h"
#include "util/u_upload_mgr.h"
#include "util/format/u_format.h"

#include "pipe/p_defines.h"
#include "pipe/p_state.h"

#include "xenos_context.h"
#include "xenos_private.h"
#include "xenos_resource.h"
#include "xenos_shader.h"

struct xenos_context
{
   struct pipe_context base;

   struct xenos_screen *screen;

   /* Bound state, kept so the PM4 conversion can read it. */
   struct pipe_framebuffer_state framebuffer;
   void *blend;
   void *dsa;
   void *rasterizer;
   void *vs;
   void *fs;
   void *vertex_elements;
   struct pipe_vertex_buffer vertex_buffers[PIPE_MAX_ATTRIBS];
   unsigned num_vertex_buffers;
   struct pipe_blend_color blend_color;
   struct pipe_stencil_ref stencil_ref;
   struct pipe_clip_state clip;
   struct pipe_scissor_state scissor[PIPE_MAX_VIEWPORTS];
   struct pipe_viewport_state viewport[PIPE_MAX_VIEWPORTS];
   struct pipe_sampler_state *samplers[PIPE_MAX_SAMPLERS];
   struct pipe_sampler_view *sampler_views[PIPE_MAX_SHADER_SAMPLER_VIEWS];
   unsigned num_samplers;
   unsigned num_sampler_views;
   struct pipe_constant_buffer constant_buffer[PIPE_MAX_CONSTANT_BUFFERS];
};

static inline struct xenos_context *
xenos_context(struct pipe_context *pipe)
{
   return (struct xenos_context *)pipe;
}

static void
xenos_destroy(struct pipe_context *pipe)
{
   struct xenos_context *x = xenos_context(pipe);

   for (unsigned i = 0; i < PIPE_MAX_ATTRIBS; i++)
      pipe_vertex_buffer_unreference(&x->vertex_buffers[i]);
   for (unsigned i = 0; i < PIPE_MAX_CONSTANT_BUFFERS; i++)
      pipe_resource_reference(&x->constant_buffer[i].buffer, NULL);
   util_unreference_framebuffer_state(&x->framebuffer);

   if (pipe->stream_uploader)
      u_upload_destroy(pipe->stream_uploader);

   FREE(x);
}

/* CSO objects: the driver keeps a private copy of the state struct. */
static void *
xenos_create_blend_state(struct pipe_context *pipe,
                         const struct pipe_blend_state *state)
{
   return mem_dup(state, sizeof(*state));
}

static void
xenos_bind_blend_state(struct pipe_context *pipe, void *cso)
{
   xenos_context(pipe)->blend = cso;
}

static void
xenos_delete_blend_state(struct pipe_context *pipe, void *cso)
{
   FREE(cso);
}

static void *
xenos_create_depth_stencil_alpha_state(struct pipe_context *pipe,
                       const struct pipe_depth_stencil_alpha_state *state)
{
   return mem_dup(state, sizeof(*state));
}

static void
xenos_bind_depth_stencil_alpha_state(struct pipe_context *pipe, void *cso)
{
   xenos_context(pipe)->dsa = cso;
}

static void
xenos_delete_depth_stencil_alpha_state(struct pipe_context *pipe, void *cso)
{
   FREE(cso);
}

static void *
xenos_create_rasterizer_state(struct pipe_context *pipe,
                              const struct pipe_rasterizer_state *state)
{
   return mem_dup(state, sizeof(*state));
}

static void
xenos_bind_rasterizer_state(struct pipe_context *pipe, void *cso)
{
   xenos_context(pipe)->rasterizer = cso;
}

static void
xenos_delete_rasterizer_state(struct pipe_context *pipe, void *cso)
{
   FREE(cso);
}

static void *
xenos_create_fs_state(struct pipe_context *pipe,
                      const struct pipe_shader_state *state)
{
   return xenos_create_shader(pipe->screen, state);
}

static void
xenos_bind_fs_state(struct pipe_context *pipe, void *cso)
{
   xenos_context(pipe)->fs = cso;
}

static void
xenos_delete_fs_state(struct pipe_context *pipe, void *cso)
{
   xenos_delete_shader(cso);
}

static void *
xenos_create_vs_state(struct pipe_context *pipe,
                      const struct pipe_shader_state *state)
{
   return xenos_create_shader(pipe->screen, state);
}

static void
xenos_bind_vs_state(struct pipe_context *pipe, void *cso)
{
   xenos_context(pipe)->vs = cso;
}

static void
xenos_delete_vs_state(struct pipe_context *pipe, void *cso)
{
   xenos_delete_shader(cso);
}

static void *
xenos_create_sampler_state(struct pipe_context *pipe,
                           const struct pipe_sampler_state *state)
{
   return mem_dup(state, sizeof(*state));
}

static void
xenos_bind_sampler_states(struct pipe_context *pipe,
                          mesa_shader_stage shader,
                          unsigned start, unsigned count,
                          void **states)
{
   struct xenos_context *x = xenos_context(pipe);

   if (shader != MESA_SHADER_VERTEX && shader != MESA_SHADER_FRAGMENT)
      return;

   for (unsigned i = 0; i < count; i++)
      x->samplers[start + i] = states[i];
   x->num_samplers = MAX2(x->num_samplers, start + count);
}

static void
xenos_delete_sampler_state(struct pipe_context *pipe, void *cso)
{
   FREE(cso);
}

static struct pipe_sampler_view *
xenos_create_sampler_view(struct pipe_context *pipe,
                          struct pipe_resource *resource,
                          const struct pipe_sampler_view *state)
{
   struct pipe_sampler_view *view = CALLOC_STRUCT(pipe_sampler_view);

   if (!view)
      return NULL;

   *view = *state;
   pipe_reference_init(&view->reference, 1);
   view->texture = NULL;
   pipe_resource_reference(&view->texture, resource);
   view->context = pipe;

   return view;
}

static void
xenos_sampler_view_destroy(struct pipe_context *pipe,
                           struct pipe_sampler_view *view)
{
   pipe_resource_reference(&view->texture, NULL);
   FREE(view);
}

static void
xenos_set_sampler_views(struct pipe_context *pipe,
                        mesa_shader_stage shader,
                        unsigned start, unsigned count,
                        unsigned unbind_num_trailing_slots,
                        struct pipe_sampler_view **views)
{
   struct xenos_context *x = xenos_context(pipe);

   if (shader != MESA_SHADER_VERTEX && shader != MESA_SHADER_FRAGMENT)
      return;

   for (unsigned i = 0; i < count; i++)
      pipe_sampler_view_reference(&x->sampler_views[start + i], views[i]);
   for (unsigned i = 0; i < unbind_num_trailing_slots; i++)
      pipe_sampler_view_reference(&x->sampler_views[start + count + i], NULL);
   x->num_sampler_views = MAX2(x->num_sampler_views, start + count);
}

static void *
xenos_create_vertex_elements_state(struct pipe_context *pipe,
                                   unsigned count,
                                   const struct pipe_vertex_element *state)
{
   struct pipe_vertex_element *dup =
      mem_dup(state, count * sizeof(*state));

   if (!dup)
      return NULL;
   return dup;
}

static void
xenos_bind_vertex_elements_state(struct pipe_context *pipe, void *cso)
{
   xenos_context(pipe)->vertex_elements = cso;
}

static void
xenos_delete_vertex_elements_state(struct pipe_context *pipe, void *cso)
{
   FREE(cso);
}

static void
xenos_set_constant_buffer(struct pipe_context *pipe,
                          mesa_shader_stage shader, unsigned index,
                          const struct pipe_constant_buffer *cb)
{
   struct xenos_context *x = xenos_context(pipe);

   if (shader != MESA_SHADER_VERTEX && shader != MESA_SHADER_FRAGMENT)
      return;

   pipe_resource_reference(&x->constant_buffer[index].buffer, NULL);
   if (cb) {
      x->constant_buffer[index] = *cb;
      pipe_resource_reference(&x->constant_buffer[index].buffer, cb->buffer);
   }
}

static void
xenos_set_framebuffer_state(struct pipe_context *pipe,
                            const struct pipe_framebuffer_state *state)
{
   struct xenos_context *x = xenos_context(pipe);

   util_copy_framebuffer_state(&x->framebuffer, state);
}

static void
xenos_set_blend_color(struct pipe_context *pipe,
                      const struct pipe_blend_color *color)
{
   xenos_context(pipe)->blend_color = *color;
}

static void
xenos_set_stencil_ref(struct pipe_context *pipe,
                      const struct pipe_stencil_ref ref)
{
   xenos_context(pipe)->stencil_ref = ref;
}

static void
xenos_set_clip_state(struct pipe_context *pipe,
                     const struct pipe_clip_state *state)
{
   xenos_context(pipe)->clip = *state;
}

static void
xenos_set_scissor_states(struct pipe_context *pipe,
                         unsigned start, unsigned count,
                         const struct pipe_scissor_state *states)
{
   struct xenos_context *x = xenos_context(pipe);

   for (unsigned i = 0; i < count; i++)
      x->scissor[start + i] = states[i];
}

static void
xenos_set_viewport_states(struct pipe_context *pipe,
                          unsigned start, unsigned count,
                          const struct pipe_viewport_state *states)
{
   struct xenos_context *x = xenos_context(pipe);

   for (unsigned i = 0; i < count; i++)
      x->viewport[start + i] = states[i];
}

static void
xenos_set_vertex_buffers(struct pipe_context *pipe,
                         unsigned count,
                         const struct pipe_vertex_buffer *buffers)
{
   struct xenos_context *x = xenos_context(pipe);

   for (unsigned i = 0; i < count; i++) {
      pipe_vertex_buffer_unreference(&x->vertex_buffers[i]);
      pipe_vertex_buffer_reference(&x->vertex_buffers[i], &buffers[i]);
   }
   x->num_vertex_buffers = MAX2(x->num_vertex_buffers, count);
}

static void
xenos_render_condition(struct pipe_context *pipe,
                       struct pipe_query *query, bool condition,
                       enum pipe_render_cond_flag mode)
{
}

static void
xenos_clear(struct pipe_context *pipe, unsigned buffers,
            uint32_t color_clear_mask, uint8_t stencil_clear_mask,
            const struct pipe_scissor_state *scissor_state,
            const union pipe_color_union *color, double depth,
            unsigned stencil)
{
   /* The EDRAM clear (kCopy / fast-clear path) replaces this once the
    * framebuffer state conversion lands. */
   fprintf(stderr, "xenos: clear 0x%x\n", buffers);
}

static void
xenos_draw_vbo(struct pipe_context *pipe,
               const struct pipe_draw_info *dinfo,
               unsigned drawid_offset,
               const struct pipe_draw_indirect_info *indirect,
               const struct pipe_draw_start_count_bias *draws,
               unsigned num_draws)
{
   fprintf(stderr, "xenos: draw_vbo %u draws\n", num_draws);
}

static void
xenos_flush(struct pipe_context *pipe, struct pipe_fence_handle **fence,
            unsigned flags)
{
}

static void *
xenos_buffer_map(struct pipe_context *pipe,
                 struct pipe_resource *pres,
                 unsigned level, unsigned usage,
                 const struct pipe_box *box,
                 struct pipe_transfer **out_transfer)
{
   struct xenos_resource *res = xenos_resource(pres);
   struct pipe_transfer *transfer = CALLOC_STRUCT(pipe_transfer);

   if (!transfer)
      return NULL;

   assert(level == 0);

   transfer->resource = pres;
   pipe_resource_reference(&transfer->resource, pres);
   transfer->level = level;
   transfer->usage = usage;
   transfer->box = *box;
   transfer->stride = res->stride;
   transfer->layer_stride = res->size;

   *out_transfer = transfer;

   return (char *)res->data + box->x;
}

static void
xenos_buffer_unmap(struct pipe_context *pipe,
                   struct pipe_transfer *transfer)
{
   pipe_resource_reference(&transfer->resource, NULL);
   FREE(transfer);
}

static void *
xenos_texture_map(struct pipe_context *pipe,
                  struct pipe_resource *pres,
                  unsigned level, unsigned usage,
                  const struct pipe_box *box,
                  struct pipe_transfer **out_transfer)
{
   struct xenos_resource *res = xenos_resource(pres);
   struct pipe_transfer *transfer = CALLOC_STRUCT(pipe_transfer);

   if (!transfer)
      return NULL;

   transfer->resource = pres;
   pipe_resource_reference(&transfer->resource, pres);
   transfer->level = level;
   transfer->usage = usage;
   transfer->box = *box;
   transfer->stride = res->stride;
   transfer->layer_stride = res->size;

   *out_transfer = transfer;

   unsigned blocksize = util_format_get_blocksize(pres->format);
   unsigned offset = box->z * transfer->layer_stride +
                     box->y * res->stride +
                     box->x * blocksize;

   return (char *)res->data + offset;
}

static void
xenos_texture_unmap(struct pipe_context *pipe,
                    struct pipe_transfer *transfer)
{
   pipe_resource_reference(&transfer->resource, NULL);
   FREE(transfer);
}

static void
xenos_transfer_flush_region(struct pipe_context *pipe,
                            struct pipe_transfer *transfer,
                            const struct pipe_box *box)
{
}

static void
xenos_buffer_subdata(struct pipe_context *pipe,
                     struct pipe_resource *pres,
                     unsigned usage, unsigned offset,
                     unsigned size, const void *data)
{
   struct xenos_resource *res = xenos_resource(pres);

   memcpy((char *)res->data + offset, data, size);
}

struct pipe_context *
xenos_create_context(struct pipe_screen *screen, void *priv, unsigned flags)
{
   struct xenos_context *x = CALLOC_STRUCT(xenos_context);

   if (!x)
      return NULL;

   x->screen = xenos_screen(screen);

   x->base.screen = screen;
   x->base.priv = priv;

   x->base.destroy = xenos_destroy;

   x->base.create_blend_state = xenos_create_blend_state;
   x->base.bind_blend_state = xenos_bind_blend_state;
   x->base.delete_blend_state = xenos_delete_blend_state;

   x->base.create_depth_stencil_alpha_state = xenos_create_depth_stencil_alpha_state;
   x->base.bind_depth_stencil_alpha_state = xenos_bind_depth_stencil_alpha_state;
   x->base.delete_depth_stencil_alpha_state = xenos_delete_depth_stencil_alpha_state;

   x->base.create_rasterizer_state = xenos_create_rasterizer_state;
   x->base.bind_rasterizer_state = xenos_bind_rasterizer_state;
   x->base.delete_rasterizer_state = xenos_delete_rasterizer_state;

   x->base.create_fs_state = xenos_create_fs_state;
   x->base.bind_fs_state = xenos_bind_fs_state;
   x->base.delete_fs_state = xenos_delete_fs_state;

   x->base.create_vs_state = xenos_create_vs_state;
   x->base.bind_vs_state = xenos_bind_vs_state;
   x->base.delete_vs_state = xenos_delete_vs_state;

   x->base.create_sampler_state = xenos_create_sampler_state;
   x->base.bind_sampler_states = xenos_bind_sampler_states;
   x->base.delete_sampler_state = xenos_delete_sampler_state;

   x->base.create_sampler_view = xenos_create_sampler_view;
   x->base.sampler_view_destroy = xenos_sampler_view_destroy;
   x->base.set_sampler_views = xenos_set_sampler_views;

   x->base.create_vertex_elements_state = xenos_create_vertex_elements_state;
   x->base.bind_vertex_elements_state = xenos_bind_vertex_elements_state;
   x->base.delete_vertex_elements_state = xenos_delete_vertex_elements_state;

   x->base.set_constant_buffer = xenos_set_constant_buffer;
   x->base.set_framebuffer_state = xenos_set_framebuffer_state;
   x->base.set_blend_color = xenos_set_blend_color;
   x->base.set_stencil_ref = xenos_set_stencil_ref;
   x->base.set_clip_state = xenos_set_clip_state;
   x->base.set_scissor_states = xenos_set_scissor_states;
   x->base.set_viewport_states = xenos_set_viewport_states;
   x->base.set_vertex_buffers = xenos_set_vertex_buffers;

   x->base.render_condition = xenos_render_condition;
   x->base.set_debug_callback = u_default_set_debug_callback;

   x->base.clear = xenos_clear;
   x->base.draw_vbo = xenos_draw_vbo;
   x->base.flush = xenos_flush;

   x->base.buffer_map = xenos_buffer_map;
   x->base.buffer_unmap = xenos_buffer_unmap;
   x->base.texture_map = xenos_texture_map;
   x->base.texture_unmap = xenos_texture_unmap;
   x->base.transfer_flush_region = xenos_transfer_flush_region;
   x->base.buffer_subdata = xenos_buffer_subdata;

   x->base.stream_uploader = u_upload_create_default(&x->base);
   if (!x->base.stream_uploader) {
      FREE(x);
      return NULL;
   }
   x->base.const_uploader = x->base.stream_uploader;

   return &x->base;
}