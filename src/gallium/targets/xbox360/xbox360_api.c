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
#include "frontend/sw_winsys.h"
#include "softpipe/sp_public.h"
#include "sw/null/null_sw_winsys.h"

#include "state_tracker/st_context.h"

#include "xbox360_api.h"

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
   struct sw_winsys *ws;
   enum st_context_error sterr = ST_CONTEXT_ERROR_NO_MEMORY;
   struct st_context_attribs attribs;

   d = CALLOC_STRUCT(xbox360_display);
   if (!d)
      return false;

   ws = null_sw_create();
   if (!ws) {
      free(d);
      return false;
   }

   DbgPrint("xbox360_create: null_sw ok");

   d->fscreen.screen = softpipe_create_screen(ws);
   if (!d->fscreen.screen) {
      free(d);
      return false;
   }
   DbgPrint("xbox360_create: softpipe screen ok");
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
   attribs.major = 3;
   attribs.minor = 3;
   attribs.visual = d->visual;

   DbgPrint("xbox360_create: creating st context (GL %u.%u compat)...",
            attribs.major, attribs.minor);

   d->context = st_api_create_context(&d->fscreen, &attribs, &sterr, NULL);
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

   frame->ptr = NULL;
   frame->width = d->width;
   frame->height = d->height;
   frame->stride = 0;

   if (!d->context)
      return;

   pipe = d->context->pipe;

   st_context_flush(d->context, ST_FLUSH_END_OF_FRAME | ST_FLUSH_WAIT,
                    NULL, NULL, NULL);

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

   frame->ptr = d->mapped;
   frame->stride = d->present->stride;
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