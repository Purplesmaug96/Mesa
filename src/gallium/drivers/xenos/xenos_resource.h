/*
 * xenos - hardware (PM4) Gallium driver for the Xbox 360 GPU.
 *
 * Resource management: buffers and textures live in GPU-visible (guest
 * physical) memory allocated through the winsys.  Linear layout for now;
 * the Xenos tiled textures (32x32x4 macro tiles) and the EDRAM-backed
 * colour/depth buffers are added with the real state conversion.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef XENOS_RESOURCE_H
#define XENOS_RESOURCE_H

#include "pipe/p_context.h"
#include "pipe/p_state.h"

/* System-memory (guest physical) resource. */
struct xenos_resource
{
   struct pipe_resource base;

   void *data;          /* CPU (guest) virtual address */
   uint32_t gpu_addr;   /* guest physical address >> 2, as used in PM4
                           fetch constants and packets */
   unsigned size;       /* bytes allocated */
   unsigned stride;     /* bytes per row of level 0 */
};

static inline struct xenos_resource *
xenos_resource(struct pipe_resource *res)
{
   return (struct xenos_resource *)res;
}

void xenos_init_screen_resource_funcs(struct pipe_screen *screen);

#endif /* XENOS_RESOURCE_H */