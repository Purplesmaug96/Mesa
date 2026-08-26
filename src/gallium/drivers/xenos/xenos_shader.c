/*
 * xenos - hardware (PM4) Gallium driver for the Xbox 360 GPU.
 *
 * NIR -> Xenos microcode compiler.
 *
 * The state tracker hands this driver NIR (PIPE_SHADER_IR_NIR) whose
 * uniforms were lowered to UBO 0 (nir_lower_uniforms_to_ubo) with byte
 * offsets driver_location*16; vertex inputs are nir_intrinsic_load_input
 * with .base = VERT_ATTRIB_*, fragment inputs nir_load_interpolated_input
 * with .base = VARYING_SLOT_*.
 *
 * Codegen model (see docs/gpu.md "Microcode register encoding"):
 *  - every nir_def is a full vec4 in a GPR or a float constant; swizzles and
 *    negates are folded into the consuming instruction's src fields.
 *  - VS inputs: one vfetch slot per load_input, attrib i -> GPR i, fetch
 *    constant i, vertex index from r0.x (auto-generated for non-indexed
 *    draws).  stride/offset/format are patched at draw time.
 *  - FS inputs: interpolator feeds GPR i directly (param_shade byte i =
 *    (i<<4)|i), no instructions.
 *  - store_output: a final "max t,t" slot with export_data=1 writing the
 *    export register (VS position=62, point size=63, varyings 0-15; PS
 *    color0-3, depth=61).
 *  - uniforms stay at their driver_location constant indices; immediates are
 *    deduplicated after them; helpers 0/1/-1/0.5 live at 252..255.
 *  - the whole shader is a single kExecEnd block: 1 CF pair (3 dwords) +
 *    3*num_slots dwords.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"

#include "util/u_math.h"
#include "util/u_memory.h"

#include "gpu/xenos_ucode.h"
#include "xenos_shader.h"
#include "xenos_private.h"

#define XE_MAX_UCODE_DWORDS 6144u /* 3 + 3*2047 slots */
#define XE_MAX_SLOTS         ((XE_MAX_UCODE_DWORDS - 3) / 3)
#define XE_HELPER_CONST_BASE 252u /* 0.0, 1.0, -1.0, 0.5 */

/* One relocated operand: a temp GPR or a float constant.  Field layout
 * matches xe_ucode_alu_src so operands can be assigned straight into ALU
 * src slots. */
typedef xe_ucode_alu_src xe_operand;

struct xe_cctx
{
   struct nir_shader *nir;
   mesa_shader_stage stage;

   uint32_t ucode[XE_MAX_UCODE_DWORDS];
   uint32_t num_slots;
   uint32_t sequence;

   uint32_t next_temp_gpr;
   uint32_t max_gpr;

   /* Uniform constants keep their driver_location (loc) index; immediates
    * are appended after the highest one. */
   uint32_t next_imm_const;
   uint32_t num_consts;
   float const_values[XE_MAX_UCODE_DWORDS];

   /* def index -> operand. */
   xe_ucode_alu_src *defs;
   uint8_t *def_set;
   uint32_t num_defs;

   uint32_t varying_mask; /* VS interpolator exports / FS input slots */
   uint32_t num_varying_exports;

   struct xenos_vfetch_fixup vfetch[16];
   uint32_t vfetch_count;

   bool failed;
};

static const xe_ucode_alu_src xe_op_zero = { false, 0, 0x00, false };

static xe_ucode_alu_src *
xe_def(struct xe_cctx *c, nir_def *def)
{
   if (def->index < c->num_defs)
      return &c->defs[def->index];
   c->failed = true;
   return NULL;
}

/* Compose a NIR absolute swizzle over an operand whose stored swizzle is in
 * xenia's component-relative encoding (abs[i] = (rel[i] + i) & 3).
 * abs'[i] = abs[s[i]] = (rel[s[i]] + s[i]) & 3
 * rel'[i] = (abs'[i] - i) & 3 */
static uint32_t
xe_swiz_compose(uint32_t outer_abs, uint32_t inner_rel)
{
   uint32_t out = 0;
   for (unsigned i = 0; i < 4; i++) {
      unsigned s = (outer_abs >> (2 * i)) & 3;
      unsigned abs_i = (((inner_rel >> (2 * s)) & 3) + s) & 3;
      out |= ((abs_i + 4u - i) & 3u) << (2 * i);
   }
   return out;
}

/* Splat absolute component c to every lane, relative encoding:
 * rel[i] = (c - i) & 3. */
static uint32_t
xe_swiz_splat(uint32_t c)
{
   uint32_t v = 0;
   for (unsigned i = 0; i < 4; i++)
      v |= ((c + 4u - i) & 3u) << (2 * i);
   return v;
}

static void
xe_emit_slot(struct xe_cctx *c, const uint32_t slot[3], bool is_fetch)
{
   if (c->num_slots >= XE_MAX_SLOTS) {
      c->failed = true;
      return;
   }
   memcpy(&c->ucode[3 + 3 * c->num_slots], slot, 3 * sizeof(uint32_t));
   c->sequence |= (is_fetch ? 1u : 0u) << (2 * c->num_slots);
   c->num_slots++;
}

static uint32_t
xe_alloc_temp(struct xe_cctx *c)
{
   uint32_t gpr = c->next_temp_gpr++;
   if (gpr > c->max_gpr)
      c->max_gpr = gpr;
   return gpr;
}

/* Register an immediate float constant (1-4 components) at a relocated
 * index, deduplicating by value. */
static uint32_t
xe_add_const(struct xe_cctx *c, const float v[4], unsigned ncomp)
{
   for (uint32_t i = c->next_imm_const; i < c->num_consts; i++) {
      if (memcmp(&c->const_values[4 * i], v, 4 * sizeof(float)) == 0)
         return i;
   }
   uint32_t idx = c->num_consts;
   if (idx + 1 > XE_HELPER_CONST_BASE) {
      c->failed = true;
      return 0;
   }
   memcpy(&c->const_values[4 * idx], v, 4 * sizeof(float));
   c->num_consts++;
   return idx;
}

static xe_operand
xe_imm_op(struct xe_cctx *c, nir_load_const_instr *lc)
{
   float v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
   for (unsigned i = 0; i < lc->def.num_components; i++)
      v[i] = nir_const_value_as_float(lc->value[i], lc->def.bit_size);
   uint32_t idx = xe_add_const(c, v, lc->def.num_components);
   xe_ucode_alu_src op = { false, idx, XE_UCODE_ALU_SWIZ_XYZW, false };
   if (lc->def.num_components == 1)
      op.swiz = 0x00; /* scalar: component 0 everywhere */
   return op;
}

/* Emit an ALU slot.  dst is a temp GPR (or ignored for exports);
 * export_dest = UINT32_MAX for no export. */
static void
xe_emit_alu(struct xe_cctx *c, uint32_t opc, uint32_t dst_gpr,
            uint32_t write_mask, const xe_ucode_alu_src *src0,
            const xe_ucode_alu_src *src1, const xe_ucode_alu_src *src2,
            uint32_t export_dest)
{
   xe_ucode_alu alu;
   memset(&alu, 0, sizeof(alu));
   alu.opc = opc;
   alu.write_mask = write_mask & 0xF;
   alu.dst = dst_gpr;
   alu.export_ = export_dest != UINT32_MAX;
   alu.export_dest = export_dest;
   alu.src[0] = *src0;
   alu.src[1] = *src1;
   alu.src[2] = *src2;
   alu.scalar_opc = XE_UCODE_SCALAR_NOP;
   alu.scalar_mask = 0;
   alu.scalar_dst = 0;
   alu.vector_clamp = false;
   alu.scalar_clamp = false;
   alu.abs_constants = false;

   uint32_t slot[3];
   xe_ucode_alu_build(slot, &alu);
   xe_emit_slot(c, slot, false);
}

/* mov via max t, s, s (Xenos has no plain mov). */
static void
xe_emit_mov(struct xe_cctx *c, uint32_t dst_gpr, uint32_t write_mask,
            const xe_ucode_alu_src *src)
{
   xe_ucode_alu_src s1 = *src;
   xe_emit_alu(c, XE_UCODE_ALU_MAX, dst_gpr, write_mask, src, &s1,
               &xe_op_zero, UINT32_MAX);
}

/* |x| = max(x, -x). */
static void
xe_emit_abs(struct xe_cctx *c, uint32_t dst_gpr, const xe_ucode_alu_src *src)
{
   xe_ucode_alu_src neg = *src;
   neg.negate = !neg.negate;
   xe_emit_alu(c, XE_UCODE_ALU_MAX, dst_gpr, 0xF, src, &neg, &xe_op_zero,
               UINT32_MAX);
}

/* clamps (saturate) via min(max(x, lo), hi). */
static void
xe_emit_clamp(struct xe_cctx *c, uint32_t dst_gpr, const xe_ucode_alu_src *src,
              float lo, float hi)
{
   float vlo[4] = { lo, lo, lo, lo }, vhi[4] = { hi, hi, hi, hi };
   xe_ucode_alu_src olo = { false, xe_add_const(c, vlo, 1), 0x00, false };
   xe_ucode_alu_src ohi = { false, xe_add_const(c, vhi, 1), 0x00, false };
   uint32_t t = xe_alloc_temp(c);
   xe_emit_alu(c, XE_UCODE_ALU_MAX, t, 0xF, src, &olo, &xe_op_zero,
               UINT32_MAX);
   xe_emit_alu(c, XE_UCODE_ALU_MIN, dst_gpr, 0xF, &(xe_ucode_alu_src){ false, t,
               XE_UCODE_ALU_SWIZ_XYZW, false }, &ohi, &xe_op_zero,
               UINT32_MAX);
}

/* Per-component reciprocal/rsqrt/sqrt using the scalar unit: 4 slots, one
 * component each (scalar ops read component 3 of the src swizzle). */
static void
xe_emit_scalar_op(struct xe_cctx *c, uint32_t scalar_opc, uint32_t dst_gpr,
                  const xe_ucode_alu_src *src)
{
   uint32_t t = dst_gpr;
   for (unsigned i = 0; i < 4; i++) {
      xe_ucode_alu_src s = *src;
      /* Scalar unit reads the source through lane 3 of the swizzle:
       * abs = (rel[3] + 3) & 3 -> rel[3] = (abs + 1) & 3. */
      unsigned abs_i = (((s.swiz >> (2 * i)) & 3) + i) & 3;
      s.swiz = (s.swiz & 0x3Fu) | (((abs_i + 1u) & 3u) << 6);
      xe_ucode_alu alu;
      memset(&alu, 0, sizeof(alu));
      alu.opc = XE_UCODE_ALU_MAX; /* vector part: nop-ish write to t */
      alu.write_mask = 0;
      alu.dst = t;
      alu.src[0] = s;
      alu.src[1] = s;
      alu.scalar_opc = scalar_opc;
      alu.scalar_mask = 1u << i;
      alu.scalar_dst = t;
      alu.scalar_clamp = false;
      uint32_t slot[3];
      xe_ucode_alu_build(slot, &alu);
      xe_emit_slot(c, slot, false);
   }
}

static void
xe_vfetch(struct xe_cctx *c, uint32_t attrib, uint32_t dst_gpr)
{
   uint32_t slot[3];
   /* Format/stride/offset are patched at draw time; use 4x float32 + zero
    * stride here. */
   xe_ucode_vfetch(slot, attrib, dst_gpr, XE_UCODE_DST_SWIZ_XYZW,
                   0 /* r0.x = auto vertex index */, 0,
                   XE_UCODE_FORMAT_32_32_32_32_FLOAT, 0, 0, true, true);
   uint32_t slot_idx = c->num_slots;
   if (c->vfetch_count < 16) {
      c->vfetch[c->vfetch_count].ucode_dword = 3 + 3 * slot_idx;
      c->vfetch[c->vfetch_count].attrib = attrib;
      c->vfetch_count++;
   } else {
      c->failed = true;
   }
   xe_emit_slot(c, slot, true);
}

static void
xe_def_set(struct xe_cctx *c, nir_def *def, xe_ucode_alu_src op)
{
   if (def->index >= c->num_defs) {
      c->failed = true;
      return;
   }
   c->defs[def->index] = op;
   c->def_set[def->index] = 1;
}

/* Turn a nir_src into an operand: apply the source swizzle/negate/abs on top
 * of the def's operand.  abs materializes a new temp. */
static xe_operand
xe_src_op(struct xe_cctx *c, nir_src src, const uint8_t swizzle[4],
          bool negate, bool abs)
{
   xe_ucode_alu_src *d = xe_def(c, src.ssa);
   if (d && !c->def_set[src.ssa->index]) {
      /* Materialize load_const defs lazily so that immediates only used as
       * intrinsic offsets (store_output src[1], load_input src[1], ...) do
       * not burn a constant slot. */
      nir_instr *pi = (nir_instr *)((char *)src.ssa -
                                   offsetof(nir_load_const_instr, def));
      if (pi && pi->type == nir_instr_type_load_const)
         xe_def_set(c, src.ssa, xe_imm_op(c, nir_instr_as_load_const(pi)));
      else
         c->failed = true;
   }
   d = xe_def(c, src.ssa);
   if (!d || !c->def_set[src.ssa->index]) {
      c->failed = true;
      return xe_op_zero;
   }
   xe_ucode_alu_src op = *d;
   if (swizzle) {
      uint32_t s8 = 0;
      for (unsigned i = 0; i < 4; i++)
         s8 |= (uint32_t)(swizzle[i] & 3) << (2 * i);
      op.swiz = xe_swiz_compose(s8, op.swiz);
   }
   if (negate)
      op.negate = !op.negate;
   if (abs) {
      uint32_t t = xe_alloc_temp(c);
      xe_emit_abs(c, t, &op);
      op = (xe_ucode_alu_src){ true, t, XE_UCODE_ALU_SWIZ_XYZW, false };
   }
   return op;
}

static xe_operand
xe_alu_src_op(struct xe_cctx *c, nir_alu_src src)
{
   return xe_src_op(c, src.src, src.swizzle, false, false);
}

static void
xe_emit_vs_vfetch(struct xe_cctx *c, nir_intrinsic_instr *intr)
{
   uint32_t attrib = nir_intrinsic_io_semantics(intr).location;
   uint32_t gpr = attrib; /* attrib i -> GPR i */
   if (gpr + 1 > c->max_gpr)
      c->max_gpr = gpr + 1;
   if (gpr >= c->next_temp_gpr)
      c->next_temp_gpr = gpr + 1;
   xe_vfetch(c, attrib, gpr);
   xe_def_set(c, &intr->def, (xe_ucode_alu_src){ true, gpr,
               XE_UCODE_ALU_SWIZ_XYZW, false });
}

static void
xe_emit_fs_input(struct xe_cctx *c, nir_intrinsic_instr *intr)
{
   uint32_t varying = nir_intrinsic_io_semantics(intr).location;
   uint32_t gpr = varying; /* interpolator i -> GPR i */
   if (gpr + 1 > c->max_gpr)
      c->max_gpr = gpr + 1;
   if (gpr >= c->next_temp_gpr)
      c->next_temp_gpr = gpr + 1;
   c->varying_mask |= 1u << (varying & 0x1F);
   xe_def_set(c, &intr->def, (xe_ucode_alu_src){ true, gpr,
               XE_UCODE_ALU_SWIZ_XYZW, false });
}

static void
xe_emit_store_output(struct xe_cctx *c, nir_intrinsic_instr *intr)
{
   xe_ucode_alu_src val = xe_src_op(c, intr->src[0], NULL, false, false);
   uint32_t slot = nir_intrinsic_io_semantics(intr).location;
   uint32_t export_reg;
   if (c->stage == MESA_SHADER_VERTEX) {
      switch (slot) {
      case VARYING_SLOT_POS:  export_reg = XE_UCODE_EXP_VS_POSITION; break;
      case VARYING_SLOT_PSIZ: export_reg = XE_UCODE_EXP_VS_POINT_SIZE; break;
      default:
         export_reg = slot & 0x3F; /* interpolator export = slot */
         if (slot < 16)
            c->varying_mask |= 1u << slot;
         else
            c->failed = true;
         break;
      }
   } else {
      switch (slot) {
      case FRAG_RESULT_COLOR: export_reg = XE_UCODE_EXP_PS_COLOR0; break;
      case FRAG_RESULT_DEPTH: export_reg = XE_UCODE_EXP_PS_DEPTH; break;
      default:                export_reg = slot & 0x3F; break;
      }
   }
   /* Final export: max t, t with export_data.  Partial write masks export
    * 0 for the missing components (hardware behaviour). */
   uint32_t t = xe_alloc_temp(c);
   xe_emit_alu(c, XE_UCODE_ALU_MAX, t, nir_intrinsic_write_mask(intr),
               &val, &val, &xe_op_zero, export_reg);
}

/* Uniform constant: uniforms live at their driver_location index. */
static void
xe_emit_uniform(struct xe_cctx *c, nir_def *def, uint32_t const_idx)
{
   if (const_idx >= XE_HELPER_CONST_BASE) {
      c->failed = true;
      return;
   }
   if (const_idx + 1 > c->num_consts)
      c->num_consts = const_idx + 1;
   if (const_idx >= c->next_imm_const)
      c->next_imm_const = const_idx + 1;
   xe_def_set(c, def, (xe_ucode_alu_src){ false, const_idx,
            XE_UCODE_ALU_SWIZ_XYZW, false });
}

static void
xe_emit_intrinsic(struct xe_cctx *c, nir_intrinsic_instr *intr)
{
   switch (intr->intrinsic) {
   case nir_intrinsic_load_input:
      if (c->stage == MESA_SHADER_VERTEX) {
         xe_emit_vs_vfetch(c, intr);
         return;
      }
      break;

   case nir_intrinsic_load_interpolated_input:
      if (c->stage == MESA_SHADER_FRAGMENT) {
         xe_emit_fs_input(c, intr);
         return;
      }
      break;

   case nir_intrinsic_load_barycentric_pixel:
      /* Xenos interpolates automatically; the value is not needed. */
      xe_def_set(c, &intr->def, xe_op_zero);
      return;

   case nir_intrinsic_store_output:
      xe_emit_store_output(c, intr);
      return;

   case nir_intrinsic_load_uniform: {
      /* base = driver_location (vec4 index), src0 = offset within the var
       * (vec4 units). */
      nir_const_value *off = nir_src_as_const_value(intr->src[0]);
      if (!off) {
         c->failed = true;
         return;
      }
      uint32_t idx = nir_intrinsic_base(intr) + off[0].u32;
      xe_emit_uniform(c, &intr->def, idx);
      return;
   }

   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_ubo_vec4: {
      nir_const_value *ubo = nir_src_as_const_value(intr->src[0]);
      if (!ubo || ubo[0].u32 != 0) {
         c->failed = true;
         return;
      }
      nir_const_value *off = nir_src_as_const_value(intr->src[1]);
      if (!off) {
         c->failed = true;
         return;
      }
      uint32_t idx;
      if (intr->intrinsic == nir_intrinsic_load_ubo_vec4)
         idx = nir_intrinsic_base(intr) + off[0].u32;
      else
         idx = off[0].u32 / 16; /* byte offset, incl. the folded base */
      xe_emit_uniform(c, &intr->def, idx);
      return;
   }

   case nir_intrinsic_load_deref: {
      nir_variable *var = nir_intrinsic_get_var(intr, 0);
      if (var && (var->data.mode & nir_var_uniform)) {
         xe_emit_uniform(c, &intr->def, var->data.driver_location);
         return;
      }
      break;
   }

   case nir_intrinsic_demote:
   case nir_intrinsic_demote_if:
   case nir_intrinsic_terminate:
   case nir_intrinsic_terminate_if:
      fprintf(stderr, "xenos: %s: discard not supported yet\n",
              c->stage == MESA_SHADER_VERTEX ? "VS" : "FS");
      c->failed = true;
      return;

   default:
      break;
   }

   fprintf(stderr, "xenos: %s: unsupported intrinsic #%u\n",
           c->stage == MESA_SHADER_VERTEX ? "VS" : "FS",
           (unsigned)intr->intrinsic);
   c->failed = true;
}

static void
xe_emit_alu_instr(struct xe_cctx *c, nir_alu_instr *alu)
{
   xe_ucode_alu_src s[3];
   unsigned nsrc = nir_op_infos[alu->op].num_inputs;
   for (unsigned i = 0; i < 3; i++) {
      if (i < nsrc)
         s[i] = xe_alu_src_op(c, alu->src[i]);
      else
         s[i] = xe_op_zero;
   }

   uint32_t tmp = 0, opc = XE_UCODE_ALU_MAX, mask = 0xF;
   xe_ucode_alu_src result = xe_op_zero;

   switch (alu->op) {
   case nir_op_mov:
      /* Fold into the def: aliases the source operand (swizzle composed). */
      xe_def_set(c, &alu->def, s[0]);
      return;

   case nir_op_fneg:
      s[0].negate = !s[0].negate;
      xe_def_set(c, &alu->def, s[0]);
      return;

   case nir_op_fabs:
      tmp = xe_alloc_temp(c);
      xe_emit_abs(c, tmp, &s[0]);
      break;

   case nir_op_fmul:  opc = XE_UCODE_ALU_MUL;  break;
   case nir_op_fadd:  opc = XE_UCODE_ALU_ADD;  break;
   case nir_op_fsub:
      s[1].negate = !s[1].negate;
      opc = XE_UCODE_ALU_ADD;
      break;
   case nir_op_fmin:  opc = XE_UCODE_ALU_MIN;  break;
   case nir_op_fmax:  opc = XE_UCODE_ALU_MAX;  break;
   case nir_op_ffma:
   case nir_op_ffma_weak:
   case nir_op_fmad:  opc = XE_UCODE_ALU_MAD;  break;
   case nir_op_fdot3: opc = XE_UCODE_ALU_DP3;  break;
   case nir_op_fdot4: opc = XE_UCODE_ALU_DP4;  break;
   case nir_op_fsat:  tmp = xe_alloc_temp(c);
                      xe_emit_clamp(c, tmp, &s[0], 0.0f, 1.0f);
                      break;
   case nir_op_frcp:
      tmp = xe_alloc_temp(c);
      xe_emit_scalar_op(c, XE_UCODE_SCALAR_RCP, tmp, &s[0]);
      break;
   case nir_op_frsq:
      tmp = xe_alloc_temp(c);
      xe_emit_scalar_op(c, XE_UCODE_SCALAR_RSQ, tmp, &s[0]);
      break;
   case nir_op_fsqrt:
      tmp = xe_alloc_temp(c);
      xe_emit_scalar_op(c, XE_UCODE_SCALAR_SQRT, tmp, &s[0]);
      break;
   case nir_op_ffloor: opc = XE_UCODE_ALU_FLOOR; break;
   case nir_op_ffract: opc = XE_UCODE_ALU_FRC;   break;

   case nir_op_vec2:
   case nir_op_vec3:
   case nir_op_vec4: {
      /* Materialize: one per-component mov into a temp GPR.  Also covers
       * vector_insert_imm, which is built as a vecN with a scalar src. */
      unsigned ncomp = nir_op_infos[alu->op].output_size;
      tmp = xe_alloc_temp(c);
      for (unsigned i = 0; i < ncomp; i++) {
         xe_ucode_alu_src v = s[i];
         v.swiz = xe_swiz_splat(v.swiz & 3u); /* scalar src: its comp 0 */
         xe_emit_mov(c, tmp, 1u << i, &v);
      }
      break;
   }

   default:
      fprintf(stderr, "xenos: %s: unsupported ALU op %s\n",
              c->stage == MESA_SHADER_VERTEX ? "VS" : "FS",
              nir_op_infos[alu->op].name);
      c->failed = true;
      return;
   }

   if (opc != XE_UCODE_ALU_MAX || alu->op == nir_op_fmax) {
      tmp = xe_alloc_temp(c);
      if (alu->op == nir_op_fdot3 || alu->op == nir_op_fdot4)
         mask = 0xF;
      else
         mask = alu->def.num_components == 1 ? 0x1 : 0xF;
      xe_emit_alu(c, opc, tmp, mask, &s[0], &s[1], &s[2], UINT32_MAX);
   }

   if (alu->def.num_components == 1) {
      result = (xe_ucode_alu_src){ true, tmp, xe_swiz_splat(0u), false };
   } else {
      result = (xe_ucode_alu_src){ true, tmp, XE_UCODE_ALU_SWIZ_XYZW, false };
   }
   xe_def_set(c, &alu->def, result);
}

static void
xe_compile_block(struct xe_cctx *c, nir_block *block)
{
   nir_foreach_instr (instr, block) {
      switch (instr->type) {
      case nir_instr_type_intrinsic:
         xe_emit_intrinsic(c, nir_instr_as_intrinsic(instr));
         break;
      case nir_instr_type_alu:
         xe_emit_alu_instr(c, nir_instr_as_alu(instr));
         break;
      case nir_instr_type_undef:
         xe_def_set(c, &nir_instr_as_undef(instr)->def, xe_op_zero);
         break;
      case nir_instr_type_phi:
         fprintf(stderr, "xenos: %s: control flow not supported\n",
                 c->stage == MESA_SHADER_VERTEX ? "VS" : "FS");
         c->failed = true;
         break;
      default:
         break; /* nop / jump / deref / parallel_copy: no code */
      }
      if (c->failed)
         return;
   }
}

static struct xenos_shader *
xenos_compile(struct nir_shader *nir)
{
   struct xenos_shader *shader = NULL;
   struct xe_cctx c;
   memset(&c, 0, sizeof(c));
   c.nir = nir;
   c.stage = nir->info.stage;

   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   nir_index_instrs(impl);
   c.num_defs = impl->ssa_alloc;
   c.defs = calloc(c.num_defs ? c.num_defs : 1, sizeof(*c.defs));
   c.def_set = calloc(c.num_defs ? c.num_defs : 1, 1);
   if (!c.defs || !c.def_set) {
free(c.defs);
   free(c.def_set);
      free(c.def_set);
      return NULL;
   }

   nir_foreach_block (block, impl) {
      xe_compile_block(&c, block);
      if (c.failed)
         break;
   }

   if (!c.failed) {
      if (c.next_temp_gpr == 0)
         c.next_temp_gpr = 1; /* at least 1 GPR */
      c.max_gpr = MAX2(c.max_gpr, c.next_temp_gpr);
   }

   if (!c.failed) {
      uint32_t total = 3 + 3 * c.num_slots;
      shader = CALLOC_STRUCT(xenos_shader);
      if (shader) {
         shader->type = c.stage;
         shader->num_slots = c.num_slots;
         shader->ucode_dwords = total;
         shader->ucode = malloc(total * sizeof(uint32_t));
         if (!shader->ucode) {
            FREE(shader);
            shader = NULL;
         }
      }
   }

   if (shader) {
      uint32_t cf[2];
      xe_ucode_cf_exec(cf, 1, c.num_slots, c.sequence, XE_UCODE_CF_EXEC_END);
      xe_ucode_cf_emit_pair(shader->ucode, 0, 0, cf[0], cf[1]);
      memcpy(shader->ucode + 3, c.ucode + 3, 3 * c.num_slots *
             sizeof(uint32_t));
      shader->num_gprs = c.max_gpr;
      shader->num_consts = c.num_consts;
      shader->num_inputs = util_bitcount(nir->info.inputs_read);
      shader->num_outputs = util_bitcount(nir->info.outputs_written);
      shader->varying_mask = c.varying_mask;
      memcpy(shader->vfetch, c.vfetch,
             c.vfetch_count * sizeof(*c.vfetch));
      shader->vfetch_count = c.vfetch_count;
   }

   free(c.defs);

   if (!shader)
      fprintf(stderr, "xenos: %s shader compile FAILED\n",
              c.stage == MESA_SHADER_VERTEX ? "VS" : "FS");

   return shader;
}

struct xenos_shader *
xenos_create_shader(struct pipe_screen *screen,
                    const struct pipe_shader_state *state)
{
   if (state->type != PIPE_SHADER_IR_NIR)
      return NULL;

   struct nir_shader *nir = state->ir.nir;
   if (nir->info.stage != MESA_SHADER_VERTEX &&
       nir->info.stage != MESA_SHADER_FRAGMENT)
      return NULL;

   struct xenos_shader *shader = xenos_compile(nir);
   if (shader) {
      fprintf(stderr, "xenos: compiled %s (%u inputs, %u outputs, %u slots, "
              "%u gprs, %u consts)\n",
              shader->type == MESA_SHADER_VERTEX ? "VS" : "FS",
              shader->num_inputs, shader->num_outputs, shader->num_slots,
              shader->num_gprs, shader->num_consts);
   }

   return shader;
}

struct xenos_shader *
xenos_compile_nir(struct nir_shader *nir)
{
   if (!nir || (nir->info.stage != MESA_SHADER_VERTEX &&
                nir->info.stage != MESA_SHADER_FRAGMENT))
      return NULL;
   return xenos_compile(nir);
}

void
xenos_delete_shader(struct xenos_shader *shader)
{
   if (!shader)
      return;
   free(shader->ucode);
   FREE(shader);
}