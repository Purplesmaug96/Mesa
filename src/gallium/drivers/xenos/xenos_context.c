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
#include <string.h>

#include <xecore/xboxkrnl.h>

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

#include "gpu/xenos_gpu.h"
#include "gpu/xenos_ucode.h"
#include "gpu/xenos_fetch.h"

#include "../xenos_public.h"

/* Xenos vertex surface formats (a2xx_sq_surfaceformat). */
#define XE_VFMT_8_8_8_8            6u
#define XE_VFMT_2_10_10_10         7u
#define XE_VFMT_16_16              25u
#define XE_VFMT_16_16_FLOAT        31u
#define XE_VFMT_16_16_16_16        26u
#define XE_VFMT_16_16_16_16_FLOAT  32u
#define XE_VFMT_32_FLOAT           36u
#define XE_VFMT_32_32_FLOAT        37u
#define XE_VFMT_32_32_32_32_FLOAT  38u
#define XE_VFMT_32_32_32_FLOAT     57u

struct xenos_context
{
   struct pipe_context base;

   struct xenos_screen *screen;

   /* Bound state, kept so the PM4 conversion can read it. */
   struct pipe_framebuffer_state framebuffer;
   void *blend;
   void *dsa;
   void *rasterizer;
   struct xenos_shader *vs;
   struct xenos_shader *fs;
   void *vertex_elements;
   unsigned num_vertex_elements;
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

   /* Command stream: dwords accumulate here and go to the shared primary
    * ring on flush points (buffer full, clear-to-draw boundaries, frame
    * end).  The ring itself is shared with VdSwap; wptr/rptr are tracked
    * free-running so wraparound reuses space only after the CP passed it. */
   struct xenos_cmdbuf cb;
   xenos_ring ring;
   bool ring_ready;
   uint64_t wptr_linear;     /* never-masked mirror of *wptr_slot */
   uint64_t rptr_linear;     /* completed dwords, derived from rptr page */
   uint32_t rptr_prev_mod;

   /* Resolve rectangle vertices (0,0)-(w,h), guest-endian floats. */
   void *resolve_rect;
   uint32_t resolve_rect_phys;

   /* Clear fullscreen triangle vertices (NDC [-1..3]), guest-endian floats. */
   void *clear_vert;
   uint32_t clear_vert_phys;

   /* Clear program: fullscreen triangle + solid colour from PS c48. */
   uint32_t clear_vs[64];
   uint32_t clear_vs_dwords;
   uint32_t clear_ps[64];
   uint32_t clear_ps_dwords;
};

static inline struct xenos_context *
xenos_context(struct pipe_context *pipe)
{
   return (struct xenos_context *)pipe;
}

/* ------------------------------------------------------------------ */
/* Command stream submission                                          */
/* ------------------------------------------------------------------ */

#include <xecore/xboxkrnl.h>

/* Reconcile the masked rptr page into the free-running completion counter.
 * Standard monotonic-mod tracking: forward deltas accumulate, anything that
 * looks like a backward jump is a stale read and ignored. */
static void
ctx_track_rptr(struct xenos_context *x)
{
   if (!x->ring_ready || !x->screen->ws->rptr_page)
      return;

   /* Xenia writes its free-running read index into the writeback page.  It
    * is already monotonic and free-running (the CP wraps it modulo the ring
    * internally), so trust it directly: a modulo-delta with a half-ring
    * "stale" threshold deadlocks on a small ring once the CP advances more
    * than half the ring between two polls (delta then reads as a backward
    * jump and is rejected, leaving rptr at 0 and the ring "permanently
    * full"). */
   x->rptr_linear = *x->screen->ws->rptr_page;
   x->rptr_prev_mod =
      (uint32_t)x->rptr_linear & (x->ring.size_dwords - 1u);
}

/* Submit everything accumulated as one 64-aligned block, waiting for room
 * in the shared ring first. */
static void
ctx_submit(struct xenos_context *x)
{
   struct xenos_winsys *ws = x->screen->ws;
   struct xenos_cmdbuf *cb = &x->cb;
   uint32_t block, prev_mod;
   int spins = 0;

   if (!cb->used)
      return;

   if (!ws->ring_buffer || !ws->wptr_slot) {
      /* Ring not attached yet - nothing we can do with the commands. */
      cb->used = 0;
      return;
   }

   if (!x->ring_ready) {
      x->ring.buffer = ws->ring_buffer;
      /* Xenia's ring is (1 << (size_log2 + 3)) BYTES (= 64 KiB here, i.e.
       * 16384 dwords).  The free-running read/write indices are dword counts
       * that the CP wraps modulo this ring size. */
      x->ring.size_dwords = 1u << (ws->ring_size_log2 + 1);
      x->ring.write_ptr = 0;
      cb->ring = &x->ring;
      x->wptr_linear = *ws->wptr_slot;
      /* Assume everything up to the attach point completed long ago. */
      x->rptr_linear = x->wptr_linear;
       x->rptr_prev_mod = (uint32_t)x->wptr_linear & (x->ring.size_dwords - 1u);
       x->ring_ready = true;
    }

    /* The shared write-pointer slot is also advanced by screen_present/VdSwap
     * (it appends its own 64-dword block to the same ring).  Re-sync our
     * free-running write pointer from the slot on every submit so we never
     * overwrite present's block (or a block a concurrent submit just placed). */
    if (ws->wptr_slot) {
       uint32_t slot = *ws->wptr_slot;
       if ((uint32_t)x->wptr_linear < slot)
          x->wptr_linear = slot;
    }

    /* Pad to a 64-dword boundary with Type-2 NOP fillers so every published
     * dword parses as a valid packet. */
   block = (cb->used + 63u) & ~63u;
   while (cb->used < block)
      cb->dwords[cb->used++] = 0x80000000u; /* Type-2 NOP after BE store */

   /* Never overwrite dwords the CP has not consumed yet. */
   while (x->wptr_linear - x->rptr_linear + block > x->ring.size_dwords) {
      ctx_track_rptr(x);
      if (++spins > 4000000) {
         /* ~400 s: drop rather than hang the GL thread forever. */
         DbgPrint("xenos: ring full, dropping %u dwords", block);
         cb->used = 0;
         return;
      }
      KeDelayExecutionThread(0, 0, &(int64_t){-1000});
   }

   prev_mod = (uint32_t)x->wptr_linear & (x->ring.size_dwords - 1u);
   x->ring.write_ptr = prev_mod;
   xe_gpu_ring_submit(&x->ring, cb->dwords, block);

   /* xe_gpu_ring_submit advanced write_ptr modulo size; fold the possible
    * wrap back into the free-running counter. */
   {
      uint32_t new_mod = x->ring.write_ptr;
      uint64_t base = x->wptr_linear & ~(uint64_t)(x->ring.size_dwords - 1u);
      x->wptr_linear = base + new_mod;
   }

   /* Publish the free-running write index to the CP.  The CP treats both the
    * read and write pointers as free-running dword indices and wraps them
    * modulo the ring size internally, so we must expose the *unmasked*
    * counter (never the masked modulo value). */
   *((volatile uint32_t *)XE_MMIO_ADDR(XE_REG_CP_RB_WPTR)) =
      (uint32_t)x->wptr_linear;

   /* Publish the updated free-running wptr so VdSwap (called later during
    * present) writes its swap block AFTER our block rather than overwriting
    * it at the same ring offset.  The slot points at the guest's own
    * screen->xenia_ring_wptr; we are guest (big-endian) code, so a plain
    * store of the free-running counter is what the guest expects to read. */
   if (x->screen->ws->wptr_slot)
      *x->screen->ws->wptr_slot = (uint32_t)x->wptr_linear;
   cb->used = 0;
}

/* Make sure n more dwords fit; flush first otherwise. */
static void
ctx_reserve(struct xenos_context *x, unsigned n)
{
   if (x->cb.used + n > XE_GPU_CMDBUF_DWORDS - 128u)
      ctx_submit(x);
}

static void
xenos_destroy(struct pipe_context *pipe)
{
   struct xenos_context *x = xenos_context(pipe);

   ctx_submit(x);
   for (unsigned i = 0; i < PIPE_MAX_ATTRIBS; i++)
      pipe_vertex_buffer_unreference(&x->vertex_buffers[i]);
   for (unsigned i = 0; i < PIPE_MAX_CONSTANT_BUFFERS; i++)
      pipe_resource_reference(&x->constant_buffer[i].buffer, NULL);
   util_unreference_framebuffer_state(&x->framebuffer);

   if (pipe->stream_uploader)
      u_upload_destroy(pipe->stream_uploader);

   if (x->cb.dwords)
      FREE(x->cb.dwords);
   if (x->resolve_rect)
      x->screen->ws->free(x->screen->ws, x->resolve_rect);
   if (x->clear_vert)
      x->screen->ws->free(x->screen->ws, x->clear_vert);

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

struct xenos_velems
{
   unsigned count;
   struct pipe_vertex_element elements[PIPE_MAX_ATTRIBS];
};

static void *
xenos_create_vertex_elements_state(struct pipe_context *pipe,
                                   unsigned count,
                                   const struct pipe_vertex_element *state)
{
   struct xenos_velems *ve = MALLOC_STRUCT(xenos_velems);

   if (!ve || count > PIPE_MAX_ATTRIBS)
      return NULL;
   ve->count = count;
   memcpy(ve->elements, state, count * sizeof(*state));
   return ve;
}

static void
xenos_bind_vertex_elements_state(struct pipe_context *pipe, void *cso)
{
   struct xenos_context *x = xenos_context(pipe);
   struct xenos_velems *ve = cso;

   x->vertex_elements = ve;
   x->num_vertex_elements = ve ? ve->count : 0;
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

/* ------------------------------------------------------------------ */
/* State -> PM4 emission                                              */
/* ------------------------------------------------------------------ */

static void
xenos_load_shader_raw(struct xenos_context *x, const uint32_t *ucode,
                      uint32_t dwords, uint32_t type);

static uint32_t
xenos_vfmt(enum pipe_format fmt)
{
   switch (fmt) {
   case PIPE_FORMAT_R32G32B32A32_FLOAT: return XE_VFMT_32_32_32_32_FLOAT;
   case PIPE_FORMAT_R32G32B32_FLOAT:    return XE_VFMT_32_32_32_FLOAT;
   case PIPE_FORMAT_R32G32_FLOAT:       return XE_VFMT_32_32_FLOAT;
   case PIPE_FORMAT_R32_FLOAT:          return XE_VFMT_32_FLOAT;
   case PIPE_FORMAT_R16G16B16A16_FLOAT: return XE_VFMT_16_16_16_16_FLOAT;
   case PIPE_FORMAT_R16G16_FLOAT:       return XE_VFMT_16_16_FLOAT;
   case PIPE_FORMAT_R16G16B16A16_UNORM: return XE_VFMT_16_16_16_16;
   case PIPE_FORMAT_R8G8B8A8_UNORM:
   case PIPE_FORMAT_R8G8B8X8_UNORM:
   case PIPE_FORMAT_B8G8R8A8_UNORM:     return XE_VFMT_8_8_8_8;
   default:                             return XE_VFMT_32_32_32_32_FLOAT;
   }
}

static uint32_t
float_bits(float f)
{
   union { float f; uint32_t u; } u = { f };
   return u.u;
}

static void
xenos_prim_to_initiator(struct xenos_context *x, unsigned mode, unsigned count)
{
   /* xenos PrimitiveType (xenia xenos.h). */
   static const uint8_t map[MESA_PRIM_MAX] = {
      [MESA_PRIM_POINTS]        = 1,
      [MESA_PRIM_LINES]         = 2,
      [MESA_PRIM_LINE_STRIP]    = 3,
      [MESA_PRIM_TRIANGLES]     = 4,
      [MESA_PRIM_TRIANGLE_STRIP]= 5,
      [MESA_PRIM_TRIANGLE_FAN]  = 6,
      [MESA_PRIM_QUADS]         = 4,   /* st lowers; fall back to tris */
      [MESA_PRIM_LINES_ADJACENCY]= 2,
   };
   uint32_t prim = mode < MESA_PRIM_MAX && map[mode] ? map[mode] : 4u;
   uint32_t initiator = prim |
                        (XE_SOURCE_SEL_AUTO_INDEX << 6) |
                        (XE_MAJOR_MODE_IMPLICIT << 8) |
                        ((count & 0xFFFF) << 16);
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_VGT_MAX_VTX_INDX + 0, count ? count - 1 : 0);
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_VGT_MIN_VTX_INDX, 0);
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_VGT_INDX_OFFSET, 0);
   xe_gpu_cmd_draw(&x->cb, initiator, 0, 0, 0);
}

/* Full render state: surfaces, viewport/scissor, shader program control.
 * Emitted before every draw/clear - correctness first, dirty tracking is a
 * later optimisation. */
static void
xenos_emit_frame_state(struct xenos_context *x)
{
   struct pipe_framebuffer_state *fb = &x->framebuffer;
   struct xenos_resource *color =
      (fb->cbufs[0].texture)
         ? xenos_resource(fb->cbufs[0].texture) : NULL;
   struct xenos_resource *depth =
      (fb->zsbuf.texture)
         ? xenos_resource(fb->zsbuf.texture) : NULL;
    uint32_t w = fb->width, h = fb->height;

    if (!w || !h)
       return;

    ctx_reserve(x, 256);

    {
       unsigned cw = color ? color->base.width0 : 0;
       unsigned ch = color ? color->base.height0 : 0;
       DbgPrint("FRAMEINFO fbw=%u fbh=%u cw=%u ch=%u pitch_field=%u",
                w, h, cw, ch, w & 0x3FFF);
    }

    /* Surfaces. */
    xe_gpu_cmd_reg_write(&x->cb, XE_REG_RB_SURFACE_INFO, w | (XE_MSAA_1X << 16));
   {
      /* color_base: bits 0-11 (tile index), format bits 16-19. */
      uint32_t ci = color ? ((color->edram_base & 0x7FF) |
                             ((color->edram_base >> 11) << 11) |
                             (XE_COLOR_FORMAT_8_8_8_8 << 16)) : 0;
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_RB_COLOR_INFO, ci);
   }
   {
      /* depth_format bit: 0 = kD24S8. */
      uint32_t di;
      if (depth) {
         di = ((depth->edram_base & 0x7FF) |
               ((depth->edram_base >> 11) << 11));
      } else {
         /* WORKAROUND: Always emit a valid RB_DEPTHINFO so the host includes
          * a depth attachment in the render pass.  Without this, draws without
          * a depth buffer hit a host code path that enables depth testing with
          * NEVER comparison, killing every fragment.
          *
          * TODO(Xbox360): Determine whether real Xbox 360 hardware also
          * requires a depth surface to be configured even when depth testing
          * is disabled, or if this is purely a host emulation bug. */
         di = ((x->screen->dummy_depth_edram_base & 0x7FF) |
               ((x->screen->dummy_depth_edram_base >> 11) << 11));
      }
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_RB_DEPTH_INFO, di);
   }
   {
      /* Depth test controls.  Emit from the bound DSA state so the host does
       * not inherit a stale RB_DEPTHCONTROL (default zfunc=NEVER, which would
       * reject every fragment).  When depth testing is disabled, use ALWAYS so
       * the host keeps depth testing off.  PIPE_FUNC_* and the Xenos
       * CompareFunction share the same numeric encoding. */
      const struct pipe_depth_stencil_alpha_state *dsa =
         x->dsa ? (const struct pipe_depth_stencil_alpha_state *)x->dsa : NULL;
      bool depth_enabled = dsa && dsa->depth_enabled;
      bool depth_writemask = dsa && dsa->depth_writemask;
      uint32_t zfunc = dsa ? (uint32_t)dsa->depth_func
                           : (uint32_t)PIPE_FUNC_ALWAYS;
      /* WORKAROUND: When depth testing is disabled, force z_enable=1 with
       * zfunc=ALWAYS so the host includes the depth attachment in the render
       * pass.  The host gates depth RT inclusion on z_enable, not on whether
       * RB_DEPTHINFO is set.  Without this, the host enables depth test with
       * NEVER comparison when there's no depth attachment, killing all
       * fragments.
       *
       * TODO(Xbox360): Determine whether real Xbox 360 hardware also
       * requires z_enable to be set even when depth testing is functionally
       * off, or if this is purely a host emulation bug. */
      if (!depth_enabled) {
         depth_enabled = true;
         zfunc = (uint32_t)PIPE_FUNC_ALWAYS;
         depth_writemask = false;
      }
      uint32_t dc = (depth_enabled ? (1u << 1) : 0) |
                    (depth_writemask ? (1u << 2) : 0) |
                    (zfunc << 4);
      DbgPrint("[DCDBG] RB_DEPTHCONTROL dc=%08x dsa=%p fen=%d fwr=%d ffunc=%u",
               dc, (void *)dsa, depth_enabled, depth_writemask, zfunc);
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_RB_DEPTHCONTROL, dc);
   }

   /* Scissors/cliprect: full target. */
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_SC_SCREEN_SCISSOR_TL, 0);
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_SC_SCREEN_SCISSOR_BR,
                        w | (h << 16));
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_SC_WINDOW_OFFSET, 0);
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_SC_WINDOW_SCISSOR_TL, 0x80000000u);
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_SC_WINDOW_SCISSOR_BR,
                        w | (h << 16));
   {
      uint32_t rule[3] = { 0xFFFFu, 0u, w | (h << 16) };
      xe_gpu_cmd_reg_writen(&x->cb, XE_REG_PA_SC_CLIPRECT_RULE, 3, rule);
   }

   /* Colour write, no blending (opaque) for M1. */
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_RB_COLOR_MASK, 0xF);
   /* No blending: src=kOne(1), dest=kZero(0), ADD(0) on colour and alpha.
    * Must be emitted explicitly, otherwise a stale/zero RB_BLENDCONTROL
    * (src=kZero) makes the host blend the output to zero and nothing shows. */
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_RB_BLENDCONTROL0, 0x00010001u);

   /* Viewport transform.  Gallium gives GL-style scale/translate; the Xenos
    * D3D pixel-center convention needs a half-pixel shift. */
   {
      const struct pipe_viewport_state *vp = &x->viewport[0];
      uint32_t vte = XE_VTE_VPORT_X_SCALE_ENA | XE_VTE_VPORT_X_OFFSET_ENA |
                     XE_VTE_VPORT_Y_SCALE_ENA | XE_VTE_VPORT_Y_OFFSET_ENA |
                     XE_VTE_VPORT_Z_SCALE_ENA | XE_VTE_VPORT_Z_OFFSET_ENA;
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_CL_VTE_CNTL, vte);
       xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_CL_VPORT_XSCALE,
                           float_bits(vp->scale[0]));
       xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_CL_VPORT_XOFFSET,
                           float_bits(vp->translate[0]));
       xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_CL_VPORT_YSCALE,
                           float_bits(vp->scale[1]));
       xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_CL_VPORT_YOFFSET,
                           float_bits(vp->translate[1]));
       xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_CL_VPORT_ZSCALE,
                           float_bits(vp->scale[2]));
       xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_CL_VPORT_ZOFFSET,
                           float_bits(vp->translate[2]));
       static int s_vp_once = 0;
       if (s_vp_once++ < 4)
         DbgPrint("[VPORT] sx=%08x sy=%08x sz=%08x tx=%08x ty=%08x tz=%08x "
                  "xo=%08x yo=%08x",
                  float_bits(vp->scale[0]), float_bits(vp->scale[1]),
                  float_bits(vp->scale[2]), float_bits(vp->translate[0]),
                  float_bits(vp->translate[1]), float_bits(vp->translate[2]),
                  float_bits(vp->translate[0]),
                  float_bits(vp->translate[1]));
      /* GL fixed-function shaders output NDC (-1..1); xenia's clip-disabled
       * path assumes the shader outputs pixel coordinates, so enable clipping
       * so the standard NDC->pixel viewport path is used. */
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_CL_CLIP_CNTL, 0);
   }

   /* Shader program control from compiled metadata. */
   {
      struct xenos_shader *vs = x->vs, *fs = x->fs;
      uint32_t cntl = 0;
      if (vs)
         cntl |= (vs->num_gprs ? vs->num_gprs - 1u : 0u) & 0x3F;
      if (fs)
         cntl |= ((fs->num_gprs ? fs->num_gprs - 1u : 0u) & 0x3F) << 8;
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_SQ_PROGRAM_CNTL, vs ? cntl : 0);

      /* Interpolator wiring: byte i = (VS export slot i << 4) | PS GPR i.
       * Only the first four varyings fit SQ_INTERPOLATOR_CNTL for now. */
      uint32_t ic = 0;
      for (unsigned i = 0; i < 4; ++i) {
         unsigned byte = 0;
         if (vs && (vs->varying_mask & (1u << i)))
            byte |= i << 4;
         if (fs && (fs->varying_mask & (1u << i)))
            byte |= i;
         ic |= byte << (i * 8);
      }
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_SQ_INTERPOLATOR_CNTL, ic);
   }
}

/* Upload a shader's constants: VS to bank 0, FS to bank 256. */
static void
xenos_upload_constants(struct xenos_context *x, struct xenos_shader *sh,
                       mesa_shader_stage stage)
{
   const struct pipe_constant_buffer *cbuf = &x->constant_buffer[stage];
   const uint8_t *data;
   unsigned num_vec4s;

   if (!sh || !sh->num_consts || !cbuf->buffer_size)
      return;

   num_vec4s = MIN2(sh->num_ubos, cbuf->buffer_size / 16);
   if (!num_vec4s)
      return;

   if (cbuf->user_buffer) {
      data = cbuf->user_buffer;
   } else if (cbuf->buffer) {
      struct xenos_resource *res = xenos_resource(cbuf->buffer);
      data = res->data;
   } else {
      return;
   }

   uint32_t base = stage == MESA_SHADER_FRAGMENT ? 256u : 0u;
   ctx_reserve(x, (num_vec4s + sh->num_consts - sh->num_ubos) * 5);
   for (unsigned i = 0; i < num_vec4s; ++i) {
      xe_gpu_cmd_reg_writen(&x->cb, XE_REG_SHADER_CONST(base + i), 4,
                            (const uint32_t *)(data + i * 16));
   }
   for (unsigned i = sh->num_ubos; i < sh->num_consts; ++i) {
      xe_gpu_cmd_reg_writen(&x->cb, XE_REG_SHADER_CONST(base + i), 4,
                            (const uint32_t *)(sh->const_values +
                                               (i - sh->num_ubos) * 4));
   }
}

/* Load a compiled microcode blob via IM_LOAD_IMMEDIATE. */
static void
xenos_load_shader(struct xenos_context *x, struct xenos_shader *sh,
                  uint32_t type)
{
   if (!sh || !sh->ucode_dwords)
      return;
   xenos_load_shader_raw(x, sh->ucode, sh->ucode_dwords, type);
}

static void
xenos_load_shader_raw(struct xenos_context *x, const uint32_t *ucode,
                      uint32_t dwords, uint32_t type)
{
   if (!ucode || !dwords)
      return;
   ctx_reserve(x, dwords + 8);
   /* Type-3 IM_LOAD_IMMEDIATE: count field = payload dwords - 1. */
   x->cb.dwords[x->cb.used++] =
      0xC0000000u |
      ((unsigned)XE_PM4_OP_IM_LOAD_IMMEDIATE & 0x7F) << 8 |
      ((dwords + 1u) & 0x3FFF) << 16;
   x->cb.dwords[x->cb.used++] = type;      /* 0 = vertex, 1 = pixel */
   x->cb.dwords[x->cb.used++] = dwords;    /* start<<16 | size */
   memcpy(&x->cb.dwords[x->cb.used], ucode, dwords * 4);
   x->cb.used += dwords;
}

/* Patch every recorded vfetch slot of the bound VS with the layout of the
 * currently bound vertex elements/buffers (format, stride, offset). */
static void
xenos_patch_vfetch(struct xenos_context *x)
{
   struct xenos_shader *vs = x->vs;
   struct xenos_velems *ve = x->vertex_elements;
   const struct pipe_vertex_element *elements;

   fprintf(stderr, "[VFDPROBE] vfetch_count=%u num_ve=%u vs=%p ve=%p\n",
           vs ? vs->vfetch_count : 99, x->num_vertex_elements, vs, ve);
   DbgPrint("[VFDPROBE] vfetch_count=%u num_ve=%u vs=%p ve=%p",
            vs ? vs->vfetch_count : 99, x->num_vertex_elements, (void *)vs,
            (void *)ve);
if (!vs || !vs->vfetch_count || !ve)
       return;
    elements = ve->elements;
    DbgPrint("[VFDPROBE] VELEMS nve=%u:", x->num_vertex_elements);
    for (uint32_t ei = 0; ei < x->num_vertex_elements && ei < PIPE_MAX_ATTRIBS; ++ei)
       DbgPrint("[VFDPROBE] VELEM elem=%u buf=%u off=%u fmt=%u stride=%u",
                ei, elements[ei].vertex_buffer_index, elements[ei].src_offset,
                elements[ei].src_format, elements[ei].src_stride);

    for (uint32_t f = 0; f < vs->vfetch_count; ++f) {
       struct xenos_vfetch_fixup *fx = &vs->vfetch[f];
       const struct pipe_vertex_element *e = NULL;
       struct pipe_vertex_buffer *vb;
       uint32_t phys, out[3];
       DbgPrint("[VFDPROBE] VFIXUP f=%u attrib=%u nve=%u skip=%d",
                f, fx->attrib, x->num_vertex_elements,
                (fx->attrib >= x->num_vertex_elements));

       if (fx->attrib >= x->num_vertex_elements ||
           fx->attrib >= PIPE_MAX_ATTRIBS)
          continue;
      e = &elements[fx->attrib];
      vb = &x->vertex_buffers[e->vertex_buffer_index];
      if (!vb)
         continue;
      DbgPrint("[VFDPROBE] vfetch attrib=%u is_user=%d res=%p vb=%d stride=%u",
               fx->attrib, vb->is_user_buffer, vb->buffer.resource,
               e->vertex_buffer_index, e->src_stride);

      if (!vb->is_user_buffer && vb->buffer.resource)
         phys = (xenos_resource(vb->buffer.resource)->gpu_addr << 2) +
                vb->buffer_offset;
      else
         continue;   /* user buffers need the uploader (M2) */

      phys += e->src_offset;

      xe_ucode_vfetch(out, fx->attrib, fx->dst_gpr, XE_UCODE_DST_SWIZ_XYZW,
                      0, 0, xenos_vfmt(e->src_format),
                      e->src_stride >> 2, 0, true, true);
      vs->ucode[fx->ucode_dword + 0] = out[0];
      vs->ucode[fx->ucode_dword + 1] = out[1];
      vs->ucode[fx->ucode_dword + 2] = out[2];
   }

   /* Fetch constant groups: a 2-dword entry per attrib, packed 3 per 6-dword
    * fetch-constant block.  Write only the attrib's own 2-dword sub-slot so
    * that sibling attribs sharing the block are not clobbered. */
   for (unsigned i = 0; i < x->num_vertex_elements; ++i) {
      const struct pipe_vertex_element *e = &elements[i];
      struct pipe_vertex_buffer *vb =
         &x->vertex_buffers[e->vertex_buffer_index];
      xenos_vertex_fetch vf;
      uint32_t phys;
      uint32_t fetch_bytes;

      if (!vb)
         continue;

      if (!vb->is_user_buffer && vb->buffer.resource) {
         struct xenos_resource *xr = xenos_resource(vb->buffer.resource);
         phys = (xr->gpu_addr << 2) + vb->buffer_offset;
         /* The fetch constant size bounds the whole fetchable range of the
          * vertex buffer, not a single attribute's stride — the sequencer
          * reads base + vertex_index * stride for every vertex of the draw,
          * so all of it must sit inside [base, base + size). */
         fetch_bytes = xr->size - vb->buffer_offset;
         if (fetch_bytes < 4u)
            fetch_bytes = 4u;   /* at least one word */
         phys += e->src_offset;
         if (vb->buffer_offset + e->src_offset > xr->size ||
             xr->size - vb->buffer_offset - e->src_offset < 4u)
            fetch_bytes = 4u;
      } else
         continue;

      DbgPrint("[VFDPROBE] fc elem=%u phys=%08x data_va=%08x off=%u stride=%u size=%u",
               i, phys,
               (uint32_t)(uintptr_t)xenos_resource(vb->buffer.resource)->data,
               e->src_offset, e->src_stride, fetch_bytes);

      xe_gpu_vfetch_build(&vf, phys, MAX2(fetch_bytes, 4u),
                          XE_ENDIAN_8IN32);
      ctx_reserve(x, 3);
      xe_gpu_cmd_reg_writen(&x->cb,
                            XE_REG_SHADER_CONST_FETCH(i / 3) + 2 * (i % 3), 2,
                            (const uint32_t *)&vf);
   }
}

static void
xenos_clear(struct pipe_context *pipe, unsigned buffers,
            uint32_t color_clear_mask, uint8_t stencil_clear_mask,
            const struct pipe_scissor_state *scissor_state,
            const union pipe_color_union *color, double depth,
            unsigned stencil)
{
   struct xenos_context *x = xenos_context(pipe);

   DbgPrint("[VFDPROBE] xenos_clear buffers=%x w=%u", buffers,
            x->framebuffer.width);
   if (!buffers || !x->framebuffer.width)
      return;

   xenos_emit_frame_state(x);

   /* Clear through draws: fullscreen triangle with the minimal shaders
    * (VS covers [-1..3] NDC; PS outputs guest c48 = host bank-256 c48).
    * Depth clear is not wired yet (no depth test in M1 targets). */
   if (!x->clear_vs_dwords) {
      x->clear_vs_dwords = xe_ucode_build_vs_minimal(x->clear_vs);
      x->clear_ps_dwords = xe_ucode_build_ps_minimal(x->clear_ps);
   }
   xenos_load_shader_raw(x, x->clear_vs, x->clear_vs_dwords, 0);
   xenos_load_shader_raw(x, x->clear_ps, x->clear_ps_dwords, 1);

   if (buffers & PIPE_CLEAR_COLOR) {
      uint32_t cc[4] = { float_bits(color->f[0]), float_bits(color->f[1]),
                         float_bits(color->f[2]), float_bits(color->f[3]) };
      ctx_reserve(x, 6);
      xe_gpu_cmd_reg_writen(&x->cb, XE_REG_SHADER_CONST(256 + 48), 4, cc);
   }

   /* Point fetch constant 0 at a fullscreen triangle (NDC [-1..3], w=1) so
    * the minimal clear VS (vfetch const0 -> r0.xyzw) produces real NDC
    * positions covering the whole viewport, instead of stale/garbage data.
    * The vertex index comes from the sequencer, stride 16 bytes. The fetch
    * size spans all three vertices (12 words, 48 bytes), not just the first. */
   if (x->clear_vert) {
      float *cv = (float *)x->clear_vert;
      cv[ 0] = -1.0f; cv[ 1] = -1.0f; cv[ 2] = 0.0f; cv[ 3] = 1.0f;
      cv[ 4] =  3.0f; cv[ 5] = -1.0f; cv[ 6] = 0.0f; cv[ 7] = 1.0f;
      cv[ 8] = -1.0f; cv[ 9] =  3.0f; cv[10] = 0.0f; cv[11] = 1.0f;
      xenos_vertex_fetch vf = { 0 };
      xe_gpu_vfetch_build(&vf, x->clear_vert_phys, 48u, XE_ENDIAN_8IN32);
      ctx_reserve(x, 3);
      xe_gpu_cmd_reg_writen(&x->cb, XE_REG_SHADER_CONST_FETCH(0), 2,
                            (const uint32_t *)&vf);
   }
   xenos_prim_to_initiator(x, MESA_PRIM_TRIANGLES, 3);
   ctx_submit(x);
}

static void
xenos_draw_vbo(struct pipe_context *pipe,
               const struct pipe_draw_info *dinfo,
               unsigned drawid_offset,
               const struct pipe_draw_indirect_info *indirect,
               const struct pipe_draw_start_count_bias *draws,
               unsigned num_draws)
{
   struct xenos_context *x = xenos_context(pipe);

   DbgPrint("[VFDPROBE] xenos_draw_vbo vs=%p fs=%p n=%u idx=%d",
            x->vs, x->fs, num_draws, dinfo->index_size);
   if (!x->vs || !x->fs || indirect || dinfo->index_size)
      return;                     /* indexed path arrives with M2 */

   for (unsigned d = 0; d < num_draws; d++) {
      xenos_emit_frame_state(x);
      xenos_patch_vfetch(x);
      xenos_load_shader(x, x->vs, 0);
      xenos_load_shader(x, x->fs, 1);
      xenos_upload_constants(x, x->vs, MESA_SHADER_VERTEX);
      xenos_upload_constants(x, x->fs, MESA_SHADER_FRAGMENT);
      xenos_prim_to_initiator(x, dinfo->mode, draws[d].count);
   }
   ctx_submit(x);
}

static void
xenos_flush(struct pipe_context *pipe, struct pipe_fence_handle **fence,
            unsigned flags)
{
   struct xenos_context *x = xenos_context(pipe);

   ctx_submit(x);
   if (fence)
      *fence = NULL;
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

   DbgPrint("[VFDPROBE] buffer_map res->data=%08x boxx=%u usage=%u size=%u",
            (uint32_t)(uintptr_t)res->data, box->x, usage, res->size);

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

   DbgPrint("[VFDPROBE] buffer_subdata data=%08x off=%u size=%u",
            (uint32_t)(uintptr_t)res->data, offset, size);

   memcpy((char *)res->data + offset, data, size);
}

static void
xenos_texture_barrier_stub(struct pipe_context *pipe, unsigned flags)
{
}

static void
xenos_memory_barrier_stub(struct pipe_context *pipe, unsigned barriers)
{
}

static void
xenos_sampler_view_release(struct pipe_context *pipe,
                           struct pipe_sampler_view *view)
{
   if (view && pipe_reference(&view->reference, NULL))
      pipe->sampler_view_destroy(pipe, view);
}

static void
xenos_fence_server_sync_stub(struct pipe_context *pipe,
                             struct pipe_fence_handle *fence,
                             uint64_t timeout)
{
   (void)pipe; (void)fence; (void)timeout;
}

static bool
xenos_resource_commit_stub(struct pipe_context *pipe,
                           struct pipe_resource *resource,
                           unsigned level, struct pipe_box *box, bool commit)
{
   return false;
}

static void
xenos_resource_release_stub(struct pipe_context *pipe,
                            struct pipe_resource *resource)
{
}


static void
xenos_set_sample_mask_stub(struct pipe_context *pipe, unsigned sample_mask)
{
}

static void
xenos_set_min_samples_stub(struct pipe_context *pipe, unsigned min_samples)
{
}

static void
xenos_set_tess_state_stub(struct pipe_context *pipe,
                          const float default_outer_level[4],
                          const float default_inner_level[2])
{
}

static void
xenos_set_patch_vertices_stub(struct pipe_context *pipe, uint8_t patch_vertices)
{
}

static void
xenos_set_shader_buffers_stub(struct pipe_context *pipe,
                              mesa_shader_stage shader, unsigned start_slot,
                              unsigned count,
                              const struct pipe_shader_buffer *buffers,
                              unsigned writable_bitmask)
{
}

static void
xenos_set_hw_atomic_buffers_stub(struct pipe_context *pipe, unsigned start_slot,
                                 unsigned count,
                                 const struct pipe_shader_buffer *buffers)
{
}

static void
xenos_set_shader_images_stub(struct pipe_context *pipe,
                             mesa_shader_stage shader, unsigned start_slot,
                             unsigned count, unsigned unbind_num_trailing_slots,
                             const struct pipe_image_view *images)
{
}

static struct pipe_stream_output_target *
xenos_create_stream_output_target_stub(struct pipe_context *pipe,
                                       struct pipe_resource *resource,
                                       unsigned buffer_offset,
                                       unsigned buffer_size)
{
   return NULL;
}

static void
xenos_stream_output_target_destroy_stub(
   struct pipe_context *pipe, struct pipe_stream_output_target *target)
{
}

static void
xenos_set_stream_output_targets_stub(struct pipe_context *pipe,
                                     unsigned num_targets,
                                     struct pipe_stream_output_target **targets,
                                     const unsigned *offsets,
                                     enum mesa_prim output_prim)
{
}

static uint32_t
xenos_stream_output_target_offset_stub(
   const struct pipe_stream_output_target *target)
{
   return 0;
}


static void
xenos_set_sample_locations_stub(struct pipe_context *pipe, size_t size,
                                const uint8_t *locations)
{
}

static void
xenos_set_polygon_stipple_stub(struct pipe_context *pipe,
                               const struct pipe_poly_stipple *stipple)
{
}

static void
xenos_blit_stub(struct pipe_context *pipe, const struct pipe_blit_info *info)
{
}

static void
xenos_clear_render_target_stub(struct pipe_context *pipe,
                               struct pipe_surface *dst,
                               const union pipe_color_union *color,
                               unsigned dstx, unsigned dsty, unsigned width,
                               unsigned height, bool render_condition_enabled)
{
}

static void
xenos_clear_depth_stencil_stub(
   struct pipe_context *pipe, struct pipe_surface *dst, unsigned clear_flags,
   double depth, unsigned stencil, unsigned dstx, unsigned dsty,
   unsigned width, unsigned height, bool render_condition_enabled)
{
}

static void
xenos_flush_resource_stub(struct pipe_context *pipe,
                          struct pipe_resource *resource)
{
}

struct pipe_context *
xenos_create_context(struct pipe_screen *screen, void *priv, unsigned flags)
{
   struct xenos_context *x = CALLOC_STRUCT(xenos_context);

   DbgPrint("xenos: context_create enter");
   if (!x)
      return NULL;

   x->screen = xenos_screen(screen);
   DbgPrint("xenos: ctx screen ok");

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

   /* Optional hooks st/mesa touches during normal operation. */
   x->base.texture_barrier = xenos_texture_barrier_stub;
   x->base.memory_barrier = xenos_memory_barrier_stub;
   x->base.resource_commit = xenos_resource_commit_stub;
   x->base.resource_release = xenos_resource_release_stub;
   x->base.sampler_view_release = xenos_sampler_view_release;
   x->base.fence_server_sync = xenos_fence_server_sync_stub;
   x->base.fence_server_signal = xenos_fence_server_sync_stub;
   x->base.set_sample_locations = xenos_set_sample_locations_stub;
   x->base.set_polygon_stipple = xenos_set_polygon_stipple_stub;
   x->base.blit = xenos_blit_stub;
   x->base.clear_render_target = xenos_clear_render_target_stub;
   x->base.clear_depth_stencil = xenos_clear_depth_stencil_stub;
   x->base.flush_resource = xenos_flush_resource_stub;
   x->base.set_sample_mask = xenos_set_sample_mask_stub;
   x->base.set_min_samples = xenos_set_min_samples_stub;
   x->base.set_tess_state = xenos_set_tess_state_stub;
   x->base.set_patch_vertices = xenos_set_patch_vertices_stub;
   x->base.set_shader_buffers = xenos_set_shader_buffers_stub;
   x->base.set_hw_atomic_buffers = xenos_set_hw_atomic_buffers_stub;
   x->base.set_shader_images = xenos_set_shader_images_stub;
   x->base.create_stream_output_target = xenos_create_stream_output_target_stub;
   x->base.stream_output_target_destroy =
      xenos_stream_output_target_destroy_stub;
   x->base.set_stream_output_targets = xenos_set_stream_output_targets_stub;
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

   DbgPrint("xenos: ctx uploader ok");
   /* Command scratch + resolve rectangle (page-aligned physical memory). */
   x->cb.dwords = MALLOC(XE_GPU_CMDBUF_DWORDS * 4);
   if (!x->cb.dwords) {
      u_upload_destroy(x->base.stream_uploader);
      FREE(x);
      return NULL;
   }
   DbgPrint("xenos: ctx cmdbuf ok");
   x->cb.used = 0;
   x->resolve_rect = x->screen->ws->alloc(x->screen->ws, 64, 12);
   if (x->resolve_rect && x->screen->ws->get_physical)
      x->resolve_rect_phys =
         (uint32_t)x->screen->ws->get_physical(x->screen->ws,
                                               x->resolve_rect);
   x->clear_vert = x->screen->ws->alloc(x->screen->ws, 64, 12);
   if (x->clear_vert && x->screen->ws->get_physical)
      x->clear_vert_phys =
         (uint32_t)x->screen->ws->get_physical(x->screen->ws, x->clear_vert);

   return &x->base;
}

/* ------------------------------------------------------------------ */
/* Frame end: resolve the colour target into its tiled system backing  */
/* ------------------------------------------------------------------ */

bool
xenos_flush_frame(struct pipe_context *pipe, struct pipe_resource *color,
                  struct xenos_present_info *out)
{
   struct xenos_context *x = xenos_context(pipe);
   struct xenos_resource *res;
   uint32_t w, h;
   float *rect;

   if (!color || color->target == PIPE_BUFFER)
      return false;
   res = xenos_resource(color);
   if (!res->has_edram || !res->resolve_data)
      return false;

   w = color->width0;
   h = color->height0;

   xenos_emit_frame_state(x);
   ctx_reserve(x, 128);

   /* kCopy: copy the EDRAM surface into the tiled system buffer. */
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_RB_COPY_CONTROL, 0);
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_RB_COPY_DEST_BASE, res->resolve_phys);
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_RB_COPY_DEST_PITCH, w | (h << 16));
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_RB_COPY_DEST_INFO, 6u << 7);

   /* Resolve rectangle covering the whole target, guest-endian floats. */
   rect = (float *)x->resolve_rect;
   rect[0] = 0.0f; rect[1] = 0.0f;
   rect[2] = (float)w; rect[3] = 0.0f;
   rect[4] = 0.0f; rect[5] = (float)h;
   {
      xenos_vertex_fetch vf[3];
      memset(vf, 0, sizeof(vf));
      xe_gpu_vfetch_build(&vf[0], x->resolve_rect_phys, 24u, XE_ENDIAN_8IN32);
      xe_gpu_cmd_reg_writen(&x->cb, XE_REG_SHADER_CONST_FETCH(0), 6,
                            (const uint32_t *)vf);
   }

   xe_gpu_cmd_reg_write(&x->cb, XE_REG_RB_MODECONTROL, XE_EDRAM_MODE_COPY);
   xenos_prim_to_initiator(x, MESA_PRIM_TRIANGLES, 3);
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_RB_MODECONTROL, XE_EDRAM_MODE_COLOR_DEPTH);

   ctx_submit(x);

   if (out) {
      out->phys = res->resolve_phys;
      out->width = w;
      out->height = h;
      out->tiled = 1;
   }
   return true;
}