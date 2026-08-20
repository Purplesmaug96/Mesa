/*
 * xenos - hardware (PM4) Gallium driver for the Xbox 360 GPU.
 *
 * Winsys interface: the only platform-specific services the driver needs are
 * CPU-visible memory that the GPU can also see (guest physical) and a way to
 * push command buffers to the primary ring.  Everything else (packet
 * building, ring buffering, microcode encoding) lives in gpu/ and is
 * console-agnostic.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef XENOS_WINSYS_H
#define XENOS_WINSYS_H

#include <stdint.h>

struct xenos_winsys
{
   void *ws;

   /** Allocate memory visible to the GPU.  Returns a guest virtual address
    * (or NULL).  \p align_log2 is the minimum alignment, in bits. */
   void *(*alloc)(void *ws, unsigned size, unsigned align_log2);

   /** Release memory returned by alloc(). */
   void (*free)(void *ws, void *ptr);

   /** Translate a guest virtual address into a guest physical address. */
   uint64_t (*get_physical)(void *ws, void *guest_va);

   /** Submit \p ndwords big-endian dwords to the primary GPU ring.  The
    * implementation is responsible for copying the dwords into the ring
    * (keeping 64-dword block alignment) and bumping CP_RB_WPTR. */
   void (*ring_submit)(void *ws, const uint32_t *dwords_be, unsigned ndwords);
};

#endif /* XENOS_WINSYS_H */