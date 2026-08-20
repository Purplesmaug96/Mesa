/*
 * xenos - hardware (PM4) Gallium driver for the Xbox 360 GPU.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef XENOS_CONTEXT_H
#define XENOS_CONTEXT_H

#include "pipe/p_context.h"

struct pipe_screen;

struct pipe_context *xenos_create_context(struct pipe_screen *screen,
                                          void *priv, unsigned flags);

#endif /* XENOS_CONTEXT_H */