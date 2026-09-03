/*
 * xenos - hardware (PM4) Gallium driver for the Xbox 360 GPU.
 *
 * Private driver structs shared between the screen, context and resources.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef XENOS_PRIVATE_H
#define XENOS_PRIVATE_H

#include "compiler/shader_enums.h"
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"

#include "xenos_winsys.h"

#include "xenos_shader.h"

struct xenos_screen
{
   struct pipe_screen base;
   struct xenos_winsys *ws;

   /* EDRAM tile bump allocator: render targets claim ranges of the 10 MB
    * embedded DRAM (each tile is 2 KB; pitch/rows per surface). */
   unsigned next_edram_tile;
};

static inline struct xenos_screen *
xenos_screen(struct pipe_screen *screen)
{
   return (struct xenos_screen *)screen;
}

#endif /* XENOS_PRIVATE_H */