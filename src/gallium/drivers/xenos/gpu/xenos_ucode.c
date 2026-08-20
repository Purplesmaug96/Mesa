#include "xenos_ucode.h"

/*
 * Xenos GPR-ISA microcode builders.
 * Layouts per xenia ucode.h (see xenos_ucode.h for the summary).
 */

uint32_t xe_ucode_cf_exec(uint32_t out[2], uint32_t address, uint32_t count,
                          uint32_t sequence, uint32_t opcode)
{
    /* word0: address_:12 | count_:3 | is_yield_:1 | sequence_:12 | vc_hi_:4 */
    out[0] = (address & 0xFFF) | ((count & 0x7) << 12) |
             ((sequence & 0xFFF) << 16);
    /* word1: vc_lo_:2 | pad | is_predicate_clean_:1 | pad | address_mode_:1 |
     * opcode_:4  ->  opcode << 12 */
    out[1] = (opcode & 0xF) << 12;
    return 2;
}

uint32_t xe_ucode_vfetch(uint32_t out[3], uint32_t fetch_const,
                         uint32_t dst_reg, uint32_t dst_swiz,
                         uint32_t src_reg, uint32_t src_swiz,
                         uint32_t format, uint32_t stride_dwords,
                         int32_t offset_dwords, bool fomat_comp_all,
                         bool num_format_all)
{
    uint32_t const_index = (fetch_const / 3) & 0x1F;
    uint32_t const_index_sel = fetch_const % 3;

    /* dword0: opcode_value:5 | src_reg:6 | src_reg_am:1 | dst_reg:6 |
     * dst_reg_am:1 | must_be_one:1 | const_index:5 | const_index_sel:2 |
     * prefetch_count:3 | src_swiz:2 */
    out[0] = (XE_UCODE_FETCH_VERTEX & 0x1F) |
             ((src_reg & 0x3F) << 5) |
             ((dst_reg & 0x3F) << 12) |
             (1u << 19) |                  /* must_be_one */
             (const_index << 20) |
             (const_index_sel << 25) |
             ((src_swiz & 0x3) << 30);

    /* dword1: dst_swiz:12 | fomat_comp_all:1 | num_format_all:1 |
     * signed_rf_mode_all:1 | is_index_rounded:1 | format:6 | reserved:2 |
     * exp_adjust:6 | is_mini_fetch:1 | is_predicated:1 */
    out[1] = (dst_swiz & 0xFFF) |
             ((fomat_comp_all ? 1u : 0u) << 12) |
             ((num_format_all ? 1u : 0u) << 13) |
             ((format & 0x3F) << 16);

    /* dword2: stride:8 | offset:23 | pred_condition:1 */
    out[2] = (stride_dwords & 0xFF) |
             ((uint32_t)offset_dwords & 0x7FFFFFu) << 8;

    return 3;
}

uint32_t xe_ucode_alu_build(uint32_t out[3], const xe_ucode_alu *a)
{
    /* dword0: vector_dest:6 | vector_dest_rel:1 | abs_constants:1 |
     * scalar_dest:6 | scalar_dest_rel:1 | export_data:1 | vector_write_mask:4
     * | scalar_write_mask:4 | vector_clamp:1 | scalar_clamp:1 |
     * scalar_opc:6 */
    out[0] = ((a->export_ ? a->export_dest : a->dst) & 0x3F) |
             ((a->abs_constants ? 1u : 0u) << 7) |
             ((a->scalar_dst & 0x3F) << 8) |
             ((a->export_ ? 1u : 0u) << 15) |
             ((a->write_mask & 0xF) << 16) |
             ((a->scalar_mask & 0xF) << 20) |
             ((a->vector_clamp ? 1u : 0u) << 24) |
             ((a->scalar_clamp ? 1u : 0u) << 25) |
             ((a->scalar_opc & 0x3F) << 26);

    /* dword1: src3_swiz:8 | src2_swiz:8 | src1_swiz:8 | src3_reg_negate:1 |
     * src2_reg_negate:1 | src1_reg_negate:1 | pred_condition:1 |
     * is_predicated:1 | const_address_register_relative:1 | const_1_rel_abs:1
     * | const_0_rel_abs:1 */
    out[1] = ((a->src[2].swiz & 0xFF)) |
             ((a->src[1].swiz & 0xFF) << 8) |
             ((a->src[0].swiz & 0xFF) << 16) |
             ((a->src[2].negate ? 1u : 0u) << 24) |
             ((a->src[1].negate ? 1u : 0u) << 25) |
             ((a->src[0].negate ? 1u : 0u) << 26);

    /* dword2: src3_reg:8 | src2_reg:8 | src1_reg:8 | vector_opc:5 |
     * src3_sel:1 | src2_sel:1 | src1_sel:1 */
    out[2] = ((a->src[2].reg & 0xFF)) |
             ((a->src[1].reg & 0xFF) << 8) |
             ((a->src[0].reg & 0xFF) << 16) |
             ((a->opc & 0x1F) << 24) |
             ((a->src[2].is_temp ? 1u : 0u) << 29) |
             ((a->src[1].is_temp ? 1u : 0u) << 30) |
             ((a->src[0].is_temp ? 1u : 0u) << 31);

    return 3;
}

uint32_t xe_ucode_build_vs_minimal(uint32_t out[8])
{
    uint32_t i = 0;
    xe_ucode_alu alu;

    /* CF exec: 2 slots (vfetch + alu); sequence: slot0 = fetch, slot1 = ALU. */
    i += xe_ucode_cf_exec(&out[i], 0, 2, 0b10, XE_UCODE_CF_EXEC);

    /* vfetch_full const0 -> r0.xyzw, vertex index from r0.x (auto-injected
     * by the sequencer at VS entry), 4x float32, stride 16 bytes. */
    i += xe_ucode_vfetch(&out[i], 0, 0, XE_UCODE_DST_SWIZ_XYZW,
                         0, 0 /* r0.x */,
                         XE_UCODE_FORMAT_32_32_32_32_FLOAT,
                         4 /* dword stride */, 0, true, true);

    /* max r0.xyzw, r0, r0  ->  position = fetched vertex; export to
     * kVSPosition (62).  Scalar slot is the default nop. */
    alu.opc = XE_UCODE_ALU_MAX;
    alu.write_mask = 0xF;
    alu.dst = 0;
    alu.export_ = true;
    alu.export_dest = XE_UCODE_EXP_VS_POSITION;
    alu.src[0].is_temp = true;
    alu.src[0].reg = 0;
    alu.src[0].swiz = XE_UCODE_ALU_SWIZ_XYZW;
    alu.src[0].negate = false;
    alu.src[1].is_temp = true;
    alu.src[1].reg = 0;
    alu.src[1].swiz = XE_UCODE_ALU_SWIZ_XYZW;
    alu.src[1].negate = false;
    alu.src[2].is_temp = false;
    alu.src[2].reg = 0;
    alu.src[2].swiz = 0;
    alu.src[2].negate = false;
    alu.scalar_opc = XE_UCODE_SCALAR_NOP;
    alu.scalar_mask = 0;
    alu.scalar_dst = 0;
    alu.vector_clamp = false;
    alu.scalar_clamp = false;
    alu.abs_constants = false;
    i += xe_ucode_alu_build(&out[i], &alu);

    return i;
}

uint32_t xe_ucode_build_ps_minimal(uint32_t out[5])
{
    uint32_t i = 0;
    xe_ucode_alu alu;

    /* CF exec: 1 ALU slot. */
    i += xe_ucode_cf_exec(&out[i], 0, 1, 0, XE_UCODE_CF_EXEC);

    /* max c48.xyzw, c48, c48  ->  oC0 = c48; export to kPSColor0 (0).
     * c48 = SHADER_CONSTANT_000_X index 48, written by the caller. */
    alu.opc = XE_UCODE_ALU_MAX;
    alu.write_mask = 0xF;
    alu.dst = 0;
    alu.export_ = true;
    alu.export_dest = XE_UCODE_EXP_PS_COLOR0;
    alu.src[0].is_temp = false;
    alu.src[0].reg = 48;
    alu.src[0].swiz = XE_UCODE_ALU_SWIZ_XYZW;
    alu.src[0].negate = false;
    alu.src[1].is_temp = false;
    alu.src[1].reg = 48;
    alu.src[1].swiz = XE_UCODE_ALU_SWIZ_XYZW;
    alu.src[1].negate = false;
    alu.src[2].is_temp = false;
    alu.src[2].reg = 0;
    alu.src[2].swiz = 0;
    alu.src[2].negate = false;
    alu.scalar_opc = XE_UCODE_SCALAR_NOP;
    alu.scalar_mask = 0;
    alu.scalar_dst = 0;
    alu.vector_clamp = false;
    alu.scalar_clamp = false;
    alu.abs_constants = false;
    i += xe_ucode_alu_build(&out[i], &alu);

    return i;
}
