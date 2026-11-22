Native code for unnamed compute shader  (src_hash 0x35389274fdcafee2) (blake3 ac21df2ed6c0e8185584e3e95a35906825b1f33eaf1e86c4c040408144783a2f)
SIMD8 shader: 27 instructions. 0 loops. 292 cycles. 0:0 spills:fills, 6 sends, scheduled with mode top-down. Promoted 0 constants. GRF registers: 127. Compacted 432 to 320 bytes (26%)
(W)     mov (8)                   r28           r0.1<0>                           {Compacted}
(W)     shl (8)                   r2:d          r12.4<0>:d        0x00000003
(W)     mov (8)                   r126          r0                                {Compacted}
(W)     add (8)                   r5:d          r28<0>:d          r1.5<0>:d       {Compacted}
(W)     mov (8)                   r3:uw         0x76543210:v
(W)     shl (8)                   r6:d          r5<0>:d           0x00000006
(W)     mov (8)                   r3            r3:uw
        add (8)                   r4:d          r3:d              r2<0>:d         {Compacted}
        and (8)                   r7            r4                0x0000003f      {Compacted}
        add (8)                   r8:d          r6<0>:d           r7:d            {Compacted}
        shl (8)                   r9:d          r8:d              0x00000004
        shl (8)                   r27:d         r8:d              0x00000002
        sends.hdc1 (8)            r10     r9     null    0x00000000  0x02406002  // wr:1+0, rd:4; untyped_read:xyzw simd8 (8) bti(2)
        sends.hdc1 (8)            r14     r9     null    0x00000000  0x02406001  // wr:1+0, rd:4; untyped_read:xyzw simd8 (8) bti(1)
        add (8)                   r18:d         r9:d              1024:d          {Compacted}
        sends.hdc1 (8)            r19     r18    null    0x00000000  0x02406001  // wr:1+0, rd:4; untyped_read:xyzw simd8 (8) bti(1)
        sends.hdc1 (8)            r23     r18    null    0x00000000  0x02406002  // wr:1+0, rd:4; untyped_read:xyzw simd8 (8) bti(2)
        mul (8)                   acc0:f        r22:f             r26:f           {Compacted}
        mac (8)                   acc0:f        r25:f             r21:f           {Compacted}
        mac (8)                   acc0:f        r24:f             r20:f           {Compacted}
        mac (8)                   acc0:f        r23:f             r19:f           {Compacted}
        mac (8)                   acc0:f        r13:f             r17:f           {Compacted}
        mac (8)                   acc0:f        r12:f             r16:f           {Compacted}
        mac (8)                   acc0:f        r11:f             r15:f           {Compacted}
        mac (8)                   r29:f         r10:f             r14:f           {Compacted}
        sends.hdc1 (8)            null    r27    r29     0x00000040  0x02026E03  // wr:1+1, rd:0; untyped_write:x simd8 (8) bti(3)
(W)     sends.ts (8)              null    r126   null    0x00000000  0x02000010   {EOT}
  tile 0 [0][0]: got -997214811 want 174
  tile 0 [0][1]: got -64001 want -331
  tile 0 [0][2]: got 2139095040 want 44
  tile 0 [0][3]: got 2139095040 want 207
signed int8, 2688 tiles x 1 iterations: 172032 wrong of 172032; GPU median 0.106 ms (min 0.106, max 0.106), 51.77 GMAC/s
