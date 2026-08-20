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

#include "pipe/p_state.h"

struct xenos_shader;

struct xenos_shader *xenos_create_shader(struct pipe_screen *screen,
                                         const struct pipe_shader_state *state);
void xenos_delete_shader(struct xenos_shader *shader);

#endif /* XENOS_SHADER_H */