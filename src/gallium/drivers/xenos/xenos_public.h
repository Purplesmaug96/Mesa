/*
 * xenos - hardware (PM4) Gallium driver for the Xbox 360 GPU.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef XENOS_PUBLIC_H
#define XENOS_PUBLIC_H

#include <stdint.h>

struct pipe_screen;
struct pipe_context;
struct pipe_resource;
struct xenos_winsys;

struct pipe_screen *xenos_screen_create(struct xenos_winsys *ws);

/** Result of a hardware frame flush: the tiled surface the GPU resolved
 * into, ready to be handed to VdSwap as the swap texture. */
struct xenos_present_info
{
   uint32_t phys;    /**< guest physical address of the tiled surface */
   uint32_t width;
   uint32_t height;
   uint32_t tiled;   /**< always 1 for now */
};

/** Resolve the bound colour target into its system-memory backing and
 * submit the command stream.  Returns false if \p color has no EDRAM
 * backing (e.g. a softpipe-style resource). */
bool xenos_flush_frame(struct pipe_context *pipe,
                       struct pipe_resource *color,
                       struct xenos_present_info *out);

#endif /* XENOS_PUBLIC_H */