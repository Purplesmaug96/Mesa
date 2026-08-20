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

/* Per-slot vfetch fixup: the stride/offset/format fields are patched at draw
 * time from the bound pipe_vertex_element + vertex buffer, so the compiler
 * only records which slot fetched which vertex attrib. */
struct xenos_vfetch_fixup
{
   uint32_t ucode_dword; /* dword index of the slot's first dword */
   uint32_t attrib;      /* VERT_ATTRIB_* = fetch-constant / vbuf index */
};

/* An opaque compiled Xenos shader: CF pair + vfetch/ALU slots + the metadata
 * the draw path needs (GPR counts, uniform/const count, interpolator
 * exports, vfetch fixups). */
struct xenos_shader
{
   mesa_shader_stage type;

   /* Microcode blob (CF pair = 3 dwords + 3*num_slots dwords), little-endian
    * dwords as stored in guest memory. */
   uint32_t *ucode;
   uint32_t ucode_dwords;
   uint32_t num_slots;

   /* Highest GPR in use + 1 (SQ_PROGRAM_CNTL vs/ps_num_reg = this - 1). */
   uint32_t num_gprs;
   /* Relocated uniform + immediate constants; helpers 0/1/-1/0.5 live at
    * 252..255.  The draw path uploads the shader's const buffer to
    * SHADER_CONSTANT_000_X. */
   uint32_t num_consts;

   unsigned num_inputs;
   unsigned num_outputs;

   /* Varying slot (0-15) the shader exports (VS) or reads (FS).  For the VS
    * these are the interpolator exports (SQ_INTERPOLATOR_CNTL param_shade
    * high nibbles, export register = slot); for the FS the PS input GPRs
    * (param_shade low nibbles, input GPR = slot). */
   uint32_t varying_mask;

   struct xenos_vfetch_fixup vfetch[16];
   uint32_t vfetch_count;
};

#endif /* XENOS_PRIVATE_H */