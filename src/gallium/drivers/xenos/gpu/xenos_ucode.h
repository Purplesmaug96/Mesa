#ifndef XENOS_UCODE_H
#define XENOS_UCODE_H

/*
 * Xenos GPR-ISA microcode builders.
 *
 * Bit layouts taken from xenia's ucode.h (authoritative):
 *   ControlFlowExecInstruction (CF exec, 2 dwords),
 *   VertexFetchInstruction (vfetch_full, 3 dwords),
 *   AluInstruction (ALU, 3 dwords).
 *
 * Notes (verified in docs/gpu.md "Microcode register encoding"):
 *   - GPR space is flat 0-63 in both vfetch dest and ALU src/dst fields.
 *   - ALU src field: {0x3F reg, 0x40 aL-relative, 0x80 abs-value}.
 *   - srcX_sel = 0 -> constant (8-bit index), = 1 -> temp register.
 *   - CF exec count_ = number of following ISA slots (pairs), 0-based for
 *     the last one: 1 slot -> count 1, 2 slots -> count 2.
 *   - sequence_: 2 bits per slot; bit0 = 0 for ALU, 1 for fetch.
 *
 * Shader-type values (xenos.h ShaderType): 0 = vertex, 1 = pixel.
 * Position export register = 62, PS color0 export register = 0.
 */

#include <stdbool.h>
#include <stdint.h>

#define XE_UCODE_CF_EXEC     1u /* ControlFlowOpcode::kExec */
#define XE_UCODE_CF_EXEC_END 2u /* ControlFlowOpcode::kExecEnd */

/* FetchOpcode::kVertexFetch (5-bit opcode field of the fetch slot). */
#define XE_UCODE_FETCH_VERTEX 0u

/* xenos VertexFormat for 4x float32 (vfetch_full format field). */
#define XE_UCODE_FORMAT_32_32_32_32_FLOAT 38u

/* Fetch destination swizzle: 3 bits per component (FetchDestinationSwizzle:
 * kX=0, kY=1, kZ=2, kW=3, k0=4, k1=5, kKeep=7).  Pass-through .xyzw: */
#define XE_UCODE_DST_SWIZ_XYZW 0x688u /* 3<<9 | 2<<6 | 1<<3 | 0 */

/* ALU source swizzle: 2 bits per component, component-relative.
 * Pass-through .xyzw: */
#define XE_UCODE_ALU_SWIZ_XYZW 0xE4u /* 3<<6 | 2<<4 | 1<<2 | 0 */

/* AluVectorOpcode (ucode.h): add=0, mul=1, max=2, mad=11, ... */
#define XE_UCODE_ALU_ADD 0u
#define XE_UCODE_ALU_MUL 1u
#define XE_UCODE_ALU_MAX 2u

/* AluScalarOpcode: adds=0 is the default "nop" scalar slot. */
#define XE_UCODE_SCALAR_NOP 0u

/* ExportRegister (ucode.h): kVSPosition, kPSColor0. */
#define XE_UCODE_EXP_VS_POSITION 62u
#define XE_UCODE_EXP_PS_COLOR0   0u

/* ALU operand: a temp GPR or a float constant (0-255). */
typedef struct xe_ucode_alu_src {
    bool is_temp;    /* srcX_sel; false = constant */
    uint32_t reg;    /* 6-bit GPR index or 8-bit constant index */
    uint32_t swiz;   /* 8-bit, 2 bits per component */
    bool negate;
} xe_ucode_alu_src;

typedef struct xe_ucode_alu {
    uint32_t opc;         /* AluVectorOpcode */
    uint32_t write_mask;  /* 4 bits, per component */
    uint32_t dst;         /* 6-bit GPR; ignored for exports */
    bool export_;         /* export_data */
    uint32_t export_dest; /* vector_dest when exporting (EXP register) */
    xe_ucode_alu_src src[3];
    uint32_t scalar_opc;  /* AluScalarOpcode */
    uint32_t scalar_mask; /* 4 bits */
    uint32_t scalar_dst;  /* 6-bit GPR (ignored for exports) */
    bool vector_clamp;
    bool scalar_clamp;
    bool abs_constants;
} xe_ucode_alu;

/* CF exec (kExec/kExecEnd): 2 dwords.  count = number of ISA slots. */
uint32_t xe_ucode_cf_exec(uint32_t out[2], uint32_t address, uint32_t count,
                          uint32_t sequence, uint32_t opcode);

/* vfetch_full: 3 dwords.
 *   fetch_const  : fetch constant index 0-95 (const_index*3 + sel).
 *   dst_reg/dst_swiz: destination GPR + 3-bits-per-component swizzle.
 *   src_reg/src_swiz: vertex-index GPR + 2-bit component select.
 *   format       : xenos VertexFormat.
 *   stride_dwords/offset_dwords: vertex stride / element offset in dwords.
 *   fomat_comp_all / num_format_all: signed / numeric-format override. */
uint32_t xe_ucode_vfetch(uint32_t out[3], uint32_t fetch_const,
                         uint32_t dst_reg, uint32_t dst_swiz,
                         uint32_t src_reg, uint32_t src_swiz,
                         uint32_t format, uint32_t stride_dwords,
                         int32_t offset_dwords, bool fomat_comp_all,
                         bool num_format_all);

/* ALU: 3 dwords. */
uint32_t xe_ucode_alu_build(uint32_t out[3], const xe_ucode_alu *a);

/* Minimal triangle VS (CF + vfetch_full + ALU export position), 8 dwords:
 *   vfetch const0 -> r0.xyzw (32_32_32_32_FLOAT, stride 4 dwords, vertex
 *   index from r0.x = the auto-injected current vertex index), then
 *   max r0.xyzw, r0, r0; export to kVSPosition (62).
 * Returns the number of dwords written (8). */
uint32_t xe_ucode_build_vs_minimal(uint32_t out[8]);

/* Minimal constant-color PS (CF + ALU export), 5 dwords:
 *   max c48.xyzw, c48, c48; export to kPSColor0 (0).
 * c48 must be written (SHADER_CONSTANT_000_X) before the draw.
 * Returns the number of dwords written (5). */
uint32_t xe_ucode_build_ps_minimal(uint32_t out[5]);

#endif /* XENOS_UCODE_H */
