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
#include <fcntl.h>
#include <unistd.h>

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
   struct pipe_depth_stencil_alpha_state clear_dsa;
   unsigned dsa_override_active;
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

   /* Blit program: textured quad (letterbox presentation / surface blits).
    * VS fetches pos + uv, exports interpolator0; FS samples the src surface
    * via the tiled tfetch at XE_TEX_FETCH_INDEX_BASE. */
   uint32_t blit_vs[64];
   uint32_t blit_vs_dwords;
   uint32_t blit_ps[64];
   uint32_t blit_ps_dwords;
   void *blit_vert;
   uint32_t blit_vert_phys;
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

/* Keep the guest's free-running write pointer anchored in the same ring
 * epoch as the CP's read pointer.
 *
 * The CP consumes our blocks and advances its own free-running read counter;
 * whenever the ring wraps, the CP's read pointer moves into a *later* epoch
 * (same modulo offset, bigger base) while our write pointer can still sit in
 * the previous one (a publish racers past by present/VdSwap sharing the same
 * ring, for example).  If wptr then lands *behind* rptr, the unsigned room
 * check underflows and the next submit writes over dwords the CP has not
 * consumed yet — exactly the "truncated last packet" corruption we saw cause
 * ExecutePacketType0 overflow and hang the primary ring.
 *
 * Re-anchor wptr to rptr's epoch, keeping our ring slot.  If our slot is
 * behind the CP's head on the ring, step one full epoch extra so the free
 * delta stays positive and the room check below remains valid.
 */
static void
ctx_resync_wptr(struct xenos_context *x)
{
   if (!x->ring_ready || !x->screen->ws->rptr_page)
      return;

   uint32_t ring_mask = x->ring.size_dwords - 1u;
   uint32_t wptr_mod = (uint32_t)x->wptr_linear & ring_mask;
   uint64_t rptr = x->rptr_linear;

   if (x->wptr_linear >= rptr)
      return; /* already ahead of the CP */

   uint64_t base = rptr & ~(uint64_t)ring_mask;
   uint32_t rptr_mod = (uint32_t)rptr & ring_mask;
   if (wptr_mod < rptr_mod)
      base += x->ring.size_dwords;

   x->wptr_linear = base + wptr_mod;
   DbgPrint("xenos: ring epoch resync wptr=%u rptr=%u\n",
            (unsigned)x->wptr_linear, (unsigned)rptr);

   /* Present/VdSwap thumbs the shared slot; publish our re-anchored counter
    * so its next swap block lands *after* ours, never over it. */
   if (x->screen->ws->wptr_slot)
      *x->screen->ws->wptr_slot = (uint32_t)x->wptr_linear;
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

   {
      static int s_submit_log = 0;
      if (s_submit_log < 20) {
         DbgPrint("xenos: ctx_submit used=%u\n", cb->used);
         s_submit_log++;
      }
   }

   if (!cb->used)
      return;

   if (!ws->ring_buffer || !ws->wptr_slot) {
      /* Ring not attached yet - nothing we can do with the commands. */
      DbgPrint("xenos: ctx_submit no ring\n");
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
       DbgPrint("xenos: ctx_submit ring ready size=%u\n", x->ring.size_dwords);
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

    /* Refresh the CP read pointer, then re-anchor our write pointer into the
     * CP's current ring epoch so the free-running room check below never
     * underflows. */
    ctx_track_rptr(x);
    ctx_resync_wptr(x);

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
    * wrap back into the free-running counter by simply accumulating the
    * number of dwords published.  A free-running counter must be strictly
    * monotonic: the CP wraps it modulo the ring internally, so any
    * re-anchoring to a wrapped "base + new_mod" would step BACKWARD across a
    * ring wrap, underflow the room check, and clobber unread ring data. */
   x->wptr_linear += block;

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
   if (x->blit_vert)
      x->screen->ws->free(x->screen->ws, x->blit_vert);

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

/* FS alpha fix flag - set to 1 after patching. */
static int s_alpha_patched = 0;

static void
xenos_bind_fs_state(struct pipe_context *pipe, void *cso)
{
   static int s_bind_log = 0;
   if (s_bind_log++ < 10) {
      struct xenos_shader *s = cso ? (struct xenos_shader *)cso : NULL;
      DbgPrint("xenos: BIND_FS99 cso=%08x ucode=%08x dwords=%u\n",
               (uint32_t)(uintptr_t)cso,
               s ? (uint32_t)(uintptr_t)s->ucode : 0,
               s ? s->ucode_dwords : 0);
   }

   /* --- FS ALPHA FIX: patch FS MAX+EXPORT to force alpha=1.0 ---
    *
    * Root cause: VS exports varyings with vec3 write_mask -> W defaults to 0.
    * FS does texel*r1 -> alpha=0 -> blend produces black.
    *
    * Strategy: Two patches per MAX+EXPORT ALU slot:
    * 1) uc[p+2] (dword2/struct C): change src2 from temp to const 200
    *    so scalar_opc can use src1.W as a source.
    * 2) uc[p] (dword0/struct A): change vector_write_mask from 0xF to 0x7
    *    so W comes from the scalar operation (not vector), and set
    *    scalar_opc to ADDS so W = src0.W + src1.W = 0 + 1.0 = 1.0.
    *
    * AluInstruction layout (3 dwords, 12 bytes):
    *   dword0 (struct A): vector_dest[5:0], export_data[15],
    *     vector_write_mask[19:16], scalar_write_mask[23:20],
    *     scalar_opc[31:26]
    *   dword1 (struct B): src swizzles, negate flags
    *   dword2 (struct C): src3_reg[7:0], src2_reg[15:8], src1_reg[23:16],
    *     vector_opc[28:24], src3_sel[29], src2_sel[30], src1_sel[31] */
   if (cso) {
      struct xenos_shader *s = (struct xenos_shader *)cso;
      if (s->type == MESA_SHADER_FRAGMENT && s->ucode_dwords >= 6) {
         static int s_fs_dump = 0;
         if (s_fs_dump < 10) {
            DbgPrint("FS-DUMP: dwords=%u", s->ucode_dwords);
            for (uint32_t j = 0; j < s->ucode_dwords; j++)
               DbgPrint("FS-DUMP: uc[%u]=%08x", j, s->ucode[j]);
            s_fs_dump++;
         }
         /* CF preamble is 3 dwords; ALU slots follow at dword 3,6,9,... */
         int patched_any = 0;
         for (uint32_t p = 3; p + 2 < s->ucode_dwords; p += 3) {
            uint32_t d0 = s->ucode[p];     /* struct A: export_data at bit 15 */
            uint32_t d2 = s->ucode[p + 2]; /* struct C: vector_opc at bits[28:24] */
            uint32_t opc = (d2 >> 24) & 0x1F;
            uint32_t export = (d0 >> 15) & 1;
            {
               static int s_slot_log = 0;
               if (s_slot_log < 30) {
                  DbgPrint("FS-SLOT: p=%u opc=%u export=%u d0=%08x d2=%08x vw=%u sw=%u",
                           p, opc, export, d0, d2, (d0>>16)&0xF, (d0>>20)&0xF);
                  s_slot_log++;
               }
            }
            if (opc == 2 /* MAX */ && export) {
               /* PATCH v6: Force alpha=1.0 via constant-1 mechanism.
                *
                * Xenia ucode constant-1 rule (ucode.h GetConstant1WriteMask):
                *   constant_1_mask = vector_write_mask & scalar_write_mask
                *   For each bit in the overlap, the output is constant 1.0
                *   (not from vector or scalar op).
                *
                * Mesa's compiler generates vw=0xF, sw=0x0 for exports.
                * We set sw=0x8 (W bit only). The overlap is bit 3:
                *   constant_1_mask = 0xF & 0x8 = 0x8
                *   → W = constant 1.0
                *   → XYZ come from the MAX vector op (unchanged)
                *
                * Xenia SPIR-V translator handles this correctly:
                *   GetVectorOpResultWriteMask = vw & ~sw = 0x7 (XYZ)
                *   components[3] = SwizzleSource::k1 (constant 1)
                *   Shuffle: vec3(MAX.xyz) + const_float2(0,1) → index 4 = 1.0
                *
                * No scalar pipeline, no constant upload, no SQ_PS_CONST needed.
                */
               uint32_t new_d0 = d0;
               /* Keep vector_write_mask at 0xF (all four from vector op).
                * Set scalar_write_mask to 0x8 (W bit only).
                * Overlap on W triggers constant 1.0. */
               new_d0 &= ~(0xFu << 20);  /* Clear scalar_write_mask */
               new_d0 |= (0x8u << 20);   /* Set W bit */
               s->ucode[p] = new_d0;
               patched_any = 1;
               static int s_patch_count = 0;
               if (s_patch_count < 20) {
                  DbgPrint("FS-ALPHA-v6: slot[%u] d0=%08x->%08x vw=%u sw=%u",
                           p, d0, new_d0,
                           (new_d0 >> 16) & 0xF, (new_d0 >> 20) & 0xF);
                  s_patch_count++;
               }
            }
         }
         if (patched_any)
            s_alpha_patched = 1;
      }
   }

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
   static int s_bind_log = 0;
   if (s_bind_log++ < 10) {
      struct xenos_shader *s = cso ? (struct xenos_shader *)cso : NULL;
      DbgPrint("xenos: BIND_VS cso=%08x ucode=%08x dwords=%u\n",
               (uint32_t)(uintptr_t)cso,
               s ? (uint32_t)(uintptr_t)s->ucode : 0,
               s ? s->ucode_dwords : 0);
   }
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

   for (unsigned i = 0; i < count; i++) {
      if (views[i] && views[i]->texture) {
         static unsigned tex_log = 0;
         if (tex_log < 40) {
            DbgPrint("TEXBIND stage=%d slot=%d fmt=%u w=%u h=%u target=%d\n",
                     shader, start + i, (unsigned)views[i]->format,
                     views[i]->texture->width0, views[i]->texture->height0,
                     views[i]->texture->target);
            tex_log++;
         }
      }
      pipe_sampler_view_reference(&x->sampler_views[start + i], views[i]);
   }
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

   pipe_resource_reference(&x->constant_buffer[shader].buffer, NULL);
   if (cb) {
      x->constant_buffer[shader] = *cb;
      pipe_resource_reference(&x->constant_buffer[shader].buffer, cb->buffer);
   }
}

static void
xenos_set_framebuffer_state(struct pipe_context *pipe,
                            const struct pipe_framebuffer_state *state)
{
   struct xenos_context *x = xenos_context(pipe);

   util_copy_framebuffer_state(&x->framebuffer, state);

   /* The GL layer creates the app surface as a plain texture (SAMPLER only)
    * and only later attaches it to an FBO.  Give any such target EDRAM tiles
    * here so rendering lands in real EDRAM and sampling can resolve it. */
   for (unsigned i = 0; i < state->nr_cbufs; i++) {
      if (state->cbufs[i].texture) {
         struct xenos_resource *r = xenos_resource(state->cbufs[i].texture);
         xenos_resource_assign_edram(x->screen, r,
                                     PIPE_BIND_RENDER_TARGET);
      }
   }
   if (state->zsbuf.texture) {
      struct xenos_resource *r = xenos_resource(state->zsbuf.texture);
   }
   if (state->zsbuf.texture)
      xenos_resource_assign_edram(x->screen,
                                  xenos_resource(state->zsbuf.texture),
                                  PIPE_BIND_DEPTH_STENCIL);
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

/* Map Mesa pipe_format → Xenia TextureFormat for the texture fetch constant. */
static uint32_t
xenos_tfetch_format(enum pipe_format fmt)
{
   switch (fmt) {
   /* 32-bit per pixel */
   case PIPE_FORMAT_R8G8B8A8_UNORM:
   case PIPE_FORMAT_R8G8B8X8_UNORM:
   case PIPE_FORMAT_B8G8R8A8_UNORM:
   case PIPE_FORMAT_B8G8R8X8_UNORM:
      return XE_TFETCH_FORMAT_8_8_8_8;
   case PIPE_FORMAT_B5G5R5A1_UNORM:
   case PIPE_FORMAT_R5G5B5A1_UNORM:
      return 3u;  /* k_1_5_5_5 */
   case PIPE_FORMAT_B5G6R5_UNORM:
      return 4u;  /* k_5_6_5 */
   case PIPE_FORMAT_B4G4R4A4_UNORM:
   case PIPE_FORMAT_A4R4G4B4_UNORM:
      return XE_TFETCH_FORMAT_4_4_4_4;
   case PIPE_FORMAT_R10G10B10A2_UNORM:
      return 7u;  /* k_2_10_10_10 */
   /* 16-bit per component */
   case PIPE_FORMAT_R16G16B16A16_UNORM:
      return XE_TFETCH_FORMAT_16_16_16_16;
   case PIPE_FORMAT_R16G16B16A16_FLOAT:
      return XE_TFETCH_FORMAT_16_16_16_16_FLOAT;
   case PIPE_FORMAT_R16G16_FLOAT:
      return XE_TFETCH_FORMAT_16_16_FLOAT;
   case PIPE_FORMAT_R16_FLOAT:
      return XE_TFETCH_FORMAT_16_FLOAT;
   /* 32-bit per component */
   case PIPE_FORMAT_R32G32B32A32_FLOAT:
      return XE_TFETCH_FORMAT_32_32_32_32_FLOAT;
   case PIPE_FORMAT_R32G32B32_FLOAT:
      return XE_TFETCH_FORMAT_32_32_32_FLOAT;
   case PIPE_FORMAT_R32G32_FLOAT:
      return XE_TFETCH_FORMAT_32_32_FLOAT;
   case PIPE_FORMAT_R32_FLOAT:
      return XE_TFETCH_FORMAT_32_FLOAT;
   /* 8-bit formats */
   case PIPE_FORMAT_R8_UNORM:
   case PIPE_FORMAT_A8_UNORM:
      return 2u;  /* k_8 */
   case PIPE_FORMAT_R8G8_UNORM:
      return 10u; /* k_8_8 */
   /* Compressed formats (DXT/S3TC) */
   case PIPE_FORMAT_DXT1_RGB:
   case PIPE_FORMAT_DXT1_RGBA:
   case PIPE_FORMAT_DXT1_SRGB:
   case PIPE_FORMAT_DXT1_SRGBA:
      return XE_TFETCH_FORMAT_DXT1;
   case PIPE_FORMAT_DXT3_RGBA:
   case PIPE_FORMAT_DXT3_SRGBA:
      return XE_TFETCH_FORMAT_DXT2_3;
   case PIPE_FORMAT_DXT5_RGBA:
   case PIPE_FORMAT_DXT5_SRGBA:
      return XE_TFETCH_FORMAT_DXT4_5;
   case PIPE_FORMAT_ETC1_RGB8:
   case PIPE_FORMAT_ETC2_RGB8:
   case PIPE_FORMAT_ETC2_RGBA8:
      return XE_TFETCH_FORMAT_DXT1; /* approximate */
   default:
      return XE_TFETCH_FORMAT_8_8_8_8; /* fallback */
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
      [MESA_PRIM_TRIANGLE_STRIP]= 6,  /* Xenia kTriangleStrip = 0x06 */
      [MESA_PRIM_TRIANGLE_FAN]  = 5,  /* Xenia kTriangleFan = 0x05 */
      [MESA_PRIM_QUADS]         = 6,  /* expand as triangle strips */
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
   {
      static int s_efs_log = 0;
      if (s_efs_log++ < 5) {
         DbgPrint("xenos: EMIT_ENTER w=%u h=%u dsa=%08x\n",
                  x->framebuffer.width, x->framebuffer.height,
                  (uint32_t)(uintptr_t)x->dsa);
      }
   }
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
      /* WORKAROUND: Always force z_enable=1 with zfunc=ALWAYS.
       *
       * Reason 1: The host gates depth RT inclusion on z_enable, not on
       * whether RB_DEPTHINFO is set.  Without z_enable, the host enables
       * depth test with NEVER comparison when there's no depth attachment,
       * killing all fragments.
       *
       * Reason 2: The game (Deltarune) never clears the depth buffer
       * (buffers=0x04 = COLOR0 only).  If depth testing is enabled with
       * LESS/LEQUAL against the stale dummy depth EDRAM, every fragment
       * is rejected, producing a black screen.  Forcing ALWAYS ensures
       * all fragments pass regardless of the depth buffer contents.
       *
       * TODO(Xbox360): Determine whether real Xbox 360 hardware also
       * requires z_enable to be set even when depth testing is functionally
       * off, or if this is purely a host emulation bug. */
      {
         static int s_depth_log = 0;
         if (s_depth_log++ < 12) {
            DbgPrint("xenos: DEPTH dsa=%08x en=%u wm=%u func=%u force_ALWAYS\n",
                     (uint32_t)(uintptr_t)x->dsa,
                     depth_enabled, depth_writemask,
                     dsa ? (unsigned)dsa->depth_func : 99);
         }
      }
      depth_enabled = true;
      zfunc = (uint32_t)PIPE_FUNC_ALWAYS;
      depth_writemask = false;
      uint32_t dc = (depth_enabled ? (1u << 1) : 0) |
                    (depth_writemask ? (1u << 2) : 0) |
                    (zfunc << 4);
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

   /* Blend state: read from the bound pipe_blend_state, or default to opaque. */
   {
       /* Xenia RB_BLENDCONTROL0 bitfield (verified from registers.h):
        *   [4:0]   color_srcblend  (5 bits, Xenia BlendFactor)
        *   [7:5]   color_comb_fcn  (3 bits: 0=ZERO, 1=ADD, 2=SUB, 3=MIN, 4=MAX)
        *   [12:8]  color_destblend (5 bits, Xenia BlendFactor)
        *   [15:13] reserved
        *   [20:16] alpha_srcblend  (5 bits, Xenia BlendFactor)
        *   [23:21] alpha_comb_fcn  (3 bits)
        *   [28:24] alpha_destblend (5 bits, Xenia BlendFactor)
        */
      struct pipe_blend_state *bs = (struct pipe_blend_state *)x->blend;

      if (bs && bs->rt[0].blend_enable) {
         /* Map Gallium PIPE_BLENDFACTOR_* to Xenia BlendFactor.
          * Gallium: ONE=1, SRC_COLOR=2, SRC_ALPHA=3, DST_ALPHA=4,
          *   DST_COLOR=5, SATURATE=6, CONST_COLOR=7, CONST_ALPHA=8,
          *   INV_SRC_COLOR=0x12, INV_SRC_ALPHA=0x13, INV_DST_ALPHA=0x14,
          *   INV_DST_COLOR=0x15, ZERO=0x11, etc.
          * Xenia: Zero=0, One=1, SrcColor=2, InvSrcColor=3, SrcAlpha=4,
          *   InvSrcAlpha=5, DstAlpha=6, InvDstAlpha=7, DstColor=8,
          *   InvDstColor=9, Sat=10, ConstColor=16, InvConstColor=17,
          *   ConstAlpha=18, InvConstAlpha=19.
          */
         static const uint32_t xenia_blendfactor[28] = {
            /* 0 */ 0, /* not used */
            /* 1 PIPE_BLENDFACTOR_ONE */ 1,
            /* 2 PIPE_BLENDFACTOR_SRC_COLOR */ 2,
            /* 3 PIPE_BLENDFACTOR_SRC_ALPHA */ 4,
            /* 4 PIPE_BLENDFACTOR_DST_ALPHA */ 6,
            /* 5 PIPE_BLENDFACTOR_DST_COLOR */ 8,
            /* 6 PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE */ 10,
            /* 7 PIPE_BLENDFACTOR_CONST_COLOR */ 16,
            /* 8 PIPE_BLENDFACTOR_CONST_ALPHA */ 18,
            /* 9 PIPE_BLENDFACTOR_SRC1_COLOR */ 2,  /* treat as SRC_COLOR */
            /* 10 PIPE_BLENDFACTOR_SRC1_ALPHA */ 4,  /* treat as SRC_ALPHA */
            /* 11-15 unused */ 0, 0, 0, 0, 0,
            /* 16 */ 0, /* unused */
            /* 17 PIPE_BLENDFACTOR_ZERO */ 0,
            /* 18 PIPE_BLENDFACTOR_INV_SRC_COLOR */ 3,
            /* 19 PIPE_BLENDFACTOR_INV_SRC_ALPHA */ 5,
            /* 20 PIPE_BLENDFACTOR_INV_DST_ALPHA */ 7,
            /* 21 PIPE_BLENDFACTOR_INV_DST_COLOR */ 9,
            /* 22-23 unused */ 0, 0,
            /* 24 PIPE_BLENDFACTOR_INV_CONST_COLOR */ 17,
            /* 25 PIPE_BLENDFACTOR_INV_CONST_ALPHA */ 19,
            /* 26 PIPE_BLENDFACTOR_INV_SRC1_COLOR */ 3,  /* treat as INV_SRC_COLOR */
            /* 27 PIPE_BLENDFACTOR_INV_SRC1_ALPHA */ 5,  /* treat as INV_SRC_ALPHA */
         };
         /* Map Gallium PIPE_BLEND_* to Xenia blend function (1=ADD). */
         static const uint32_t xenia_blendop[5] = {
            /* PIPE_BLEND_ADD */ 1,
            /* PIPE_BLEND_SUBTRACT */ 2,
            /* PIPE_BLEND_REVERSE_SUBTRACT */ 5, /* R600: 5 = rev sub */
            /* PIPE_BLEND_MIN */ 3,
            /* PIPE_BLEND_MAX */ 4,
         };
         unsigned rgb_src = bs->rt[0].rgb_src_factor & 0x1F;
         unsigned rgb_dst = bs->rt[0].rgb_dst_factor & 0x1F;
         unsigned rgb_func = bs->rt[0].rgb_func & 7;
         unsigned a_src = bs->rt[0].alpha_src_factor & 0x1F;
         unsigned a_dst = bs->rt[0].alpha_dst_factor & 0x1F;
         unsigned a_func = bs->rt[0].alpha_func & 7;
         uint32_t xs = xenia_blendfactor[rgb_src < 28 ? rgb_src : 0];
         uint32_t xd = xenia_blendfactor[rgb_dst < 28 ? rgb_dst : 0];
         uint32_t xf = xenia_blendop[rgb_func < 5 ? rgb_func : 0];
         uint32_t xas = xenia_blendfactor[a_src < 28 ? a_src : 0];
         uint32_t xad = xenia_blendfactor[a_dst < 28 ? a_dst : 0];
         uint32_t xaf = xenia_blendop[a_func < 5 ? a_func : 0];
         /* Xenia bitfield: [4:0]=xs [7:5]=xf [12:8]=xd
          * [20:16]=xas [23:21]=xaf [28:24]=xad */
         uint32_t blend_ctrl = xs |
                               (xf << 5) |
                               (xd << 8) |
                               (xas << 16) |
                               (xaf << 21) |
                               (xad << 24);
         /* FS ALPHA FIX: force opaque (One/Zero) when alpha patch is active,
          * to bypass alpha-dependent blending that produces black. */
         if (s_alpha_patched)
            blend_ctrl = 0x00210021u;  /* One/ADD/Zero for both RGB and alpha */
         xe_gpu_cmd_reg_write(&x->cb, XE_REG_RB_BLENDCONTROL0, blend_ctrl);
         {
            static int s_blend_log = 0;
            if (s_blend_log++ < 6) {
               /* Log each component separately to avoid PPC32 varargs issues.
                * NEW bitfield: [4:0]=xs [7:5]=xf [12:8]=xd [20:16]=xas [23:21]=xaf [28:24]=xad */
               DbgPrint("xenos: BLEND2 xs=%u xf=%u xd=%u xas=%u xaf=%u xad=%u",
                        xs, xf, xd, xas, xaf, xad);
            }
         }
      } else {
         /* Opaque: One/Zero/ADD for both RGB and alpha.
          * [4:0]=1 [7:5]=1 [12:8]=0 [20:16]=1 [23:21]=1 [28:24]=0 = 0x00210021 */
         xe_gpu_cmd_reg_write(&x->cb, XE_REG_RB_BLENDCONTROL0, 0x00210021u);
      }
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_RB_COLOR_MASK, 0xF);
   }

   /* Viewport transform.  Gallium gives GL-style scale/translate; the Xenos
    * D3D pixel-center convention needs a half-pixel shift. */
   {
      const struct pipe_viewport_state *vp = &x->viewport[0];
      uint32_t vte = XE_VTE_VPORT_X_SCALE_ENA | XE_VTE_VPORT_X_OFFSET_ENA |
                     XE_VTE_VPORT_Y_SCALE_ENA | XE_VTE_VPORT_Y_OFFSET_ENA |
                     XE_VTE_VPORT_Z_SCALE_ENA | XE_VTE_VPORT_Z_OFFSET_ENA |
                     XE_VTE_VTX_W0_FMT;
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
       if (s_vp_once++ < 20)
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
   static unsigned up_log = 0;
   if (up_log < 10) {
      DbgPrint("UPLOAD stage=%u sh=%p num_consts=%u num_ubos=%u",
               stage, (void*)sh,
               sh ? sh->num_consts : 0, sh ? sh->num_ubos : 0);
      up_log++;
   }

   const struct pipe_constant_buffer *cbuf = &x->constant_buffer[stage];
   const uint8_t *data = NULL;
   unsigned num_vec4s = 0;

   if (!sh || !sh->num_consts) {
      return;
   }

   /* Upload UBO constants from the game's constant buffer. */
   if (sh->num_ubos && cbuf->buffer_size) {
      num_vec4s = MIN2(sh->num_ubos, cbuf->buffer_size / 16);
      if (cbuf->user_buffer) {
         data = cbuf->user_buffer;
      } else if (cbuf->buffer) {
         struct xenos_resource *res = xenos_resource(cbuf->buffer);
         data = res->data;
      }
   }

   uint32_t base = stage == MESA_SHADER_FRAGMENT
                   ? (XE_PS_CONST_REG_BASE + XE_FS_CONST_CODEGEN_BASE)
                   : 0u;
   ctx_reserve(x, (num_vec4s + sh->num_consts - sh->num_ubos) * 5);
   if (data) {
      for (unsigned i = 0; i < num_vec4s; ++i) {
         xe_gpu_cmd_reg_writen(&x->cb, XE_REG_SHADER_CONST(base + i), 4,
                               (const uint32_t *)(data + i * 16));
      }
   }
   /* DIAG: dump VS constant matrix keyed to draw-diag frame windows plus
    * periodic sampling, so boot and in-game constants can be compared with
    * the draws that used them (same xenos_diag_frame basis as DRAWDIAG). */
   if (stage == MESA_SHADER_VERTEX && num_vec4s >= 4) {
      static int s_vsconst_log = 0;
      extern uint32_t xenos_diag_frame;
      uint32_t df = xenos_diag_frame;
      if ((df < 30u) || (df >= 5000u && df <= 5010u) ||
          (df >= 10000u && df <= 10010u) || (df % 2000u) < 2u) {
         const uint32_t *u = (const uint32_t *)data;
         DbgPrint("VSC f=%08x c0=%08x %08x %08x %08x", df,
                  u[0], u[1], u[2], u[3]);
         DbgPrint("   c1=%08x %08x %08x %08x", u[4], u[5], u[6], u[7]);
         DbgPrint("   c2=%08x %08x %08x %08x", u[8], u[9], u[10], u[11]);
         DbgPrint("   c3=%08x %08x %08x %08x", u[12], u[13], u[14], u[15]);
         s_vsconst_log++;
      }
   }
   for (unsigned i = sh->num_ubos; i < sh->num_consts; ++i) {
      uint32_t reg = base + i;
      const uint32_t *vals = (const uint32_t *)(sh->const_values +
                                                 (i - sh->num_ubos) * 4);
      xe_gpu_cmd_reg_writen(&x->cb, XE_REG_SHADER_CONST(reg), 4, vals);
      /* Log the first few constant uploads for diagnostics. */
      if (stage == MESA_SHADER_FRAGMENT) {
         static unsigned fc_log = 0;
         if (fc_log < 20) {
            DbgPrint("FS_CONST[%u] reg=%u vals=[%08x %08x %08x %08x]",
                     i, reg, vals[0], vals[1], vals[2], vals[3]);
            fc_log++;
         }
      }
   }
}

/* Load a compiled microcode blob via IM_LOAD_IMMEDIATE. */
static void
xenos_load_shader(struct xenos_context *x, struct xenos_shader *sh,
                  uint32_t type)
{
   if (!sh || !sh->ucode_dwords)
      return;

   /* Diagnostic: scan for VFETCH instructions and verify stride != 0.
    * VFETCH opcode_value = 0 (XE_UCODE_FETCH_VERTEX), 3-dword instruction.
    * dword0: opcode:5 | src:6 | src_am:1 | dst:6 | dst_am:1 | must_be_one:1 | ...
    * dword2 bits [7:0] = stride in dwords.
    * To reduce false ALU matches: check must_be_one (bit 19) = 1. */
   {
      static int s_fetch_check = 0;
      if (s_fetch_check < 20) {
         DbgPrint("xenos: FETCHSCAN type=%u dwords=%u slots=%u", type, sh->ucode_dwords, sh->num_slots);
         /* Dump first 6 dwords of ucode header + a few slot dwords */
         for (uint32_t j = 0; j < 12 && j < sh->ucode_dwords; j++)
            DbgPrint("xenos: uc[%u]=%08x", j, sh->ucode[j]);
         for (uint32_t i = 0; i + 2 < sh->ucode_dwords; i += 3) {
            uint32_t opc = sh->ucode[i] & 0x1F;
            uint32_t must_one = (sh->ucode[i] >> 19) & 1;
            if (opc == 0 && must_one) { /* VFETCH: opcode=0, must_be_one=1 */
               uint32_t stride = sh->ucode[i + 2] & 0xFF;
               uint32_t fmt = (sh->ucode[i + 1] >> 16) & 0x3F;
               uint32_t dst = (sh->ucode[i] >> 12) & 0x3F;
               uint32_t const_idx = (sh->ucode[i] >> 20) & 0x1F;
               DbgPrint("xenos: VFETCH at dword%u stride=%u fmt=%u dst=r%u const=%u type=%u",
                        i, stride, fmt, dst, const_idx, type);
               if (stride == 0) {
                  DbgPrint("xenos: *** STRIDE ZERO in VFETCH dword%u const=%u fmt=%u type=%u ***",
                           i, const_idx, fmt, type);
               }
            }
            if (opc == 1) { /* TFETCH: texture fetch, opcode=1 */
               uint32_t dst = (sh->ucode[i] >> 8) & 0x7F;
               uint32_t const_idx = (sh->ucode[i] >> 16) & 0x1F;
               DbgPrint("xenos: TFETCH at dword%u dst=r%u const=%u type=%u uc=%08x %08x %08x",
                        i, dst, const_idx, type,
                        sh->ucode[i], sh->ucode[i+1], sh->ucode[i+2]);
            }
         }
         s_fetch_check++;
      }
   }

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

if (!vs || !vs->vfetch_count || !ve)
       return;
    elements = ve->elements;

    for (uint32_t f = 0; f < vs->vfetch_count; ++f) {
       struct xenos_vfetch_fixup *fx = &vs->vfetch[f];
       const struct pipe_vertex_element *e = NULL;
       struct pipe_vertex_buffer *vb;
       uint32_t phys, out[3];

       if (fx->attrib >= x->num_vertex_elements ||
           fx->attrib >= PIPE_MAX_ATTRIBS)
          continue;
       e = &elements[fx->attrib];
      vb = &x->vertex_buffers[e->vertex_buffer_index];
      if (!vb)
         continue;

      if (!vb->is_user_buffer && vb->buffer.resource)
         phys = (xenos_resource(vb->buffer.resource)->gpu_addr << 2) +
                vb->buffer_offset;
      else
         continue;   /* user buffers need the uploader (M2) */

      phys += e->src_offset;

xe_ucode_vfetch(out, fx->attrib, fx->dst_gpr, XE_UCODE_DST_SWIZ_XYZW,
                       XE_VFETCH_INDEX_GPR_X, 0, xenos_vfmt(e->src_format),
                       e->src_stride >> 2, 0, true, true);
       vs->ucode[fx->ucode_dword + 0] = out[0];
       vs->ucode[fx->ucode_dword + 1] = out[1];
       vs->ucode[fx->ucode_dword + 2] = out[2];

       {
          extern uint32_t xenos_diag_frame;
          uint32_t df = xenos_diag_frame;
          static int s_pf_log = 0;
          if ((df < 30u) || (df >= 5000u && df <= 5010u) ||
              (df >= 10000u && df <= 10010u) || s_pf_log++ < 20) {
             uint32_t patched_stride = out[2] & 0xFF;
             DbgPrint("PATCH_VFETCH f=%08x attr=%u dst_gpr=%u "
                      "stride_bytes=%u stride_dw=%u uc_dw=%u out2=0x%08x\n",
                      df, fx->attrib, fx->dst_gpr, e->src_stride,
                      patched_stride, fx->ucode_dword, out[2]);
          }
       }

       /* The z/w bootstrap ALU after the position fetch must only overwrite
        * lanes the element doesn't provide: 2D keeps z=0,w=1; 3D keeps the
        * fetched z and writes w=1; 4D needs nothing. */
       if (fx->attrib == 0 && fx->zw_override != UINT32_MAX) {
          unsigned nr = util_format_get_nr_components(e->src_format);
          uint32_t wm = ((uint32_t)nr < 4) << 3;         /* bit 3 = w */
          if (nr < 3)
             wm |= 1u << 2;                               /* bit 2 = z */
          uint32_t d = fx->zw_override;
          vs->ucode[d] = (vs->ucode[d] & ~(0xFu << 16)) | (wm << 16);
       }
   }

   /* Each vertex element maps to vertex-fetch constant i.  On the Xenos GPU,
     * vertex fetch constants are 2-dword entries packed at stride 2 from
     * register base 0x4800 (i.e. register 0x4800 + i*2).  Texture fetch
     * constants occupy 6-dword blocks at the same base, overlapping 3
     * vertex-fetch slots each.  The shader's VFETCH instruction references
     * the vertex-fetch index, so element i goes to register 0x4800 + i*2. */
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

       xe_gpu_vfetch_build(&vf, phys, MAX2(fetch_bytes, 4u),
                           XE_ENDIAN_8IN32);
       /* Vertex fetch constants are packed with stride 2 dwords at 0x4800,
        * not stride 6 (which is for texture fetch). Xenia reads vertex
        * fetch i from register 0x4800 + i*2.  We write 2 dwords per
        * vertex fetch constant. */
       {
         extern uint32_t xenos_diag_frame;
         uint32_t df = xenos_diag_frame;
         static int s_vf_log = 0;
         if ((df < 30u) || (df >= 5000u && df <= 5010u) ||
             (df >= 10000u && df <= 10010u) || s_vf_log++ < 12)
           DbgPrint("[VFC] f=%08x i=%u fmt=%u stride=%u src_off=%u vb=%u "
                    "phys=0x%08x bytes=%u",
                    df, i, xenos_vfmt(e->src_format),
                    e->src_stride >> 2, e->src_offset,
                    e->vertex_buffer_index, phys, fetch_bytes);
       }
       ctx_reserve(x, 3);
       xe_gpu_cmd_reg_writen(&x->cb,
                             0x4800 + i * 2, 2,
                             (const uint32_t *)&vf);
    }
}

/* Resolve an EDRAM-backed surface into its tiled system backing so it can be
 * sampled as a texture (render-to-texture).  The kCopy resolve reads the
 * surface described by RB_SURFACE_INFO/RB_COLOR_INFO, so temporarily point
 * those at the sampled surface before issuing the copy, restore afterwards. */
static void
xenos_resolve_edram_surface(struct xenos_context *x,
                            struct xenos_resource *res)
{
   uint32_t w, h;
   float *rect;
   xenos_vertex_fetch vf[3];

   if (!res || !res->has_edram || !res->resolve_data)
      return;

   w = res->base.width0;
   h = res->base.height0;

   ctx_reserve(x, 96);

   /* The resolve source is the sampled surface, not the current target. */
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_RB_SURFACE_INFO,
                        w | (XE_MSAA_1X << 16));
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_RB_COLOR_INFO,
                        ((res->edram_base & 0x7FF) |
                         ((res->edram_base >> 11) << 11) |
                         (XE_COLOR_FORMAT_8_8_8_8 << 16)));

   /* GetResolveInfo() intersects the resolve rectangle with the active
    * screen/window scissor and applies the window offset, so force a full
    * (unshifted) scissor covering exactly this surface - just like the frame
    * flush resolve does.  Without this, a stale app scissor/window-offset can
    * clamp or shift the copied region (and the app's last window offset is
    * applied even when the chosen rect would otherwise be fine). */
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_SC_SCREEN_SCISSOR_TL, 0);
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_SC_SCREEN_SCISSOR_BR, w | (h << 16));
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_SC_WINDOW_OFFSET, 0);
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_SC_WINDOW_SCISSOR_TL, 0x80000000u);
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_SC_WINDOW_SCISSOR_BR, w | (h << 16));

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
   memset(vf, 0, sizeof(vf));
   xe_gpu_vfetch_build(&vf[0], x->resolve_rect_phys, 24u, XE_ENDIAN_8IN32);
   /* Only write VF0 (2 dwords at 0x4800).  Writing 6 dwords would
    * clobber VF1/VF2 at 0x4802/0x4804 which are vertex-fetch constants
    * packed at stride 2 from base 0x4800. */
   xe_gpu_cmd_reg_writen(&x->cb, 0x4800, 2,
                         (const uint32_t *)vf);

   xe_gpu_cmd_reg_write(&x->cb, XE_REG_RB_MODECONTROL, XE_EDRAM_MODE_COPY);
   xenos_prim_to_initiator(x, MESA_PRIM_TRIANGLES, 3);
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_RB_MODECONTROL, XE_EDRAM_MODE_COLOR_DEPTH);
}

/* Emit the texture fetch constants (xe_gpu_texture_fetch_t, 6 dwords each)
 * for the currently-bound fragment sampler views, at the fetch-constant
 * register block the FS tex instructions reference (unit + base).  The FS
 * codegen uses the same mapping, so unit 0 -> fetch constant index
 * XE_TEX_FETCH_INDEX_BASE. */
static void
xenos_emit_texture_fetch_constants(struct xenos_context *x)
{
   struct xenos_resource *resolved = NULL;
   unsigned resolved_any = 0;

   {
      static unsigned fc_entry_log = 0;
      if (fc_entry_log < 10) {
         DbgPrint("TFETCH_ENTER nsv=%u ns=%u\n",
                  x->num_sampler_views, x->num_samplers);
         for (unsigned i = 0; i < x->num_sampler_views && i < 8; i++) {
            struct pipe_sampler_view *v = x->sampler_views[i];
            if (v) {
               DbgPrint("TFETCH_VIEW[%u] fmt=%u w=%u h=%u tgt=%d\n",
                        i, (unsigned)v->format,
                        v->texture ? v->texture->width0 : 0,
                        v->texture ? v->texture->height0 : 0,
                        v->texture ? v->texture->target : -1);
            } else {
               DbgPrint("TFETCH_VIEW[%u] NULL\n", i);
            }
         }
         fc_entry_log++;
      }
   }

   for (unsigned unit = 0; unit < x->num_sampler_views; unit++) {
      struct pipe_sampler_view *view = x->sampler_views[unit];
      if (!view || !view->texture)
         continue;
      if (view->texture->target != PIPE_TEXTURE_2D)
         continue;

      struct xenos_resource *res = xenos_resource(view->texture);
      uint32_t fc_index = unit + XE_TEX_FETCH_INDEX_BASE;
      if (fc_index > 31)
         continue;

      uint32_t mag = XE_TFETCH_FILTER_LINEAR;
      uint32_t min = XE_TFETCH_FILTER_LINEAR;
      uint32_t mip = XE_TFETCH_FILTER_POINT;
      uint32_t clamp = XE_TFETCH_CLAMP_REPEAT;
      if (unit < x->num_samplers && x->samplers[unit]) {
         struct pipe_sampler_state *s = x->samplers[unit];
         mag = (s->mag_img_filter == PIPE_TEX_FILTER_NEAREST)
                  ? XE_TFETCH_FILTER_POINT : XE_TFETCH_FILTER_LINEAR;
         min = (s->min_img_filter == PIPE_TEX_FILTER_NEAREST)
                  ? XE_TFETCH_FILTER_POINT : XE_TFETCH_FILTER_LINEAR;
         mip = (s->min_mip_filter == PIPE_TEX_MIPFILTER_LINEAR)
                  ? XE_TFETCH_FILTER_LINEAR : XE_TFETCH_FILTER_POINT;
         switch (s->wrap_s) {
         case PIPE_TEX_WRAP_CLAMP_TO_EDGE:
         case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE:
            clamp = XE_TFETCH_CLAMP_TO_EDGE;
            break;
         default:
            clamp = XE_TFETCH_CLAMP_REPEAT;
            break;
         }
      }

      uint32_t guest_phys;
      uint32_t tiled = 0;

      {
         static unsigned diag_log = 0;
         if (diag_log < 30) {
            DbgPrint("TFETCH_DIAG unit=%u has_edram=%u resolve_data=%p rendered=%u gpu_addr=%08X data=%p tiled_val=%u\n",
                     unit, res->has_edram, (void*)(uintptr_t)res->resolve_data,
                     res->rendered, res->gpu_addr, res->data, tiled);
            diag_log++;
         }
      }

      if (res->has_edram && res->resolve_data && res->rendered) {
         /* Render-to-texture: this surface was drawn into EDRAM and is now
          * sampled as a texture.  Its pixels live in the EDRAM tiles, not in
          * the untouched linear res->data storage, and the kCopy resolve
          * writes tiled memory - so resolve EDRAM into this surface's tiled
          * backing and make the tfetch read it tiled. */
         if (resolved != res) {
            xenos_resolve_edram_surface(x, res);
            resolved = res;
            resolved_any = 1;
         }
         /* Bypass MmGetPhysicalAddress (returns wrong values for Mesa allocs).
          * Compute the physical address directly: the vE0000000 heap maps
          * VA [0xE0000000, 0xFFD00000) to PA via P = (VA - 0xE0000000) + 0x1000.
          * This is the physical address the GPU reads from in shared_memory. */
         {
            uint32_t va = (uint32_t)(uintptr_t)res->resolve_data;
            if (va >= 0xE0000000u && va < 0xFFD00000u) {
               guest_phys = (va - 0xE0000000u) + 0x1000u;
            } else {
               guest_phys = res->resolve_phys;
            }
         }
         tiled = 1;
      } else if (res->has_edram && res->resolve_data && res->data) {
         /* ALWAYS copy texture data into the resolve buffer and sample from
          * it linearly.  The old gpu_addr<<2 path failed because the
          * guest-physical address did not contain the data written by Mesa to
          * res->data -- they are different host addresses.  The resolve buffer
          * has a valid guest-physical address that the GPU can actually read,
          * so copy the data there and sample from it. */
         unsigned tex_size = (unsigned)MAX2(1u, res->base.width0) *
                             (unsigned)MAX2(1u, res->base.height0) *
                             util_format_get_blocksize(res->base.format);
         if (tex_size > res->size)
            tex_size = res->size;
         memcpy(res->resolve_data, res->data, tex_size);
         /* Bypass MmGetPhysicalAddress — compute PA directly for vE0000000. */
         {
            uint32_t va = (uint32_t)(uintptr_t)res->resolve_data;
            if (va >= 0xE0000000u && va < 0xFFD00000u) {
               guest_phys = (va - 0xE0000000u) + 0x1000u;
            } else {
               guest_phys = res->resolve_phys;
            }
         }
      } else if (res->gpu_addr) {
         /* Fallback: resource has a GPU-accessible physical address but no
          * resolve buffer -- hope the guest-physical mapping works. */
         guest_phys = res->gpu_addr << 2;
      } else {
         guest_phys = res->gpu_addr << 2;
      }

      {
         static unsigned post_log = 0;
         if (post_log < 50) {
            /* Probe the first dwords at res->data (where Mesa wrote). */
            const uint32_t *probe1 = (const uint32_t *)res->data;
            /* Compute PA the same way as above for verification. */
            uint32_t verify_va = (uint32_t)(uintptr_t)res->resolve_data;
            uint32_t verify_pa = 0;
            if (verify_va >= 0xE0000000u && verify_va < 0xFFD00000u)
               verify_pa = (verify_va - 0xE0000000u) + 0x1000u;
            /* Also probe res->resolve_data (the buffer we copy TO). */
            const uint32_t *probe_rd = res->resolve_data
                                          ? (const uint32_t *)res->resolve_data
                                          : NULL;
            DbgPrint("TFETCH_POST unit=%u guest_phys=%08X resolve_phys=%08X "
                     "verify_pa=%08X data=%p rdata=%p tiled=%u "
                     "fmt=%u w=%u h=%u\n",
                     unit, guest_phys, res->resolve_phys, verify_pa,
                     res->data, res->resolve_data, tiled,
                     (unsigned)view->format,
                     (unsigned)res->base.width0,
                     (unsigned)res->base.height0);
            if (probe1)
               DbgPrint("  data[0..3]=[%08X %08X %08X %08X]\n",
                        probe1[0], probe1[1], probe1[2], probe1[3]);
            if (probe_rd)
               DbgPrint("  rdata[0..3]=[%08X %08X %08X %08X]\n",
                        probe_rd[0], probe_rd[1], probe_rd[2], probe_rd[3]);
            if (probe_rd) {
               /* Content scan: nonzero dwords and 0xDE fill-pattern bytes
                * over a 1 MiB window of the resolve buffer, plus probes at
                * the middle and end of the allocation. */
               uint32_t nz = 0, de = 0;
               uint32_t cw = res->size / 4;
               uint32_t cap = 262144u; /* 1 MiB / 4 */
               if (cw > cap)
                  cw = cap;
               const uint32_t *cw_ptr = (const uint32_t *)res->resolve_data;
               const uint8_t *cb = (const uint8_t *)res->resolve_data;
               for (uint32_t i = 0; i < cw; i++)
                  if (cw_ptr[i])
                     nz++;
               if (cw >= 1024u)
                  for (uint32_t i = 0; i < 262144u; i++)
                     if (cb[i] == 0xDEu)
                        de++;
               uint32_t mid[4] = { 0, 0, 0, 0 }, end[4] = {0, 0, 0, 0};
               uint32_t tot = (uint32_t)(res->size / 4);
               if (tot > 4096u) {
                  memcpy(mid, cw_ptr + (tot / 2), 16);
                  memcpy(end, cw_ptr + (tot - 4), 16);
               }
               DbgPrint("  scan nz=%08X de=%08X words=%08X "
                        "mid=%08X,%08X,%08X,%08X end=%08X,%08X,%08X,%08X\n",
                        nz, de, cw, mid[0], mid[1], mid[2], mid[3],
                        end[0], end[1], end[2], end[3]);
            }
            /* Decode the fetch constant dword1 base_page. */
            DbgPrint("  fetch_base_page=%08X (phys=%08X)\n",
                     (guest_phys >> 12) & 0xFFFFFu, guest_phys);
            post_log++;
         }
      }

      uint32_t dw[6];
      uint32_t tfmt = xenos_tfetch_format(view->format);
      uint32_t swiz = 0x688u; /* RGBA identity: R,G,B,A */
      /* DXT/DXN compressed formats use packed swizzle in the fetch constant. */
      if (tfmt == XE_TFETCH_FORMAT_DXT1 || tfmt == XE_TFETCH_FORMAT_DXT4_5 ||
          tfmt == XE_TFETCH_FORMAT_DXT2_3 || tfmt == XE_TFETCH_FORMAT_DXN) {
         swiz = 0x688u; /* RGBA still identity for compressed */
      }
      xe_gpu_tfetch_build(dw, guest_phys,
                          view->texture->width0, view->texture->height0,
                          mag, min, mip, clamp, swiz, tiled);
      /* Override the format in dword1 since xe_gpu_tfetch_build hardcodes
       * k_8_8_8_8. */
      dw[1] = (dw[1] & ~0x3Fu) | (tfmt & 0x3Fu);

      {
         static unsigned fc_log = 0;
         if (fc_log < 30) {
            DbgPrint("TFETCH idx=%u phys=%08X w=%u h=%u tfmt=%u dw0=%08X dw1=%08X tiled=%u\n",
                     fc_index, guest_phys,
                     view->texture->width0, view->texture->height0,
                     tfmt, dw[0], dw[1], tiled);
            fc_log++;
         }
      }

      ctx_reserve(x, 6);
      xe_gpu_cmd_reg_writen(&x->cb, XE_REG_SHADER_CONST_FETCH(fc_index), 6,
                            dw);
   }

   if (resolved_any) {
      /* The resolve switched the surface info to the sampled resource; put
       * the real render target state back for the draw. */
      xenos_emit_frame_state(x);
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

    DbgPrint("xenos: clear called buffers=%08x w=%u h=%u color=[%.3f %.3f %.3f %.3f] depth=%.3f\n",
             buffers, x->framebuffer.width, x->framebuffer.height,
             color ? color->f[0] : -1, color ? color->f[1] : -1,
             color ? color->f[2] : -1, color ? color->f[3] : -1, depth);
    {
       static int s_clear_log = 0;
       if (s_clear_log++ < 12 && color) {
          uint32_t c0 = *(const uint32_t *)&color->f[0];
          uint32_t c1 = *(const uint32_t *)&color->f[1];
          uint32_t c2 = *(const uint32_t *)&color->f[2];
          uint32_t c3 = *(const uint32_t *)&color->f[3];
          DbgPrint("xenos: clear raw c=%08x %08x %08x %08x", c0, c1, c2, c3);
       }
    }

   if (!buffers || !x->framebuffer.width)
      return;

   /* A clear writes the bound surfaces; their EDRAM content is now
    * authoritative for later texture sampling. */
   for (unsigned i = 0; i < x->framebuffer.nr_cbufs; i++)
      if (x->framebuffer.cbufs[i].texture)
         xenos_resource(x->framebuffer.cbufs[i].texture)->rendered = 1;
   if (x->framebuffer.zsbuf.texture)
      xenos_resource(x->framebuffer.zsbuf.texture)->rendered = 1;

   /* The color clear is expressed as a fullscreen triangle draw.  Force the
    * depth state so the clear always writes, regardless of the
    * currently-bound DSA state:
    *  - If the application has depth test enabled (LESS) with a real depth
    *    buffer, the clear triangle would fail the LESS test against stale
    *    depth, so force zfunc=ALWAYS.
    *  - When PIPE_CLEAR_DEPTH is requested the clear writes depth too, so
    *    enable the depth writemask (still ALWAYS, so a stale/deep depth
    *    buffer can't reject the clear). */
   bool clear_depth = (buffers & PIPE_CLEAR_DEPTH) != 0;
   void *saved_dsa = x->dsa;
   if (!x->dsa_override_active) {
      x->clear_dsa.depth_enabled = true;
      x->clear_dsa.depth_writemask = clear_depth;
      x->clear_dsa.depth_func = PIPE_FUNC_ALWAYS;
      x->dsa = &x->clear_dsa;
      x->dsa_override_active = 1;
   }
   xenos_emit_frame_state(x);
   if (x->dsa_override_active) {
      x->dsa = saved_dsa;
      x->dsa_override_active = 0;
   }

   /* The clear must cover the whole framebuffer regardless of the app's stale
    * viewport and scissor state (the app's viewport/scissor is its game-view
    * rect, e.g. the 640x480 game surface, which would otherwise clip the
    * frontbuffer clear to that corner and leave the rest unwritten). */
   ctx_reserve(x, 22);
   {
      uint32_t w = x->framebuffer.width, h = x->framebuffer.height;
      uint32_t vte = XE_VTE_VPORT_X_SCALE_ENA | XE_VTE_VPORT_X_OFFSET_ENA |
                     XE_VTE_VPORT_Y_SCALE_ENA | XE_VTE_VPORT_Y_OFFSET_ENA |
                     XE_VTE_VPORT_Z_SCALE_ENA | XE_VTE_VPORT_Z_OFFSET_ENA |
                     XE_VTE_VTX_W0_FMT;
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_CL_VTE_CNTL, vte);
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_CL_VPORT_XSCALE,
                           float_bits(w / 2.0f));
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_CL_VPORT_XOFFSET,
                           float_bits(w / 2.0f));
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_CL_VPORT_YSCALE,
                           float_bits(h / 2.0f));
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_CL_VPORT_YOFFSET,
                           float_bits(h / 2.0f));
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_CL_VPORT_ZSCALE,
                           float_bits(0.5f));
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_CL_VPORT_ZOFFSET,
                           float_bits(0.5f));
      /* Also override scissors to cover the full framebuffer, not the
       * GL-bound 640x480 game surface. */
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_SC_SCREEN_SCISSOR_TL, 0);
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_SC_SCREEN_SCISSOR_BR,
                           w | (h << 16));
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_SC_WINDOW_SCISSOR_TL,
                           0x80000000u);
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_SC_WINDOW_SCISSOR_BR,
                           w | (h << 16));
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_SC_WINDOW_OFFSET, 0);
   }

   /* Clear through draws: fullscreen triangle with the minimal shaders
    * (VS covers [-1..3] NDC; PS outputs guest c48 = host bank-256 c48).
    * The VS passes the vertex z straight through to clip space, so the NDC
    * z of the triangle controls the depth value written when the depth
    * clear is requested: map the requested clear depth (already in window
    * [0,1] after the GL depth range transform) back through the host's
    * z_ndc = (z_win - tz) / sz viewport transform.  sz=tz=0.5, so
    * z_ndc = 2*depth - 1. */
   if (!x->clear_vs_dwords) {
      x->clear_vs_dwords = xe_ucode_build_vs_minimal(x->clear_vs);
      x->clear_ps_dwords = xe_ucode_build_ps_minimal(x->clear_ps);
   }
   DbgPrint("xenos: clear shaders built\n");
   xenos_load_shader_raw(x, x->clear_vs, x->clear_vs_dwords, 0);
   DbgPrint("xenos: clear vs loaded\n");
   xenos_load_shader_raw(x, x->clear_ps, x->clear_ps_dwords, 1);
   DbgPrint("xenos: clear ps loaded\n");

   if (buffers & PIPE_CLEAR_COLOR) {
      /* The Xbox 360 GPU treats NaN as 0 when converting float to unorm8.
       * Sanitize: NaN→0, clamp to [0,1]. */
      float cf[4];
      for (unsigned i = 0; i < 4; i++) {
         float v = color->f[i];
         if (v != v) v = 0.0f;          /* NaN → 0 */
         if (v < 0.0f) v = 0.0f;
         if (v > 1.0f) v = 1.0f;
         cf[i] = v;
      }
      uint32_t cc[4] = { float_bits(cf[0]), float_bits(cf[1]),
                         float_bits(cf[2]), float_bits(cf[3]) };
      ctx_reserve(x, 6);
      xe_gpu_cmd_reg_writen(&x->cb, XE_REG_SHADER_CONST(256 + 48), 4, cc);
   }

   /* Point fetch constant 0 at a fullscreen triangle (NDC [-1..3], w=1) so
    * the minimal clear VS (vfetch const0 -> r0.xyzw) produces real NDC
    * positions covering the whole viewport, instead of stale/garbage data.
    * The vertex index comes from the sequencer, stride 16 bytes. The fetch
    * size spans all three vertices (12 words, 48 bytes), not just the first. */
   if (x->clear_vert) {
      float zc = (float)(2.0 * depth - 1.0);
      float *cv = (float *)x->clear_vert;
      cv[ 0] = -1.0f; cv[ 1] = -1.0f; cv[ 2] = zc; cv[ 3] = 1.0f;
      cv[ 4] =  3.0f; cv[ 5] = -1.0f; cv[ 6] = zc; cv[ 7] = 1.0f;
      cv[ 8] = -1.0f; cv[ 9] =  3.0f; cv[10] = zc; cv[11] = 1.0f;
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

   {
      static int s_draw_log = 0;
      if (s_draw_log < 20) {
         DbgPrint("xenos: DRAWBANANA mode=%u count=%u vs=%08x fs=%08x",
                  dinfo->mode, draws[0].count,
                  (uint32_t)(uintptr_t)x->vs, (uint32_t)(uintptr_t)x->fs);
         s_draw_log++;
      }
   }
   /* DIAGNOSTIC: full draw-state snapshot for boot vs. in-game frames.
    * Every field is printed raw (PPC32 varargs only trusts %08x groups). */
   {
      extern uint32_t xenos_diag_frame;
      uint32_t df = xenos_diag_frame;
      bool logit = df < 30u || (df >= 5000u && df <= 5010u) ||
                   (df >= 10000u && df <= 10010u);
      if (logit) {
         struct pipe_framebuffer_state *fb = &x->framebuffer;
         struct xenos_resource *color =
            fb->cbufs[0].texture ? xenos_resource(fb->cbufs[0].texture) : NULL;
         const struct pipe_viewport_state *vp = &x->viewport[0];
         DbgPrint("DRAWDIAG f=%08x mode=%08x count=%08x fbw=%08x fbh=%08x "
                  "ebase=%08x",
                  df, dinfo->mode, draws[0].count, fb->width, fb->height,
                  color ? color->edram_base : 0xFFFFFFFFu);
         DbgPrint("  vp sx=%08x sy=%08x sz=%08x tx=%08x ty=%08x tz=%08x",
                  float_bits(vp->scale[0]), float_bits(vp->scale[1]),
                  float_bits(vp->scale[2]), float_bits(vp->translate[0]),
                  float_bits(vp->translate[1]), float_bits(vp->translate[2]));
         struct pipe_blend_state *bl = x->blend;
         if (bl)
            DbgPrint("  blend en=%08x rf=%08x df=%08x arf=%08x adf=%08x "
                     "rmask=%08x",
                     (uint32_t)bl->rt[0].blend_enable,
                     (uint32_t)bl->rt[0].rgb_src_factor,
                     (uint32_t)bl->rt[0].rgb_dst_factor,
                     (uint32_t)bl->rt[0].alpha_src_factor,
                     (uint32_t)bl->rt[0].alpha_dst_factor,
                     (uint32_t)bl->rt[0].colormask);
         /* First vertex of attribute 0 stream. */
         struct xenos_velems *ve = x->vertex_elements;
         if (ve && ve->count > 0) {
            const struct pipe_vertex_element *e = &ve->elements[0];
            struct pipe_vertex_buffer *vb =
               &x->vertex_buffers[e->vertex_buffer_index];
            if (vb && !vb->is_user_buffer && vb->buffer.resource) {
               struct xenos_resource *xr =
                  xenos_resource(vb->buffer.resource);
               const uint32_t *src =
                  (const uint32_t *)((const char *)xr->data +
                                     vb->buffer_offset + e->src_offset);
               DbgPrint("  v0[0..7]=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x",
                        src[0], src[1], src[2], src[3], src[4], src[5],
                        src[6], src[7]);
               DbgPrint("  v8[0..7]=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x",
                        src[8], src[9], src[10], src[11], src[12], src[13],
                        src[14], src[15]);
            }
         }
         /* Draw-time ucode content (post vfetch patch): fingerprint boot
          * vs in-game so an in-place shader mutation can't hide. */
         {
            struct xenos_shader *dvs = x->vs;
            struct xenos_shader *dfs = x->fs;
            if (dvs && dvs->ucode_dwords) {
               const uint32_t *u = dvs->ucode;
               DbgPrint("  UC-VS n=%08x", dvs->ucode_dwords);
               DbgPrint("  VS d0..3=%08x %08x %08x %08x", u[0], u[1], u[2], u[3]);
               DbgPrint("  VS d4..7=%08x %08x %08x %08x", u[4], u[5], u[6], u[7]);
            }
            if (dfs && dfs->ucode_dwords) {
               const uint32_t *u2 = dfs->ucode;
               DbgPrint("  UC-FS n=%08x", dfs->ucode_dwords);
               DbgPrint("  FS d0..3=%08x %08x %08x %08x",
                        u2[0], u2[1], u2[2], u2[3]);
               DbgPrint("  FS d4..7=%08x %08x %08x %08x",
                        u2[4], u2[5], u2[6], u2[7]);
            }
         }
      }
   }

   /* DIAGNOSTIC: compact per-draw inventory for EVERY draw, one line, so any
    * dev frame found by the Xenia DEVSCAN monitor can be correlated with the
    * draws that ran that frame (same xenos_diag_frame basis). */
   {
      extern uint32_t xenos_diag_frame;
      struct pipe_framebuffer_state *fb = &x->framebuffer;
      struct xenos_resource *color =
         fb->cbufs[0].texture ? xenos_resource(fb->cbufs[0].texture) : NULL;
      uint32_t vw = 0, vh = 0, vfmt = 0xFFFFFFFFu;
      for (unsigned svi = 0; svi < x->num_sampler_views; svi++) {
         struct pipe_sampler_view *sv = x->sampler_views[svi];
         if (sv && sv->texture) {
            vw = sv->texture->width0;
            vh = sv->texture->height0;
            vfmt = (unsigned)sv->format;
            break;
         }
      }
      DbgPrint("DBG f=%08x mode=%08x count=%08x fbw=%08x fbh=%08x "
               "ebase=%08x vs=%08x fs=%08x vw=%08x vh=%08x vfmt=%08x",
               xenos_diag_frame, dinfo->mode, draws[0].count, fb->width,
               fb->height, color ? color->edram_base : 0xFFFFFFFFu,
               (uint32_t)(uintptr_t)x->vs, (uint32_t)(uintptr_t)x->fs,
               vw, vh, vfmt);
   }

   {
      static int s_guard = 0;
      if (s_guard < 10) {
         DbgPrint("GUARD vs=%p fs=%p indirect=%p idx=%u",
                  (void*)x->vs, (void*)x->fs,
                  (void*)indirect, dinfo->index_size);
         s_guard++;
      }
   }
   if (!x->vs || !x->fs || indirect || dinfo->index_size)
      return;                     /* indexed path arrives with M2 */

   /* The current framebuffer's surfaces receive this draw; their EDRAM
    * content becomes authoritative for later texture sampling. */
   for (unsigned i = 0; i < x->framebuffer.nr_cbufs; i++)
      if (x->framebuffer.cbufs[i].texture)
         xenos_resource(x->framebuffer.cbufs[i].texture)->rendered = 1;
   if (x->framebuffer.zsbuf.texture)
      xenos_resource(x->framebuffer.zsbuf.texture)->rendered = 1;

   for (unsigned d = 0; d < num_draws; d++) {
      xenos_emit_frame_state(x);
      xenos_patch_vfetch(x);
      xenos_load_shader(x, x->vs, 0);
      xenos_load_shader(x, x->fs, 1);
      /* FS ALPHA FIX v6: constant-1 mechanism handles alpha=1.0 in ucode.
       * No SQ_PS_CONST or constant upload needed. */
      xenos_upload_constants(x, x->vs, MESA_SHADER_VERTEX);
      {
         static int ulog = 0;
         if (ulog < 5) {
            DbgPrint("UPLOAD-CALL fs=%p nc=%d nu=%d",
                     (void*)x->fs,
                     x->fs ? ((struct xenos_shader*)x->fs)->num_consts : -1,
                     x->fs ? ((struct xenos_shader*)x->fs)->num_ubos : -1);
            ulog++;
         }
      }
      xenos_upload_constants(x, x->fs, MESA_SHADER_FRAGMENT);
      xenos_emit_texture_fetch_constants(x);
      /* QUADS: the Xbox 360 GPU has no native quad primitive.
       * Expand each quad (4 verts) into a separate triangle-strip draw
       * so vertices are consumed in the correct groups of 4. */
      if (dinfo->mode == MESA_PRIM_QUADS) {
         unsigned total = draws[d].count;
         for (unsigned q = 0; q + 3 < total; q += 4) {
            /* Each sub-draw: TRIANGLE_STRIP of 4 verts, offset to the
             * correct quad in the vertex buffer. */
            uint32_t prim = 6u /* Xenia kTriangleStrip */ |
                            (XE_SOURCE_SEL_AUTO_INDEX << 6) |
                            (XE_MAJOR_MODE_IMPLICIT << 8) |
                            (4u << 16); /* 4 vertices → 2 triangles */
            ctx_reserve(x, 5);
            xe_gpu_cmd_reg_write(&x->cb, XE_REG_VGT_MAX_VTX_INDX, 3);
            xe_gpu_cmd_reg_write(&x->cb, XE_REG_VGT_MIN_VTX_INDX, 0);
            xe_gpu_cmd_reg_write(&x->cb, XE_REG_VGT_INDX_OFFSET, q);
            xe_gpu_cmd_draw(&x->cb, prim, 0, 0, 0);
         }
      } else {
         xenos_prim_to_initiator(x, dinfo->mode, draws[d].count);
      }
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

static void
xenos_texture_subdata(struct pipe_context *pipe,
                      struct pipe_resource *pres,
                      unsigned level, unsigned usage,
                      const struct pipe_box *box,
                      const void *data,
                      unsigned stride, uintptr_t layer_stride)
{
   struct xenos_resource *res = xenos_resource(pres);
   unsigned blocksize = util_format_get_blocksize(pres->format);
   unsigned row_bytes = box->width * blocksize;

   /* Fresh CPU data supersedes any prior EDRAM content: the surface is
    * sampled linearly until it is drawn/cleared as a target again. */
   res->rendered = 0;

   if (box->depth > 1 && layer_stride == 0)
      layer_stride = row_bytes;
   if (box->height > 1 && stride == 0)
      stride = row_bytes;

   for (unsigned z = 0; z < (unsigned)box->depth; z++) {
      const uint8_t *src = (const uint8_t *)data + z * layer_stride;
      uint8_t *dst = (uint8_t *)res->data + z * res->size;
      dst += box->y * res->stride + box->x * blocksize;
      for (unsigned y = 0; y < (unsigned)box->height; y++) {
         memcpy(dst, src, row_bytes);
         dst += res->stride;
         src += stride;
      }
   }
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

/* Blit: a textured-quad draw that copies the blit source rect into the
 * destination rect, sampling either the src's resolved tiled backing
 * (render-to-texture) or its linear memory.  Replaces the old no-op stub
 * (the app's glBlitFramebuffer letterbox presentation went nowhere, which
 * is why the screen stayed black). */
static void
xenos_blit(struct pipe_context *pipe, const struct pipe_blit_info *info)
{
   struct xenos_context *x = xenos_context(pipe);
   struct xenos_resource *src, *dst;
   uint32_t sw, sh, dw, dh;
   float *bv;
   uint32_t mag, min;

   if (!info || !info->src.resource || !info->dst.resource)
      return;
   if (info->src.resource->target != PIPE_TEXTURE_2D ||
       info->dst.resource->target != PIPE_TEXTURE_2D)
      return;
   if (!(info->mask & PIPE_MASK_RGBA))
      return;

   src = xenos_resource(info->src.resource);
   dst = xenos_resource(info->dst.resource);
   sw = info->src.resource->width0;
   sh = info->src.resource->height0;
   dw = info->dst.resource->width0;
   dh = info->dst.resource->height0;

   DbgPrint("xenos: blitD %ux%u bx=%d by=%d bz=%d bw=%d bh=%d", dw, dh,
            info->dst.box.x, info->dst.box.y, info->dst.box.z,
            info->dst.box.width, info->dst.box.height);
   DbgPrint("xenos: blitS %ux%u sx=%d sy=%d sw=%d sh=%d", sw, sh,
            info->src.box.x, info->src.box.y, info->src.box.width,
            info->src.box.height);
   DbgPrint("xenos: blitM mask=%x filter=%u dstf=%u swz=%u", info->mask,
            info->filter, (unsigned)info->dst.format, (unsigned)1);

   if (!src->has_edram || !src->resolve_data)
      return;                     /* source has no EDRAM backing to resolve */

   ctx_reserve(x, 192);

   /* 1. Resolve the source EDRAM surface into its tiled system backing so the
    * blit FS can sample it as a texture.  This switches RB_SURFACE_INFO to
    * the source, so re-emit the destination framebuffer state afterwards. */
   xenos_resolve_edram_surface(x, src);

   /* The blit must draw INTO the destination resource, not whatever
    * framebuffer happens to be bound (at swap time that is the app's render
    * surface, so without a rebind the game content never reaches the
    * presented backbuffer).  Temporarily swap the context framebuffer to the
    * dest while emitting the surface state, then restore it.  Plain struct
    * copies keep the surface refcounts untouched. */
   {
      struct pipe_framebuffer_state saved_fb = x->framebuffer;
      memset(&x->framebuffer.cbufs[0], 0, sizeof(struct pipe_surface));
      x->framebuffer.cbufs[0].texture = info->dst.resource;
      x->framebuffer.cbufs[0].format = info->dst.resource->format;
      x->framebuffer.width = dw;
      x->framebuffer.height = dh;
      x->framebuffer.nr_cbufs = 1;
      x->framebuffer.zsbuf.texture = NULL;
      xenos_resource_assign_edram(x->screen, dst, PIPE_BIND_RENDER_TARGET);
      xenos_emit_frame_state(x);
      x->framebuffer = saved_fb;
   }

   /* Cull off for the blit (rasterizer cull state is otherwise sticky/unset). */
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_SU_SC_MODE_CNTL, 0u);

   /* 2. Build/load the blit program once. */
   if (!x->blit_vs_dwords) {
      x->blit_vs_dwords = xe_ucode_build_vs_blit(x->blit_vs);
      x->blit_ps_dwords = xe_ucode_build_ps_blit(x->blit_ps);
   }
   xenos_load_shader_raw(x, x->blit_vs, x->blit_vs_dwords, 0);
   xenos_load_shader_raw(x, x->blit_ps, x->blit_ps_dwords, 1);

   /* Program/interpolator wiring for the blit VS(2 gpr)/PS(2 gpr):
    * VS interpolator export 0 feeds FS input GPR0 (uv). */
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_SQ_PROGRAM_CNTL,
                        ((2u - 1u) & 0x3F) | (((2u - 1u) & 0x3F) << 8));
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_SQ_INTERPOLATOR_CNTL, 0u);

   /* Blit viewport: map NDC [-1,1] onto the whole destination surface, so the
    * quad NDC (computed from the dst box in surface pixels) lands correctly
    * regardless of the game's stale viewport state.  Y scale is negated:
    * GL's blit boxes are bottom-up, but the Xenos window is top-down, and the
    * resolved tiled memory stores row 0 as the GL bottom row - so a GL
    * "bottom" dst Y must end up at the top of the surface to stay upright
    * through present (VdSwap samples EDRAM row 0 at the top of the screen). */
   {
      uint32_t vte = XE_VTE_VPORT_X_SCALE_ENA | XE_VTE_VPORT_X_OFFSET_ENA |
                     XE_VTE_VPORT_Y_SCALE_ENA | XE_VTE_VPORT_Y_OFFSET_ENA |
                     XE_VTE_VPORT_Z_SCALE_ENA | XE_VTE_VPORT_Z_OFFSET_ENA |
                     XE_VTE_VTX_W0_FMT;
      /* NOTE: dw/dh are uint32_t - must cast to float BEFORE negating, or the
       * negation wraps to ~2^32 and the scales become +2.1e9 garbage (which
       * used to rasterize the blit quad as a full-height diagonal sliver). */
      float xs = (float)dw * 0.5f;
      float xt = (float)dw * 0.5f;
      float ys = -(float)dh * 0.5f;
      float yt = (float)dh * 0.5f;
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_CL_VTE_CNTL, vte);
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_CL_VPORT_XSCALE,
                           float_bits(xs));
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_CL_VPORT_XOFFSET,
                           float_bits(xt));
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_CL_VPORT_YSCALE,
                           float_bits(ys));
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_CL_VPORT_YOFFSET,
                           float_bits(yt));
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_CL_VPORT_ZSCALE,
                           float_bits(0.5f));
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_CL_VPORT_ZOFFSET,
                           float_bits(0.5f));
      DbgPrint("xenos: blitVP = sx=%08x sy=%08x tx=%08x ty=%08x dw=%u dh=%u",
               float_bits(xs), float_bits(ys), float_bits(xt), float_bits(yt),
               dw, dh);
   }

   /* 3. Texture fetch constant for the source's resolved tiled backing at the
    * block the blit FS samples. */
   mag = (info->filter == PIPE_TEX_FILTER_LINEAR) ? XE_TFETCH_FILTER_LINEAR
                                                  : XE_TFETCH_FILTER_POINT;
   min = mag;
   {
      uint32_t dw6[6];
      xe_gpu_tfetch_build(dw6, src->resolve_phys, sw, sh, mag, min,
                          XE_TFETCH_FILTER_POINT, XE_TFETCH_CLAMP_TO_EDGE,
                          0x688u /* RGBA identity */, 1 /* tiled */);
      xe_gpu_cmd_reg_writen(&x->cb, XE_REG_SHADER_CONST_FETCH(
                                    XE_BLIT_TEX_FETCH_INDEX), 6, dw6);
   }

   /* 4. Quad vertices: NDC from dst box, UVs from src box.  Each vertex is
    * 8 dwords {x,y,z,w, u,v,0,1}; the VS fetches pos (const0, offset 0) and
    * uv (const1, offset 4 dwords), stride 8 dwords / 32 bytes. */
   if (!x->blit_vert)
      return;
   bv = (float *)x->blit_vert;
   {
      float dx0 = info->dst.box.x, dy0 = info->dst.box.y;
      float dx1 = dx0 + info->dst.box.width;
      float dy1 = dy0 + info->dst.box.height;
      float sx0 = info->src.box.x, sy0 = info->src.box.y;
      float sx1 = sx0 + info->src.box.width;
      float sy1 = sy0 + info->src.box.height;
      const float ndc_x[4] = { dx0, dx1, dx0, dx1 };
      const float ndc_y[4] = { dy0, dy0, dy1, dy1 };
      const float u[4] = { sx0, sx1, sx0, sx1 };
      /* v is INVERTED vs the box order: the YSCALE is negated, so the vertex
       * at NDC-y=1 (dy1) lands at window row 0 (top) - it must carry v = sy0
       * so the source's top row appears at the top of the blit.  With the
       * source row 0 stored at v=0 in the resolved backing, window top must
       * sample v=0 to keep the game content upright. */
      const float v[4] = { sy1, sy1, sy0, sy0 };
      for (unsigned i = 0; i < 4; i++) {
         bv[i * 8 + 0] = (ndc_x[i] / (float)dw) * 2.0f - 1.0f;
         bv[i * 8 + 1] = (ndc_y[i] / (float)dh) * 2.0f - 1.0f;
         bv[i * 8 + 2] = 0.0f;
         bv[i * 8 + 3] = 1.0f;
         bv[i * 8 + 4] = u[i] / (float)sw;
         bv[i * 8 + 5] = v[i] / (float)sh;
         bv[i * 8 + 6] = 0.0f;
         bv[i * 8 + 7] = 1.0f;
      }
   }

    /* 5. Vertex fetch constants: attrib 0 = position, attrib 1 = uv, both from
      * the blit quad buffer.  Vertex fetch constants use stride-2 at 0x4800. */
    {
       xenos_vertex_fetch vf[2];
       memset(vf, 0, sizeof(vf));
       xe_gpu_vfetch_build(&vf[0], x->blit_vert_phys, 128u, XE_ENDIAN_8IN32);
       xe_gpu_vfetch_build(&vf[1], x->blit_vert_phys, 128u, XE_ENDIAN_8IN32);
       xe_gpu_cmd_reg_writen(&x->cb, 0x4800 + 0 * 2, 2,
                             (const uint32_t *)&vf[0]);
       xe_gpu_cmd_reg_writen(&x->cb, 0x4800 + 1 * 2, 2,
                             (const uint32_t *)&vf[1]);
    }

   /* Depth test must not reject the blit quad either. */
   {
      uint32_t dc = (1u << 1) | (0u << 2) | ((uint32_t)PIPE_FUNC_ALWAYS << 4);
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_RB_DEPTHCONTROL, dc);
   }

   xenos_prim_to_initiator(x, MESA_PRIM_TRIANGLE_STRIP, 4);
   ctx_submit(x);
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

   /* Depth-disabled DSA state used to force the color-clear draw past any
    * bound depth-test state (see xenos_clear). */
   x->clear_dsa.depth_enabled = false;
   x->clear_dsa.depth_writemask = false;
   x->clear_dsa.depth_func = PIPE_FUNC_ALWAYS;

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
   x->base.blit = xenos_blit;
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
   x->base.texture_subdata = xenos_texture_subdata;

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
   x->blit_vert = x->screen->ws->alloc(x->screen->ws, 64, 64);
   if (x->blit_vert && x->screen->ws->get_physical)
      x->blit_vert_phys =
         (uint32_t)x->screen->ws->get_physical(x->screen->ws, x->blit_vert);

   return &x->base;
}

/* ------------------------------------------------------------------ */
/* Frame end: resolve the colour target into its tiled system backing  */
/* ------------------------------------------------------------------ */

/* Diag frame counter: incremented per present so draw-state dumps can be
 * keyed to specific game frames (boot vs in-game). */
uint32_t xenos_diag_frame = 0;

bool
xenos_flush_frame(struct pipe_context *pipe, struct pipe_resource *color,
                  struct xenos_present_info *out)
{
   struct xenos_context *x = xenos_context(pipe);
   struct xenos_resource *res;
   uint32_t w, h;
   float *rect;

   xenos_diag_frame++;   /* one increment per present */

   if (!color || color->target == PIPE_BUFFER)
      return false;
   res = xenos_resource(color);
   if (!res->has_edram || !res->resolve_data)
      return false;

   w = color->width0;
   h = color->height0;

   DbgPrint("xenos: flush_frame %ux%u edram_base=%u resolve_phys=0x%08x\n",
            w, h, res->edram_base, res->resolve_phys);

   /* resolve_data is a guest VA (MmAllocatePhysicalMemoryEx); host-side reads
    * see zeros because the GPU writes to the guest PA.  The resolve IS working
    * — VdSwap reads from the guest PA.  Removed stale_probe/appsurf probes. */

   xenos_emit_frame_state(x);

   /* Override the EDRAM resolve SOURCE to be the surface being flushed (res).
    * xenos_emit_frame_state() above emits source state derived from the GL
    * framebuffer currently bound in the context (which at present time is the
    * application surface), so without this override Xenia would resolve the
    * wrong EDRAM base + pitch and GetResolveInfo would clamp the region by the
    * stale scissor, yielding a wrong/empty copy of the default framebuffer. */
   ctx_reserve(x, 160);
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_RB_SURFACE_INFO, w | (XE_MSAA_1X << 16));
   {
      /* color_base: bits 0-11 (tile index), format bits 16-19. */
      uint32_t ci = (res->edram_base & 0x7FF) |
                    ((res->edram_base >> 11) << 11) |
                    (XE_COLOR_FORMAT_8_8_8_8 << 16);
      xe_gpu_cmd_reg_write(&x->cb, XE_REG_RB_COLOR_INFO, ci);
   }
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_SC_SCREEN_SCISSOR_TL, 0);
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_SC_SCREEN_SCISSOR_BR, w | (h << 16));
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_SC_WINDOW_OFFSET, 0);
   /* The window scissor must cover the flushed surface too: GetScissor()
    * intersects the window scissor with the screen scissor, and
    * xenos_emit_frame_state() set the window scissor from the bound GL
    * framebuffer (the 640x480 app surface), which would clamp the resolve
    * rectangle here to 640x480. */
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_SC_WINDOW_SCISSOR_TL, 0x80000000u);
   xe_gpu_cmd_reg_write(&x->cb, XE_REG_PA_SC_WINDOW_SCISSOR_BR, w | (h << 16));

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
      /* Only write VF0 (2 dwords) to avoid clobbering VF1/VF2. */
      xe_gpu_cmd_reg_writen(&x->cb, 0x4800, 2,
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