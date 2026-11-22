/* v5 (= v4 + last group of a row done with 4 lanes when it has <= 4 pixels); v4: v2 with the per-row setup of passes 1b/3 moved after the depth test (fewer registers live in pass 1a) */
/* alpha (flags 0x0f / 0x0b): as render_polygon_alpha_blend (or alpha_pass when DISP3DCNT bit 3 is off), alpha_id_test
   and alpha_combine[_depth][_fog]; ids = the polygon-id byte plane of the first row (rows of 512) */
static int f3d_draw_v5(u8 *rt, u8 *edge, u8 *poly, u32 height, u32 *color, u32 *depth, int gour, int alpha, u8 *ids)
{
    u32 attr = *(u32 *)(poly + 4), pid = ((attr >> 24) & 63) << 24, pa = (attr >> 16) & 31;
    u32 fog = attr & 0x8000, eq = attr & 0x4000;
    u8 *tc = *(u8 **)(poly + 0xc);
    u32 tw = *(unsigned short *)(tc + 0x24), th = *(unsigned short *)(tc + 0x26);
    u8 *data = *(u8 **)(tc + 0xc);
    u32 *pal = *(u32 **)(tc + 0x10), paletted = tc[0x2e];
    u32 rep = *(unsigned short *)(poly + 2) & 15;
    u32 sm = (rep & 1) ? ((rep & 4) ? 2 : 1) : 0, tm = (rep & 2) ? ((rep & 8) ? 2 : 1) : 0;
    u32 emark = alpha ? 0 : *(u32 *)(f3d_v3dx(rt) + 0x40) & 32, blend = *(u32 *)(f3d_v3dx(rt) + 0x40) & 8, dbit = attr & 0x800;
    uint8x8_t any8 = vdup_n_u8(0), v1f = vdup_n_u8(31), v80 = vdup_n_u8(0x80), vpid8 = vdup_n_u8((u8)(pid >> 24)), vz8 = vdup_n_u8(0);
    u8 aref = (u8)*(u32 *)(f3d_v3dx(rt) + 0x44);
    uint32x4_t anyv = vdupq_n_u32(0), pidv = vdupq_n_u32(pid), m24 = vdupq_n_u32(0xffffff), c100 = vdupq_n_u32(0x100),
               e40 = vdupq_n_u32(0x40000000);
    uint8x8_t vcr = vdup_n_u8((u8)(*(unsigned short *)(edge + 0x420) >> 3)), vcg = vdup_n_u8((u8)(*(unsigned short *)(edge + 0x422) >> 3)),
              vcb = vdup_n_u8((u8)(*(unsigned short *)(edge + 0x582) >> 3)), vpa = vdup_n_u8((u8)pa), varef = vdup_n_u8(aref),
              vfog = vdup_n_u8(fog ? 0x80 : 0);
    int16x8_t wm1 = vdupq_n_s16((short)(tw - 1)), hm1 = vdupq_n_s16((short)(th - 1)), wb = vdupq_n_s16((short)tw),
              hb = vdupq_n_s16((short)th), z16 = vdupq_n_s16(0);
    uint16x4_t vtw = vdup_n_u16((unsigned short)tw);
    static const float k0[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    static const u32 lanes[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    float32x4_t ka = vld1q_f32(k0), kb = vld1q_f32(k0 + 4);
    uint32x4_t la = vld1q_u32(lanes), lb = vld1q_u32(lanes + 4), v8 = vdupq_n_u32(8);
    u32 adr[520] __attribute__((aligned(16))), tex[520] __attribute__((aligned(16))), dv[520] __attribute__((aligned(16)));
    u8 msk[520] __attribute__((aligned(16)));
    short ts[520] __attribute__((aligned(16)));
    u8 crb[520] __attribute__((aligned(16))), cgb[520] __attribute__((aligned(16))), cbb[520] __attribute__((aligned(16)));
    u32 r;
    for (r = 0; r < height; r++, color += 512, depth += 512, ids += 512) {
        u32 x0 = *(unsigned short *)(edge + 1408 + r * 4), n = *(unsigned short *)(edge + 1584 + r * 4);
        s32 w0 = *(s32 *)(edge + r * 4), dw = *(s32 *)(edge + 0xb0 + r * 4);
        float w0f = (float)w0, dwf = (float)dw, nw = (float)(s32)(w0 + dw) * (float)(s32)n;
        float32x4_t den0, den1, num0, num1, d8 = vdupq_n_f32(8.0f * dwf), n8 = vdupq_n_f32(8.0f * w0f);
        int32x4_t vdw4 = vdupq_n_s32(dw);
        int32x4_t vw0 = vdupq_n_s32(w0);
        uint32x4_t rowany = vdupq_n_u32(0);
        u32 *cp = color + x0, *dp = depth + x0, k;
        if (!n)
            continue;
        den0 = vmlsq_f32(vdupq_n_f32(nw), ka, vdupq_n_f32(dwf));
        den1 = vmlsq_f32(vdupq_n_f32(nw), kb, vdupq_n_f32(dwf));
        num0 = vmulq_f32(ka, vdupq_n_f32(w0f));
        num1 = vmulq_f32(kb, vdupq_n_f32(w0f));
        /* pass 1a: perspective step t, w, depth test, depth value to write */
        for (k = 0; k + 4 < n; k += 8) {
            float32x4_t e0 = vrecpeq_f32(den0), e1 = vrecpeq_f32(den1);
            int32x4_t c0, c1, w_0, w_1;
            uint32x4_t db0 = vld1q_u32(dp + k), db1 = vld1q_u32(dp + k + 4), m0, m1, z0, z1;
            e0 = vmulq_f32(e0, vrecpsq_f32(e0, den0));
            e1 = vmulq_f32(e1, vrecpsq_f32(e1, den1));
            e0 = vmulq_f32(e0, vrecpsq_f32(e0, den0));
            e1 = vmulq_f32(e1, vrecpsq_f32(e1, den1));
            c0 = vshlq_n_s32(vcvtq_n_s32_f32(vmulq_f32(num0, e0), 15), 16);   /* (s16)t << 16 */
            c1 = vshlq_n_s32(vcvtq_n_s32_f32(vmulq_f32(num1, e1), 15), 16);
            vst1q_s16(ts + k, vcombine_s16(vshrn_n_s32(c0, 16), vshrn_n_s32(c1, 16)));
            den0 = vsubq_f32(den0, d8);
            den1 = vsubq_f32(den1, d8);
            num0 = vaddq_f32(num0, n8);
            num1 = vaddq_f32(num1, n8);
            /* (2 * dw * (t << 16)) >> 32 == (dw * t) >> 15 exactly (t >= 0: no saturation) */
            w_0 = vaddq_s32(vqdmulhq_s32(vdw4, c0), vw0);
            w_1 = vaddq_s32(vqdmulhq_s32(vdw4, c1), vw0);
            z0 = vandq_u32(db0, m24);
            z1 = vandq_u32(db1, m24);
            if (eq) {
                m0 = vcltq_u32(vreinterpretq_u32_s32(vabsq_s32(vsubq_s32(w_0, vreinterpretq_s32_u32(z0)))), c100);
                m1 = vcltq_u32(vreinterpretq_u32_s32(vabsq_s32(vsubq_s32(w_1, vreinterpretq_s32_u32(z1)))), c100);
            } else {
                m0 = vcgtq_u32(z0, vreinterpretq_u32_s32(w_0));
                m1 = vcgtq_u32(z1, vreinterpretq_u32_s32(w_1));
            }
            rowany = vorrq_u32(rowany, vorrq_u32(m0, m1));
            vst1_u8(msk + k, vmovn_u16(vcombine_u16(vmovn_u32(m0), vmovn_u32(m1))));
            vst1q_u32(dv + k, vorrq_u32(vreinterpretq_u32_s32(w_0), pidv));
            vst1q_u32(dv + k + 4, vorrq_u32(vreinterpretq_u32_s32(w_1), pidv));
        }
        if (k < n) {   /* 1 to 4 pixels left: half a group */
            float32x4_t e0 = vrecpeq_f32(den0);
            int32x4_t c0, w_0;
            uint32x4_t z0 = vandq_u32(vld1q_u32(dp + k), m24), m0;
            e0 = vmulq_f32(e0, vrecpsq_f32(e0, den0));
            e0 = vmulq_f32(e0, vrecpsq_f32(e0, den0));
            c0 = vshlq_n_s32(vcvtq_n_s32_f32(vmulq_f32(num0, e0), 15), 16);
            vst1_s16(ts + k, vshrn_n_s32(c0, 16));
            w_0 = vaddq_s32(vqdmulhq_s32(vdw4, c0), vw0);
            if (eq)
                m0 = vcltq_u32(vreinterpretq_u32_s32(vabsq_s32(vsubq_s32(w_0, vreinterpretq_s32_u32(z0)))), c100);
            else
                m0 = vcgtq_u32(z0, vreinterpretq_u32_s32(w_0));
            rowany = vorrq_u32(rowany, m0);
            vst1_lane_u32((u32 *)(msk + k), vreinterpret_u32_u8(vmovn_u16(vcombine_u16(vmovn_u32(m0), vmovn_u32(m0)))), 0);
            vst1q_u32(dv + k, vorrq_u32(vreinterpretq_u32_s32(w_0), pidv));
        }
        /* pixels past the end of the row: never drawn */
        vst1_u8(msk + n, vdup_n_u8(0));
        /* edge marking: the first lo and the last hi pixels get 0x40 as the top byte */
        if (emark) {
            u32 lo = *(unsigned short *)(edge + 1760 + r * 4), hi = *(unsigned short *)(edge + 1762 + r * 4), i;
            if (lo > n)
                lo = n;
            /* dv = w | pid, pid only in bits 24-29: dv & 0xffffff == w & 0xffffff */
#pragma clang loop vectorize(disable)
            for (i = 0; i < lo; i++)
                dv[i] = (dv[i] & 0xffffff) | 0x40000000 | pid;
#pragma clang loop vectorize(disable)
            for (i = (hi <= n ? n - hi : n); i < n; i++)
                dv[i] = (dv[i] & 0xffffff) | 0x40000000 | pid;
        }
        {
            uint32x2_t mm = vorr_u32(vget_low_u32(rowany), vget_high_u32(rowany));
            if (!(vget_lane_u32(mm, 0) | vget_lane_u32(mm, 1)))
                continue;
        }
        if (f3d_skip & 8) continue;
        /* the row has visible pixels: what passes 1b and 3 need */
        u32 uv0 = *(u32 *)(edge + 704 + r * 4), duv = *(u32 *)(edge + 880 + r * 4);
        s32 du = (short)duv, dvv = (short)(duv >> 16);
        s32 U0 = ((s32)(short)uv0 << 15) + (du > 0 ? 0x400 : 0), V0 = ((s32)(short)(uv0 >> 16) << 15) + (dvv > 0 ? 0x400 : 0);
        int32x4_t vU0 = vdupq_n_s32(U0), vV0 = vdupq_n_s32(V0);
        int16x4_t vdu = vdup_n_s16((short)du), vdv = vdup_n_s16((short)dvv);
        int32x4_t vR0, vG0, vB0;
        int16x4_t vdR, vdG, vdB;
        if (gour) {   /* 0x0a: color(t) = ((c << 15) + dc * t) >> 18, as render_polygon_setup/interpolate_rgb */
            u32 rg0 = *(u32 *)(edge + 1056 + r * 4), drg = *(u32 *)(edge + 1232 + r * 4);
            vR0 = vdupq_n_s32((s32)((rg0 & 0xffff) << 15));
            vG0 = vdupq_n_s32((s32)((rg0 >> 16) << 15));
            vB0 = vdupq_n_s32((s32)((*(u32 *)(edge + 1408 + r * 4) >> 16) << 15));
            vdR = vdup_n_s16((short)drg);
            vdG = vdup_n_s16((short)(drg >> 16));
            vdB = vdup_n_s16((short)(*(u32 *)(edge + 1584 + r * 4) >> 16));
        }
        /* pass 1b: texture coordinates and texel addresses (0 where the pixel is hidden) */
        if (!(f3d_skip & 1)) for (k = 0; k < n; k += 8) {
            int16x8_t t = vld1q_s16(ts + k), u, v, s, tt;
            if (gour) {
                int16x4_t tl = vget_low_s16(t), th4 = vget_high_s16(t);
                vst1_u8(crb + k, vshrn_n_u16(vreinterpretq_u16_s16(vcombine_s16(vshrn_n_s32(vmlal_s16(vR0, vdR, tl), 16), vshrn_n_s32(vmlal_s16(vR0, vdR, th4), 16))), 2));
                vst1_u8(cgb + k, vshrn_n_u16(vreinterpretq_u16_s16(vcombine_s16(vshrn_n_s32(vmlal_s16(vG0, vdG, tl), 16), vshrn_n_s32(vmlal_s16(vG0, vdG, th4), 16))), 2));
                vst1_u8(cbb + k, vshrn_n_u16(vreinterpretq_u16_s16(vcombine_s16(vshrn_n_s32(vmlal_s16(vB0, vdB, tl), 16), vshrn_n_s32(vmlal_s16(vB0, vdB, th4), 16))), 2));
            }
            uint8x8_t mb = vld1_u8(msk + k);
            int16x8_t m16 = vmovl_s8(vreinterpret_s8_u8(mb));
            uint32x4_t m0 = vreinterpretq_u32_s32(vmovl_s16(vget_low_s16(m16))), m1 = vreinterpretq_u32_s32(vmovl_s16(vget_high_s16(m16)));
            u = vcombine_s16(vshr_n_s16(vshrn_n_s32(vmlal_s16(vU0, vdu, vget_low_s16(t)), 16), 3),
                             vshr_n_s16(vshrn_n_s32(vmlal_s16(vU0, vdu, vget_high_s16(t)), 16), 3));
            v = vcombine_s16(vshr_n_s16(vshrn_n_s32(vmlal_s16(vV0, vdv, vget_low_s16(t)), 16), 3),
                             vshr_n_s16(vshrn_n_s32(vmlal_s16(vV0, vdv, vget_high_s16(t)), 16), 3));
            if (sm == 0)
                s = vminq_s16(vmaxq_s16(u, z16), wm1);
            else if (sm == 1)
                s = vandq_s16(u, wm1);
            else
                s = vandq_s16(veorq_s16(u, vreinterpretq_s16_u16(vtstq_s16(u, wb))), wm1);
            if (tm == 0)
                tt = vminq_s16(vmaxq_s16(v, z16), hm1);
            else if (tm == 1)
                tt = vandq_s16(v, hm1);
            else
                tt = vandq_s16(veorq_s16(v, vreinterpretq_s16_u16(vtstq_s16(v, hb))), hm1);
            vst1q_u32(adr + k, vandq_u32(m0, vmlal_u16(vmovl_u16(vreinterpret_u16_s16(vget_low_s16(s))),
                                                        vreinterpret_u16_s16(vget_low_s16(tt)), vtw)));
            vst1q_u32(adr + k + 4, vandq_u32(m1, vmlal_u16(vmovl_u16(vreinterpret_u16_s16(vget_high_s16(s))),
                                                            vreinterpret_u16_s16(vget_high_s16(tt)), vtw)));
        }
        /* pass 2 */
        if (f3d_skip & 2) ;
        else if (paletted) {
            for (k = 0; k < n; k += 4) {
                u32 a0 = adr[k], a1 = adr[k + 1], a2 = adr[k + 2], a3 = adr[k + 3];
                u32 i0 = data[a0], i1 = data[a1], i2 = data[a2], i3 = data[a3];
                tex[k] = pal[i0]; tex[k + 1] = pal[i1]; tex[k + 2] = pal[i2]; tex[k + 3] = pal[i3];
            }
        } else {
            u32 *d32 = (u32 *)data;
            for (k = 0; k < n; k += 4) {
                u32 a0 = adr[k], a1 = adr[k + 1], a2 = adr[k + 2], a3 = adr[k + 3];
                tex[k] = d32[a0]; tex[k + 1] = d32[a1]; tex[k + 2] = d32[a2]; tex[k + 3] = d32[a3];
            }
        }
        /* pass 3 */
        if (!(f3d_skip & 4)) for (k = 0; k < n; k += 8) {
            uint8x8x4_t T = vld4_u8((u8 *)(tex + k));
            uint8x8_t ao, am;
            uint16x8_t am16;
            uint32x4_t m0, m1, o0, o1;
            uint8x8x2_t z;
            if (gour) {
                vcr = vld1_u8(crb + k);
                vcg = vld1_u8(cgb + k);
                vcb = vld1_u8(cbb + k);
            }
            T.val[0] = vshrn_n_u16(vmlal_u8(vaddl_u8(vcr, T.val[0]), vcr, T.val[0]), 6);
            T.val[1] = vshrn_n_u16(vmlal_u8(vaddl_u8(vcg, T.val[1]), vcg, T.val[1]), 6);
            T.val[2] = vshrn_n_u16(vmlal_u8(vaddl_u8(vcb, T.val[2]), vcb, T.val[2]), 6);
            ao = vshrn_n_u16(vmlal_u8(vaddl_u8(vpa, T.val[3]), vpa, T.val[3]), 5);
            am = vand_u8(vcgt_u8(ao, varef), vld1_u8(msk + k));
            if (alpha) {
                uint8x8x4_t C = vld4_u8((u8 *)(cp + k));
                uint8x8_t da = vand_u8(C.val[3], v1f), oa = vmax_u8(ao, da), I = vld1_u8(ids + x0 + k), opq = vceq_u8(ao, v1f), m2, w8;
                int16x8_t m16a, o16a, w16a;
                uint32x4_t D0 = vld1q_u32(dp + k), D1 = vld1q_u32(dp + k + 4), N0 = vld1q_u32(dv + k), N1 = vld1q_u32(dv + k + 4), o0, o1, w0, w1;
                any8 = vorr_u8(any8, am);
                if (blend) {
                    uint8x8_t dz = vceq_u8(da, vz8), a1 = vbsl_u8(dz, v1f, ao), a2 = vbic_u8(vsub_u8(v1f, ao), dz);
                    T.val[0] = vshrn_n_u16(vmlal_u8(vmlal_u8(vmovl_u8(T.val[0]), T.val[0], a1), C.val[0], a2), 5);
                    T.val[1] = vshrn_n_u16(vmlal_u8(vmlal_u8(vmovl_u8(T.val[1]), T.val[1], a1), C.val[1], a2), 5);
                    T.val[2] = vshrn_n_u16(vmlal_u8(vmlal_u8(vmovl_u8(T.val[2]), T.val[2], a1), C.val[2], a2), 5);
                }
                m2 = vbic_u8(am, vbic_u8(vceq_u8(I, vpid8), opq));   /* same translucent id already here: not drawn */
                if (fog)
                    oa = vorr_u8(oa, vbsl_u8(opq, v80, vand_u8(C.val[3], v80)));
                C.val[0] = vbsl_u8(m2, T.val[0], C.val[0]);
                C.val[1] = vbsl_u8(m2, T.val[1], C.val[1]);
                C.val[2] = vbsl_u8(m2, T.val[2], C.val[2]);
                C.val[3] = vbsl_u8(m2, oa, C.val[3]);
                vst4_u8((u8 *)(cp + k), C);
                vst1_u8(ids + x0 + k, vbsl_u8(vbic_u8(m2, opq), vpid8, I));
                o16a = vmovl_s8(vreinterpret_s8_u8(opq));
                o0 = vreinterpretq_u32_s32(vmovl_s16(vget_low_s16(o16a)));
                o1 = vreinterpretq_u32_s32(vmovl_s16(vget_high_s16(o16a)));
                if (dbit) {   /* depth of every drawn pixel; the top byte (polygon id, flags) only where opaque */
                    w8 = m2;
                    N0 = vbslq_u32(o0, N0, vbslq_u32(m24, N0, D0));
                    N1 = vbslq_u32(o1, N1, vbslq_u32(m24, N1, D1));
                } else
                    w8 = vand_u8(m2, opq);
                (void)m16a;
                w16a = vmovl_s8(vreinterpret_s8_u8(w8));
                w0 = vreinterpretq_u32_s32(vmovl_s16(vget_low_s16(w16a)));
                w1 = vreinterpretq_u32_s32(vmovl_s16(vget_high_s16(w16a)));
                vst1q_u32(dp + k, vbslq_u32(w0, N0, D0));
                vst1q_u32(dp + k + 4, vbslq_u32(w1, N1, D1));
                continue;
            }
            T.val[3] = vorr_u8(ao, vfog);
            /* interleave the 4 byte planes back into 8 pixels (as vst4 would) */
            {
                uint8x8x2_t rg = vzip_u8(T.val[0], T.val[1]), ba = vzip_u8(T.val[2], T.val[3]);
                uint16x4x2_t lo = vzip_u16(vreinterpret_u16_u8(rg.val[0]), vreinterpret_u16_u8(ba.val[0])),
                             hi = vzip_u16(vreinterpret_u16_u8(rg.val[1]), vreinterpret_u16_u8(ba.val[1]));
                o0 = vreinterpretq_u32_u16(vcombine_u16(lo.val[0], lo.val[1]));
                o1 = vreinterpretq_u32_u16(vcombine_u16(hi.val[0], hi.val[1]));
            }
            (void)z;
            am16 = vreinterpretq_u16_s16(vmovl_s8(vreinterpret_s8_u8(am)));
            m0 = vreinterpretq_u32_s32(vmovl_s16(vreinterpret_s16_u16(vget_low_u16(am16))));
            m1 = vreinterpretq_u32_s32(vmovl_s16(vreinterpret_s16_u16(vget_high_u16(am16))));
            anyv = vorrq_u32(anyv, vorrq_u32(m0, m1));
            vst1q_u32(cp + k, vbslq_u32(m0, o0, vld1q_u32(cp + k)));
            vst1q_u32(cp + k + 4, vbslq_u32(m1, o1, vld1q_u32(cp + k + 4)));
            vst1q_u32(dp + k, vbslq_u32(m0, vld1q_u32(dv + k), vld1q_u32(dp + k)));
            vst1q_u32(dp + k + 4, vbslq_u32(m1, vld1q_u32(dv + k + 4), vld1q_u32(dp + k + 4)));
        }
    }
    anyv = vorrq_u32(anyv, vreinterpretq_u32_u8(vcombine_u8(any8, any8)));
    return (vgetq_lane_u32(anyv, 0) | vgetq_lane_u32(anyv, 1) | vgetq_lane_u32(anyv, 2) | vgetq_lane_u32(anyv, 3)) != 0;
}

