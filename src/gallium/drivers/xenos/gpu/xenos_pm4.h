#ifndef XENOS_PM4_H
#define XENOS_PM4_H

/*
 * Xenos PM4 packet builders.
 * Ring/command-buffer words are BIG-ENDIAN in guest memory; every writer here
 * emits big-endian dwords. Packet formats per packet_disassembler.cc /
 * pm4_command_processor_implement.h (see docs/gpu.md).
 */

#include <stdint.h>

#define XE_PM4_OP_NOP                0x10u
#define XE_PM4_OP_INDIRECT_BUFFER    0x3Fu
#define XE_PM4_OP_INDIRECT_BUFFER_PFD 0x37u
#define XE_PM4_OP_WAIT_FOR_IDLE      0x26u
#define XE_PM4_OP_WAIT_REG_MEM       0x3Cu
#define XE_PM4_OP_WAIT_REG_EQ        0x52u
#define XE_PM4_OP_WAIT_REG_GTE       0x53u
#define XE_PM4_OP_WAIT_UNTIL_READ    0x5Cu
#define XE_PM4_OP_WAIT_IB_PFD_COMPLETE 0x5Du
#define XE_PM4_OP_REG_RMW            0x21u
#define XE_PM4_OP_REG_TO_MEM         0x3Eu
#define XE_PM4_OP_MEM_WRITE          0x3Du
#define XE_PM4_OP_MEM_WRITE_CNTR     0x4Fu
#define XE_PM4_OP_COND_EXEC          0x44u
#define XE_PM4_OP_COND_WRITE         0x45u
#define XE_PM4_OP_EVENT_WRITE        0x46u
#define XE_PM4_OP_EVENT_WRITE_SHD    0x58u
#define XE_PM4_OP_EVENT_WRITE_CFL    0x59u
#define XE_PM4_OP_EVENT_WRITE_EXT    0x5Au
#define XE_PM4_OP_EVENT_WRITE_ZPD    0x5Bu
#define XE_PM4_OP_DRAW_INDX          0x22u
#define XE_PM4_OP_DRAW_INDX_2        0x36u /* modern xenia; older trees used 0x23 */
#define XE_PM4_OP_DRAW_INDX_2_BIN    0x35u
#define XE_PM4_OP_IM_LOAD            0x27u /* load sequencer instruction memory (pointer-based) */
#define XE_PM4_OP_IM_LOAD_IMMEDIATE  0x2Bu /* load sequencer instruction memory (code embedded in packet) */
#define XE_PM4_OP_LOAD_ALU_CONSTANT  0x2Fu
#define XE_PM4_OP_ME_INIT            0x48u
#define XE_PM4_OP_XE_SWAP            0x64u

/* big-endian store/load for command words */
static inline uint32_t xe_pm4_bswap(uint32_t v) {
    return __builtin_bswap32(v);
}

/* dword index of the next free payload slot after a header at `i` */
static inline uint32_t xe_pm4_type0_emit(uint32_t *dst, uint32_t i,
                                         uint32_t base_reg, uint32_t ndwords,
                                         int write_one)
{
    /* type0 count field = registers written - 1 (ndwords >= 1) */
    uint32_t hdr = ((ndwords - 1) & 0x3FFF) << 16 | (base_reg & 0x7FFF);
    if (write_one)
        hdr |= 1u << 15;
    dst[i] = xe_pm4_bswap(hdr);
    return i + 1;
}

static inline uint32_t xe_pm4_type3_emit(uint32_t *dst, uint32_t i,
                                         uint32_t opcode, uint32_t ndwords)
{
    /* type3 count field = payload dwords - 1; every type3 packet carries at
     * least one payload dword (xenia: count = field+1, AdvanceRead(count*4)).
     * NOTE: packet TYPE bits [31:30] = 0b11 are mandatory — without them the
     * header decodes as a Type0 register write! */
    uint32_t hdr = 0xC0000000u | (opcode & 0x7F) << 8 |
                   ((ndwords - 1) & 0x3FFF) << 16;
    dst[i] = xe_pm4_bswap(hdr);
    return i + 1;
}

static inline uint32_t xe_pm4_type1_emit(uint32_t *dst, uint32_t i,
                                         uint32_t reg1, uint32_t reg2)
{
    uint32_t hdr = 0x40000000u | (reg1 & 0x7FF) | (reg2 & 0x7FF) << 11;
    dst[i] = xe_pm4_bswap(hdr);
    return i + 1;
}

/* event write with an optional data dword (count 0 or 1) */
static inline uint32_t xe_pm4_event_emit(uint32_t *dst, uint32_t i,
                                         uint32_t opcode, uint32_t event_type,
                                         int with_data)
{
    i = xe_pm4_type3_emit(dst, i, opcode, with_data ? 1u : 0u);
    if (with_data)
        dst[i++] = xe_pm4_bswap(event_type);
    return i;
}

/* PM4_MEM_WRITE: address (guest phys, dword aligned) + data words */
static inline uint32_t xe_pm4_mem_write_emit(uint32_t *dst, uint32_t i,
                                             uint32_t addr, const uint32_t *data,
                                             uint32_t ndwords)
{
    uint32_t n = 1 + ndwords;
    i = xe_pm4_type3_emit(dst, i, XE_PM4_OP_MEM_WRITE, n);
    dst[i++] = xe_pm4_bswap(addr);
    for (uint32_t k = 0; k < ndwords; k++)
        dst[i++] = xe_pm4_bswap(data[k]);
    return i;
}

/* PM4_DRAW_INDX_2 (no viz query): initiator + optional kDMA base/size */
static inline uint32_t xe_pm4_draw_emit(uint32_t *dst, uint32_t i,
                                        uint32_t initiator, int indexed,
                                        uint32_t dma_base, uint32_t dma_size)
{
    uint32_t n = indexed ? 3u : 1u;
    i = xe_pm4_type3_emit(dst, i, XE_PM4_OP_DRAW_INDX_2, n);
    dst[i++] = xe_pm4_bswap(initiator);
    if (indexed) {
        dst[i++] = xe_pm4_bswap(dma_base);
        dst[i++] = xe_pm4_bswap(dma_size);
    }
    return i;
}

#endif /* XENOS_PM4_H */