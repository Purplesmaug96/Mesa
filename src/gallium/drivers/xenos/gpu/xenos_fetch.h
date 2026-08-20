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
#define XE_FETCH_TYPE_VERTEX   3u   /* xe_gpu_vertex_fetch_t */

/* Endian (xenos.h Endian) */
#define XE_ENDIAN_NONE    0u
#define XE_ENDIAN_8IN16   1u
#define XE_ENDIAN_8IN32   2u
#define XE_ENDIAN_16IN32  3u

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
    v->type = XE_FETCH_TYPE_VERTEX;
    v->address = guest_phys >> 2;
    v->endian = endian & 0x3;
    v->size = (size_bytes >> 2) & 0xFFFFFF;
}

#define XE_GPU_TFETCH_SUBRESOURCE_ALIGN_LOG2 12u

#endif /* XENOS_FETCH_H */