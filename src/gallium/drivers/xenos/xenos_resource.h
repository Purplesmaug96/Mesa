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
   uint32_t size;       /* bytes allocated */
   uint32_t stride;     /* bytes per row of level 0 */

   /* Render targets additionally own an EDRAM tile range (the actual GPU
    * drawing surface) and a tiled system-memory resolve backing. */
   uint32_t has_edram;        /* render target with EDRAM backing */
   uint32_t rendered;         /* drawn/cleared as a target since last upload */
   uint32_t edram_base;       /* first tile index */
   uint32_t edram_pitch_tiles;
   uint32_t edram_rows;
   void *resolve_data;        /* tiled system memory the GPU copies into */
   uint32_t resolve_phys;     /* ... its physical address */
};

static inline struct xenos_resource *
xenos_resource(struct pipe_resource *res)
{
   return (struct xenos_resource *)res;
}

struct xenos_screen;

/* Lazily give a texture EDRAM tiles + tiled resolve backing when it is bound
 * as a render target.  Harmless no-op if the resource already has EDRAM (or
 * is a buffer). */
void xenos_resource_assign_edram(struct xenos_screen *xs,
                                 struct xenos_resource *res,
                                 unsigned bind);

void xenos_init_screen_resource_funcs(struct pipe_screen *screen);

#endif /* XENOS_RESOURCE_H */