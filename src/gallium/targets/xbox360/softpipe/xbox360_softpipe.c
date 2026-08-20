/*
 * xbox360 target - software (softpipe) screen creation.
 *
 * The original software path: render with the CPU through softpipe into a
 * system-memory colour buffer and hand the pixels to the scanout path
 * ourselves.  Kept as a fallback while the xenos hardware driver matures;
 * delete this directory once the PM4 path renders.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pipe/p_screen.h"

#include "frontend/sw_winsys.h"
#include "softpipe/sp_public.h"
#include "sw/null/null_sw_winsys.h"

#include "util/u_memory.h"

#include "../xbox360_screen.h"

struct pipe_screen *
xbox360_screen_create(void)
{
   struct sw_winsys *ws = null_sw_create();

   if (!ws)
      return NULL;
   return softpipe_create_screen(ws);
}