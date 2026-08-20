/*
 * xenos - hardware (PM4) Gallium driver for the Xbox 360 GPU.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef XENOS_PUBLIC_H
#define XENOS_PUBLIC_H

struct pipe_screen;
struct xenos_winsys;

struct pipe_screen *xenos_screen_create(struct xenos_winsys *ws);

#endif /* XENOS_PUBLIC_H */