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

#include "xenos_resource.h"
#include "xenos_private.h"

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
   memset(res->data, 0, size);

   if (xs->ws->get_physical)
      res->gpu_addr = (uint32_t)(xs->ws->get_physical(xs->ws, res->data) >> 2);

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