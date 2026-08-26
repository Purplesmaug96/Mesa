#ifndef XENOS_GPU_H
#define XENOS_GPU_H

/*
 * Raw Xenos (GPU "PM4") backend API for the xbox360 target.
 * Public, dependency-free C: register constants, PM4 packet builders and a
 * ring-buffer submission path identical to the proven present path in
 * samples/common/screen.c (VdInitializeRingBuffer + CP_RB_WPTR write).
 * See /hdd/buildscript/docs/gpu.md for the full architecture.
 */

#include <stdint.h>
#include "xenos_regs.h"
#include "xenos_pm4.h"

typedef struct xenos_ring {
    volatile uint32_t *buffer;   /* guest ring VA (physical-memory backed) */
    uint32_t size_dwords;        /* 1 << (init size_log2 + 3) */
    uint32_t write_ptr;          /* dword offset, == contents of CP_RB_WPTR */
} xenos_ring;

/* Attach the ring to an existing VD-ring (screen.c pattern). */
void xe_gpu_ring_init(xenos_ring *ring, volatile uint32_t *buffer,
                      uint32_t size_log2);
void xe_gpu_ring_init_at(xenos_ring *ring, volatile uint32_t *buffer,
                         uint32_t size_log2, uint32_t start_wptr);

/* Copy `ndwords` big-endian command words into the ring, then update
 * CP_RB_WPTR so the GPU picks them up.  NEVER call with dwords > free space. */
void xe_gpu_ring_submit(xenos_ring *ring, const uint32_t *cmds,
                        uint32_t ndwords);

/* Scratch command buffer (guest-visible memory) + submission helper. */
#define XE_GPU_CMDBUF_DWORDS 2048u
typedef struct xenos_cmdbuf {
    xenos_ring *ring;
    uint32_t *dwords;            /* little-endian scratch, BE on submit */
    uint32_t used;
} xenos_cmdbuf;

void xe_gpu_cmd_init(xenos_cmdbuf *cb, xenos_ring *ring, uint32_t *dwords);
void xe_gpu_cmd_reset(xenos_cmdbuf *cb);
void xe_gpu_cmd_submit(xenos_cmdbuf *cb);

/* Command-building API (little-endian scratch; BE-encoded on submit). */
void xe_gpu_cmd_reg_write(xenos_cmdbuf *cb, uint32_t reg, uint32_t value);
void xe_gpu_cmd_reg_writen(xenos_cmdbuf *cb, uint32_t reg, uint32_t n,
                           const uint32_t *values);
void xe_gpu_cmd_mem_write(xenos_cmdbuf *cb, uint32_t addr,
                          const uint32_t *data, uint32_t ndwords);
void xe_gpu_cmd_wait_for_idle(xenos_cmdbuf *cb);
void xe_gpu_cmd_event_write(xenos_cmdbuf *cb, uint32_t event_type);
void xe_gpu_cmd_draw(xenos_cmdbuf *cb, uint32_t initiator, int indexed,
                     uint32_t dma_base, uint32_t dma_size);

/* Baseline block: NOPs + WAIT_FOR_IDLE so the CP has clean traffic before
 * any real work.  Returns 0 on success. */
int xe_gpu_init_baseline(xenos_ring *ring, uint32_t *scratch);
int xe_gpu_draw_begin(xenos_cmdbuf *cb);
int xe_gpu_draw_end(xenos_cmdbuf *cb);

/* Host-side smoke test: submits baseline + PM4_MEM_WRITE of a magic dword
 * and waits for the GPU writeback.  Returns 0 on success.  Used by samples
 * to prove the ring/packet path works before any draw code exists. */
int xe_gpu_dev_verify(uint32_t ring_va, uint32_t size_log2,
                      uint32_t start_wptr);

/* First real draw (roadmap step 4): programs a minimal VS/PS pair via
 * PM4_IM_LOAD_IMMEDIATE, the full RB/PA/SC/SQ state, one vertex fetch
 * constant (physical triangle buffer) and a non-indexed
 * PM4_DRAW_INDX_2 (kAutoIndex) into the current ring.  Validation:
 * xenia pipeline-cache logs (shader analyse/translate, pipeline build,
 * draw).  Returns 0 on success. */
int xe_gpu_dev_triangle(uint32_t ring_va, uint32_t size_log2,
                        uint32_t start_wptr);

/* Dev triangle through the NIR->microcode compiler (xenos_compile_nir):
 * builds a VS (fetch POS, export position) and FS (constant colour from
 * uniform const 0) with nir_builder, uploads the compiled ucode via
 * PM4_IM_LOAD_IMMEDIATE, draws into EDRAM tile 0, then resolves the render
 * target (RB_MODECONTROL kCopy + RB_COPY_*) into a tiled system-memory
 * buffer and unswizzles it into the linear front buffer at (0,0).  The
 * caller presents afterwards (VdSwap).  front_va must be the front buffer's
 * guest virtual address (screen.xenia_fb_address).  On success *wptr_out is
 * set to the free-running ring write pointer to continue the present loop
 * from (so the CP never re-executes stale command data).  Returns 0. */
int xe_gpu_dev_triangle_nir(uint32_t ring_va, uint32_t size_log2,
                            uint32_t start_wptr, uint32_t front_va,
                            uint32_t *wptr_out, int skip_resolve);

#endif /* XENOS_GPU_H */