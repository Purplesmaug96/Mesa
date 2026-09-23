/*
 * xenos - hardware (PM4) Gallium driver for the Xbox 360 GPU.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/macros.h"
#include "util/format/u_format.h"

#include "pipe/p_screen.h"

#include <xecore/xboxkrnl.h>

#include "xenos_resource.h"
#include "xenos_private.h"

void
xenos_resource_assign_edram(struct xenos_screen *xs, struct xenos_resource *res,
                            unsigned bind)
{
   unsigned bpp, pitch_px, pitch_samples;
   unsigned size;

   if (res->has_edram)
      return;
   if (res->base.target == PIPE_BUFFER)
      return;
   if (!(bind & (PIPE_BIND_RENDER_TARGET | PIPE_BIND_DEPTH_STENCIL)))
      return;

   bpp = util_format_get_blocksize(res->base.format) * 8;
   pitch_px = MAX2(1u, res->base.width0);
   size = (unsigned)MAX2(1u, res->base.width0) *
          (unsigned)MAX2(1u, res->base.height0) *
          (unsigned)MAX2(1u, res->base.depth0) *
          (unsigned)MAX2(1u, res->base.array_size) * bpp / 8u;

   /* The Xenos EDRAM is laid out as 2048 tiles of 80x16 32bpp samples
    * (5120 bytes each), addressed per RB_COLOR_INFO/DEPTH_INFO base in
    * xenia's numbering.  The tile span of a surface is computed as
    * (pitch rounded up to the 80-sample tile width) x (height rounded up
    * to 16).  64bpp surfaces take twice the tiles per row.  Surfaces must
    * be allocated contiguously in this numbering or their spans overlap,
    * which corrupts ownership tracking and makes resolves sample the wrong
    * render target for half the screen. */
   res->has_edram = 1;
   pitch_samples = bpp > 32 ? pitch_px * 2 : pitch_px;
   res->edram_pitch_tiles = (pitch_samples + 79u) / 80u;
   if (bpp > 32)
      res->edram_pitch_tiles <<= 1;
   res->edram_rows = (res->base.height0 + 15u) / 16u;
   res->edram_base = xs->next_edram_tile;
   {
      static uint32_t log_count = 0;
      if (log_count++ < 30)
         DbgPrint("xenos: assign_edram %ux%u fmt=%u bind=0x%x bpp=%u base=%u pitch=%u rows=%u\n",
                  res->base.width0, res->base.height0, res->base.format, bind,
                  bpp, res->edram_base, res->edram_pitch_tiles, res->edram_rows);
   }
   xs->next_edram_tile += res->edram_pitch_tiles * res->edram_rows;

   res->resolve_data = xs->ws->alloc(xs->ws, size, 12);
   if (res->resolve_data && xs->ws->get_physical)
      res->resolve_phys =
         (uint32_t)(xs->ws->get_physical(xs->ws, res->resolve_data));
}

static struct pipe_resource *
xenos_resource_create(struct pipe_screen *screen,
                      const struct pipe_resource *templ)
{
   struct xenos_screen *xs = xenos_screen(screen);
   struct xenos_resource *res;
   unsigned blocksize;
   unsigned size;

   res = CALLOC_STRUCT(xenos_resource);
   if (!res)
      return NULL;

   res->base = *templ;
   pipe_reference_init(&res->base.reference, 1);
   res->base.screen = screen;

   blocksize = util_format_get_blocksize(templ->format);
   size = (unsigned)MAX2(1u, templ->width0) *
          (unsigned)MAX2(1u, templ->height0) *
          (unsigned)MAX2(1u, templ->depth0) *
          (unsigned)MAX2(1u, templ->array_size) * blocksize;

   if (templ->target == PIPE_BUFFER) {
      size = MAX2(size, templ->width0);
      res->stride = templ->width0;
   } else {
      /* Level 0 only for now; the mip chain arrives with texture support. */
      res->stride = (unsigned)MAX2(1u, templ->width0) * blocksize;
   }

   res->size = size;
   res->data = xs->ws->alloc(xs->ws, size, 12);
   if (!res->data) {
      FREE(res);
      return NULL;
   }
   /* Diagnostic: fill with pattern to track if data survives to draw time. */
   if (size >= 1024 * 1024)
      memset(res->data, 0xDE, size);
   else
      memset(res->data, 0, size);

   if (xs->ws->get_physical)
      res->gpu_addr = (uint32_t)(xs->ws->get_physical(xs->ws, res->data) >> 2);

   /* Verify struct field offsets on first large resource. */
   {
      static int s_offset_verify = 0;
      if (s_offset_verify++ == 0) {
         DbgPrint("xenos: SZ sizeof(xenos_resource)=%u sizeof(pipe_resource)=%u "
                  "sizeof(pipe_reference)=%u "
                  "ref_off=%u w0_off=%u data_off=%u gpu_off=%u sz_off=%u str_off=%u "
                  "edram_off=%u rend_off=%u ebase_off=%u epitch_off=%u erows_off=%u "
                  "rdata_off=%u rphys_off=%u",
                  (unsigned)sizeof(struct xenos_resource),
                  (unsigned)sizeof(struct pipe_resource),
                  (unsigned)sizeof(struct pipe_reference),
                  (unsigned)((char *)&res->base.reference - (char *)res),
                  (unsigned)((char *)&res->base.width0 - (char *)res),
                  (unsigned)((char *)&res->data - (char *)res),
                  (unsigned)((char *)&res->gpu_addr - (char *)res),
                  (unsigned)((char *)&res->size - (char *)res),
                  (unsigned)((char *)&res->stride - (char *)res),
                  (unsigned)((char *)&res->has_edram - (char *)res),
                  (unsigned)((char *)&res->rendered - (char *)res),
                  (unsigned)((char *)&res->edram_base - (char *)res),
                  (unsigned)((char *)&res->edram_pitch_tiles - (char *)res),
                  (unsigned)((char *)&res->edram_rows - (char *)res),
                  (unsigned)((char *)&res->resolve_data - (char *)res),
                  (unsigned)((char *)&res->resolve_phys - (char *)res));
      }
   }

   {
      static int s_create_log = 0;
      if (s_create_log++ < 60 && size >= 4096) {
         DbgPrint("xenos: resource_create %ux%u fmt=%u bind=0x%x size=%u res=%08x data=%08x gpu=%08x",
                  templ->width0, templ->height0, templ->format,
                  templ->bind, size,
                  (uint32_t)(uintptr_t)res,
                  (uint32_t)(uintptr_t)res->data,
                  res->gpu_addr);
      }
   }

   /* Render targets draw into EDRAM tiles; the system memory above is only
    * used for CPU access.  Give each target a tile range plus the tiled
    * resolve backing VdSwap can sample after kCopy. */
   if (templ->target != PIPE_BUFFER)
      xenos_resource_assign_edram(xs, res, templ->bind);

   return &res->base;
}

static void
xenos_resource_destroy(struct pipe_screen *screen,
                       struct pipe_resource *pres)
{
   struct xenos_screen *xs = xenos_screen(screen);
   struct xenos_resource *res = xenos_resource(pres);

   if (res->data)
      xs->ws->free(xs->ws, res->data);
   if (res->resolve_data)
      xs->ws->free(xs->ws, res->resolve_data);
   FREE(res);
}

static bool
xenos_resource_get_handle(struct pipe_screen *screen,
                          struct pipe_context *ctx,
                          struct pipe_resource *pres,
                          struct winsys_handle *whandle,
                          unsigned usage)
{
   /* No external handles on the console; the guest maps resources directly
    * through transfer_map(). */
   return false;
}

static struct pipe_resource *
xenos_resource_from_handle(struct pipe_screen *screen,
                           const struct pipe_resource *templ,
                           struct winsys_handle *whandle,
                           unsigned usage)
{
   return NULL;
}

void
xenos_init_screen_resource_funcs(struct pipe_screen *screen)
{
   screen->resource_create = xenos_resource_create;
   screen->resource_create_front = NULL;
   screen->resource_destroy = xenos_resource_destroy;
   screen->resource_get_handle = xenos_resource_get_handle;
   screen->resource_from_handle = xenos_resource_from_handle;
}