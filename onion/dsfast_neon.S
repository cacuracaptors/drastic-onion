/* Fast Cortex-A7 NEON + PLD replacements for top 3D 4x memory/scalar kernels in DraStic r2.5.0.4.
 * All 5 routines are 100% bit-exact equivalents of the originals in drastic:
 *   - dsfast_resolve_bin_4x            (0x080c50fc)
 *   - dsfast_writeback_4x              (0x080c4a90)  [scalar ARM -> 8-px NEON]
 *   - dsfast_load_depth_4x             (0x080c49d0)
 *   - dsfast_load_depth_colors_id_4x   (0x080c4a18)
 *   - dsfast_writeback_all_pass_4x     (0x080c4b08)
 */
    .syntax unified
    .arch armv7-a
    .fpu neon-vfpv4
    .arm
    .text

/* void dsfast_resolve_bin_4x(u32 *dst, const u32 *src)
 * 32 rows x 512 u32s: deinterleaves [even, odd] pairs into dst[0..255] and dst[256..511]
 * masked with 0x1fffffff, advancing dst by 2048 B/row. Unrolled 32 words (64 B line/dest) + PLD. */
    .global dsfast_resolve_bin_4x
    .type dsfast_resolve_bin_4x, %function
    .p2align 4
dsfast_resolve_bin_4x:
    pld     [r1, #0]
    pld     [r1, #64]
    pld     [r1, #128]
    vmvn.i32 q15, #0xe0000000
    add     ip, r0, #1024
    mov     r3, #32
.Lrb_row:
    mov     r2, #16                 /* 16 x 32 u32 = 512 u32s/row */
.Lrb_col:
    pld     [r1, #192]
    pld     [r1, #256]
    vld2.32 {d0, d2},   [r1 :128]!
    vld2.32 {d1, d3},   [r1 :128]!
    vld2.32 {d4, d6},   [r1 :128]!
    vld2.32 {d5, d7},   [r1 :128]!
    vld2.32 {d16, d18}, [r1 :128]!
    vld2.32 {d17, d19}, [r1 :128]!
    vld2.32 {d20, d22}, [r1 :128]!
    vld2.32 {d21, d23}, [r1 :128]!
    vand    q0, q0, q15
    vand    q2, q2, q15
    vand    q8, q8, q15
    vand    q10, q10, q15
    vand    q1, q1, q15
    vand    q3, q3, q15
    vand    q9, q9, q15
    vand    q11, q11, q15
    /* Store full 64-byte line contiguously to r0, then full 64-byte line to ip */
    vst1.32 {d0-d1},   [r0 :128]!
    vst1.32 {d4-d5},   [r0 :128]!
    vst1.32 {d16-d17}, [r0 :128]!
    vst1.32 {d20-d21}, [r0 :128]!
    vst1.32 {d2-d3},   [ip :128]!
    vst1.32 {d6-d7},   [ip :128]!
    vst1.32 {d18-d19}, [ip :128]!
    vst1.32 {d22-d23}, [ip :128]!
    subs    r2, r2, #1
    bne     .Lrb_col
    add     r0, r0, #1024
    add     ip, ip, #1024
    subs    r3, r3, #1
    bne     .Lrb_row
    bx      lr
    .size dsfast_resolve_bin_4x, .-dsfast_resolve_bin_4x


/* void dsfast_writeback_4x(const void *rt, u32 *depth_tile, u32 *color_tile, u32 height,
 *                          u32 poly_id, const u32 *src_depth, const u32 *src_color, const u8 *src_mask) */
    .global dsfast_writeback_4x
    .type dsfast_writeback_4x, %function
    .p2align 4
dsfast_writeback_4x:
    push    {r4, r5, r6, r7, r8, r9, sl, fp, lr}
    ldr     ip, [sp, #36]           /* poly_id */
    add     r0, r0, #1408           /* &span_start[0] (0x580) */
    ldr     lr, [sp, #40]           /* src_depth */
    ldr     r4, [sp, #44]           /* src_color */
    lsl     ip, ip, #24
    ldr     r5, [sp, #48]           /* src_mask */
    vdup.32 q15, ip
.Lwb_row:
    ldrh    r6, [r0]                /* x_start */
    ldr     r7, [r0, #176]          /* width in low 16 bits (+0x630) */
    add     r0, r0, #4
    ubfx    r7, r7, #0, #16
    add     r8, r1, r6, lsl #2      /* dst_depth */
    add     r9, r2, r6, lsl #2      /* dst_color */
    pld     [r8, #0]
    pld     [r9, #0]
    pld     [r8, #2048]
    pld     [r9, #2048]
    pld     [r4, #0]
    pld     [lr, #0]
    subs    r7, r7, #8
    blt     .Lwb_tail_setup
.Lwb_neon:
    pld     [r4, #128]
    pld     [lr, #128]
    pld     [r8, #128]
    pld     [r9, #128]
    vld1.8  d0, [r5]!               /* 8 mask bytes */
    vld1.32 {d16-d19}, [r4]!        /* src_color (q8, q9) */
    vtst.8  d0, d0, d0              /* 0x00 / 0xff */
    vld1.32 {d20-d23}, [lr]!        /* src_depth (q10, q11) */
    vmovl.s8 q12, d0                /* 8 x s16 in d24..d25 */
    vld1.32 {d0-d3}, [r9]           /* dst_color (q0, q1) */
    vorr    q8, q8, q15
    vorr    q9, q9, q15
    vmovl.s16 q13, d25              /* 32-bit mask 4..7 (reads d25 first) */
    vmovl.s16 q12, d24              /* 32-bit mask 0..3 (overwrites d24..d25) */
    vbit    q0, q8, q12
    vbit    q1, q9, q13
    vld1.32 {d4-d7}, [r8]           /* dst_depth (q2, q3) */
    vst1.32 {d0-d3}, [r9]!
    vbit    q2, q10, q12
    vbit    q3, q11, q13
    vst1.32 {d4-d7}, [r8]!
    subs    r7, r7, #8
    bge     .Lwb_neon
.Lwb_tail_setup:
    adds    r7, r7, #8
    beq     .Lwb_next_row
.Lwb_tail:
    ldrb    r6, [r5], #1
    ldr     sl, [r4], #4
    ldr     fp, [lr], #4
    orr     sl, sl, ip
    cmp     r6, #0
    strne   sl, [r9]
    add     r9, r9, #4
    strne   fp, [r8]
    add     r8, r8, #4
    subs    r7, r7, #1
    bne     .Lwb_tail
.Lwb_next_row:
    add     r1, r1, #2048
    add     r2, r2, #2048
    subs    r3, r3, #1
    bne     .Lwb_row
    pop     {r4, r5, r6, r7, r8, r9, sl, fp, pc}
    .size dsfast_writeback_4x, .-dsfast_writeback_4x


/* void dsfast_load_depth_4x(u32 *dst_buf, const u32 *depth_tile, const void *rt, u32 height) */
    .global dsfast_load_depth_4x
    .type dsfast_load_depth_4x, %function
    .p2align 4
dsfast_load_depth_4x:
    add     ip, r2, #1408           /* x_start array */
    add     r2, r2, #1584           /* width array */
    push    {r4, r5, lr}
.Lld_row:
    ldrh    r4, [ip], #4
    ldrh    r5, [r2], #4
    add     lr, r1, r4, lsl #2
    add     r4, lr, #2048           /* prefetch next scanline start */
    pld     [lr, #0]
    pld     [lr, #64]
    pld     [r4, #0]
    pld     [r4, #64]
.Lld_col:
    pld     [lr, #192]
    vld1.32 {d0-d3}, [lr]!
    vld1.32 {d4-d7}, [lr]!
    subs    r5, r5, #16
    vst1.32 {d0-d3}, [r0]!
    vst1.32 {d4-d7}, [r0]!
    bgt     .Lld_col
    add     r0, r0, r5, lsl #2
    add     r1, r1, #2048
    subs    r3, r3, #1
    bne     .Lld_row
    pop     {r4, r5, pc}
    .size dsfast_load_depth_4x, .-dsfast_load_depth_4x


/* void dsfast_load_depth_colors_id_4x(u32 *dst_d, u32 *dst_c, u8 *dst_id, const u32 *d_tile,
 *                                     const u32 *c_tile, const u8 *id_tile, const void *rt, u32 height) */
    .global dsfast_load_depth_colors_id_4x
    .type dsfast_load_depth_colors_id_4x, %function
    .p2align 4
dsfast_load_depth_colors_id_4x:
    push    {r4, r5, r6, r7, r8, r9, sl, lr}
    ldr     ip, [sp, #32]           /* color_tile */
    ldr     lr, [sp, #36]           /* id_tile */
    ldr     r4, [sp, #40]           /* rt */
    ldr     r5, [sp, #44]           /* height */
    add     r6, r4, #1408
    add     r4, r4, #1584
.Lldc_row:
    ldrh    r7, [r6], #4
    ldrh    r8, [r4], #4
    add     r9, lr, r7
    add     sl, ip, r7, lsl #2
    add     r7, r3, r7, lsl #2
    pld     [sl, #0]
    pld     [r7, #0]
    pld     [r9, #0]
    pld     [sl, #2048]
    pld     [r7, #2048]
    pld     [r9, #512]
.Lldc_col:
    pld     [sl, #128]
    pld     [r7, #128]
    vld1.32 {d4-d7}, [sl]!
    vld1.32 {d0-d3}, [r7]!
    vld1.32 {d16},   [r9]!
    subs    r8, r8, #8
    vst1.32 {d4-d7}, [r1]!
    vst1.32 {d0-d3}, [r0]!
    vst1.32 {d16},   [r2]!
    bgt     .Lldc_col
    add     r0, r0, r8, lsl #2
    add     r1, r1, r8, lsl #2
    add     r2, r2, r8
    add     ip, ip, #2048
    add     r3, r3, #2048
    add     lr, lr, #512
    subs    r5, r5, #1
    bne     .Lldc_row
    pop     {r4, r5, r6, r7, r8, r9, sl, pc}
    .size dsfast_load_depth_colors_id_4x, .-dsfast_load_depth_colors_id_4x


/* void dsfast_writeback_all_pass_4x(const void *rt, u32 *depth_tile, u32 *color_tile, u32 height,
 *                                   u32 poly_id, const u32 *src_depth, const u32 *src_color) */
    .global dsfast_writeback_all_pass_4x
    .type dsfast_writeback_all_pass_4x, %function
    .p2align 4
dsfast_writeback_all_pass_4x:
    push    {r4, r5, r6, r7, r8, lr}
    ldr     r7, [sp, #24]           /* poly_id */
    add     r0, r0, #1408
    ldr     lr, [sp, #28]           /* src_depth */
    lsl     r7, r7, #24
    ldr     r4, [sp, #32]           /* src_color */
    vdup.32 q8, r7
    mov     ip, #0
.Lwap_row:
    ldrh    r5, [r0]
    ldr     r6, [r0, #176]
    add     r0, r0, #4
    add     ip, ip, r5
    ubfx    r6, r6, #0, #16
    add     r2, r2, ip, lsl #2
    add     r1, r1, ip, lsl #2
    rsb     ip, r6, #512
    sub     ip, ip, r5
    pld     [r4, #0]
    pld     [lr, #0]
    pld     [r2, #0]
    pld     [r1, #0]
    subs    r6, r6, #16
    blt     .Lwap_chk8
.Lwap_16:
    pld     [r4, #64]
    pld     [lr, #64]
    pld     [r2, #64]
    pld     [r1, #64]
    vld1.32 {d0-d3},   [r4]!
    vld1.32 {d18-d21}, [r4]!
    vld1.32 {d4-d7},   [lr]!
    vld1.32 {d22-d25}, [lr]!
    vorr    q0,  q0,  q8
    vorr    q1,  q1,  q8
    vorr    q9,  q9,  q8
    vorr    q10, q10, q8
    subs    r6, r6, #16
    vst1.32 {d0-d3},   [r2]!
    vst1.32 {d18-d21}, [r2]!
    vst1.32 {d4-d7},   [r1]!
    vst1.32 {d22-d25}, [r1]!
    bge     .Lwap_16
.Lwap_chk8:
    adds    r6, r6, #8
    blt     .Lwap_tail
    vld1.32 {d0-d3}, [r4]!
    vld1.32 {d4-d7}, [lr]!
    vorr    q0, q0, q8
    vorr    q1, q1, q8
    vst1.32 {d0-d3}, [r2]!
    vst1.32 {d4-d7}, [r1]!
    subs    r6, r6, #8
.Lwap_tail:
    adds    r6, r6, #8
    beq     .Lwap_next
.Lwap_tloop:
    ldr     r5, [r4], #4
    ldr     r8, [lr], #4
    orr     r5, r5, r7
    str     r5, [r2], #4
    str     r8, [r1], #4
    subs    r6, r6, #1
    bne     .Lwap_tloop
.Lwap_next:
    pld     [r2, #2048]
    pld     [r1, #2048]
    subs    r3, r3, #1
    bne     .Lwap_row
    pop     {r4, r5, r6, r7, r8, pc}
    .size dsfast_writeback_all_pass_4x, .-dsfast_writeback_all_pass_4x


    .balign 16
.Lbitmask_const:
    .byte   0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80
    .byte   0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80

/* u32 dsfast_render_scanline_set_3d_visibility(u8 *bg_visibility_3d, const u32 *layer_buffer_3d)
 * Patches 0x08048c50: eliminates 256B stack buffer, render_scanline_gather_3d_alpha call, and
 * 240 GCC-spilled NEON instructions. Computes 32-byte 1bpp visibility mask and returns
 * 2 (any translucent: alpha != 0 && alpha != 31), 16 (any opaque: alpha != 0), or 0. */
    .global dsfast_render_scanline_set_3d_visibility
    .type dsfast_render_scanline_set_3d_visibility, %function
    .p2align 4
dsfast_render_scanline_set_3d_visibility:
    pld     [r1, #0]
    pld     [r1, #64]
    adr     ip, .Lbitmask_const
    vld1.32 {d30-d31}, [ip :128]    /* q15 = bitmask {1,2,4,8,16,32,64,128} x 2 */
    vmov.i8 q14, #31
    vmov.i32 q12, #0                /* any_vis */
    vmov.i32 q13, #0                /* any_trans */
    mov     r2, #16                 /* 16 x 16 = 256 pixels */
.Ls3v_loop:
    pld     [r1, #128]
    pld     [r1, #192]
    vld4.8  {d0, d2, d4, d6}, [r1 :128]!
    vld4.8  {d1, d3, d5, d7}, [r1 :128]!    /* q3 = 16 alpha bytes */
    vtst.8  q8, q3, q3              /* vis: 0xff iff alpha != 0 */
    vceq.i8 q9, q3, q14             /* eq31: 0xff iff alpha == 31 */
    vorr    q12, q12, q8
    vbic    q9, q8, q9              /* trans: 0xff iff alpha != 0 && alpha != 31 */
    vand    q8, q8, q15
    vorr    q13, q13, q9
    vpadd.i8 d0, d16, d17
    vpadd.i8 d0, d0, d0
    vpadd.i8 d0, d0, d0             /* d0[0]=bytes[0..7], d0[1]=bytes[8..15] */
    subs    r2, r2, #1
    vst1.16 {d0[0]}, [r0]!
    bne     .Ls3v_loop
    vorr    d0, d26, d27            /* check any_trans */
    vmov    r2, r3, d0
    orrs    r2, r2, r3
    movne   r0, #2
    bxne    lr
    vorr    d0, d24, d25            /* check any_vis */
    vmov    r2, r3, d0
    orrs    r2, r2, r3
    movne   r0, #16
    moveq   r0, #0
    bx      lr
    .size dsfast_render_scanline_set_3d_visibility, .-dsfast_render_scanline_set_3d_visibility


/* void dsfast_select_pixels_binary32(...)
 * Patches 0x080bef80: fixes DraStic's useless pld [r2, #1024] (past end of 1024B scanline)
 * with proper +64/+128/+192 prefetches across r2, r0, r4, lr, r3 (and r1 when alpha != 0). */
    .global dsfast_select_pixels_binary32
    .type dsfast_select_pixels_binary32, %function
    .p2align 4
dsfast_select_pixels_binary32:
    push    {r4, lr}
    pld     [r2, #0]
    pld     [r2, #64]
    pld     [r0, #0]
    add     r4, r0, #256
    add     lr, r0, #512
    pld     [r4, #0]
    pld     [lr, #0]
    pld     [r3, #0]
    adr     ip, .Lbitmask_const
    vld1.32 {d30-d31}, [ip :128]
    mov     ip, #16
    cmp     r1, #0
    bne     .Lspb_alpha
.Lspb_noalpha:
    pld     [r2, #128]
    pld     [r2, #192]
    pld     [r0, #64]
    pld     [r4, #64]
    pld     [lr, #64]
    vld4.8  {d0, d2, d4, d6}, [r2 :128]!
    vld4.8  {d1, d3, d5, d7}, [r2 :128]!
    vld2.8  {d18[], d19[]}, [r3 :16]!
    vld1.32 {d20-d21}, [r0 :128]
    vtst.8  q9, q9, q15
    vld1.32 {d22-d23}, [r4 :128]
    vbit    d20, d0, d18
    vld1.32 {d24-d25}, [lr :128]
    vbit    d21, d1, d19
    vbit    d22, d2, d18
    vbit    d23, d3, d19
    vbit    d24, d4, d18
    vst1.32 {d20-d21}, [r0 :128]!
    vbit    d25, d5, d19
    vst1.32 {d22-d23}, [r4 :128]!
    vst1.32 {d24-d25}, [lr :128]!
    subs    ip, ip, #1
    bne     .Lspb_noalpha
    pop     {r4, pc}
.Lspb_alpha:
    pld     [r1, #0]
    pld     [r1, #64]
.Lspb_alpha_loop:
    pld     [r2, #128]
    pld     [r2, #192]
    pld     [r1, #128]
    vld4.8  {d0, d2, d4, d6}, [r2 :128]!
    vld4.8  {d1, d3, d5, d7}, [r2 :128]!
    vld2.8  {d18[], d19[]}, [r3 :16]!
    vld1.32 {d20-d21}, [r0 :128]
    vtst.8  q9, q9, q15
    vld1.32 {d22-d23}, [r4 :128]
    vbit    d20, d0, d18
    vld1.32 {d24-d25}, [lr :128]
    vbit    d21, d1, d19
    vld1.32 {d16-d17}, [r1 :128]
    vbit    d22, d2, d18
    vbit    d23, d3, d19
    vbit    d24, d4, d18
    vbit    d25, d5, d19
    vst1.32 {d20-d21}, [r0 :128]!
    vbit    d16, d6, d18
    vst1.32 {d22-d23}, [r4 :128]!
    vbit    d17, d7, d19
    vst1.32 {d24-d25}, [lr :128]!
    subs    ip, ip, #1
    vst1.32 {d16-d17}, [r1 :128]!
    bne     .Lspb_alpha_loop
    pop     {r4, pc}
    .size dsfast_select_pixels_binary32, .-dsfast_select_pixels_binary32


/* void dsfast_edge_mark_4x(u32 *dst, const u32 *src_color_tile, const u8 *edge_ids, const u8 *edge_cache)
 * Patches 0x080c55a0: identical NEON logic with PLD lookahead on [r1] (64KB tile) and [r2]. */
    .global dsfast_edge_mark_4x
    .type dsfast_edge_mark_4x, %function
    .p2align 4
dsfast_edge_mark_4x:
    push    {lr}
    vpush   {d8-d11}
    pld     [r1, #0]
    pld     [r1, #64]
    pld     [r2, #0]
    vld1.32 {d4-d5}, [r3 :128]!
    vld1.32 {d6}, [r3 :64]
    mov     ip, #512
    vmov.i8 d7, #31
    add     lr, r0, #1024
.Lem_loop:
    pld     [r1, #192]
    pld     [r1, #256]
    pld     [r2, #64]
    vld4.8  {d16, d18, d20, d22}, [r1 :128]!
    vld4.8  {d17, d19, d21, d23}, [r1 :128]!
    vld4.8  {d24, d26, d28, d30}, [r1 :128]!
    vld4.8  {d25, d27, d29, d31}, [r1 :128]!
    vld1.32 {d8-d11}, [r2 :128]!
    vshr.u8 q0, q4, #3
    vshr.u8 q1, q5, #3
    vtbx.8  d16, {d4}, d8
    vtbx.8  d18, {d5}, d8
    vtbx.8  d20, {d6}, d8
    vbif    d22, d0, d7
    vtbx.8  d17, {d4}, d9
    vtbx.8  d19, {d5}, d9
    vtbx.8  d21, {d6}, d9
    vbif    d23, d1, d7
    vtbx.8  d24, {d4}, d10
    vtbx.8  d26, {d5}, d10
    vtbx.8  d28, {d6}, d10
    vbif    d30, d2, d7
    vtbx.8  d25, {d4}, d11
    vtbx.8  d27, {d5}, d11
    vtbx.8  d29, {d6}, d11
    vbif    d31, d3, d7
    vuzp.8  d16, d17
    vuzp.8  d24, d25
    vuzp.8  d18, d19
    vuzp.8  d26, d27
    vuzp.8  d20, d21
    vuzp.8  d28, d29
    vuzp.8  d22, d23
    vuzp.8  d30, d31
    vst4.8  {d16, d18, d20, d22}, [r0 :128]!
    vst4.8  {d17, d19, d21, d23}, [lr :128]!
    vst4.8  {d24, d26, d28, d30}, [r0 :128]!
    vst4.8  {d25, d27, d29, d31}, [lr :128]!
    subs    ip, ip, #32
    bne     .Lem_loop
    vpop    {d8-d11}
    pop     {pc}
    .size dsfast_edge_mark_4x, .-dsfast_edge_mark_4x


/* void dsfast_edge_identify_4x(u8 *edge_ids, const u32 *d_y, const u32 *d_ym1, const u32 *d_yp1, u32 clear_depth_id)
 * Patches 0x080c5658: identical NEON logic with PLD lookahead across 3 depth rows (64KB tile). */
    .global dsfast_edge_identify_4x
    .type dsfast_edge_identify_4x, %function
    .p2align 4
dsfast_edge_identify_4x:
    push    {r4, r5, lr}
    vpush   {d8-d15}
    pld     [r2, #0]
    pld     [r2, #64]
    pld     [r1, #0]
    pld     [r1, #64]
    pld     [r3, #0]
    pld     [r3, #64]
    add     ip, r2, #60
    add     lr, r2, #4
    add     r5, sp, #76             /* &clear_depth_id on caller stack */
    vmov.i8 q5, #127
    vmov.i8 q4, #63
    vmov.i8 q6, #64
    mov     r4, #480
    vld4.8  {d0, d2, d4, d6}, [r2 :128]!
    vld4.8  {d1, d3, d5, d7}, [r2 :128]!
    vld4.8  {d17[], d19[], d21[], d23[]}, [r5 :32]
    vext.8  q8, q8, q0, #15
    vext.8  q9, q9, q1, #15
    vext.8  q10, q10, q2, #15
    vext.8  q11, q11, q3, #15
    vand    q3, q3, q5
    vand    q11, q11, q4
    veor    q3, q3, q6
    vceq.i8 q12, q2, q10
    vceq.i8 q13, q1, q9
    vcgt.u8 q10, q10, q2
    vcgt.u8 q9, q9, q1
    vcgt.u8 q8, q8, q0
    vand    q13, q12, q13
    vand    q12, q12, q9
    vand    q13, q13, q8
    vceq.i8 q15, q3, q11
    vorr    q14, q10, q12
    vld4.8  {d16, d18, d20, d22}, [lr]!
    vld4.8  {d17, d19, d21, d23}, [lr]!
    vorr    q14, q14, q13
    vand    q11, q11, q4
    vbic    q14, q14, q15
    vceq.i8 q12, q2, q10
    vmvn    q7, q14
    vceq.i8 q13, q1, q9
    vcgt.u8 q10, q10, q2
    vcgt.u8 q9, q9, q1
    vcgt.u8 q8, q8, q0
    vand    q13, q12, q13
    vand    q12, q12, q9
    vand    q13, q13, q8
    vceq.i8 q15, q3, q11
    vorr    q14, q10, q12
    vld4.8  {d16, d18, d20, d22}, [r1 :128]!
    vorr    q14, q14, q13
    vld4.8  {d17, d19, d21, d23}, [r1 :128]!
    vbic    q14, q14, q15
    vand    q11, q11, q4
    vbic    q7, q7, q14
    vceq.i8 q12, q2, q10
    vceq.i8 q13, q1, q9
    vcgt.u8 q10, q10, q2
    vcgt.u8 q9, q9, q1
    vcgt.u8 q8, q8, q0
    vand    q13, q12, q13
    vand    q12, q12, q9
    vand    q13, q13, q8
    vceq.i8 q15, q3, q11
    vorr    q14, q10, q12
    vld4.8  {d16, d18, d20, d22}, [r3 :128]!
    vorr    q14, q14, q13
    vld4.8  {d17, d19, d21, d23}, [r3 :128]!
    vbic    q14, q14, q15
    vand    q11, q11, q4
    vbic    q7, q7, q14
    vceq.i8 q12, q2, q10
    vceq.i8 q13, q1, q9
    vcgt.u8 q10, q10, q2
    vcgt.u8 q9, q9, q1
    vcgt.u8 q8, q8, q0
    vand    q13, q12, q13
    vand    q12, q12, q9
    vand    q13, q13, q8
    vceq.i8 q15, q3, q11
    vorr    q14, q10, q12
    vorr    q14, q14, q13
    vbic    q14, q14, q15
    vbic    q7, q7, q14
    vshr.u8 q3, q3, #3
    vorr    q3, q3, q7
    vst1.32 {d6-d7}, [r0 :128]!
.Lei_loop:
    pld     [r2, #192]
    pld     [r1, #192]
    pld     [r3, #192]
    vld4.8  {d0, d2, d4, d6}, [r2 :128]!
    vld4.8  {d1, d3, d5, d7}, [r2 :128]!
    vld4.8  {d16, d18, d20, d22}, [ip]!
    vld4.8  {d17, d19, d21, d23}, [ip]!
    vand    q3, q3, q5
    vand    q11, q11, q4
    veor    q3, q3, q6
    vceq.i8 q12, q2, q10
    vceq.i8 q13, q1, q9
    vcgt.u8 q10, q10, q2
    vcgt.u8 q9, q9, q1
    vcgt.u8 q8, q8, q0
    vand    q13, q12, q13
    vand    q12, q12, q9
    vand    q13, q13, q8
    vceq.i8 q15, q3, q11
    vorr    q14, q10, q12
    vld4.8  {d16, d18, d20, d22}, [lr]!
    vld4.8  {d17, d19, d21, d23}, [lr]!
    vorr    q14, q14, q13
    vand    q11, q11, q4
    vbic    q14, q14, q15
    vceq.i8 q12, q2, q10
    vmvn    q7, q14
    vceq.i8 q13, q1, q9
    vcgt.u8 q10, q10, q2
    vcgt.u8 q9, q9, q1
    vcgt.u8 q8, q8, q0
    vand    q13, q12, q13
    vand    q12, q12, q9
    vand    q13, q13, q8
    vceq.i8 q15, q3, q11
    vorr    q14, q10, q12
    vld4.8  {d16, d18, d20, d22}, [r1 :128]!
    vorr    q14, q14, q13
    vld4.8  {d17, d19, d21, d23}, [r1 :128]!
    vbic    q14, q14, q15
    vand    q11, q11, q4
    vbic    q7, q7, q14
    vceq.i8 q12, q2, q10
    vceq.i8 q13, q1, q9
    vcgt.u8 q10, q10, q2
    vcgt.u8 q9, q9, q1
    vcgt.u8 q8, q8, q0
    vand    q13, q12, q13
    vand    q12, q12, q9
    vand    q13, q13, q8
    vceq.i8 q15, q3, q11
    vorr    q14, q10, q12
    vld4.8  {d16, d18, d20, d22}, [r3 :128]!
    vorr    q14, q14, q13
    vld4.8  {d17, d19, d21, d23}, [r3 :128]!
    vbic    q14, q14, q15
    vand    q11, q11, q4
    vbic    q7, q7, q14
    vceq.i8 q12, q2, q10
    vceq.i8 q13, q1, q9
    vcgt.u8 q10, q10, q2
    vcgt.u8 q9, q9, q1
    vcgt.u8 q8, q8, q0
    vand    q13, q12, q13
    vand    q12, q12, q9
    vand    q13, q13, q8
    vceq.i8 q15, q3, q11
    vorr    q14, q10, q12
    vorr    q14, q14, q13
    vbic    q14, q14, q15
    vbic    q7, q7, q14
    vshr.u8 q3, q3, #3
    vorr    q3, q3, q7
    vst1.32 {d6-d7}, [r0 :128]!
    subs    r4, r4, #16
    bne     .Lei_loop
    vld4.8  {d0, d2, d4, d6}, [r2 :128]!
    vld4.8  {d1, d3, d5, d7}, [r2 :128]!
    vld4.8  {d16, d18, d20, d22}, [ip]!
    vld4.8  {d17, d19, d21, d23}, [ip]!
    vand    q3, q3, q5
    vand    q11, q11, q4
    veor    q3, q3, q6
    vceq.i8 q12, q2, q10
    vceq.i8 q13, q1, q9
    vcgt.u8 q10, q10, q2
    vcgt.u8 q9, q9, q1
    vcgt.u8 q8, q8, q0
    vand    q13, q12, q13
    vand    q12, q12, q9
    vand    q13, q13, q8
    vceq.i8 q15, q3, q11
    vorr    q14, q10, q12
    vld4.8  {d16[], d18[], d20[], d22[]}, [r5 :32]
    vext.8  q8, q0, q8, #1
    vext.8  q9, q1, q9, #1
    vext.8  q10, q2, q10, #1
    vext.8  q11, q3, q11, #1
    vorr    q14, q14, q13
    vand    q11, q11, q4
    vbic    q14, q14, q15
    vceq.i8 q12, q2, q10
    vmvn    q7, q14
    vceq.i8 q13, q1, q9
    vcgt.u8 q10, q10, q2
    vcgt.u8 q9, q9, q1
    vcgt.u8 q8, q8, q0
    vand    q13, q12, q13
    vand    q12, q12, q9
    vand    q13, q13, q8
    vceq.i8 q15, q3, q11
    vorr    q14, q10, q12
    vld4.8  {d16, d18, d20, d22}, [r1 :128]!
    vorr    q14, q14, q13
    vld4.8  {d17, d19, d21, d23}, [r1 :128]!
    vbic    q14, q14, q15
    vand    q11, q11, q4
    vbic    q7, q7, q14
    vceq.i8 q12, q2, q10
    vceq.i8 q13, q1, q9
    vcgt.u8 q10, q10, q2
    vcgt.u8 q9, q9, q1
    vcgt.u8 q8, q8, q0
    vand    q13, q12, q13
    vand    q12, q12, q9
    vand    q13, q13, q8
    vceq.i8 q15, q3, q11
    vorr    q14, q10, q12
    vld4.8  {d16, d18, d20, d22}, [r3 :128]!
    vorr    q14, q14, q13
    vld4.8  {d17, d19, d21, d23}, [r3 :128]!
    vbic    q14, q14, q15
    vand    q11, q11, q4
    vbic    q7, q7, q14
    vceq.i8 q12, q2, q10
    vceq.i8 q13, q1, q9
    vcgt.u8 q10, q10, q2
    vcgt.u8 q9, q9, q1
    vcgt.u8 q8, q8, q0
    vand    q13, q12, q13
    vand    q12, q12, q9
    vand    q13, q13, q8
    vceq.i8 q15, q3, q11
    vorr    q14, q10, q12
    vorr    q14, q14, q13
    vbic    q14, q14, q15
    vbic    q7, q7, q14
    vshr.u8 q3, q3, #3
    vorr    q3, q3, q7
    vst1.32 {d6-d7}, [r0 :128]!
    vpop    {d8-d15}
    pop     {r4, r5, pc}
    .size dsfast_edge_identify_4x, .-dsfast_edge_identify_4x

    .section .note.GNU-stack,"",%progbits

