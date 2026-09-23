/*
 * xbox360 target - hardware (xenos) screen creation.
 *
 * Wires the xenos Gallium driver to the console: GPU-visible memory comes
 * from the physical heap (MmAllocatePhysicalMemoryEx), and command buffers
 * go to the primary ring once the ring is attached (the samples initialise
 * it in screen.c; the GL driver's ring_submit hook is still a stub until the
 * draw path exists).
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include <xecore/xboxkrnl.h>

#include "pipe/p_screen.h"

#include "xenos_public.h"
#include "xenos_winsys.h"
#include "xenos_private.h"
#include "../xbox360_screen.h"

struct xenos_xecore_winsys
{
   struct xenos_winsys base;
};

static void *
xenos_xecore_alloc(void *ws, unsigned size, unsigned align_log2)
{
   return MmAllocatePhysicalMemoryEx(REGION_AUTO, size, 4, 0,
                                     0xFFFFFFFFu, 1u << align_log2);
}

static void
xenos_xecore_free(void *ws, void *ptr)
{
   if (ptr)
      MmFreePhysicalMemory(REGION_AUTO, ptr);
}

static uint64_t
xenos_xecore_get_physical(void *ws, void *guest_va)
{
   /* MmGetPhysicalAddress returns wrong values for Mesa's allocations.
    * Compute the physical address directly using the vE0000000 heap formula:
    * P = (VA - 0xE0000000) + 0x1000  (for VA in [0xE0000000, 0xFFD00000))
    * This is what PhysicalHeap::GetPhysicalAddress should return. */
   uint32_t va = (uint32_t)(uintptr_t)guest_va;
   if (va >= 0xE0000000u && va < 0xFFD00000u) {
      return (uint64_t)((va - 0xE0000000u) + 0x1000u);
   }
   return MmGetPhysicalAddress(guest_va);
}

static void
xenos_xecore_ring_submit(void *ws, const uint32_t *dwords_be, unsigned ndwords)
{
   /* The GL driver does not own the primary ring; screen.c (samples)
    * initialises it for VdSwap.  Once the hardware draw path lands, this
    * submits through the shared ring (see xe_gpu_ring_submit in
    * drivers/xenos/gpu/xenos_gpu.c) keeping the 64-dword block alignment. */
   fprintf(stderr, "xenos: ring_submit %u dwords (ring not attached)\n",
           ndwords);
}

struct pipe_screen *
xbox360_screen_create(void)
{
   struct xenos_xecore_winsys *winsys =
      (struct xenos_xecore_winsys *)calloc(1, sizeof(*winsys));

   if (!winsys)
      return NULL;

   winsys->base.ws = winsys;
   winsys->base.alloc = xenos_xecore_alloc;
   winsys->base.free = xenos_xecore_free;
   winsys->base.get_physical = xenos_xecore_get_physical;
   winsys->base.ring_submit = xenos_xecore_ring_submit;

   return xenos_screen_create(&winsys->base);
}

void
xbox360_xenos_attach_ring(struct pipe_screen *screen,
                          volatile uint32_t *ring_buffer,
                          unsigned ring_size_log2,
                          uint32_t *wptr_slot,
                          volatile uint32_t *rptr_page)
{
   struct xenos_screen *xs = xenos_screen(screen);

   if (!xs || xs->base.get_name(&xs->base) == NULL ||
       strcmp(xs->base.get_name(&xs->base), "xenos") != 0)
      return;

   xs->ws->ring_buffer = ring_buffer;
   xs->ws->ring_size_log2 = ring_size_log2;
   xs->ws->wptr_slot = wptr_slot;
   xs->ws->rptr_page = rptr_page;
}