/*
 * Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

/**
 * @file
 * Chroma reconstruction for packed 4:2:2 formats.
 *
 * VK_FORMAT_G16B16G16R16_422_UNORM and VK_FORMAT_B16G16R16G16_422_UNORM:
 *
 * The sampler has no 16-bit packed 4:2:2 format.  These images are stored
 * as R16G16_UNORM at full width: a pixel pair (G0 B G1 R) is two R16G16
 * pixels (G0, B) and (G1, R), or (B, G0) and (R, G1) for the B-first
 * format.  Luma is then one channel at every pixel, so the hardware sample
 * that nir_vk_lower_ycbcr_tex() emits already filters it correctly.  The
 * other channel alternates Cb and Cr, so chroma is rebuilt here from texel
 * fetches on the half-width chroma grid, honouring the conversion's chroma
 * location and chroma filter.
 *
 * VK_FORMAT_G8B8G8R8_422_UNORM and VK_FORMAT_B8G8R8G8_422_UNORM use the
 * sampler's YCRCB formats, whose linear filtering of chroma does not match
 * Vulkan's implicit reconstruction (it fails CTS).  With a linear chroma
 * filter, luma still comes from the hardware sample, and chroma is rebuilt
 * from point samples at pixel 2k, which return pair k's Cb and Cr.
 *
 * Runs after nir_vk_lower_ycbcr_tex(), which leaves one plane-0 sample per
 * conversion and expects it as (Cb, Y, Cr) in channels 0-2 for the 16-bit
 * formats (the runtime's YCBCR_SWIZ(B, G, R)) and as (Cr, Y, Cb) for the
 * 8-bit ones (YCBCR_SWIZ(R, G, B)).
 */

#include "anv_nir.h"
#include "nir_builder.h"
#include "vk_nir_convert_ycbcr.h"
#include "vk_ycbcr_conversion.h"

struct lower_packed_422_state {
   nir_vk_ycbcr_conversion_lookup_cb cb;
   const void *cb_data;
};

static const struct vk_ycbcr_conversion_state *
tex_conversion(const nir_tex_instr *tex,
               const struct lower_packed_422_state *state)
{
   if (tex->embedded_sampler) {
      return state->cb(state->cb_data, VK_NIR_YCBCR_SET_IMMUTABLE_SAMPLERS,
                       tex->sampler_index, 0);
   }

   const int deref_idx = nir_tex_instr_src_index(tex, nir_tex_src_texture_deref);
   if (deref_idx < 0)
      return NULL;

   nir_deref_instr *deref = nir_src_as_deref(tex->src[deref_idx].src);
   nir_variable *var = nir_deref_instr_get_variable(deref);
   unsigned array_index = 0;
   if (deref->deref_type != nir_deref_type_var) {
      if (!nir_src_is_const(deref->arr.index))
         return NULL;
      array_index = nir_src_as_uint(deref->arr.index);
   }

   return state->cb(state->cb_data, var->data.descriptor_set,
                    var->data.binding, array_index);
}

static bool
is_texture_src(nir_tex_src_type type)
{
   return type == nir_tex_src_texture_deref ||
          type == nir_tex_src_texture_offset ||
          type == nir_tex_src_texture_handle ||
          type == nir_tex_src_texture_heap_offset ||
          type == nir_tex_src_plane;
}

/* A txs or txf on the same texture and plane as \p orig, at LOD 0. */
static nir_tex_instr *
texture_op(nir_builder *b, const nir_tex_instr *orig, nir_texop op,
           nir_def *coord)
{
   unsigned num_srcs = 1 + (coord != NULL);
   for (unsigned i = 0; i < orig->num_srcs; i++)
      num_srcs += is_texture_src(orig->src[i].src_type);

   nir_tex_instr *tex = nir_tex_instr_create(b->shader, num_srcs);
   unsigned s = 0;
   for (unsigned i = 0; i < orig->num_srcs; i++) {
      if (is_texture_src(orig->src[i].src_type)) {
         tex->src[s].src_type = orig->src[i].src_type;
         tex->src[s].src = nir_src_for_ssa(orig->src[i].src.ssa);
         s++;
      }
   }
   tex->src[s++] = nir_tex_src_for_ssa(nir_tex_src_lod, nir_imm_int(b, 0));
   if (coord)
      tex->src[s++] = nir_tex_src_for_ssa(nir_tex_src_coord, coord);
   assert(s == num_srcs);

   tex->op = op;
   tex->sampler_dim = orig->sampler_dim;
   tex->is_array = orig->is_array;
   tex->texture_index = orig->texture_index;
   tex->sampler_index = orig->sampler_index;
   tex->embedded_sampler = orig->embedded_sampler;
   tex->coord_components = coord ? coord->num_components : 0;

   if (op == nir_texop_txs) {
      tex->dest_type = nir_type_int32;
      nir_def_init(&tex->instr, &tex->def, 2 + orig->is_array, 32);
   } else {
      tex->dest_type = nir_type_float32;
      nir_def_init(&tex->instr, &tex->def, 4, 32);
   }
   nir_builder_instr_insert(b, &tex->instr);
   return tex;
}

struct packed_422_layout {
   /* Chroma alternates Cb/Cr between pixels 2k and 2k + 1 in this channel;
    * otherwise a point sample of pixel 2k returns both.
    */
   bool alternating;
   unsigned luma_chan, chroma_chan, cb_chan, cr_chan;
   /* Output channels of Cb and Cr; luma is always channel 1. */
   unsigned out_cb, out_cr;
};

/* Cb and Cr of chroma sample (x, y). */
static void
fetch_chroma(nir_builder *b, const nir_tex_instr *orig,
             const struct packed_422_layout *layout, nir_def *x,
             nir_def *y, nir_def *layer, nir_def **cb, nir_def **cr)
{
   nir_def *px = nir_ishl_imm(b, x, 1);
   if (!layout->alternating) {
      nir_def *comps[3] = { px, y, layer };
      nir_def *coord = nir_vec(b, comps, layer ? 3 : 2);
      nir_tex_instr *txf = texture_op(b, orig, nir_texop_txf, coord);
      *cb = nir_channel(b, &txf->def, layout->cb_chan);
      *cr = nir_channel(b, &txf->def, layout->cr_chan);
      return;
   }

   const unsigned chroma_chan = layout->chroma_chan;
   for (unsigned i = 0; i < 2; i++) {
      nir_def *comps[3] = { nir_iadd_imm(b, px, i), y, layer };
      nir_def *coord = nir_vec(b, comps, layer ? 3 : 2);
      nir_tex_instr *txf = texture_op(b, orig, nir_texop_txf, coord);
      nir_def *v = nir_channel(b, &txf->def, chroma_chan);
      if (i == 0)
         *cb = v;
      else
         *cr = v;
   }
}

static bool
lower_packed_422_tex(nir_builder *b, nir_tex_instr *tex, void *_state)
{
   const struct lower_packed_422_state *state = _state;

   if (nir_tex_instr_src_index(tex, nir_tex_src_plane) < 0)
      return false;
   if (tex->op != nir_texop_tex && tex->op != nir_texop_txb &&
       tex->op != nir_texop_txl && tex->op != nir_texop_txd)
      return false;
   if (tex->sampler_dim != GLSL_SAMPLER_DIM_2D)
      return false;

   const struct vk_ycbcr_conversion_state *conversion =
      tex_conversion(tex, state);
   if (conversion == NULL)
      return false;

   struct packed_422_layout layout;
   switch (conversion->format) {
   case VK_FORMAT_G16B16G16R16_422_UNORM:
   case VK_FORMAT_B16G16R16G16_422_UNORM: {
      const bool g_first =
         conversion->format == VK_FORMAT_G16B16G16R16_422_UNORM;
      layout = (struct packed_422_layout) {
         .alternating = true,
         .luma_chan = g_first ? 0 : 1,
         .chroma_chan = g_first ? 1 : 0,
         .out_cb = 0, .out_cr = 2,
      };
      break;
   }
   case VK_FORMAT_G8B8G8R8_422_UNORM:
   case VK_FORMAT_B8G8R8G8_422_UNORM:
      /* The hardware reconstructs nearest chroma correctly. */
      if (conversion->chroma_filter != VK_FILTER_LINEAR)
         return false;
      layout = (struct packed_422_layout) {
         .alternating = false,
         .luma_chan = 1, .cb_chan = 2, .cr_chan = 0,
         .out_cb = 2, .out_cr = 0,
      };
      break;
   default:
      return false;
   }

   b->cursor = nir_after_instr(&tex->instr);

   nir_def *luma = nir_channel(b, &tex->def, layout.luma_chan);
   nir_def *coord =
      tex->src[nir_tex_instr_src_index(tex, nir_tex_src_coord)].src.ssa;

   nir_def *size = &texture_op(b, tex, nir_texop_txs, NULL)->def;
   nir_def *chroma_w = nir_ushr_imm(b, nir_channel(b, size, 0), 1);
   nir_def *h = nir_channel(b, size, 1);
   nir_def *max_x = nir_iadd_imm(b, chroma_w, -1);
   nir_def *max_y = nir_iadd_imm(b, h, -1);

   nir_def *layer = NULL;
   if (tex->is_array) {
      layer = nir_f2i32(b, nir_fround_even(b, nir_channel(b, coord, 2)));
      layer = nir_imin(b, nir_imax(b, layer, nir_imm_int(b, 0)),
                       nir_iadd_imm(b, nir_channel(b, size, 2), -1));
   }

   /* Position in chroma texels.  A midpoint chroma sample k covers luma
    * pixels 2k and 2k + 1; a cosited one is centred on pixel 2k, a quarter
    * of a chroma texel to the left.
    */
   nir_def *cx = nir_fmul(b, nir_channel(b, coord, 0), nir_i2f32(b, chroma_w));
   if (conversion->chroma_offsets[0] == VK_CHROMA_LOCATION_COSITED_EVEN)
      cx = nir_fadd_imm(b, cx, 0.25);
   nir_def *cy = nir_fmul(b, nir_channel(b, coord, 1), nir_i2f32(b, h));

   nir_def *cb, *cr;
   if (conversion->chroma_filter == VK_FILTER_NEAREST) {
      nir_def *x = nir_iclamp(b, nir_f2i32(b, nir_ffloor(b, cx)),
                              nir_imm_int(b, 0), max_x);
      nir_def *y = nir_iclamp(b, nir_f2i32(b, nir_ffloor(b, cy)),
                              nir_imm_int(b, 0), max_y);
      fetch_chroma(b, tex, &layout, x, y, layer, &cb, &cr);
   } else {
      nir_def *px = nir_fadd_imm(b, cx, -0.5);
      nir_def *py = nir_fadd_imm(b, cy, -0.5);
      nir_def *fx = nir_ffract(b, px);
      nir_def *fy = nir_ffract(b, py);
      nir_def *x0 = nir_f2i32(b, nir_ffloor(b, px));
      nir_def *y0 = nir_f2i32(b, nir_ffloor(b, py));
      nir_def *xs[2], *ys[2];
      for (unsigned i = 0; i < 2; i++) {
         xs[i] = nir_iclamp(b, nir_iadd_imm(b, x0, i), nir_imm_int(b, 0), max_x);
         ys[i] = nir_iclamp(b, nir_iadd_imm(b, y0, i), nir_imm_int(b, 0), max_y);
      }

      nir_def *row_cb[2], *row_cr[2];
      for (unsigned j = 0; j < 2; j++) {
         nir_def *cb0, *cr0, *cb1, *cr1;
         fetch_chroma(b, tex, &layout, xs[0], ys[j], layer, &cb0, &cr0);
         fetch_chroma(b, tex, &layout, xs[1], ys[j], layer, &cb1, &cr1);
         row_cb[j] = nir_flrp(b, cb0, cb1, fx);
         row_cr[j] = nir_flrp(b, cr0, cr1, fx);
      }
      cb = nir_flrp(b, row_cb[0], row_cb[1], fy);
      cr = nir_flrp(b, row_cr[0], row_cr[1], fy);
   }

   nir_def *comps[4] = { NULL, luma, NULL, nir_imm_float(b, 1.0) };
   comps[layout.out_cb] = cb;
   comps[layout.out_cr] = cr;
   nir_def *result = nir_vec(b, comps, 4);
   nir_def_rewrite_uses_after(&tex->def, result);
   return true;
}

bool
anv_nir_lower_packed_422(nir_shader *shader,
                         nir_vk_ycbcr_conversion_lookup_cb cb,
                         const void *cb_data)
{
   struct lower_packed_422_state state = {
      .cb = cb,
      .cb_data = cb_data,
   };

   return nir_shader_tex_pass(shader, lower_packed_422_tex,
                              nir_metadata_control_flow, &state);
}
