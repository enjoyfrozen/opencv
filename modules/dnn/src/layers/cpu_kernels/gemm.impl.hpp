// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.

// This file is modified from the ficus (https://github.com/vpisarev/ficus/blob/master/runtime/ficus/impl/gemm.impl.h).
// Here is the original license:
/*
    This file is a part of ficus language project.
    See ficus/LICENSE for the licensing terms
*/

#ifndef OPENCV_DNN_GEMM_IMPL_HPP
#define OPENCV_DNN_GEMM_IMPL_HPP

// #include <arm_neon.h>

#include <iostream>
#include <functional>

#include <opencv2/dnn/shape_utils.hpp>
#include <opencv2/core/hal/intrin.hpp>

#define _FX_GEMM_STORAGE (1<<20) // 2^20
#define _FX_GEMM_MAX_STACKBUF (1 << 14)

#define _FX_SGEMM_MC 64
#define _FX_SGEMM_NC 240
#define _FX_SGEMM_VOL (1<<18) // 2^18
#define _FX_SGEMM_MR 8
#define _FX_SGEMM_NR 12

namespace cv { namespace dnn {

#define _FX_GEMM_IMPLEMENT_PACK(N, suffix, styp, dtyp) \
static void _fx_gemm_pack##N##suffix( int m, int k, const void* A_, \
                                      int lda0, int lda1, void* packA_ ) \
{ \
    const styp* A = (const styp*)A_; \
    dtyp* packA = (dtyp*)packA_; \
    for( int i = 0; i < m; i += N ) { \
        if (i + N-1 < m) { \
            const styp* a_ptr = A + lda0*i; \
            for( int j = 0; j < k*lda1; packA += N, j += lda1 ) \
            { \
                _FX_GEMM_LOAD_TO_BUF_##N(styp); \
                _FX_GEMM_PACK##suffix##_##N(buf, packA); \
            } \
        } else { \
            const styp* a_ptr[N]; \
            for (int k = 0; k < N; k++) a_ptr[k] = A + lda0*(i+k < m ? i+k : i); \
            for( int j = 0; j < k*lda1; packA += N, j += lda1 ) \
            { \
                _FX_GEMM_LOAD_TO_BUF_BORDERS_##N(styp); \
                _FX_GEMM_PACK##suffix##_##N(buf, packA); \
            } \
        } \
    } \
}

#define _FX_GEMM_LOAD_TO_BUF_6(styp) \
    styp buf[] = { \
        a_ptr[j], a_ptr[j+lda0], a_ptr[j+lda0*2], a_ptr[j+lda0*3], \
        a_ptr[j+lda0*4], a_ptr[j+lda0*5] }

#define _FX_GEMM_LOAD_TO_BUF_BORDERS_6(styp) \
    styp buf[] = { \
        a_ptr[0][j], a_ptr[1][j], a_ptr[2][j], a_ptr[3][j], \
        a_ptr[4][j], a_ptr[5][j] }

#define _FX_GEMM_LOAD_TO_BUF_8(styp) \
    styp buf[] = { \
        a_ptr[j], a_ptr[j+lda0], a_ptr[j+lda0*2], a_ptr[j+lda0*3], \
        a_ptr[j+lda0*4], a_ptr[j+lda0*5], a_ptr[j+lda0*6], a_ptr[j+lda0*7] }

#define _FX_GEMM_LOAD_TO_BUF_BORDERS_8(styp) \
    styp buf[] = { \
        a_ptr[0][j], a_ptr[1][j], a_ptr[2][j], a_ptr[3][j], \
        a_ptr[4][j], a_ptr[5][j], a_ptr[6][j], a_ptr[7][j] }

#define _FX_GEMM_LOAD_TO_BUF_12(styp) \
    styp buf[] = { \
        a_ptr[j], a_ptr[j+lda0], a_ptr[j+lda0*2], a_ptr[j+lda0*3], \
        a_ptr[j+lda0*4], a_ptr[j+lda0*5], a_ptr[j+lda0*6], a_ptr[j+lda0*7], \
        a_ptr[j+lda0*8], a_ptr[j+lda0*9], a_ptr[j+lda0*10], a_ptr[j+lda0*11] }

#define _FX_GEMM_LOAD_TO_BUF_BORDERS_12(styp) \
    styp buf[] = { \
        a_ptr[0][j], a_ptr[1][j], a_ptr[2][j], a_ptr[3][j], \
        a_ptr[4][j], a_ptr[5][j], a_ptr[6][j], a_ptr[7][j], \
        a_ptr[8][j], a_ptr[9][j], a_ptr[10][j], a_ptr[11][j] }

#define _FX_GEMM_PACK_COPY(src, dst, N) \
    memcpy((dst), (src), N*sizeof(src[0]))
#define _FX_GEMM_PACK_f32_8(src, dst) _FX_GEMM_PACK_COPY((src), (dst), 8)
#define _FX_GEMM_PACK_f32_12(src, dst) _FX_GEMM_PACK_COPY((src), (dst), 12)

_FX_GEMM_IMPLEMENT_PACK(8, _f32, float, float)
_FX_GEMM_IMPLEMENT_PACK(12, _f32, float, float)

static void fx_gemm8x12_f32(int k, const char *a_, const char *b_,
                            char *c_, int ldc, const void* palpha)
{
    const float* a = (const float*)a_;
    const float* b = (const float*)b_;
    float* c = (float*)c_;
    float alpha = *(const float*)palpha;

#ifdef CV_SIMD128
    v_float32x4 s00 = v_setzero_f32(), s01 = s00, s02 = s00;
    v_float32x4 s10 = s00, s11 = s00, s12 = s00;
    v_float32x4 s20 = s00, s21 = s00, s22 = s00;
    v_float32x4 s30 = s00, s31 = s00, s32 = s00;
    v_float32x4 s40 = s00, s41 = s00, s42 = s00;
    v_float32x4 s50 = s00, s51 = s00, s52 = s00;
    v_float32x4 s60 = s00, s61 = s00, s62 = s00;
    v_float32x4 s70 = s00, s71 = s00, s72 = s00;

    for(int p = 0; p < k; p++, a += _FX_SGEMM_MR, b += _FX_SGEMM_NR) {
        // v_float32x4 a0 = v_load(a);
        v_float32x4 b0 = v_load(b), b1 = v_load(b + 4), b2 = v_load(b + 8);

        // s00 = vfmaq_laneq_f32(s00, b0, a0, 0);
        // s01 = vfmaq_laneq_f32(s01, b1, a0, 0);
        // s02 = vfmaq_laneq_f32(s02, b2, a0, 0);
        // s10 = vfmaq_laneq_f32(s10, b0, a0, 1);
        // s11 = vfmaq_laneq_f32(s11, b1, a0, 1);
        // s12 = vfmaq_laneq_f32(s12, b2, a0, 1);
        v_float32x4 a0 = v_setall_f32(*a);
        s00 = v_fma(b0, a0, s00);
        s01 = v_fma(b1, a0, s01);
        s02 = v_fma(b2, a0, s02);
        v_float32x4 a1 = v_setall_f32(*(a + 1));
        s10 = v_fma(b0, a1, s10);
        s11 = v_fma(b1, a1, s11);
        s12 = v_fma(b2, a1, s12);

        // s20 = vfmaq_laneq_f32(s20, b0, a0, 2);
        // s21 = vfmaq_laneq_f32(s21, b1, a0, 2);
        // s22 = vfmaq_laneq_f32(s22, b2, a0, 2);
        // s30 = vfmaq_laneq_f32(s30, b0, a0, 3);
        // s31 = vfmaq_laneq_f32(s31, b1, a0, 3);
        // s32 = vfmaq_laneq_f32(s32, b2, a0, 3);
        v_float32x4 a2 = v_setall_f32(*(a + 2));
        s20 = v_fma(b0, a2, s20);
        s21 = v_fma(b1, a2, s21);
        s22 = v_fma(b2, a2, s22);
        v_float32x4 a3 = v_setall_f32(*(a + 3));
        s30 = v_fma(b0, a3, s30);
        s31 = v_fma(b1, a3, s31);
        s32 = v_fma(b2, a3, s32);

        // a0 = vld1q_f32(a + 4);

        // s40 = vfmaq_laneq_f32(s40, b0, a0, 0);
        // s41 = vfmaq_laneq_f32(s41, b1, a0, 0);
        // s42 = vfmaq_laneq_f32(s42, b2, a0, 0);
        // s50 = vfmaq_laneq_f32(s50, b0, a0, 1);
        // s51 = vfmaq_laneq_f32(s51, b1, a0, 1);
        // s52 = vfmaq_laneq_f32(s52, b2, a0, 1);
        a0 = v_setall_f32(*(a + 4));
        s40 = v_fma(b0, a0, s40);
        s41 = v_fma(b1, a0, s41);
        s42 = v_fma(b2, a0, s42);
        a1 = v_setall_f32(*(a + 5));
        s50 = v_fma(b0, a1, s50);
        s51 = v_fma(b1, a1, s51);
        s52 = v_fma(b2, a1, s52);

        // s60 = vfmaq_laneq_f32(s60, b0, a0, 2);
        // s61 = vfmaq_laneq_f32(s61, b1, a0, 2);
        // s62 = vfmaq_laneq_f32(s62, b2, a0, 2);
        // s70 = vfmaq_laneq_f32(s70, b0, a0, 3);
        // s71 = vfmaq_laneq_f32(s71, b1, a0, 3);
        // s72 = vfmaq_laneq_f32(s72, b2, a0, 3);
        a2 = v_setall_f32(*(a + 6));
        s60 = v_fma(b0, a2, s60);
        s61 = v_fma(b1, a2, s61);
        s62 = v_fma(b2, a2, s62);
        a3 = v_setall_f32(*(a + 7));
        s70 = v_fma(b0, a3, s70);
        s71 = v_fma(b1, a3, s71);
        s72 = v_fma(b2, a3, s72);
    }

    v_float32x4 c0, c1, c2, c3, c4, c5, v_alpha = v_setall_f32(alpha);
#define V_SGEMM_FINALE(row0, row1)       \
    c0 = v_load(c + row0 * ldc);         \
    c1 = v_load(c + row0 * ldc + 4);     \
    c2 = v_load(c + row0 * ldc + 8);     \
    c3 = v_load(c + row1 * ldc);         \
    c4 = v_load(c + row1 * ldc + 4);     \
    c5 = v_load(c + row1 * ldc + 8);     \
    c0 = v_fma(s##row0##0, v_alpha, c0); \
    c1 = v_fma(s##row0##1, v_alpha, c1); \
    c2 = v_fma(s##row0##2, v_alpha, c2); \
    c3 = v_fma(s##row1##0, v_alpha, c3); \
    c4 = v_fma(s##row1##1, v_alpha, c4); \
    c5 = v_fma(s##row1##2, v_alpha, c5); \
    v_store(c + row0 * ldc, c0);         \
    v_store(c + row0 * ldc + 4, c1);     \
    v_store(c + row0 * ldc + 8, c2);     \
    v_store(c + row1 * ldc, c3);         \
    v_store(c + row1 * ldc + 4, c4);     \
    v_store(c + row1 * ldc + 8, c5);

    V_SGEMM_FINALE(0, 1);
    V_SGEMM_FINALE(2, 3);
    V_SGEMM_FINALE(4, 5);
    V_SGEMM_FINALE(6, 7);
#undef V_SGEMM_FINALE

#else
    float sbuf[_FX_SGEMM_MR*_FX_SGEMM_NR];
    memset(sbuf, 0, sizeof(sbuf));
    for(int p = 0; p < k; p++) {
        for( int i = 0; i < _FX_SGEMM_MR; i++ ) {
            float ai = a[_FX_SGEMM_MR*p + i];
            for( int j = 0; j < _FX_SGEMM_NR; j++ )
                sbuf[i*_FX_SGEMM_NR+j] += b[_FX_SGEMM_NR*p + j]*ai;
        }
    }
    for (int i = 0; i < _FX_SGEMM_MR; i++) {
        for (int j = 0; j < _FX_SGEMM_NR; j++)
            c[i*ldc + j] += alpha*sbuf[i*_FX_SGEMM_NR+j];
    }
#endif
}

static void fx_gemm_macro_kernel(int m, int n, int k,
                                 const char *packA, const char *packB,
                                 const void* palpha, char *c, int ldc0,
                                 int MR, int NR) {
    int esz = sizeof(float);
    int ldc0_esz = ldc0 * esz;

    double tempC[_FX_SGEMM_MR*_FX_SGEMM_NR]; // make sure the buffer is big enough
    for(int i = 0; i < m; i += MR) {
        for(int j = 0; j < n; j += NR) {
            char* cptr0 = &c[i * ldc0_esz + j * esz];
            char* cptr = cptr0;
            int ldc = ldc0;
            int mr = m - i < MR ? m - i : MR;
            int nr = n - j < NR ? n - j : NR;
            int nr_esz = nr*esz;
            bool partial = (bool)((mr < MR) | (nr < NR));
            if (partial) {
                memset(tempC, 0, sizeof(tempC));
                cptr = (char*)tempC;
                ldc = NR;
                for(int p = 0; p < mr; p++)
                    memcpy(cptr + p*(ldc*esz), cptr0 + p*ldc0_esz, nr_esz);
            }

            fx_gemm8x12_f32(k, packA + i * k * esz, packB + j * k * esz, cptr, ldc, palpha);

            if (partial) {
                for(int p = 0; p < mr; p++)
                    memcpy(cptr0 + p*ldc0_esz, cptr + p*(ldc*esz), nr_esz);
            }
        }
    }
}

static void fx_gemm_thin(float alpha, float beta, int M, int N, int K,
                         const void *a_, int lda0, int lda1,
                         const void *b_, int ldb,
                         void *c_, int ldc) {
    int nsubtasks = 1, Nblocks = 1;
    int num_threads = getNumThreads();
    if (num_threads > 1 && (uint64_t)M * N * K >= 100000) {
        if (M < num_threads)
            Nblocks = num_threads / M; // why?
    } else {
        num_threads = 1;
    }
    nsubtasks = M * Nblocks;

    const float* a = (const float*)a_;

    auto fn = [&](const Range &r) {
        for(int start = r.start ; start < r.end; start++ ) {
            int i = start / Nblocks;
            int nb = start - i * Nblocks;
            int j0 = nb * N / Nblocks, j1 = (nb + 1) * N / Nblocks;
            int j, k;
            float* c_i = (float*)c_ + i * ldc;
            if (beta == 0.f)
                for( j = j0; j < j1; j++ ) c_i[j] = 0.f;
            else if (beta != 1.f)
                for( j = j0; j < j1; j++ ) c_i[j] *= beta;
            for( k = 0; k < K; k++ ) {
                const float* b_k = (const float*)b_ + k * ldb;
                float aval = alpha * a[i * lda0 + k * lda1];
                for( j = j0; j < j1; j++ )
                    c_i[j] += aval * b_k[j];
            }
        }
    };

    int total = nsubtasks; // outer loops
    int cost_per_thread = static_cast<int>(K * N); // inner loops
    double nstripes = (size_t)total * cost_per_thread * (1 / 1024.0);
    parallel_for_(Range(0, total), fn, nstripes);
}

void ocv_gemm(bool trans_a, bool trans_b,
              float alpha, const Mat &A, const Mat &B,
              float beta, Mat &C) {
    CV_CheckTypeEQ(A.type(), B.type(), "DNN/gemm: A and B should have the same type");
    CV_CheckTypeEQ(B.type(), C.type(), "DNN/gemm: B and C should have the same type");
    CV_CheckTypeEQ(A.type(), CV_32F, "DNN/gemm: only support float32 for now");

    const auto shape_a = shape(A);
    const auto shape_b = shape(B);
    const auto shape_c = shape(C);

    int ma = shape_a[0], na = shape_a[1];
    int mb = shape_b[0], nb = shape_b[1];

    int M = trans_a ? na : ma;
    int N = trans_b ? mb : nb;
    int K = trans_a ? ma : na;
    int lda0 = na, lda1 = 1, ldb0 = nb, ldb1 = 1, ldc = shape_c[1];
    if (trans_a) {
        std::swap(lda0, lda1);
    }
    if (trans_b) {
        std::swap(ldb0, ldb1);
    }

    /*
    std::cout << cv::format("trans_a=%d, trans_b=%d, m=%d, n=%d, k=%d, alpha=%f, beta=%f, lda0=%d, lda1=%d, ldb0=%d, ldb1=%d\n", static_cast<int>(trans_a), static_cast<int>(trans_b), m, n, k, alphaf, betaf, lda0, lda1, ldb0, ldb1);
    */

    const char *a = A.ptr<const char>();
    const char *b = B.ptr<const char>();
    char *c = C.ptr<char>();

    const void* palpha = (const void*)&alpha;

    if (!trans_b && ldb1 == 1 && (M <= 4 || (uint64_t)M * N * K <= 10000)) {
        return fx_gemm_thin(alpha, beta, M, N, K, a, lda0, lda1, b, ldb0, c, ldc);
    }

    int esz = sizeof(float);

    int GEMM_MC = _FX_SGEMM_MC,
        GEMM_NC = _FX_SGEMM_NC,
        // GEMM_VOL = _FX_SGEMM_VOL,
        GEMM_MR = _FX_SGEMM_MR,
        GEMM_NR = _FX_SGEMM_NR;

    int MC = (((GEMM_MC < M ? GEMM_MC : M) + GEMM_MR - 1) / GEMM_MR) * GEMM_MR;
    int NC = (((GEMM_NC < N ? GEMM_NC : N) + GEMM_NR - 1) / GEMM_NR) * GEMM_NR;
    int KC = _FX_GEMM_STORAGE / ((MC + NC) * esz);
    KC = KC > 8 ? KC : 8;
    KC = KC < K ? KC : K;

    size_t buff_size = KC * (MC + NC) * esz;
    bool use_stackbuff = buff_size <= _FX_GEMM_MAX_STACKBUF;
    int m_tiles = (M + MC - 1) / MC;
    int n_tiles = (N + NC - 1) / NC;
    int total_tiles = m_tiles * n_tiles;

    std::function<void(int, int, const void*, int, int, void*)> a_packer, b_packer;
    a_packer = _fx_gemm_pack8_f32;
    b_packer = _fx_gemm_pack12_f32;

    int total = total_tiles;
    int cost_per_thread = static_cast<int>((K / KC) * (MC / GEMM_MR) * (NC / GEMM_NR));
    double nstripes = (size_t)total * cost_per_thread * (1 / 1024.0);

    auto fn = [&](const Range &r) {
        char* pack_a = (char*)(use_stackbuff ? alloca(buff_size) : malloc(buff_size));
        char* pack_b = pack_a + KC * MC * esz;
        int start = r.start;
        int end = r.end;

        for (int tile_idx = start; tile_idx < end; tile_idx++) {
            int i0 = (tile_idx / n_tiles) * MC;
            int j0 = (tile_idx % n_tiles) * NC;
            int mc = M - i0 < MC ? M - i0 : MC;
            int nc = N - j0 < NC ? N - j0 : NC;
            int ldc_block = ldc;
            char* c_block = c + (i0 * ldc + j0) * esz;

            if (beta == 0.f) {
                for(int i = 0; i < mc; i++)
                    memset(c_block + i * ldc_block * esz, 0, nc * esz);
            } else if (beta != 1.f) {
                for(int i = 0; i < mc; i++) {
                    float* c_i = (float*)c_block + i * ldc_block;
                    for(int j = 0; j < nc; j++)
                        c_i[j] *= beta;
                }
            }

            for(int k0 = 0; k0 < K; k0 += KC)
            {
                int kc = K - k0 < KC ? K - k0 : KC;
                a_packer(mc, kc, a + (i0 * lda0 + k0 * lda1) * esz, lda0, lda1, pack_a);
                b_packer(nc, kc, b + (k0 * ldb0 + j0 * ldb1) * esz, ldb1, ldb0, pack_b);
                fx_gemm_macro_kernel(mc, nc, kc, pack_a, pack_b, palpha,
                                     c_block, ldc_block, GEMM_MR, GEMM_NR);
            }
        }

        if (!use_stackbuff) {
            free(pack_a);
        }
    };

    parallel_for_(Range(0, total), fn, nstripes);
}

}} // cv::dnn

#endif // OPENCV_DNN_GEMM_IMPL_HPP
