#ifndef XENOS_REGS_H
#define XENOS_REGS_H

/*
 * Xenos register map: dword indices into the xenia register table
 * (register_table.inc). Byte MMIO address = 0x7FC80000 + index * 4.
 * See /hdd/buildscript/docs/gpu.md for the full picture.
 */

#define XE_MMIO_BASE             0x7FC80000u
#define XE_MMIO_ADDR(reg)        (XE_MMIO_BASE + (uint32_t)(reg) * 4u)

#define XE_REG_CP_RB_WPTR        0x01C5u   /* write pointer, ring submission */
#define XE_REG_CP_RB_CNTL        0x0704u   /* ring size / enable */
#define XE_REG_CP_RB_RPTR_ADDR   0x070Cu   /* read-pointer writeback address */

#define XE_REG_PA_SC_SCREEN_SCISSOR_TL 0x200Eu
#define XE_REG_PA_SC_SCREEN_SCISSOR_BR 0x200Fu
#define XE_REG_PA_SC_WINDOW_OFFSET    0x2080u
#define XE_REG_PA_SC_WINDOW_SCISSOR_TL 0x2081u
#define XE_REG_PA_SC_WINDOW_SCISSOR_BR 0x2082u
#define XE_REG_PA_SC_CLIPRECT_RULE    0x2083u
#define XE_REG_PA_SC_CLIPRECT_0_TL    0x2084u
#define XE_REG_PA_SC_CLIPRECT_0_BR    0x2085u
#define XE_REG_PA_SC_CLIPRECT_1_TL    0x2086u
#define XE_REG_PA_SC_CLIPRECT_1_BR    0x2087u
#define XE_REG_PA_SC_CLIPRECT_2_TL    0x2088u
#define XE_REG_PA_SC_CLIPRECT_2_BR    0x2089u
#define XE_REG_PA_SC_CLIPRECT_3_TL    0x208Au
#define XE_REG_PA_SC_CLIPRECT_3_BR    0x208Bu

#define XE_REG_VGT_MAX_VTX_INDX  0x2100u
#define XE_REG_VGT_MIN_VTX_INDX  0x2101u
#define XE_REG_VGT_INDX_OFFSET   0x2102u
#define XE_REG_RB_COLOR_MASK     0x2104u
#define XE_REG_RB_BLEND_RED      0x2105u
#define XE_REG_RB_BLEND_GREEN    0x2106u
#define XE_REG_RB_BLEND_BLUE     0x2107u
#define XE_REG_RB_BLEND_ALPHA    0x2108u

#define XE_REG_SQ_PROGRAM_CNTL      0x2180u
#define XE_REG_SQ_INTERPOLATOR_CNTL 0x2182u
#define XE_REG_SQ_PS_PROGRAM        0x21F6u
#define XE_REG_SQ_VS_PROGRAM        0x21F7u
#define XE_REG_VGT_DRAW_INITIATOR   0x21FCu

/* Additional indices verified against modern xenia register_table.inc */
#define XE_REG_PA_CL_VPORT_XSCALE   0x210Fu
#define XE_REG_PA_CL_VPORT_XOFFSET  0x2110u
#define XE_REG_PA_CL_VPORT_YSCALE   0x2111u
#define XE_REG_PA_CL_VPORT_YOFFSET  0x2112u
#define XE_REG_PA_CL_VPORT_ZSCALE   0x2113u
#define XE_REG_PA_CL_VPORT_ZOFFSET  0x2114u
#define XE_REG_RB_BLENDCONTROL0     0x2201u
#define XE_REG_PA_CL_CLIP_CNTL      0x2204u
#define XE_REG_PA_SU_SC_MODE_CNTL   0x2205u
#define XE_REG_PA_CL_VTE_CNTL       0x2206u
#define XE_REG_RB_DEPTHCONTROL      0x2200u
#define XE_REG_RB_MODECONTROL       0x2208u

#define XE_REG_RB_SURFACE_INFO  0x2000u
#define XE_REG_RB_COLOR_INFO    0x2001u
#define XE_REG_RB_DEPTH_INFO    0x2002u

/* EDRAM copy/resolve (register_table.inc: 0x2318-0x231F) */
#define XE_REG_RB_COPY_CONTROL    0x2318u
#define XE_REG_RB_COPY_DEST_BASE  0x2319u
#define XE_REG_RB_COPY_DEST_PITCH 0x231Au
#define XE_REG_RB_COPY_DEST_INFO  0x231Bu
#define XE_REG_RB_DEPTH_CLEAR     0x231Du
#define XE_REG_RB_COLOR_CLEAR     0x231Eu
#define XE_REG_RB_COLOR_CLEAR_LO  0x231Fu

/* SHADER_CONSTANT_000_X: 4 dwords per float4 constant, index c -> 0x4000+c*4 */
#define XE_REG_SHADER_CONST(c)      (0x4000u + (uint32_t)(c) * 4u)
/* SHADER_CONSTANT_FETCH: 6 dwords per fetch const, index i -> 0x4800+i*6 */
#define XE_REG_SHADER_CONST_FETCH(i) (0x4800u + (uint32_t)(i) * 6u)

/* VGT_DRAW_INITIATOR (modern xenia registers.h) — value/bit layouts */
typedef union xenos_draw_initiator {
    struct {
        uint32_t prim_type    : 6;  /* xenos PrimitiveType */
        uint32_t source_select : 2; /* xenos SourceSelect */
        uint32_t major_mode   : 2;  /* xenos MajorMode */
        uint32_t _pad_10      : 1;
        uint32_t index_size   : 1;  /* xenos IndexFormat */
        uint32_t not_eop      : 1;
        uint32_t _pad_13      : 3;
        uint32_t num_indices  : 16;
    };
    uint32_t value;
} xenos_draw_initiator;

/* xenos PrimitiveType: kTriangleList = 0x04 */
#define XE_PRIM_TYPE_TRIANGLE_LIST 0x4u
/* xenos SourceSelect: kDMA=0 (indexed), kImmediate=1, kAutoIndex=2 */
#define XE_SOURCE_SEL_DMA          0u   /* indexed; packet carries base+size */
#define XE_SOURCE_SEL_IMMEDIATE    1u   /* vertex data follows in the packet */
#define XE_SOURCE_SEL_AUTO_INDEX   2u   /* non-indexed autodraw */
/* xenos MajorMode: kImplicit=0, kExplicit=1 */
#define XE_MAJOR_MODE_IMPLICIT     0u
#define XE_MAJOR_MODE_EXPLICIT     1u
/* xenos IndexFormat: kInt16=0, kInt32=1 */
#define XE_INDEX_SIZE_16BIT        0u
#define XE_INDEX_SIZE_32BIT        1u

/* RB_SURFACE_INFO (modern layout: pitch pixels, msaa, hiz pitch) */
typedef union xenos_rb_surface_info {
    struct {
        uint32_t surface_pitch : 14;  /* pixels */
        uint32_t _pad_14       : 2;
        uint32_t msaa_samples  : 2;   /* xenos MsaaSamples */
        uint32_t hiz_pitch     : 14;
    };
    uint32_t value;
} xenos_rb_surface_info;

/* RB_COLOR_INFO: color_base in EDRAM tiles (11 bits + bit 11), format bits */
typedef union xenos_rb_color_info {
    struct {
        uint32_t color_base    : 11; /* EDRAM tile index */
        uint32_t color_base_bit_11 : 1;
        uint32_t _pad_12       : 4;
        uint32_t color_format  : 4;  /* xenos ColorRenderTargetFormat */
        uint32_t color_exp_bias : 6;
        uint32_t _pad_26       : 6;
    };
    uint32_t value;
} xenos_rb_color_info;

/* RB_DEPTH_INFO: depth_base in EDRAM tiles, 1-bit format */
typedef union xenos_rb_depth_info {
    struct {
        uint32_t depth_base    : 11;
        uint32_t depth_base_bit_11 : 1;
        uint32_t _pad_12       : 4;
        uint32_t depth_format  : 1;  /* xenos DepthRenderTargetFormat */
        uint32_t _pad_17       : 15;
    };
    uint32_t value;
} xenos_rb_depth_info;

/* RB_MODECONTROL: edram_mode (xenos EdramMode). Values verified against
 * xenia's xenos.h (Triang3l's hardware research): kNoOperation = 0,
 * kColorDepth = 4, kDepthOnly = 5, kCopy = 6. Modes 1-3 are undefined. */
#define XE_EDRAM_MODE_NO_OPERATION 0u
#define XE_EDRAM_MODE_COLOR_DEPTH  4u
#define XE_EDRAM_MODE_DEPTH_ONLY   5u
#define XE_EDRAM_MODE_COPY         6u

/* xenos ColorRenderTargetFormat: k_8_8_8_8 = 0 */
#define XE_COLOR_FORMAT_8_8_8_8      0u
#define XE_COLOR_FORMAT_16_16_16_16_FLOAT 7u

/* xenos MsaaSamples: k1X = 0 */
#define XE_MSAA_1X                 0u

/* PA_CL_VTE_CNTL: vport transform enables (bits 0-5) */
#define XE_VTE_VPORT_X_SCALE_ENA   (1u << 0)
#define XE_VTE_VPORT_X_OFFSET_ENA  (1u << 1)
#define XE_VTE_VPORT_Y_SCALE_ENA   (1u << 2)
#define XE_VTE_VPORT_Y_OFFSET_ENA  (1u << 3)
#define XE_VTE_VPORT_Z_SCALE_ENA   (1u << 4)
#define XE_VTE_VPORT_Z_OFFSET_ENA  (1u << 5)

/* PA_CL_VTE_CNTL vertex format bits.  The guest xenos VS exports GCN-style
 * clip-space XYZ and the true W0 (not pre-divided by W, not 1/W).  Tell the
 * host that: vtx_w0_fmt=1 means W0 is real W (host reciprocates it), and
 * vtx_xy_fmt/vtx_z_fmt=0 mean XYZ are raw clip coords (host divides by W). */
#define XE_VTE_VTX_XY_FMT          (1u << 8)
#define XE_VTE_VTX_Z_FMT           (1u << 9)
#define XE_VTE_VTX_W0_FMT          (1u << 10)

/* PA_CL_CLIP_CNTL: clip_disable (bit 16) */
#define XE_CLIP_DISABLE            (1u << 16)

/* SQ_PROGRAM_CNTL bits (modern layout): v:0, vs_num_gprs:5@1,
 * ps_num_gprs:5@7, vtx_export@13, ps_export@16 */
#define XE_SQ_PROGRAM_CNTL_V       (1u << 0)
#define XE_SQ_PROGRAM_CNTL_VTX_EXPORT (1u << 13)
#define XE_SQ_PROGRAM_CNTL_PS_EXPORT  (1u << 16)

#endif /* XENOS_REGS_H */