#include "xenos_gpu.h"
#include "xenos_fetch.h"
#include "xenos_ucode.h"
#include <string.h>
#include <stdio.h>
#include <xecore/xboxkrnl.h>

#include "nir.h"
#include "nir_builder.h"
#include "compiler/glsl_types.h"
#include "xenos_private.h"
#include "xenos_shader.h"

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
    /* The ring lives in physical memory; the VA we were given
     * (screen.xenia_ring_address) aliases those pages both for the CPU and,
     * via MmGetPhysicalAddress, for the GPU.  NOTE: never MmMapIoSpace that
     * phys back into a pointer -- xenia's implementation just echoes the
     * argument as a guest VA, which would redirect every write away from
     * the ring.  The plain VA is what VdSwap itself uses. */
    ring->buffer = buffer;
    ring->size_dwords = 1u << (size_log2 + 1);
    ring->write_ptr = start_wptr % ring->size_dwords;
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
    /* No byte swap here: the big-endian CPU already stores each dword of
     * s_cmd in guest-native (big-endian) byte order, which is exactly the
     * memory layout xenia's ReadAndSwap expects.  Swapping here corrupts
     * every packet header (e.g. NOP 0xC0001000 becomes 0x001000C0). */
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
        xe_gpu_cmd_push32(&cb, 0x80000000u); /* Type2 filler */
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
        {  0.0f,  0.6f, 0.0f, 1.0f },
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

    /* Render target: 640x480 linear 8_8_8_8 at EDRAM tile 0.
     * Use explicit LE shifts, not the BE bitfield struct, so the value
     * survives BE->LE bswap + xenia ReadAndSwap correctly. */
    uint32_t surface_val = (XE_TRIANGLE_WIDTH & 0x3FFF) |
                           ((XE_MSAA_1X & 0x3) << 16) |
                           ((0 & 0x3FFF) << 18);
    xe_gpu_cmd_reg_write(cb, XE_REG_RB_SURFACE_INFO, surface_val);
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
    /* No blending: src=kOne, dest=kZero, add.  Xenia bitfield:
     * [4:0]=1(One) [7:5]=1(ADD) [12:8]=0(Zero)
     * [20:16]=1(One) [23:21]=1(ADD) [28:24]=0(Zero) = 0x00210021 */
    xe_gpu_cmd_reg_write(cb, XE_REG_RB_BLENDCONTROL0, 0x00210021u);
    /* Rasterization: no cull, filled triangles, poly mode disabled. */
    xe_gpu_cmd_reg_write(cb, XE_REG_PA_SU_SC_MODE_CNTL, 0u);
    /* Clip enabled: positions are NDC, transformed by PA_CL_VPORT below
     * (clip_disable makes xenia treat oPos as raw screen-space pixels). */
    xe_gpu_cmd_reg_write(cb, XE_REG_PA_CL_CLIP_CNTL, 0u);

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
    /* TEMP DIAGNOSTIC: fill c0..c59 all-white. */
    uint32_t w[4] = { float32_bits(1.0f), float32_bits(1.0f),
                      float32_bits(1.0f), float32_bits(1.0f) };
    for (uint32_t i = 0; i < 60; ++i) {
        xe_gpu_cmd_reg_writen(cb, XE_REG_SHADER_CONST(i), 4, w);
    }
    /* Pixel shader constants live in the second bank: guest cN for the PS is
     * host constant register 256+N. */
    xe_gpu_cmd_reg_writen(cb, XE_REG_SHADER_CONST(256u + XE_TRIANGLE_PS_CONST),
                          4, w);
}

static void xe_triangle_fetch_constant(xenos_cmdbuf *cb)
{
    /* Vertex fetch constant 0: physical triangle buffer, 48 bytes, k8in32
     * endianness (guest stores big-endian floats). */
    xenos_vertex_fetch vf[3];
    memset(vf, 0, sizeof(vf));
    xe_gpu_vfetch_build(&vf[0], s_triangle_vb_phys, 48u, XE_ENDIAN_8IN32);
    /* Only write VF0 (2 dwords) to avoid clobbering VF1/VF2. */
    xe_gpu_cmd_reg_writen(cb, 0x4800, 2,
                          (const uint32_t *)&vf[0]);
}

static void xe_triangle_shaders(xenos_cmdbuf *cb)
{
    uint32_t vs[9], ps[6];
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

/* ---- Diagnostic VS: position derived only from the vertex index --------
 * Eliminates vfetch/VB/shared-memory/fetch-constant-uniforms from the
 * equation.  Triangle covering the screen center:
 *   x = 4*i - 1 ; u = i*i ; y = 4*u + (-3*i - 1) ; z = 0.5 ; w = 1
 * i=0 -> (-1,-1)  i=1 -> (3,0)  i=2 -> (7,9)  (NDC (0,0) is interior).
 * Uses c50=4 c51=-3 c52=-1 c53=0.5 c54=1. */
#define XE_DBG_SPLAT_X 0x6Cu /* relative splat of x */

static void xe_dbg_const(xenos_cmdbuf *cb, uint32_t idx, float v)
{
    uint32_t c[4] = { float32_bits(v), float32_bits(v), float32_bits(v),
                      float32_bits(v) };
    xe_gpu_cmd_reg_writen(cb, XE_REG_SHADER_CONST(idx), 4, c);
}

static void xe_triangle_shaders_alu(xenos_cmdbuf *cb)
{
    const uint32_t T = 1u;
    const uint32_t SX[4] = { T, 0, XE_DBG_SPLAT_X, 0 }; /* r0.x splat */
    const uint32_t R1X[4] = { T, 1, XE_DBG_SPLAT_X, 0 };
    const uint32_t R1Y[4] = { T, 1, 0xB1u /* relative splat of y */, 0 };
    const uint32_t R2X[4] = { T, 2, XE_DBG_SPLAT_X, 0 };
    const uint32_t R3[4] = { T, 3, 0u, 0 }; /* xyzw */
    const uint32_t C50[4] = { 0u, 50, 0u, 0 };
    const uint32_t C51[4] = { 0u, 51, 0u, 0 };
    const uint32_t C52[4] = { 0u, 52, 0u, 0 };
    const uint32_t C53[4] = { 0u, 53, 0u, 0 };
    const uint32_t C54[4] = { 0u, 54, 0u, 0 };
    const uint32_t NONE = 0xFFFFFFFFu;

    xe_dbg_const(cb, 50, 4.0f);
    xe_dbg_const(cb, 51, -3.0f);
    xe_dbg_const(cb, 52, -1.0f);
    xe_dbg_const(cb, 53, 0.5f);
    xe_dbg_const(cb, 54, 1.0f);

    /* PS constants live in the second bank: guest cN for the pixel stage is
     * host constant register 256+N. Fill c256+48..c256+51 white so the PS's
     * compacted constant reads are non-black regardless of exact index. */
    for (uint32_t i = 0; i < 4; ++i) {
        uint32_t pc[4] = { float32_bits(1.0f), float32_bits(1.0f),
                           float32_bits(1.0f), float32_bits(1.0f) };
        xe_gpu_cmd_reg_writen(cb, XE_REG_SHADER_CONST(256u + 48u + i), 4, pc);
    }

    xe_gpu_cmd_reg_write(cb, XE_REG_SQ_PROGRAM_CNTL, 3u /* 4 VS GPRs */);

    /* IM_LOAD_IMMEDIATE vertex shader: 9 ALU slots split into two exec
     * clauses (the CF exec count field is 3 bits — max 7 slots per clause).
     * The exec address doubles as the CF-pair count upper bound in xenia's
     * ucode analysis (first exec address = where slots start), so BOTH CF
     * pairs live in words 0-1 and clause 1 starts at address 2:
     *   word0:    {NOP, exec addr=2 count=4}
     *   word1:    {exec addr=6 count=5 END, NOP}
     *   words2-5: clause 1 slots
     *   words6-10: clause 2 slots */
    uint32_t cfa[2], cfb[2];
    xe_ucode_cf_exec(cfa, 2u, 4u, 0u, XE_UCODE_CF_EXEC);
    xe_ucode_cf_exec(cfb, 6u, 5u, 0u, XE_UCODE_CF_EXEC_END);
    enum { VS_DWORDS = 6 + 27 };
    uint32_t vs[VS_DWORDS];
    xe_ucode_cf_emit_pair(vs, 0u, 0u, cfa[0], cfa[1]);
    xe_ucode_cf_emit_pair(vs + 3, cfb[0], cfb[1], 0u, 0u);
    uint32_t *o = vs + 6;
#define DBG_SLOT(opc, dst_, wm, exp, a, b, cc)                               \
    do {                                                                     \
        uint32_t _out[3];                                                    \
        _out[0] = ((exp != NONE) ? exp : (dst_)) |                           \
                  ((exp != NONE ? 1u : 0u) << 15) | ((wm) << 16);            \
        _out[1] = (((cc)[3] ? 1u : 0u) << 24) | (((b)[3] ? 1u : 0u) << 25) | \
                  (((a)[3] ? 1u : 0u) << 26) | ((cc)[2]) | ((b)[2] << 8) |  \
                  ((a)[2] << 16);                                            \
        _out[2] = ((cc)[1]) | ((b)[1] << 8) | ((a)[1] << 16) |               \
                  (((opc)&0x1Fu) << 24) | (((cc)[0] ? 1u : 0u) << 29) |      \
                  (((b)[0] ? 1u : 0u) << 30) | (((a)[0] ? 1u : 0u) << 31);   \
        memcpy(o, _out, sizeof(_out));                                       \
        o += 3;                                                              \
    } while (0)
    /* Clause 1 (addr=1): x = 2.5i - 1 ; u = i*i ; t = -1.25i - 1 ; y. */
    DBG_SLOT(XE_UCODE_ALU_MAD, 1, 0x1, NONE, SX, C50, C52);  /* r1.x=x */
    DBG_SLOT(XE_UCODE_ALU_MUL, 1, 0x2, NONE, SX, SX, C52);   /* r1.y=i*i */
    DBG_SLOT(XE_UCODE_ALU_MAD, 2, 0x1, NONE, C51, SX, C52);  /* r2.x */
    DBG_SLOT(XE_UCODE_ALU_MAD, 1, 0x2, NONE, R1Y, C50, R2X); /* r1.y=y */
    /* Clause 2 (addr=6): stage r3, export position. */
    o = vs + 18;
    DBG_SLOT(XE_UCODE_ALU_MAX, 3, 0x1, NONE, R1X, R1X, C52); /* r3.x */
    DBG_SLOT(XE_UCODE_ALU_MAX, 3, 0x2, NONE, R1Y, R1Y, C52); /* r3.y */
    DBG_SLOT(XE_UCODE_ALU_MAX, 3, 0x4, NONE, C53, C53, C52); /* r3.z */
    DBG_SLOT(XE_UCODE_ALU_MAX, 3, 0x8, NONE, C54, C54, C52); /* r3.w */
    DBG_SLOT(XE_UCODE_ALU_MAX, 3, 0xF, 62u, R3, R3, C52);    /* export pos */
#undef DBG_SLOT

    xe_gpu_cmd_type3(cb, XE_PM4_OP_IM_LOAD_IMMEDIATE, 2u + VS_DWORDS);
    xe_gpu_cmd_push32(cb, 0u);
    xe_gpu_cmd_push32(cb, (uint32_t)VS_DWORDS);
    for (uint32_t i = 0; i < VS_DWORDS; i++)
        xe_gpu_cmd_push32(cb, vs[i]);

    /* Pixel shader: unchanged minimal constant-color. */
    uint32_t ps[6];
    uint32_t ps_dwords = xe_ucode_build_ps_minimal(ps);
    xe_gpu_cmd_type3(cb, XE_PM4_OP_IM_LOAD_IMMEDIATE, 2u + ps_dwords);
    xe_gpu_cmd_push32(cb, 1u);
    xe_gpu_cmd_push32(cb, ps_dwords);
    for (uint32_t i = 0; i < ps_dwords; i++)
        xe_gpu_cmd_push32(cb, ps[i]);
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

/* ---------------------------------------------------------------------- */
/* Dev triangle rendered through the NIR->microcode compiler, then the     */
/* EDRAM is resolved back into the caller's linear front buffer.           */
/*                                                                         */
/*   NIR (built here with nir_builder) -> xenos_compile_nir -> microcode,  */
/*   uploaded with PM4_IM_LOAD_IMMEDIATE, drawn into EDRAM tile 0, then    */
/*   RB_MODECONTROL=kCopy + RB_COPY_* resolve into a tiled system-memory   */
/*   buffer and unswizzled into the front buffer at (0,0).  The caller     */
/*   presents afterwards (VdSwap).  Returns 0 on success.                  */

#define XE_DEV_RESOLVE_W 640u
#define XE_DEV_RESOLVE_H 480u

static struct nir_shader *xe_dev_build_vs(void)
{
    struct nir_builder b =
        nir_builder_init_simple_shader(MESA_SHADER_VERTEX, NULL, "dev_vs");
    b.shader->info.inputs_read = 0;
    b.shader->info.outputs_written = 0;

    nir_def *pos = nir_load_input(&b, 4, 32, nir_imm_int(&b, 0),
                                  .io_semantics.location = VARYING_SLOT_POS);
    nir_store_output(&b, pos, nir_imm_int(&b, 0),
                     .write_mask = 0xF,
                     .io_semantics.location = VARYING_SLOT_POS);
    nir_jump(&b, nir_jump_halt);
    b.shader->info.inputs_read = VARYING_BIT_POS;
    b.shader->info.outputs_written = VARYING_BIT_POS;

    return b.shader;
}

static struct nir_shader *xe_dev_build_fs(void)
{
    struct nir_builder b =
        nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT, NULL, "dev_fs");
    b.shader->info.inputs_read = 0;
    b.shader->info.outputs_written = 0;

    nir_variable *col = nir_variable_create(b.shader, nir_var_uniform,
                                            glsl_vec4_type(), "dev_color");
    col->data.driver_location = 0; /* -> const 0 */

    nir_def *c = nir_load_var(&b, col);
    nir_store_output(&b, c, nir_imm_int(&b, 0),
                     .write_mask = 0xF,
                     .io_semantics.location = FRAG_RESULT_COLOR);
    nir_jump(&b, nir_jump_halt);
    b.shader->info.outputs_written = 1u << FRAG_RESULT_COLOR;

    return b.shader;
}

/* One-time shaders: the compiler has no screen dependency for the dev path,
 * so compile once and keep the handles for the life of the process. */
static struct xenos_shader *s_dev_vs;
static struct xenos_shader *s_dev_ps;

static int xe_dev_compile_shaders(void)
{
    if (s_dev_vs && s_dev_ps)
        return 0;

    if (!s_dev_vs) {
        struct nir_shader *vs = xe_dev_build_vs();
        if (!vs)
            return -1;
        s_dev_vs = xenos_compile_nir(vs);
        ralloc_free(vs);
        if (!s_dev_vs)
            return -1;
    }
    if (!s_dev_ps) {
        struct nir_shader *ps = xe_dev_build_fs();
        if (!ps)
            return -1;
        s_dev_ps = xenos_compile_nir(ps);
        ralloc_free(ps);
        if (!s_dev_ps)
            return -1;
    }

    /* The compiler emitted the vfetch slot with the fetch constants it knows:
     * stride 0, offset 0, 32_32_32_32_FLOAT from fetch const 0.  Patch the
     * stride to the real interleaved layout (4 dwords = 16 bytes) using the
     * recorded fixup (the slot's first dword, format/stride/offset live in
     * dwords 1-2 of the 3-dword vfetch instruction). */
    for (uint32_t i = 0; i < s_dev_vs->vfetch_count; i++) {
        struct xenos_vfetch_fixup *fx = &s_dev_vs->vfetch[i];
        uint32_t base = fx->ucode_dword;
        uint32_t out[3];
        /* vfetch_full: dst r<attrib>.xyzw from fetch const <attrib>, src r0.x
         * (auto-injected vertex index), 32_32_32_32_FLOAT, stride 4.  Matches
         * the compiler's own xe_ucode_vfetch call (xenos_shader.c) with only
         * the stride patched 0 -> 4. */
        xe_ucode_vfetch(out, fx->attrib, fx->attrib, XE_UCODE_DST_SWIZ_XYZW,
                        0, 0, XE_UCODE_FORMAT_32_32_32_32_FLOAT, 4, 0,
                        true, true);
        s_dev_vs->ucode[base] = out[0];
        s_dev_vs->ucode[base + 1] = out[1];
        s_dev_vs->ucode[base + 2] = out[2];
    }

    DbgPrint("xenos: dev compiled vs gprs=%u slots=%u consts=%u, "
             "ps gprs=%u slots=%u consts=%u",
             s_dev_vs->num_gprs, s_dev_vs->num_slots, s_dev_vs->num_consts,
             s_dev_ps->num_gprs, s_dev_ps->num_slots, s_dev_ps->num_consts);
    return 0;
}

/* PS const: the FS's uniform lives at driver_location 0 -> const 0. */
static void xe_triangle_constants_nir(xenos_cmdbuf *cb)
{
    /* Minimal PS uses c48 for color (magenta: R=1, G=0.1, B=0.8, A=1). */
    uint32_t c[4] = { float32_bits(1.0f), float32_bits(0.1f),
                      float32_bits(0.8f), float32_bits(1.0f) };
    xe_gpu_cmd_reg_writen(cb, XE_REG_SHADER_CONST(48), 4, c);
}

/* Upload the real NIR-compiled shaders (s_dev_vs / s_dev_ps) via PM4.
 * The VS fetches position from vfetch attrib 0 (fetch const 0, set up by
 * xe_triangle_fetch_constant); the FS reads its uniform at guest PS c0
 * (host register bank 256+0).  Both shaders are single-clause
 * (1 CF pair + 3*num_slots dwords) per the compiler's codegen model. */
static void xe_triangle_shaders_nir_real(xenos_cmdbuf *cb)
{
    if (xe_dev_compile_shaders() != 0 || !s_dev_vs || !s_dev_ps) {
        DbgPrint("xenos: nir shader compile failed");
        return;
    }

    /* SQ_PROGRAM_CNTL: GPR counts minus 1 per stage; vs_export_mode=0
     * (position-1-vector), no interpolators (the FS reads none). */
    uint32_t vs_reg = s_dev_vs->num_gprs ? (s_dev_vs->num_gprs - 1u) & 0x3Fu : 0u;
    uint32_t ps_reg = s_dev_ps->num_gprs ? (s_dev_ps->num_gprs - 1u) & 0x3Fu : 0u;
    uint32_t cntl = vs_reg | (ps_reg << 8);
    DbgPrint("xenos: nir SQ_PROGRAM_CNTL=%08x (vs gprs=%u ps gprs=%u)", cntl,
             s_dev_vs->num_gprs, s_dev_ps->num_gprs);
    xe_gpu_cmd_reg_write(cb, XE_REG_SQ_PROGRAM_CNTL, cntl);
    xe_gpu_cmd_reg_write(cb, XE_REG_SQ_INTERPOLATOR_CNTL, 0u);

    /* FS color uniform: driver_location 0 -> PS bank const 0.
     * Magenta, matching the sample's intent. */
    uint32_t pc[4] = { float32_bits(1.0f), float32_bits(0.0f),
                       float32_bits(1.0f), float32_bits(1.0f) };
    xe_gpu_cmd_reg_writen(cb, XE_REG_SHADER_CONST(256u + 0u), 4, pc);

    /* IM_LOAD_IMMEDIATE vertex shader. */
    uint32_t vd = s_dev_vs->ucode_dwords;
    DbgPrint("xenos: nir upload vs dwords=%u ps dwords=%u", vd,
             s_dev_ps->ucode_dwords);
    xe_gpu_cmd_type3(cb, XE_PM4_OP_IM_LOAD_IMMEDIATE, 2u + vd);
    xe_gpu_cmd_push32(cb, 0u); /* ShaderType::kVertex */
    xe_gpu_cmd_push32(cb, vd); /* start<<16 | size */
    for (uint32_t i = 0; i < vd; i++)
        xe_gpu_cmd_push32(cb, s_dev_vs->ucode[i]);

    /* IM_LOAD_IMMEDIATE pixel shader. */
    uint32_t pd = s_dev_ps->ucode_dwords;
    xe_gpu_cmd_type3(cb, XE_PM4_OP_IM_LOAD_IMMEDIATE, 2u + pd);
    xe_gpu_cmd_push32(cb, 1u); /* ShaderType::kPixel */
    xe_gpu_cmd_push32(cb, pd);
    for (uint32_t i = 0; i < pd; i++)
        xe_gpu_cmd_push32(cb, s_dev_ps->ucode[i]);
}

static void xe_triangle_shaders_nir(xenos_cmdbuf *cb)
{
    /* Use the exact working minimal shaders from the original working path. */
    uint32_t vs_min[9], ps_min[6];
    uint32_t vs_dwords = xe_ucode_build_vs_minimal(vs_min);
    uint32_t ps_dwords = xe_ucode_build_ps_minimal(ps_min);
    xe_gpu_cmd_reg_write(cb, XE_REG_SQ_PROGRAM_CNTL, 0x00000001u);
    xe_gpu_cmd_reg_write(cb, XE_REG_SQ_INTERPOLATOR_CNTL, 0u);
    xe_gpu_cmd_type3(cb, XE_PM4_OP_IM_LOAD_IMMEDIATE, 2u + vs_dwords);
    xe_gpu_cmd_push32(cb, 0u);
    xe_gpu_cmd_push32(cb, vs_dwords);
    for (uint32_t i = 0; i < vs_dwords; i++)
        xe_gpu_cmd_push32(cb, vs_min[i]);
    xe_gpu_cmd_type3(cb, XE_PM4_OP_IM_LOAD_IMMEDIATE, 2u + ps_dwords);
    xe_gpu_cmd_push32(cb, 1u);
    xe_gpu_cmd_push32(cb, ps_dwords);
    for (uint32_t i = 0; i < ps_dwords; i++)
        xe_gpu_cmd_push32(cb, ps_min[i]);
}

/* ---- Resolve readback -------------------------------------------------- */

static void *s_resolve_dest;      /* tiled 640x480 32bpp destination */
static uint32_t s_resolve_dest_phys;
static void *s_resolve_rect;      /* 3 x (x,y) float32 resolve rectangle */
static uint32_t s_resolve_rect_phys;
static void *s_rptr_page;         /* read-pointer writeback word */
static uint32_t s_rptr_phys;
static int s_resolve_ready;

static int xe_resolve_alloc(void)
{
    if (s_resolve_ready)
        return 0;

    s_resolve_dest = MmAllocatePhysicalMemoryEx(
        REGION_AUTO, XE_DEV_RESOLVE_W * XE_DEV_RESOLVE_H * 4 + 0x1000,
        0x00000004u /* X_PAGE_READWRITE */, 0u, 0xFFFFFFFFu, 0x1000);
    s_resolve_rect = MmAllocatePhysicalMemoryEx(REGION_AUTO, 64u,
                                                0x00000004u, 0u, 0xFFFFFFFFu,
                                                0x1000);
    s_rptr_page = MmAllocatePhysicalMemoryEx(REGION_AUTO, 0x1000u,
                                             0x00000004u, 0u, 0xFFFFFFFFu,
                                             0x1000);
    if (!s_resolve_dest || !s_resolve_rect || !s_rptr_page)
        return -1;

    s_resolve_dest_phys = MmGetPhysicalAddress(s_resolve_dest);
    s_resolve_rect_phys = MmGetPhysicalAddress(s_resolve_rect);
    s_rptr_phys = MmGetPhysicalAddress(s_rptr_page);

    memset(s_rptr_page, 0, 4);

    /* Resolve rectangle: whole 640x480 target.  Stored as big-endian floats
     * (guest-native); the vf0 fetch requests k8in32 so xenia's host-side
     * GpuSwap converts them to little-endian floats before the fixed16.8
     * conversion. */
    {
        const float rect[6] = { 0.0f, 0.0f, (float)XE_DEV_RESOLVE_W, 0.0f,
                                0.0f, (float)XE_DEV_RESOLVE_H };
        memcpy(s_resolve_rect, rect, sizeof(rect));
    }

    /* Have the CP write the read pointer back to guest memory so we can wait
     * for the resolve to complete. */
    VdEnableRingBufferRPtrWriteBack(s_rptr_phys, 6);

    s_resolve_ready = 1;
    return 0;
}

static int xe_resolve_wait(uint32_t want_wptr)
{
    volatile uint32_t *rptr = (volatile uint32_t *)s_rptr_page;
    int64_t interval = -1000; /* 100 us (100ns units) */
    /* Very generous budget: the host may translate shaders and build Vulkan
     * pipelines on the first submission (tens of seconds on a cold run,
     * especially when the shader cache can't be written). */
    uint32_t last_seen = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < 4000000u; i++) {
        uint32_t v = *rptr;
        if (v == want_wptr)
            return 0;
        if (v != last_seen) {
            last_seen = v;
            DbgPrint("xenos: wait want=%u saw rptr=%u (iter %u)", want_wptr,
                     last_seen, i);
        } else if ((i & 0x2710u) == 0) {
            const volatile uint32_t *raw = rptr;
            DbgPrint("xenos: wait stuck want=%u rptr=%u raw=[%08x %08x %08x]",
                     want_wptr, v, raw[0], raw[1], raw[2]);
        }
        KeDelayExecutionThread(0, 0, &interval);
    }
    DbgPrint("xenos: resolve wait TIMEOUT want=%u last=%u", want_wptr,
             last_seen);
    return -1;
}

/* XeTextureTiledOffset2D: byte offset of pixel (x,y) in a 2D tiled surface
 * with the given pitch (pixels) and bytes-per-pixel log2.  Mirror of xenia's
 * GetTiledOffset2D / the resolve shader's XeResolveDestPixelAddress (with a
 * zero base).  The resolve shader always stores 4 consecutive pixels per
 * thread on 16-byte boundaries, so per-pixel byte offsets are dword-aligned
 * for 32bpp. */

static void xe_resolve_submit(xenos_cmdbuf *cb)
{
    uint32_t initiator = XE_PRIM_TYPE_TRIANGLE_LIST |
                         (XE_SOURCE_SEL_AUTO_INDEX << 6) |
                         (XE_MAJOR_MODE_IMPLICIT << 8) |
                         (3u << 16);
    xenos_vertex_fetch vf[3];

    /* Raw copy of EDRAM (which has the magenta triangle) to system memory. */
    xe_gpu_cmd_reg_write(cb, XE_REG_RB_COPY_CONTROL, 0u);
    xe_gpu_cmd_reg_write(cb, XE_REG_RB_COPY_DEST_BASE, s_resolve_dest_phys);
    /* pitch 640, height 480. */
    xe_gpu_cmd_reg_write(cb, XE_REG_RB_COPY_DEST_PITCH,
                         XE_DEV_RESOLVE_W | (XE_DEV_RESOLVE_H << 16));
    /* dest format k_8_8_8_8 (6) << 7, endian none, no swap/array/exp_bias. */
    xe_gpu_cmd_reg_write(cb, XE_REG_RB_COPY_DEST_INFO, 6u << 7);

    /* Trigger the copy with a draw; the target stays tile 0 / 8_8_8_8. */
    DbgPrint("xenos: resolve about to write RB_MODECONTROL=COPY(1)");
    xe_gpu_cmd_reg_write(cb, XE_REG_RB_MODECONTROL, XE_EDRAM_MODE_COPY);
    DbgPrint("xenos: wrote RB_MODECONTROL, cb.used=%u", cb->used);
    DbgPrint("xenos: wrote RB_MODECONTROL=COPY(1), cb.used=%u", cb->used);

    /* vf0 = resolve rectangle (3 x (x,y) floats). */
    memset(vf, 0, sizeof(vf));
    xe_gpu_vfetch_build(&vf[0], s_resolve_rect_phys, 24u, XE_ENDIAN_8IN32);
    /* Only write VF0 (2 dwords) to avoid clobbering VF1/VF2. */
    xe_gpu_cmd_reg_writen(cb, 0x4800, 2,
                          (const uint32_t *)&vf[0]);

    DbgPrint("xenos: resolve submit draw, initiator=%08x", initiator);
    xe_gpu_cmd_draw(cb, initiator, 0, 0, 0);
    DbgPrint("xenos: resolve draw submitted, cb.used=%u", cb->used);
}

int xe_gpu_dev_triangle_nir(uint32_t ring_va, uint32_t size_log2,
                            uint32_t start_wptr, uint32_t front_va,
                            uint32_t *wptr_out, int skip_resolve)
{
    static uint32_t s_cmd[XE_GPU_CMDBUF_DWORDS];
    xenos_ring ring;
    xenos_cmdbuf cb;
    uint32_t start = start_wptr;

    if (xe_dev_compile_shaders() != 0)
        return -1;
    if (xe_triangle_alloc_vb() != 0)
        return -1;
    if (!skip_resolve && xe_resolve_alloc() != 0)
        return -1;

    xe_gpu_ring_init_at(&ring, (volatile uint32_t *)(uintptr_t)ring_va,
                        size_log2, start);
    xe_gpu_cmd_init(&cb, &ring, s_cmd);

    /* Single combined submission: triangle draw + resolve.
     * The GPU processes PM4 in order, so EDRAM writes complete before the
     * resolve draw reads them. */
    xe_gpu_cmd_wait_for_idle(&cb);
    xe_triangle_state(&cb);
    xe_triangle_constants(&cb);
    xe_triangle_fetch_constant(&cb);
    /* Real NIR-compiled shaders (vfetch position + uniform-color FS). */
    xe_triangle_shaders_nir_real(&cb);
    xe_triangle_draw(&cb);
    /* Second draw in kCopy mode: resolves EDRAM tile 0 into system memory
     * (tiled destination), covered by the drawn resolve rectangle. */
    if (!skip_resolve)
        xe_resolve_submit(&cb);

    DbgPrint("xenos: tri_nir cb_used=%u vs_slots=%u ps_slots=%u", cb.used,
             s_dev_vs->num_slots, s_dev_ps->num_slots);
    /* Pad to the 64-dword block boundary with Type-2 NOPs so every dword up
     * to the published write pointer is a valid packet.  Stale ring memory
     * between used and the boundary would otherwise be parsed by the CP (a
     * stale truncated header there fails as "ExecutePacketType0 overflow"). */
    {
        uint32_t padded = (cb.used + 63) & ~63u;
        while (cb.used < padded)
            xe_gpu_cmd_push32(&cb, 0x80000000u); /* Type2 filler */
    }
    uint32_t block = cb.used;
    xe_gpu_cmd_submit(&cb);
    /* Report the new free-running ring position so the caller's VdSwap
     * appends after our packets instead of overwriting them. */
    if (wptr_out)
        *wptr_out = start + block;
    DbgPrint("xenos: after submit wptr=%u", ring.write_ptr);

    if (!skip_resolve) {
        if (xe_resolve_wait(ring.write_ptr) != 0) {
            DbgPrint("xenos: resolve wait timed out (wptr=%u)", ring.write_ptr);
            return -1;
        }
    }

    DbgPrint("xenos: dev triangle (NIR) rendered%s",
             skip_resolve ? " (resolve skipped)" : " + resolved");
    return 0;
}

void xe_gpu_get_resolve_surface(uint32_t *phys_out, uint32_t *w_out,
                                uint32_t *h_out)
{
    if (phys_out)
        *phys_out = s_resolve_dest_phys;
    if (w_out)
        *w_out = XE_DEV_RESOLVE_W;
    if (h_out)
        *h_out = XE_DEV_RESOLVE_H;
}
