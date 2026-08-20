/*
 * xbox360 target - screen creation hook.
 *
 * The GL frontend glue (xbox360_api.c) is renderer-agnostic; this is the
 * seam where the software (softpipe) and hardware (xenos) backends plug in.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef XBOX360_SCREEN_H
#define XBOX360_SCREEN_H

struct pipe_screen;

/**
 * Create the pipe_screen for the GL display.
 *
 * softpipe/ provides the software implementation (null_sw + softpipe);
 * xenos/ provides the hardware implementation (xenos_screen_create with the
 * xecore winsys).  MESA_OP_XBOX360_XENOS selects which one this returns.
 */
struct pipe_screen *xbox360_screen_create(void);

#endif /* XBOX360_SCREEN_H */