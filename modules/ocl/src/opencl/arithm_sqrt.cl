/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                           License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2010-2012, Institute Of Software Chinese Academy Of Science, all rights reserved.
// Copyright (C) 2010-2012, Advanced Micro Devices, Inc., all rights reserved.
// Third party copyrights are property of their respective owners.
//
// @Authors
//    Peter Andreas Entschev, peter@entschev.com
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * The name of the copyright holders may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// This software is provided by the copyright holders and contributors as is and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall the Intel Corporation or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//M*/

#ifdef DOUBLE_SUPPORT
#ifdef cl_amd_fp64
#pragma OPENCL EXTENSION cl_amd_fp64:enable
#elif defined (cl_khr_fp64)
#pragma OPENCL EXTENSION cl_khr_fp64:enable
#endif
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////LOG/////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

__kernel void arithm_sqrt_C1(__global srcT *src, __global srcT *dst,
    int cols1, int rows,
    int srcOffset1, int dstOffset1,
    int srcStep1, int dstStep1)
{
    int x = get_global_id(0);
    int y = get_global_id(1);

    if(x < cols1 && y < rows)
    {
        int srcIdx = mad24(y, srcStep1, x + srcOffset1);
        int dstIdx = mad24(y, dstStep1, x + dstOffset1);

        dst[dstIdx] = sqrt(src[srcIdx]);
    }
}

__kernel void arithm_sqrt_C2(__global srcT *src, __global srcT *dst,
    int cols1, int rows,
    int srcOffset1, int dstOffset1,
    int srcStep1, int dstStep1)
{
    int x1 = get_global_id(0) << 1;
    int y = get_global_id(1);

    if(x1 < cols1 && y < rows)
    {
        int srcIdx = mad24(y, srcStep1, x1 + srcOffset1);
        int dstIdx = mad24(y, dstStep1, x1 + dstOffset1);

        dst[dstIdx] =                      sqrt(src[srcIdx]);
        dst[dstIdx + 1] = x1 + 1 < cols1 ? sqrt(src[srcIdx + 1]) : dst[dstIdx + 1];
    }
}

__kernel void arithm_sqrt_C4(__global srcT *src, __global srcT *dst,
    int cols1, int rows,
    int srcOffset1, int dstOffset1,
    int srcStep1, int dstStep1)
{
    int x1 = get_global_id(0) << 2;
    int y = get_global_id(1);

    if(x1 < cols1 && y < rows)
    {
        int srcIdx = mad24(y, srcStep1, x1 + srcOffset1);
        int dstIdx = mad24(y, dstStep1, x1 + dstOffset1);

        dst[dstIdx] =                      sqrt(src[srcIdx]);
        dst[dstIdx + 1] = x1 + 1 < cols1 ? sqrt(src[srcIdx + 1]) : dst[dstIdx + 1];
        dst[dstIdx + 2] = x1 + 2 < cols1 ? sqrt(src[srcIdx + 2]) : dst[dstIdx + 2];
        dst[dstIdx + 3] = x1 + 3 < cols1 ? sqrt(src[srcIdx + 3]) : dst[dstIdx + 3];
    }
}
