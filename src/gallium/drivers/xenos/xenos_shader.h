/*
 * xenos - hardware (PM4) Gallium driver for the Xbox 360 GPU.
 *
 * NIR->Xenos microcode compilation.  The screen always hands this driver NIR
 * (PIPE_SHADER_IR_NIR); there is no TGSI path from the state tracker in this
 * Mesa.  The real compiler (CF + vfetch + ALU translation) is built on top
 * of the encoders in gpu/xenos_ucode.h; for now the shader object only
 * carries metadata so a valid handle can be bound.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef XENOS_SHADER_H
#define XENOS_SHADER_H

#include <stdint.h>

#include "compiler/shader_enums.h"
#include "pipe/p_state.h"

struct nir_shader;

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

   /* Microcode blob (CF pair = 3 dwords + 3*num_slots dwords), stored in the
    * compiler's native byte order (guest-native on the 360). */
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

struct xenos_shader *xenos_create_shader(struct pipe_screen *screen,
                                         const struct pipe_shader_state *state);

/* Direct NIR->microcode entry point for the dev/sample path (no pipe_screen).
 * Returns a handle owned by the caller, or NULL on failure. */
struct xenos_shader *xenos_compile_nir(struct nir_shader *nir);

void xenos_delete_shader(struct xenos_shader *shader);

#endif /* XENOS_SHADER_H */