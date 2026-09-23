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

/* ALU source swizzle: 2 bits per component, COMPONENT-RELATIVE (per xenia
 * ucode.h: absolute component i = (rel_i + i) & 3).
 * Pass-through .xyzw is therefore all-zero: */
#define XE_UCODE_ALU_SWIZ_XYZW 0x00u
/* Splat of component x to all lanes (relative encoding). */
#define XE_UCODE_ALU_SWIZ_SPLAT_X \
    (0u | (3u << 2) | (2u << 4) | (1u << 6))

/* AluVectorOpcode (ucode.h): add=0, mul=1, max=2, min=3, frc=8, floor=10,
 * mad=11, dp4=15, dp3=16, dp2add=17, cube=18, max4=19. */
#define XE_UCODE_ALU_ADD 0u
#define XE_UCODE_ALU_MUL 1u
#define XE_UCODE_ALU_MAX 2u
#define XE_UCODE_ALU_MIN 3u
#define XE_UCODE_ALU_FRC 8u
#define XE_UCODE_ALU_FLOOR 10u
#define XE_UCODE_ALU_MAD 11u
#define XE_UCODE_ALU_DP4 15u
#define XE_UCODE_ALU_DP3 16u
#define XE_UCODE_ALU_DP2ADD 17u

/* AluScalarOpcode (ucode.h): adds=0 is the default "nop" scalar slot. */
#define XE_UCODE_SCALAR_NOP 0u
#define XE_UCODE_SCALAR_ADDS 0u
#define XE_UCODE_SCALAR_MULS 2u
#define XE_UCODE_SCALAR_MAXS 5u
#define XE_UCODE_SCALAR_MINS 6u
#define XE_UCODE_SCALAR_RCP 19u
#define XE_UCODE_SCALAR_RSQ 22u
#define XE_UCODE_SCALAR_SQRT 40u

/* ExportRegister (ucode.h): kVSPosition=62, kVSPointSize=63, kPSDepth=61,
 * kVSInterpolator0..15 = 0..15, kPSColor0..3 = 0..3. */
#define XE_UCODE_EXP_VS_POSITION 62u
#define XE_UCODE_EXP_VS_POINT_SIZE 63u
#define XE_UCODE_EXP_PS_DEPTH 61u
#define XE_UCODE_EXP_VS_INTERP(i) ((uint32_t)(i) & 0x3F)
#define XE_UCODE_EXP_PS_COLOR(i) ((uint32_t)(i) & 0x3F)
#define XE_UCODE_EXP_PS_COLOR0 0u

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

/* CF exec (kExec/kExecEnd): 2 dwords.  count = number of ISA slots.
 * SLOT LAYOUT (verified against shader_interpreter.cc): the whole shader is a
 * stream of 3-dword words: dwords 0-2 = one control-flow pair holding TWO
 * CF instructions (0: NOP, 1: the real exec; pair[k] packing below), then
 * 3*N dwords of ISA slots.  The exec's address counts SLOTS from dword 0, so
 * the first slot is at address 1; count = number of slots executed. */
uint32_t xe_ucode_cf_exec(uint32_t out[2], uint32_t address, uint32_t count,
                          uint32_t sequence, uint32_t opcode);

/* Pack CF0 (NOP) + CF1 (w0/w1 from xe_ucode_cf_exec) into a 3-dword pair:
 *   pair[0] = CF0.w0
 *   pair[1] = (CF0.w1 & 0xFFFF) | (CF1.w0 << 16)
 *   pair[2] = (CF1.w0 >> 16) | (CF1.w1 << 16)
 * (inverse of the interpreter's UnpackControlFlowInstructions). */
void xe_ucode_cf_emit_pair(uint32_t out[3], uint32_t cf0_w0, uint32_t cf0_w1,
                           uint32_t cf1_w0, uint32_t cf1_w1);

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

/* tex (TextureFetchInstruction: opcode = FetchOpcode::kTextureFetch = 1),
 * 3 dwords.
 *   fetch_const  : texture fetch constant index 0-31 (the 6-dword block at
 *                  SHADER_CONSTANT_FETCH(fetch_const) written for the bound
 *                  sampler, e.g. from texture unit + XE_TEX_FETCH_INDEX_BASE).
 *   dst_reg/dst_swiz: result GPR + 12-bit destination swizzle (XYZW = 0x688).
 *   src_reg/src_swiz: coordinate GPR + 6-bit source swizzle (XY = 8).
 *   use_register_lod: take LOD from the W component instead of implicit.
 * Filters default to kUseFetchConst so the fetch constant (sampler) decides;
 * use_computed_lod (implicit LOD) is on. */
#define XE_UCODE_FETCH_TEXTURE 1u /* FetchOpcode::kTextureFetch */
#define XE_UCODE_TEX_SWIZ_XY   0x04u /* X=0, Y=1 (6-bit source swizzle) */
#define XE_UCODE_TEX_DIM_2D    1u   /* FetchOpDimension::k2D */
#define XE_UCODE_TEX_DIM_CUBE  3u   /* FetchOpDimension::kCube */

uint32_t xe_ucode_tex(uint32_t out[3], uint32_t dst_reg,
                      uint32_t src_reg, uint32_t src_swiz,
                      uint32_t fetch_const, uint32_t dst_swiz,
                      uint32_t dimension, bool use_register_lod);

/* Minimal triangle VS (CF pair + vfetch_full + ALU export position), 9 dwords:
 *   CF pair = {NOP, exec_end} (3 dwords); vfetch const0 -> r0.xyzw
 *   (32_32_32_32_FLOAT, stride 4 dwords, vertex index from r0.x = the
 *   auto-injected current vertex index), then max r0.xyzw, r0, r0;
 *   export to kVSPosition (62).
 * Returns the number of dwords written (9). */
uint32_t xe_ucode_build_vs_minimal(uint32_t out[9]);

/* Minimal constant-color PS (CF pair + ALU export), 6 dwords:
 *   max c48.xyzw, c48, c48; export to kPSColor0 (0).
 * c48 must be written (SHADER_CONSTANT_000_X) before the draw.
 * Returns the number of dwords written (6). */
uint32_t xe_ucode_build_ps_minimal(uint32_t out[6]);

/* Texture-sampling blit VS, 18 dwords:
 *   slot0: mov r62.x, r0.x               (save auto-injected vertex index)
 *   slot1: vfetch const0 -> r0.xyzw      (position, 4x float32)
 *   slot2: vfetch const1 -> r1.xyzw      (uv, 4x float32)
 *   slot3: max r0.xyzw,r0,r0; export kVSPosition (62)
 *   slot4: max r1.xyzw,r1,r1; export kVSInterpolator0 (0)
 * Vertex stride is 8 dwords (32 bytes): {x,y,z,w, u,v,0,1}.  Both fetches read
 * from the same vertex buffer; const0 = offset 0, const1 = offset 4 dwords.
 * Returns the number of dwords written (18). */
uint32_t xe_ucode_build_vs_blit(uint32_t out[18]);

/* Texture-sampling blit PS, 9 dwords:
 *   slot0: tex fetch_const (unit 0 -> XE_TEX_FETCH_INDEX_BASE), coords from
 *          interpolated uv in GPR0 (XY), result -> r1.xyzw
 *   slot1: max r1.xyzw,r1,r1; export kPSColor0 (0).
 * Returns the number of dwords written (9). */
uint32_t xe_ucode_build_ps_blit(uint32_t out[9]);

/* Texture fetch-constant block index the blit PS samples (30 is past the
 * game's texture units at 4.. and the vertex fetch const blocks 0..3, so the
 * blit's tfetch can never collide).  Must match the register block the blit
 * writes with xe_gpu_tfetch_build. */
#define XE_BLIT_TEX_FETCH_INDEX   30u

#endif /* XENOS_UCODE_H */
