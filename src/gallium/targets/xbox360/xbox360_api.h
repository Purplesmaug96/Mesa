/*
 * xbox360 api - bare-metal off-screen OpenGL for the Xbox 360 (Xenos).
 *
 * OpenXeChain has no D3D9/GDI/X11, so this target adapts Mesa's software
 * (softpipe) backend into a headless GL context.  It renders with the CPU
 * into a system-memory colour buffer; the caller is then responsible for
 * handing that buffer to the Xenos scanout path (the raw GPU primary ring
 * buffer, see samples/common/screen.c in OpenXeChain).
 *
 * The GL context is a normal Mesa compatibility-profile context, so every
 * classic entry point (glClear, glBegin/glVertex/glEnd, glRotatef, ...)
 * works as usual.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef XBOX360_API_H
#define XBOX360_API_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct xbox360_display;

/** A rendered frame.  With the hardware (xenos) backend the GPU resolves
 * into a tiled surface and it is presented through the swap texture; with
 * softpipe \p ptr is a CPU-mapped linear image to blit manually. */
struct xbox360_frame
{
   void *ptr;      /**< BGRA8 pixels (softpipe), NULL for the GPU path */
   uint32_t width;  /**< surface width in pixels */
   uint32_t height; /**< surface height in pixels */
   uint32_t stride; /**< byte stride between rows (softpipe) */

   /* GPU path: tiled resolve destination for VdSwap's swap texture. */
   bool gpu_tiled;
   uint32_t gpu_phys;
};

/**
 * Create an off-screen GL display and context for software (softpipe)
 * rendering at the given resolution.
 *
 * The returned handle owns a persistent render target.  After calling
 * xbox360_present() you usually immediately blit \p frame into the Xenos
 * front buffer (see samples/common/screen.c) and present that.
 *
 * Returns false on failure.
 */
bool xbox360_create(struct xbox360_display **out,
                    uint32_t width, uint32_t height);

/**
 * Make this display's context the current GL context for the calling thread.
 *
 * Must be called once before doing GL work.  After this, the stock GL
 * entry points (glClear, glBegin, ...) operate on the display.
 */
void xbox360_make_current(struct xbox360_display *d);

/**
 * Flush pending GL rendering and expose the resulting color buffer.
 *
 * The previous frame, if still mapped, is unmapped and the freshly rendered
 * pixels are returned in \p frame.
 */
void xbox360_present(struct xbox360_display *d, struct xbox360_frame *frame);

/**
 * Release all resources of a display.
 */
void xbox360_destroy(struct xbox360_display *d);

/**
 * Attach the shared primary GPU ring (screen.c's VD ring) to the hardware
 * driver.  Must be called after screen_init() and before the first GL draw
 * when running against the xenos backend.  \p wptr_slot is the shared
 * free-running write pointer; \p rptr_page the CP read-pointer writeback.
 */
void xbox360_attach_ring(struct xbox360_display *d,
                         volatile uint32_t *ring_buffer,
                         unsigned ring_size_log2,
                         uint32_t *wptr_slot,
                         volatile uint32_t *rptr_page);

#ifdef __cplusplus
}
#endif

#endif /* XBOX360_API_H */