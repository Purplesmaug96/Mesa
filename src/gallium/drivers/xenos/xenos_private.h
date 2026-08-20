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

struct xenos_screen
{
   struct pipe_screen base;
   struct xenos_winsys *ws;
};

static inline struct xenos_screen *
xenos_screen(struct pipe_screen *screen)
{
   return (struct xenos_screen *)screen;
}

/* An opaque compiled Xenos shader (CF + vfetch + ALU microcode).  The real
 * NIR->Xenos compiler lands here; for now the handle only carries metadata
 * so the state tracker sees a valid shader object. */
struct xenos_shader
{
   mesa_shader_stage type;
   unsigned nir_instruction_count;
   unsigned num_inputs;
   unsigned num_outputs;
};

#endif /* XENOS_PRIVATE_H */