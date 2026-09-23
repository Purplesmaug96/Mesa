/*
 * xbox360 api - bare-metal off-screen OpenGL for the Xbox 360.
 *
 * This is the "xbox360" Mesa backend.  It adapts Mesa's software
 * rasterizer (softpipe) into a headless display+context suitable for
 * console homebrew: there is no D3D9, X11, wayland or DRM on the 360, so
 * the context renders into a system-memory colour buffer that the caller
 * hands to the Xenos scanout path (raw GPU ring buffer) itself.
 *
 * The GL entry points come from Mesa's st/mesa frontend wired to glapi;
 * glClear, glBegin/glVertex/glEnd, glRotatef, ... all work as usual on a
 * compatibility-profile context.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stddef.h>
#include <string.h>

#include <xecore/xboxkrnl.h>

#include "util/box.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"

#include "frontend/api.h"
#include "state_tracker/st_context.h"

#include "xbox360_api.h"
#include "xbox360_screen.h"
#include "xenos_public.h"

void xbox360_xenos_attach_ring(struct pipe_screen *screen,
                               volatile uint32_t *ring_buffer,
                               unsigned ring_size_log2,
                               uint32_t *wptr_slot,
                               volatile uint32_t *rptr_page);

struct xbox360_display
{
   /* Public embeds the Gallium frontend screen/drawable interfaces. */
   struct pipe_frontend_screen fscreen;
   struct pipe_frontend_drawable drawable;
   struct st_visual visual;

   uint32_t width;
   uint32_t height;

   struct st_context *context;

   /* Attachments allocated during validation. */
   struct pipe_resource *color;   /* FRONT_LEFT */
   struct pipe_resource *depth;   /* DEPTH_STENCIL */
   uint32_t texture_width;
   uint32_t texture_height;

   /* Results of the last present(). */
   struct pipe_transfer *present;
   void *mapped;
   uint32_t present_counter;
};

static struct xbox360_display *
xbox360_display_from_drawable(struct pipe_frontend_drawable *drawable)
{
   return (struct xbox360_display *)((char *)drawable -
                                     offsetof(struct xbox360_display, drawable));
}

static bool
xbox360_validate_textures(struct xbox360_display *x, unsigned width,
                          unsigned height, unsigned mask)
{
   struct pipe_resource templ;
   unsigned i;

   if (x->texture_width != width || x->texture_height != height) {
      pipe_resource_reference(&x->color, NULL);
      pipe_resource_reference(&x->depth, NULL);
      x->texture_width = width;
      x->texture_height = height;
   }

   memset(&templ, 0, sizeof(templ));
   templ.target = PIPE_TEXTURE_2D;
   templ.width0 = width;
   templ.height0 = height;
   templ.depth0 = 1;
   templ.array_size = 1;
   templ.last_level = 0;
   templ.nr_samples = 0;
   templ.nr_storage_samples = 0;

   for (i = 0; i < ST_ATTACHMENT_COUNT; i++) {
      enum pipe_format format = PIPE_FORMAT_NONE;
      unsigned bind = 0;
      struct pipe_resource **slot;

      if (!(mask & (1u << i)))
         continue;

      switch (i) {
      case ST_ATTACHMENT_FRONT_LEFT:
         format = x->visual.color_format;
         bind = PIPE_BIND_RENDER_TARGET;
         slot = &x->color;
         break;
      case ST_ATTACHMENT_DEPTH_STENCIL:
         format = x->visual.depth_stencil_format;
         bind = PIPE_BIND_DEPTH_STENCIL;
         slot = &x->depth;
         break;
      default:
         continue;
      }

      if (*slot)
         continue;

      templ.format = format;
      templ.bind = bind;
      *slot = x->fscreen.screen->resource_create(x->fscreen.screen, &templ);
      if (!*slot)
         return false;
   }

   return true;
}

static bool
xbox360_validate(struct st_context *st,
                 struct pipe_frontend_drawable *drawable,
                 const enum st_attachment_type *statts,
                 unsigned count,
                 struct pipe_resource **out,
                 struct pipe_resource **resolve)
{
   struct xbox360_display *x =
      xbox360_display_from_drawable(drawable);
   unsigned i;
   unsigned mask = 0;

   for (i = 0; i < count; i++)
      mask |= 1u << statts[i];

   if (!xbox360_validate_textures(x, x->width, x->height, mask))
      return false;

   for (i = 0; i < count; i++) {
      struct pipe_resource *res = NULL;

      if (statts[i] == ST_ATTACHMENT_DEPTH_STENCIL)
         res = x->depth;
      else if (statts[i] == ST_ATTACHMENT_FRONT_LEFT)
         res = x->color;

      pipe_resource_reference(&out[i], res);
   }

   return true;
}

static bool
xbox360_flush_front(struct st_context *ctx,
                    struct pipe_frontend_drawable *drawable,
                    enum st_attachment_type statt)
{
   return true;
}

static bool
xbox360_flush_swapbuffers(struct st_context *ctx,
                          struct pipe_frontend_drawable *drawable)
{
   return true;
}

static int
xbox360_get_param(struct pipe_frontend_screen *fscreen,
                  enum st_manager_param param)
{
   return 0;
}

bool
xbox360_create(struct xbox360_display **out, uint32_t width, uint32_t height)
{
   struct xbox360_display *d;
   enum st_context_error sterr = ST_CONTEXT_ERROR_NO_MEMORY;
   struct st_context_attribs attribs;

   d = CALLOC_STRUCT(xbox360_display);
   if (!d)
      return false;

   d->fscreen.screen = xbox360_screen_create();
   if (!d->fscreen.screen) {
      free(d);
      return false;
   }
   DbgPrint("xbox360_create: screen ok (%s)",
            d->fscreen.screen->get_name(d->fscreen.screen));
   d->fscreen.get_param = xbox360_get_param;

   d->width = width;
   d->height = height;

   /* Drawable interface. */
   d->drawable.stamp = 0;
   d->drawable.fscreen = &d->fscreen;
   d->drawable.visual = &d->visual;
   d->drawable.validate = xbox360_validate;
   d->drawable.flush_front = xbox360_flush_front;
   d->drawable.flush_swapbuffers = xbox360_flush_swapbuffers;

   /* Visual: single-buffered colour + depth/stencil. */
   d->visual.color_format = PIPE_FORMAT_B8G8R8A8_UNORM;
   d->visual.depth_stencil_format = PIPE_FORMAT_Z24_UNORM_S8_UINT;
   d->visual.accum_format = PIPE_FORMAT_NONE;
   d->visual.samples = 0;
   d->visual.buffer_mask = ST_ATTACHMENT_FRONT_LEFT_MASK |
                           ST_ATTACHMENT_DEPTH_STENCIL_MASK;

   memset(&attribs, 0, sizeof(attribs));
   attribs.profile = API_OPENGL_COMPAT;
   /* GL 2.1 covers the classic fixed-function samples; the extension set
    * advertised by the xenos driver does not reach 3.3 yet. */
   attribs.major = 2;
   attribs.minor = 1;
   attribs.visual = d->visual;

   DbgPrint("xbox360_create: creating st context (GL %u.%u compat)...",
            attribs.major, attribs.minor);

   DbgPrint("xbox360_create: calling st_api_create_context");
   d->context = st_api_create_context(&d->fscreen, &attribs, &sterr, NULL);
   DbgPrint("xbox360_create: st_api_create_context returned %p", d->context);
   if (!d->context) {
      DbgPrint("xbox360_create: st_api_create_context failed (sterr=%d)",
               (int)sterr);
      d->fscreen.screen->destroy(d->fscreen.screen);
      free(d);
      return false;
   }
   DbgPrint("xbox360_create: st_context ok");

   *out = d;
   return true;
}

void
xbox360_make_current(struct xbox360_display *d)
{
   if (!d->context)
      return;

   /* st_api_make_current() runs _mesa_make_current(), which routes the
    * public GL entry points (dispatch table + current context) to this
    * context for the calling thread.
    */
   st_api_make_current(d->context, &d->drawable, &d->drawable);
}

void
xbox360_present(struct xbox360_display *d, struct xbox360_frame *frame)
{
   struct pipe_box box;
   struct pipe_context *pipe;

   memset(frame, 0, sizeof(*frame));
   frame->width = d->width;
   frame->height = d->height;

   if (!d->context)
      return;

   pipe = d->context->pipe;

   st_context_flush(d->context, ST_FLUSH_END_OF_FRAME | ST_FLUSH_WAIT,
                    NULL, NULL, NULL);

   /* Hardware path: resolve the colour target into its tiled backing and
    * hand the physical address to the caller for VdSwap's swap texture. */
   {
      struct xenos_present_info info;
      if (xenos_flush_frame(pipe, d->color, &info)) {
         frame->gpu_tiled = true;
         frame->gpu_phys = info.phys;
         DbgPrint("xbox360: present GPU-tiled phys=0x%08x w=%u h=%u tiled=%u",
                  info.phys, info.width, info.height, info.tiled);
         return;
      }
      DbgPrint("xbox360: xenos_flush_frame FAILED (falling back to CPU)\n");
   }

   /* Softpipe fallback: map and expose the CPU pixels. */
   if (d->present)
      pipe->texture_unmap(pipe, d->present);
   d->present = NULL;

   memset(&box, 0, sizeof(box));
   box.width = (int)d->width;
   box.height = (int)d->height;
   box.depth = 1;

   d->mapped = pipe->texture_map(pipe, d->color, 0, PIPE_MAP_READ,
                                 &box, &d->present);
   if (!d->mapped)
      return;

   /* Periodic sanity check of the CPU buffer: report mean and the number of
    * non-black pixels so we can tell whether the presented image has real
    * content or is just the black clear. */
   if ((d->present_counter++ % 60) == 0) {
      const uint32_t *p = (const uint32_t *)d->mapped;
      unsigned nonblack = 0;
      uint64_t sum = 0;
      unsigned stride_dw = d->present->stride / 4;
      for (unsigned y = 0; y < d->height; y += 4) {
         for (unsigned x = 0; x < d->width; x += 4) {
            uint32_t c = p[y * stride_dw + x];
            sum += (c & 0xFFu) + ((c >> 8) & 0xFFu) + ((c >> 16) & 0xFFu);
            if ((c & 0xFFFFFFu) != 0)
               nonblack++;
         }
      }
      DbgPrint("xbox360: CPU present %ux%u mean=%llu nonblack=%u first12=%08x %08x %08x %08x\n",
               d->width, d->height, (unsigned long long)(sum / (d->width * d->height)),
               nonblack, p[0], p[1], p[2], p[3]);
   }

   frame->ptr = d->mapped;
   frame->stride = d->present->stride;
}

void
xbox360_attach_ring(struct xbox360_display *d,
                    volatile uint32_t *ring_buffer,
                    unsigned ring_size_log2,
                    uint32_t *wptr_slot,
                    volatile uint32_t *rptr_page)
{
   xbox360_xenos_attach_ring(d->fscreen.screen, ring_buffer, ring_size_log2,
                             wptr_slot, rptr_page);
}

void
xbox360_destroy(struct xbox360_display *d)
{
   if (!d)
      return;

   if (d->context) {
      if (d->present)
         d->context->pipe->texture_unmap(d->context->pipe, d->present);
      st_api_make_current(d->context, NULL, NULL);
      st_destroy_context(d->context);
   }
   if (d->fscreen.screen)
      d->fscreen.screen->destroy(d->fscreen.screen);
   pipe_resource_reference(&d->color, NULL);
   pipe_resource_reference(&d->depth, NULL);
   free(d);
}

/* Dummy but referenced symbol so the whole target always links. */
int xbox360_api_present = 0;