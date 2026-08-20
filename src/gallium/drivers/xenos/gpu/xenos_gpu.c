#include "xenos_gpu.h"
#include "xenos_fetch.h"
#include "xenos_ucode.h"
#include <string.h>
#include <xecore/xboxkrnl.h>

/*
 * The CPU writes BE dwords into the ring before bumping the pointer; the GPU
 * consumes only up to what the pointer has reached, so a multi-chunk copy
 * followed by a single pointer bump is race-free.
 */

void xe_gpu_ring_init(xenos_ring *ring, volatile uint32_t *buffer,
                      uint32_t size_log2)
{
    xe_gpu_ring_init_at(ring, buffer, size_log2, 0);
}

void xe_gpu_ring_init_at(xenos_ring *ring, volatile uint32_t *buffer,
                         uint32_t size_log2, uint32_t start_wptr)
{
    ring->buffer = buffer;
    ring->size_dwords = 1u << (size_log2 + 3);
    ring->write_ptr = start_wptr % (1u << (size_log2 + 3));
}

void xe_gpu_ring_submit(xenos_ring *ring, const uint32_t *cmds,
                        uint32_t ndwords)
{
    /* Xenia's CP breaks on non-64-dword-aligned submissions: always pad to
     * 64-dword blocks and fill with Type2 fillers (VdSwap pattern). */
    uint32_t block = (ndwords + 63) & ~63u;
    if (block > ring->size_dwords)
        block = ring->size_dwords;
    uint32_t first = ring->write_ptr % ring->size_dwords;
    uint32_t wrap = ring->size_dwords - first;
    uint32_t n1 = block < wrap ? block : wrap;
    uint32_t i = 0;
    for (; i < n1; i++)
        ring->buffer[first + i] = i < ndwords ? cmds[i] : 0x80000000u;
    if (block > n1)
        for (; i < block; i++)
            ring->buffer[i - n1] = i < ndwords ? cmds[i] : 0x80000000u;
    ring->write_ptr = (ring->write_ptr + block) % ring->size_dwords;
    *((volatile uint32_t *)XE_MMIO_ADDR(XE_REG_CP_RB_WPTR)) = ring->write_ptr;
}

void xe_gpu_cmd_init(xenos_cmdbuf *cb, xenos_ring *ring, uint32_t *dwords)
{
    cb->ring = ring;
    cb->dwords = dwords;
    cb->used = 0;
}

void xe_gpu_cmd_reset(xenos_cmdbuf *cb)
{
    cb->used = 0;
}

void xe_gpu_cmd_submit(xenos_cmdbuf *cb)
{
    if (cb->used == 0)
        return;
    for (uint32_t i = 0; i < cb->used; i++)
        cb->dwords[i] = xe_pm4_bswap(cb->dwords[i]);
    xe_gpu_ring_submit(cb->ring, cb->dwords, cb->used);
    cb->used = 0;
}

static void xe_gpu_cmd_push32(xenos_cmdbuf *cb, uint32_t v)
{
    cb->dwords[cb->used++] = v;
}

static void xe_gpu_cmd_type0(xenos_cmdbuf *cb, uint32_t reg, uint32_t n,
                             int write_one)
{
    xe_gpu_cmd_push32(cb,
        ((n - 1) & 0x3FFF) << 16 | (reg & 0x7FFF) |
        (write_one ? (1u << 15) : 0u));
}

static void xe_gpu_cmd_type3(xenos_cmdbuf *cb, uint32_t opcode, uint32_t n)
{
    /* type3 count field = payload dwords - 1 (n >= 1); TYPE bits [31:30] =
     * 0b11 are mandatory (without them the header decodes as Type0) */
    xe_gpu_cmd_push32(cb, 0xC0000000u | (opcode & 0x7F) << 8 |
                          ((n - 1) & 0x3FFF) << 16);
}

void xe_gpu_cmd_reg_write(xenos_cmdbuf *cb, uint32_t reg, uint32_t value)
{
    xe_gpu_cmd_type0(cb, reg, 1, 0);
    xe_gpu_cmd_push32(cb, value);
}

void xe_gpu_cmd_reg_writen(xenos_cmdbuf *cb, uint32_t reg, uint32_t n,
                           const uint32_t *values)
{
    xe_gpu_cmd_type0(cb, reg, n, 0);
    for (uint32_t k = 0; k < n; k++)
        xe_gpu_cmd_push32(cb, values[k]);
}

void xe_gpu_cmd_mem_write(xenos_cmdbuf *cb, uint32_t addr,
                          const uint32_t *data, uint32_t ndwords)
{
    xe_gpu_cmd_type3(cb, XE_PM4_OP_MEM_WRITE, 1 + ndwords);
    xe_gpu_cmd_push32(cb, addr);
    for (uint32_t k = 0; k < ndwords; k++)
        xe_gpu_cmd_push32(cb, data[k]);
}

void xe_gpu_cmd_wait_for_idle(xenos_cmdbuf *cb)
{
    /* NOTE: modern xenia (xc-src) has NO PM4_WAIT_FOR_IDLE handler and
     * asserts on it; emit a NOP pad instead. */
    xe_gpu_cmd_type3(cb, XE_PM4_OP_NOP, 1);
    xe_gpu_cmd_push32(cb, 0);
}

void xe_gpu_cmd_event_write(xenos_cmdbuf *cb, uint32_t event_type)
{
    xe_gpu_cmd_type3(cb, XE_PM4_OP_EVENT_WRITE, 1);
    xe_gpu_cmd_push32(cb, event_type);
}

void xe_gpu_cmd_draw(xenos_cmdbuf *cb, uint32_t initiator, int indexed,
                     uint32_t dma_base, uint32_t dma_size)
{
    xe_gpu_cmd_type3(cb, XE_PM4_OP_DRAW_INDX_2, indexed ? 3u : 1u);
    xe_gpu_cmd_push32(cb, initiator);
    if (indexed) {
        xe_gpu_cmd_push32(cb, dma_base);
        xe_gpu_cmd_push32(cb, dma_size);
    }
}

int xe_gpu_init_baseline(xenos_ring *ring, uint32_t *scratch)
{
    xenos_cmdbuf cb;
    xe_gpu_cmd_init(&cb, ring, scratch);
    xe_gpu_cmd_wait_for_idle(&cb); /* NOP pad */
    xe_gpu_cmd_event_write(&cb, 0x7A); /* CACHE_FLUSH_TS */
    while (cb.used < 64)
        xe_gpu_cmd_push32(&cb, 0x80000000u); /* Type2 filler (VdSwap style) */
    xe_gpu_cmd_submit(&cb);
    return 0;
}

/*
 * Host-side smoke test for the PM4 path: run once from a sample after the
 * screen ring exists.  Submits two 64-dword blocks (baseline + NOP pad) so
 * the CP parses real packets end to end.  Returns 0 on success.
 */
int xe_gpu_dev_verify(uint32_t ring_va, uint32_t size_log2,
                      uint32_t start_wptr)
{
    static uint32_t s_cmd[XE_GPU_CMDBUF_DWORDS];
    xenos_ring ring;
    xenos_cmdbuf cb;

    xe_gpu_ring_init_at(&ring, (volatile uint32_t *)(uintptr_t)ring_va,
                        size_log2, start_wptr);
    xe_gpu_init_baseline(&ring, s_cmd);

    xe_gpu_cmd_init(&cb, &ring, s_cmd);
    xe_gpu_cmd_wait_for_idle(&cb);
    xe_gpu_cmd_submit(&cb);
    return 0;
}

/* ---------------------------------------------------------------------- */
/* First real draw (roadmap step 4).                                       */

#define XE_TRIANGLE_WIDTH  640u
#define XE_TRIANGLE_HEIGHT 480u
#define XE_TRIANGLE_PS_CONST 48u

/* Guest-allocated triangle vertex buffer: 3 x (x,y,z,w) float32 in big-endian
 * (guest-native) byte order; the vertex fetch constant requests k8in32
 * endianness so xenia's host-side vfetch sees little-endian floats. */
static void *s_triangle_vb;
static uint32_t s_triangle_vb_phys;

static int xe_triangle_alloc_vb(void)
{
    static const float verts[3][4] = {
        { -0.6f, -0.6f, 0.0f, 1.0f },
        {  0.6f, -0.6f, 0.0f, 1.0f },
        {  0.0f,  0.7f, 0.0f, 1.0f },
    };

    if (s_triangle_vb)
        return 0;
    s_triangle_vb = MmAllocatePhysicalMemoryEx(REGION_AUTO, 64u,
                                               0x00000004u /* X_PAGE_READWRITE */,
                                               0u, 0xFFFFFFFFu, 0x1000);
    if (!s_triangle_vb)
        return -1;
    memcpy(s_triangle_vb, verts, sizeof(verts));
    s_triangle_vb_phys = MmGetPhysicalAddress(s_triangle_vb);
    return 0;
}

static inline uint32_t float32_bits(float f)
{
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

static void xe_triangle_state(xenos_cmdbuf *cb)
{
    uint32_t cliprect[3];
    xenos_rb_surface_info surface;
    xenos_rb_color_info color;
    xenos_rb_depth_info depth;

    /* Render target: 640x480 linear 8_8_8_8 at EDRAM tile 0. */
    surface.surface_pitch = XE_TRIANGLE_WIDTH;
    surface.msaa_samples = XE_MSAA_1X;
    surface.hiz_pitch = 0;
    xe_gpu_cmd_reg_write(cb, XE_REG_RB_SURFACE_INFO, surface.value);
    color.value = 0; /* base 0, k_8_8_8_8, bias 0 */
    xe_gpu_cmd_reg_write(cb, XE_REG_RB_COLOR_INFO, color.value);
    depth.value = 0; /* base 0, format 0 (kD24S8), no use */
    xe_gpu_cmd_reg_write(cb, XE_REG_RB_DEPTH_INFO, depth.value);

    /* Full-viewport scissor. */
    xe_gpu_cmd_reg_write(cb, XE_REG_PA_SC_SCREEN_SCISSOR_TL, 0u);
    xe_gpu_cmd_reg_write(cb, XE_REG_PA_SC_SCREEN_SCISSOR_BR,
                         XE_TRIANGLE_WIDTH | (XE_TRIANGLE_HEIGHT << 16));
    /* Window scissor: disable window offset, cover the target. */
    xe_gpu_cmd_reg_write(cb, XE_REG_PA_SC_WINDOW_OFFSET, 0u);
    xe_gpu_cmd_reg_write(cb, XE_REG_PA_SC_WINDOW_SCISSOR_TL, 0x80000000u);
    xe_gpu_cmd_reg_write(cb, XE_REG_PA_SC_WINDOW_SCISSOR_BR,
                         XE_TRIANGLE_WIDTH | (XE_TRIANGLE_HEIGHT << 16));
    /* Cliprect 0 full screen, rule "all cliprects used" (0xFFFF). */
    cliprect[0] = 0xFFFFu;
    cliprect[1] = 0u;
    cliprect[2] = XE_TRIANGLE_WIDTH | (XE_TRIANGLE_HEIGHT << 16);
    xe_gpu_cmd_reg_writen(cb, XE_REG_PA_SC_CLIPRECT_RULE, 3, cliprect);

    /* Non-indexed draw bounds: indices 0..2. */
    xe_gpu_cmd_reg_writen(cb, XE_REG_VGT_MAX_VTX_INDX, 3,
                          (uint32_t[]){ 2u, 0u, 0u });

    /* Fragment color write + opaque alpha. */
    xe_gpu_cmd_reg_write(cb, XE_REG_RB_COLOR_MASK, 0xFu);
    /* No blending: src=kOne, dest=kZero, add. */
    xe_gpu_cmd_reg_write(cb, XE_REG_RB_BLENDCONTROL0, 0x00010001u);
    /* Rasterization: no cull, filled triangles, poly mode disabled. */
    xe_gpu_cmd_reg_write(cb, XE_REG_PA_SU_SC_MODE_CNTL, 0u);
    /* Clip disabled (no user clip planes → keep everything). */
    xe_gpu_cmd_reg_write(cb, XE_REG_PA_CL_CLIP_CNTL, XE_CLIP_DISABLE);

    /* D3D9-style viewport transform: NDC -> 640x480, z passthrough. */
    xe_gpu_cmd_reg_write(cb, XE_REG_PA_CL_VTE_CNTL,
                         XE_VTE_VPORT_X_SCALE_ENA | XE_VTE_VPORT_X_OFFSET_ENA |
                         XE_VTE_VPORT_Y_SCALE_ENA | XE_VTE_VPORT_Y_OFFSET_ENA |
                         XE_VTE_VPORT_Z_SCALE_ENA | XE_VTE_VPORT_Z_OFFSET_ENA);
    xe_gpu_cmd_reg_write(cb, XE_REG_PA_CL_VPORT_XSCALE,
                         float32_bits(XE_TRIANGLE_WIDTH / 2.0f));
    xe_gpu_cmd_reg_write(cb, XE_REG_PA_CL_VPORT_XOFFSET,
                         float32_bits(XE_TRIANGLE_WIDTH / 2.0f));
    xe_gpu_cmd_reg_write(cb, XE_REG_PA_CL_VPORT_YSCALE,
                         float32_bits(XE_TRIANGLE_HEIGHT / 2.0f));
    xe_gpu_cmd_reg_write(cb, XE_REG_PA_CL_VPORT_YOFFSET,
                         float32_bits(XE_TRIANGLE_HEIGHT / 2.0f));
    xe_gpu_cmd_reg_write(cb, XE_REG_PA_CL_VPORT_ZSCALE, float32_bits(1.0f));
    xe_gpu_cmd_reg_write(cb, XE_REG_PA_CL_VPORT_ZOFFSET, float32_bits(0.0f));

    /* EDRAM mode: colour + depth. */
    xe_gpu_cmd_reg_write(cb, XE_REG_RB_MODECONTROL, XE_EDRAM_MODE_COLOR_DEPTH);

    /* Shader control: valid, exports position and colour. */
    xe_gpu_cmd_reg_write(cb, XE_REG_SQ_PROGRAM_CNTL,
                         XE_SQ_PROGRAM_CNTL_V | XE_SQ_PROGRAM_CNTL_VTX_EXPORT |
                         XE_SQ_PROGRAM_CNTL_PS_EXPORT);
    xe_gpu_cmd_reg_write(cb, XE_REG_SQ_INTERPOLATOR_CNTL, 0u);
}

static void xe_triangle_constants(xenos_cmdbuf *cb)
{
    /* c48 = PS output colour (x,y,z,w) = magenta-ish. */
    uint32_t c[4] = { float32_bits(1.0f), float32_bits(0.1f),
                      float32_bits(0.8f), float32_bits(1.0f) };
    xe_gpu_cmd_reg_writen(cb, XE_REG_SHADER_CONST(XE_TRIANGLE_PS_CONST), 4, c);
}

static void xe_triangle_fetch_constant(xenos_cmdbuf *cb)
{
    /* Vertex fetch constant 0: physical triangle buffer, 48 bytes, k8in32
     * endianness (guest stores big-endian floats). */
    xenos_vertex_fetch vf[3];
    memset(vf, 0, sizeof(vf));
    xe_gpu_vfetch_build(&vf[0], s_triangle_vb_phys, 48u, XE_ENDIAN_8IN32);
    xe_gpu_cmd_reg_writen(cb, XE_REG_SHADER_CONST_FETCH(0), 6,
                          (const uint32_t *)&vf[0]);
}

static void xe_triangle_shaders(xenos_cmdbuf *cb)
{
    uint32_t vs[8], ps[5];
    uint32_t vs_dwords = xe_ucode_build_vs_minimal(vs);
    uint32_t ps_dwords = xe_ucode_build_ps_minimal(ps);

    /* PM4_IM_LOAD_IMMEDIATE: type dword + start/size dword + ucode. */
    xe_gpu_cmd_type3(cb, XE_PM4_OP_IM_LOAD_IMMEDIATE, 2u + vs_dwords);
    xe_gpu_cmd_push32(cb, 0u);                  /* ShaderType::kVertex */
    xe_gpu_cmd_push32(cb, vs_dwords);           /* start<<16 | size */
    for (uint32_t i = 0; i < vs_dwords; i++)
        xe_gpu_cmd_push32(cb, vs[i]);

    xe_gpu_cmd_type3(cb, XE_PM4_OP_IM_LOAD_IMMEDIATE, 2u + ps_dwords);
    xe_gpu_cmd_push32(cb, 1u);                  /* ShaderType::kPixel */
    xe_gpu_cmd_push32(cb, ps_dwords);
    for (uint32_t i = 0; i < ps_dwords; i++)
        xe_gpu_cmd_push32(cb, ps[i]);
}

static void xe_triangle_draw(xenos_cmdbuf *cb)
{
    /* kAutoIndex (non-indexed), kTriangleList, 3 vertices, implicit major
     * mode — the packet carries only the initiator dword. */
    uint32_t initiator = XE_PRIM_TYPE_TRIANGLE_LIST |
                         (XE_SOURCE_SEL_AUTO_INDEX << 6) |
                         (XE_MAJOR_MODE_IMPLICIT << 8) |
                         (3u << 16);
    xe_gpu_cmd_draw(cb, initiator, 0, 0, 0);
}

int xe_gpu_dev_triangle(uint32_t ring_va, uint32_t size_log2,
                        uint32_t start_wptr)
{
    static uint32_t s_cmd[XE_GPU_CMDBUF_DWORDS];
    xenos_ring ring;
    xenos_cmdbuf cb;

    if (xe_triangle_alloc_vb() != 0)
        return -1;

    xe_gpu_ring_init_at(&ring, (volatile uint32_t *)(uintptr_t)ring_va,
                        size_log2, start_wptr);
    xe_gpu_cmd_init(&cb, &ring, s_cmd);

    xe_gpu_cmd_wait_for_idle(&cb);
    xe_triangle_state(&cb);
    xe_triangle_constants(&cb);
    xe_triangle_fetch_constant(&cb);
    xe_triangle_shaders(&cb);
    xe_triangle_draw(&cb);

    xe_gpu_cmd_submit(&cb);
    return 0;
}
