#ifndef XENOS_FETCH_H
#define XENOS_FETCH_H

/*
 * SHADER_CONSTANT_FETCH_* builders (xenos.h: xe_gpu_vertex_fetch_t /
 * xe_gpu_texture_fetch_t). Stored at reg XE_REG_SHADER_CONST_FETCH(i) + k.
 */

#include <stdint.h>

/* FetchConstantType (modern xenos.h): kInvalidTexture=0, kInvalidVertex=1,
 * kTexture=2, kVertex=3 */
#define XE_FETCH_TYPE_GENERIC  0u
#define XE_FETCH_TYPE_TEXTURE  2u   /* xe_gpu_texture_fetch_t */
#define XE_FETCH_TYPE_VERTEX   3u   /* xe_gpu_vertex_fetch_t */

/* Endian (xenos.h Endian) */
#define XE_ENDIAN_NONE    0u
#define XE_ENDIAN_8IN16   1u
#define XE_ENDIAN_8IN32   2u
#define XE_ENDIAN_16IN32  3u

/* TextureFormat values from xenos.h used in fetch constants. */
#define XE_TFETCH_FORMAT_8_8_8_8       6u
#define XE_TFETCH_FORMAT_8_8_8_8_A     14u
#define XE_TFETCH_FORMAT_4_4_4_4       15u
#define XE_TFETCH_FORMAT_10_11_11      16u
#define XE_TFETCH_FORMAT_11_11_10      17u
#define XE_TFETCH_FORMAT_DXT1          18u
#define XE_TFETCH_FORMAT_DXT2_3        19u
#define XE_TFETCH_FORMAT_DXT4_5        20u
#define XE_TFETCH_FORMAT_16_16         25u
#define XE_TFETCH_FORMAT_16_16_16_16   26u
#define XE_TFETCH_FORMAT_16_FLOAT      30u
#define XE_TFETCH_FORMAT_16_16_FLOAT   31u
#define XE_TFETCH_FORMAT_16_16_16_16_FLOAT 32u
#define XE_TFETCH_FORMAT_32_FLOAT      36u
#define XE_TFETCH_FORMAT_32_32_FLOAT   37u
#define XE_TFETCH_FORMAT_32_32_32_32_FLOAT 38u
#define XE_TFETCH_FORMAT_DXN           49u
#define XE_TFETCH_FORMAT_32_32_32_FLOAT 57u
#define XE_TFETCH_FORMAT_DXT3A         58u
#define XE_TFETCH_FORMAT_DXT5A         59u
/* DataDimension::k2DOrStacked stored in the fetch constant. */
#define XE_TFETCH_DIMENSION_2D   1u
/* TextureFilter: kPoint=0, kLinear=1, kUseFetchConst=3. */
#define XE_TFETCH_FILTER_POINT   0u
#define XE_TFETCH_FILTER_LINEAR  1u
/* ClampMode (xenos.h): kRepeat=0, kClampToEdge=2. */
#define XE_TFETCH_CLAMP_REPEAT   0u
#define XE_TFETCH_CLAMP_TO_EDGE  2u

typedef union xenos_vertex_fetch {
    struct {
        uint32_t type    : 2;
        uint32_t address : 30;   /* guest phys >> 2 */
    };
    struct {
        uint32_t endian  : 2;
        uint32_t size    : 24;   /* in words (bytes >> 2) */
        uint32_t pad     : 6;
    };
    uint32_t dw[2];
} xenos_vertex_fetch;

static inline void xe_gpu_vfetch_build(xenos_vertex_fetch *v,
                                       uint32_t guest_phys,
                                       uint32_t size_bytes,
                                       uint32_t endian)
{
    /* Explicit LE shifts: the union's bitfields are host-endian dependent,
     * but the guest is BE and xenia expects LE register values (it does
     * ReadAndSwap).  Build the dwords with LE layout directly. */
    v->dw[0] = (XE_FETCH_TYPE_VERTEX & 0x3) | ((guest_phys >> 2) << 2);
    v->dw[1] = (endian & 0x3) | (((size_bytes >> 2) & 0xFFFFFF) << 2);
}

#define XE_GPU_TFETCH_SUBRESOURCE_ALIGN_LOG2 12u

/* Guest linear-texture row pitch is required to be a multiple of 256 bytes
 * (kTextureLinearRowAlignmentBytes), so pitch_px of a 32bpp texture must be a
 * multiple of 64.  The fetch-constant pitch field stores pixels >> 5. */
static inline uint32_t xe_gpu_tfetch_pitch_word(uint32_t pitch_px)
{
    return (pitch_px + 63u) & ~63u;
}

/* Build the 6-dword texture fetch constant (xe_gpu_texture_fetch_t) xenia's
 * texture cache parses to source the sampler: a LINEAR k_8_8_8_8 texture at
 * guest_phys (bytes, 4K aligned).  gallium filter/wrap values equal the xenos
 * ones for the subset we use (point=0/linear=1, repeat=0/clamp-to-edge=2). */
static inline void
xe_gpu_tfetch_build(uint32_t dw[6], uint32_t guest_phys,
                    uint32_t width, uint32_t height,
                    uint32_t mag_filter, uint32_t min_filter,
                    uint32_t mip_filter, uint32_t clamp,
                    uint32_t swizzle, uint32_t tiled)
{
    uint32_t pitch_px = xe_gpu_tfetch_pitch_word(width);
    uint32_t wx = width ? (width - 1u) : 0u;
    uint32_t hx = height ? (height - 1u) : 0u;

    /* dword0: type:2 | sign:8 | clamp_xyz:9 | pad:3 | pitch:9 | tiled:1 */
    dw[0] = (XE_FETCH_TYPE_TEXTURE & 0x3u) |
            ((clamp & 0x7u) << 10) |
            ((clamp & 0x7u) << 13) |
            ((clamp & 0x7u) << 16) |
            (((pitch_px >> 5) & 0x1FFu) << 22) |
            ((tiled & 0x1u) << 31);
    /* dword1: format:6 | endian:2 | request_size:2 | stacked:1 |
     * nearest_clamp_policy:1 | base_address:20 (guest_phys >> 12) */
    dw[1] = (XE_TFETCH_FORMAT_8_8_8_8 & 0x3Fu) |
            ((((guest_phys >> 12) & 0xFFFFFu) << 12));
    /* dword2: size_2d width:13 | height:13 | stack_depth:6 */
    dw[2] = (wx & 0x1FFFu) | ((hx & 0x1FFFu) << 13);
    /* dword3: num_format:1 | swizzle:12 | exp_adjust:6 | mag:2 | min:2 |
     * mip:2 | aniso:3 | arbitrary:3 | border_size:1 */
    dw[3] = ((swizzle & 0xFFFu) << 1) |
            ((mag_filter & 0x3u) << 19) |
            ((min_filter & 0x3u) << 21) |
            ((mip_filter & 0x3u) << 23);
    /* dword4: no mips, no bias. */
    dw[4] = 0;
    /* dword5: border_color:2 | force_bc_w_to_max:1 | tri_clamp:2 |
     * aniso_bias:4 | dimension:2 | packed_mips:1 | mip_address:20 */
    dw[5] = (XE_TFETCH_DIMENSION_2D & 0x3u) << 9;
}

#endif /* XENOS_FETCH_H */