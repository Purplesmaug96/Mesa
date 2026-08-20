/*
 * xenos - hardware (PM4) Gallium driver for the Xbox 360 GPU.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef XENOS_SCREEN_H
#define XENOS_SCREEN_H

#include "pipe/p_screen.h"

struct xenos_winsys;

struct pipe_screen *xenos_screen_create(struct xenos_winsys *ws);

#endif /* XENOS_SCREEN_H */