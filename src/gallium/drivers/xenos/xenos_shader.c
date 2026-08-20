/*
 * xenos - hardware (PM4) Gallium driver for the Xbox 360 GPU.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include "compiler/nir/nir.h"

#include "util/u_math.h"
#include "util/u_memory.h"

#include "xenos_shader.h"
#include "xenos_private.h"

struct xenos_shader *
xenos_create_shader(struct pipe_screen *screen,
                    const struct pipe_shader_state *state)
{
   if (state->type != PIPE_SHADER_IR_NIR)
      return NULL;

   struct nir_shader *nir = state->ir.nir;
   struct xenos_shader *shader = CALLOC_STRUCT(xenos_shader);

   if (!shader)
      return NULL;

   switch (nir->info.stage) {
   case MESA_SHADER_VERTEX:
      shader->type = MESA_SHADER_VERTEX;
      break;
   case MESA_SHADER_FRAGMENT:
      shader->type = MESA_SHADER_FRAGMENT;
      break;
   default:
      FREE(shader);
      return NULL;
   }

   shader->num_inputs = util_bitcount(nir->info.inputs_read);
   shader->num_outputs = util_bitcount(nir->info.outputs_written);

   nir_foreach_function_impl (impl, nir) {
      nir_foreach_block (block, impl) {
         nir_foreach_instr (instr, block) {
            shader->nir_instruction_count++;
         }
      }
   }

   fprintf(stderr, "xenos: compiled %s shader (%u inputs, %u outputs, "
           "%u nir instructions)\n",
           shader->type == MESA_SHADER_VERTEX ? "VS" : "FS",
           shader->num_inputs, shader->num_outputs,
           shader->nir_instruction_count);

   return shader;
}

void
xenos_delete_shader(struct xenos_shader *shader)
{
   FREE(shader);
}