/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "test_helpers.h"
#include "brw_builder.h"

class lower_simd_width_test : public brw_shader_pass_test {
};

TEST_F(lower_simd_width_test, neg_abs_src)
{
   set_gfx_platform("tgl");

   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 32);
   brw_builder exp = make_shader(MESA_SHADER_FRAGMENT, 32);

   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src2 = vgrf(bld, exp, BRW_TYPE_F);

   brw_inst *inst;

   inst = bld.MAD(src0, src0, src1, src2);
   inst->src[0].negate = true;
   inst->src[0].abs = true;

   EXPECT_PROGRESS(brw_lower_simd_width, bld);

   inst = exp.group(16, 0).MAD(src0, src0, src1, src2);
   inst->src[0].negate = true;
   inst->src[0].abs = true;

   inst = exp.group(16, 1).MAD(byte_offset(src0, 16 * 4),
                               byte_offset(src0, 16 * 4),
                               byte_offset(src1, 16 * 4),
                               byte_offset(src2, 16 * 4));
   inst->src[0].negate = true;
   inst->src[0].abs = true;

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(lower_simd_width_test, gfx9_simd32_hf_mad)
{
   set_gfx_platform("skl");

   brw_builder bld = make_shader(MESA_SHADER_COMPUTE, 32);
   brw_builder exp = make_shader(MESA_SHADER_COMPUTE, 32);

   brw_reg dst  = vgrf(bld, exp, BRW_TYPE_HF);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_HF);
   brw_reg src1 = vgrf(bld, exp, BRW_TYPE_HF);
   brw_reg src2 = vgrf(bld, exp, BRW_TYPE_HF);

   bld.MAD(dst, src0, src1, src2);

   EXPECT_PROGRESS(brw_lower_simd_width, bld);

   exp.group(16, 0).MAD(dst, src0, src1, src2);
   exp.group(16, 1).MAD(byte_offset(dst, 16 * 2),
                        byte_offset(src0, 16 * 2),
                        byte_offset(src1, 16 * 2),
                        byte_offset(src2, 16 * 2));

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(lower_simd_width_test, gfx9_simd32_hf_mad_nomask)
{
   set_gfx_platform("skl");

   brw_builder bld = make_shader(MESA_SHADER_COMPUTE, 32);
   brw_builder exp = make_shader(MESA_SHADER_COMPUTE, 32);

   brw_reg dst  = vgrf(bld, exp, BRW_TYPE_HF);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_HF);
   brw_reg src1 = vgrf(bld, exp, BRW_TYPE_HF);
   brw_reg src2 = vgrf(bld, exp, BRW_TYPE_HF);

   bld.exec_all().MAD(dst, src0, src1, src2);

   EXPECT_PROGRESS(brw_lower_simd_width, bld);

   exp.exec_all().group(16, 0).MAD(dst, src0, src1, src2);
   exp.exec_all().group(16, 1).MAD(byte_offset(dst, 16 * 2),
                                   byte_offset(src0, 16 * 2),
                                   byte_offset(src1, 16 * 2),
                                   byte_offset(src2, 16 * 2));

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(lower_simd_width_test, gfx9_simd16_hf_mad)
{
   set_gfx_platform("skl");

   brw_builder bld = make_shader(MESA_SHADER_COMPUTE, 16);

   brw_reg dst  = vgrf(bld, BRW_TYPE_HF);
   brw_reg src0 = vgrf(bld, BRW_TYPE_HF);
   brw_reg src1 = vgrf(bld, BRW_TYPE_HF);
   brw_reg src2 = vgrf(bld, BRW_TYPE_HF);

   bld.MAD(dst, src0, src1, src2);

   EXPECT_NO_PROGRESS(brw_lower_simd_width, bld);
}

TEST_F(lower_simd_width_test, gfx11_simd32_hf_mad)
{
   set_gfx_platform("icl");

   brw_builder bld = make_shader(MESA_SHADER_COMPUTE, 32);

   brw_reg dst  = vgrf(bld, BRW_TYPE_HF);
   brw_reg src0 = vgrf(bld, BRW_TYPE_HF);
   brw_reg src1 = vgrf(bld, BRW_TYPE_HF);
   brw_reg src2 = vgrf(bld, BRW_TYPE_HF);

   bld.MAD(dst, src0, src1, src2);

   EXPECT_NO_PROGRESS(brw_lower_simd_width, bld);
}
