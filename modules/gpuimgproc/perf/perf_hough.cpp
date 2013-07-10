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
// Copyright (C) 2000-2008, Intel Corporation, all rights reserved.
// Copyright (C) 2009, Willow Garage Inc., all rights reserved.
// Third party copyrights are property of their respective owners.
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
// This software is provided by the copyright holders and contributors "as is" and
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

#include "perf_precomp.hpp"

using namespace std;
using namespace testing;
using namespace perf;

//////////////////////////////////////////////////////////////////////
// HoughLines

namespace
{
    struct Vec4iComparator
    {
        bool operator()(const cv::Vec4i& a, const cv::Vec4i b) const
        {
            if (a[0] != b[0]) return a[0] < b[0];
            else if(a[1] != b[1]) return a[1] < b[1];
            else if(a[2] != b[2]) return a[2] < b[2];
            else return a[3] < b[3];
        }
    };
    struct Vec3fComparator
    {
        bool operator()(const cv::Vec3f& a, const cv::Vec3f b) const
        {
            if(a[0] != b[0]) return a[0] < b[0];
            else if(a[1] != b[1]) return a[1] < b[1];
            else return a[2] < b[2];
        }
    };
    struct Vec2fComparator
    {
        bool operator()(const cv::Vec2f& a, const cv::Vec2f b) const
        {
            if(a[0] != b[0]) return a[0] < b[0];
            else return a[1] < b[1];
        }
    };
}

PERF_TEST_P(Sz, HoughLines,
            GPU_TYPICAL_MAT_SIZES)
{
    declare.time(30.0);

    const cv::Size size = GetParam();

    const float rho = 1.0f;
    const float theta = static_cast<float>(CV_PI / 180.0);
    const int threshold = 300;

    cv::Mat src(size, CV_8UC1, cv::Scalar::all(0));
    cv::line(src, cv::Point(0, 100), cv::Point(src.cols, 100), cv::Scalar::all(255), 1);
    cv::line(src, cv::Point(0, 200), cv::Point(src.cols, 200), cv::Scalar::all(255), 1);
    cv::line(src, cv::Point(0, 400), cv::Point(src.cols, 400), cv::Scalar::all(255), 1);
    cv::line(src, cv::Point(100, 0), cv::Point(100, src.rows), cv::Scalar::all(255), 1);
    cv::line(src, cv::Point(200, 0), cv::Point(200, src.rows), cv::Scalar::all(255), 1);
    cv::line(src, cv::Point(400, 0), cv::Point(400, src.rows), cv::Scalar::all(255), 1);

    if (PERF_RUN_GPU())
    {
        const cv::gpu::GpuMat d_src(src);
        cv::gpu::GpuMat d_lines;
        cv::gpu::HoughLinesBuf d_buf;

        TEST_CYCLE() cv::gpu::HoughLines(d_src, d_lines, d_buf, rho, theta, threshold);

        cv::Mat gpu_lines(d_lines.row(0));
        cv::Vec2f* begin = gpu_lines.ptr<cv::Vec2f>(0);
        cv::Vec2f* end = begin + gpu_lines.cols;
        std::sort(begin, end, Vec2fComparator());
        SANITY_CHECK(gpu_lines);
    }
    else
    {
        std::vector<cv::Vec2f> cpu_lines;

        TEST_CYCLE() cv::HoughLines(src, cpu_lines, rho, theta, threshold);

        SANITY_CHECK(cpu_lines);
    }
}

//////////////////////////////////////////////////////////////////////
// HoughLinesP

DEF_PARAM_TEST_1(Image, std::string);

PERF_TEST_P(Image, HoughLinesP,
            testing::Values("cv/shared/pic5.png", "stitching/a1.png"))
{
    declare.time(30.0);

    const std::string fileName = getDataPath(GetParam());

    const float rho = 1.0f;
    const float theta = static_cast<float>(CV_PI / 180.0);
    const int threshold = 100;
    const int minLineLenght = 50;
    const int maxLineGap = 5;

    const cv::Mat image = cv::imread(fileName, cv::IMREAD_GRAYSCALE);
    ASSERT_FALSE(image.empty());

    cv::Mat mask;
    cv::Canny(image, mask, 50, 100);

    if (PERF_RUN_GPU())
    {
        const cv::gpu::GpuMat d_mask(mask);
        cv::gpu::GpuMat d_lines;
        cv::gpu::HoughLinesBuf d_buf;

        TEST_CYCLE() cv::gpu::HoughLinesP(d_mask, d_lines, d_buf, rho, theta, minLineLenght, maxLineGap);

        cv::Mat gpu_lines(d_lines);
        cv::Vec4i* begin = gpu_lines.ptr<cv::Vec4i>();
        cv::Vec4i* end = begin + gpu_lines.cols;
        std::sort(begin, end, Vec4iComparator());
        SANITY_CHECK(gpu_lines);
    }
    else
    {
        std::vector<cv::Vec4i> cpu_lines;

        TEST_CYCLE() cv::HoughLinesP(mask, cpu_lines, rho, theta, threshold, minLineLenght, maxLineGap);

        SANITY_CHECK(cpu_lines);
    }
}

//////////////////////////////////////////////////////////////////////
// HoughCircles

DEF_PARAM_TEST(Sz_Dp_MinDist, cv::Size, float, float);

PERF_TEST_P(Sz_Dp_MinDist, HoughCircles,
            Combine(GPU_TYPICAL_MAT_SIZES,
                    Values(1.0f, 2.0f, 4.0f),
                    Values(1.0f)))
{
    declare.time(30.0);

    const cv::Size size = GET_PARAM(0);
    const float dp = GET_PARAM(1);
    const float minDist = GET_PARAM(2);

    const int minRadius = 10;
    const int maxRadius = 30;
    const int cannyThreshold = 100;
    const int votesThreshold = 15;

    cv::Mat src(size, CV_8UC1, cv::Scalar::all(0));
    cv::circle(src, cv::Point(100, 100), 20, cv::Scalar::all(255), -1);
    cv::circle(src, cv::Point(200, 200), 25, cv::Scalar::all(255), -1);
    cv::circle(src, cv::Point(200, 100), 25, cv::Scalar::all(255), -1);

    if (PERF_RUN_GPU())
    {
        const cv::gpu::GpuMat d_src(src);
        cv::gpu::GpuMat d_circles;
        cv::gpu::HoughCirclesBuf d_buf;

        TEST_CYCLE() cv::gpu::HoughCircles(d_src, d_circles, d_buf, cv::HOUGH_GRADIENT, dp, minDist, cannyThreshold, votesThreshold, minRadius, maxRadius);

        cv::Mat gpu_circles(d_circles);
        cv::Vec3f* begin = gpu_circles.ptr<cv::Vec3f>(0);
        cv::Vec3f* end = begin + gpu_circles.cols;
        std::sort(begin, end, Vec3fComparator());
        SANITY_CHECK(gpu_circles);
    }
    else
    {
        std::vector<cv::Vec3f> cpu_circles;

        TEST_CYCLE() cv::HoughCircles(src, cpu_circles, cv::HOUGH_GRADIENT, dp, minDist, cannyThreshold, votesThreshold, minRadius, maxRadius);

        SANITY_CHECK(cpu_circles);
    }
}

//////////////////////////////////////////////////////////////////////
// GeneralizedHough

enum { GHT_POSITION = cv::GeneralizedHough::GHT_POSITION,
       GHT_SCALE    = cv::GeneralizedHough::GHT_SCALE,
       GHT_ROTATION = cv::GeneralizedHough::GHT_ROTATION
     };

CV_FLAGS(GHMethod, GHT_POSITION, GHT_SCALE, GHT_ROTATION);

DEF_PARAM_TEST(Method_Sz, GHMethod, cv::Size);

PERF_TEST_P(Method_Sz, GeneralizedHough,
            Combine(Values(GHMethod(GHT_POSITION), GHMethod(GHT_POSITION | GHT_SCALE), GHMethod(GHT_POSITION | GHT_ROTATION), GHMethod(GHT_POSITION | GHT_SCALE | GHT_ROTATION)),
                    GPU_TYPICAL_MAT_SIZES))
{
    declare.time(10);

    const int method = GET_PARAM(0);
    const cv::Size imageSize = GET_PARAM(1);

    const cv::Mat templ = readImage("cv/shared/templ.png", cv::IMREAD_GRAYSCALE);
    ASSERT_FALSE(templ.empty());

    cv::Mat image(imageSize, CV_8UC1, cv::Scalar::all(0));
    templ.copyTo(image(cv::Rect(50, 50, templ.cols, templ.rows)));

    cv::RNG rng(123456789);
    const int objCount = rng.uniform(5, 15);
    for (int i = 0; i < objCount; ++i)
    {
        double scale = rng.uniform(0.7, 1.3);
        bool rotate = 1 == rng.uniform(0, 2);

        cv::Mat obj;
        cv::resize(templ, obj, cv::Size(), scale, scale);
        if (rotate)
            obj = obj.t();

        cv::Point pos;

        pos.x = rng.uniform(0, image.cols - obj.cols);
        pos.y = rng.uniform(0, image.rows - obj.rows);

        cv::Mat roi = image(cv::Rect(pos, obj.size()));
        cv::add(roi, obj, roi);
    }

    cv::Mat edges;
    cv::Canny(image, edges, 50, 100);

    cv::Mat dx, dy;
    cv::Sobel(image, dx, CV_32F, 1, 0);
    cv::Sobel(image, dy, CV_32F, 0, 1);

    if (PERF_RUN_GPU())
    {
        const cv::gpu::GpuMat d_edges(edges);
        const cv::gpu::GpuMat d_dx(dx);
        const cv::gpu::GpuMat d_dy(dy);
        cv::gpu::GpuMat posAndVotes;

        cv::Ptr<cv::gpu::GeneralizedHough_GPU> d_hough = cv::gpu::GeneralizedHough_GPU::create(method);
        if (method & GHT_ROTATION)
        {
            d_hough->set("maxAngle", 90.0);
            d_hough->set("angleStep", 2.0);
        }

        d_hough->setTemplate(cv::gpu::GpuMat(templ));

        TEST_CYCLE() d_hough->detect(d_edges, d_dx, d_dy, posAndVotes);

        const cv::gpu::GpuMat positions(1, posAndVotes.cols, CV_32FC4, posAndVotes.data);
        GPU_SANITY_CHECK(positions);
    }
    else
    {
        cv::Mat positions;

        cv::Ptr<cv::GeneralizedHough> hough = cv::GeneralizedHough::create(method);
        if (method & GHT_ROTATION)
        {
            hough->set("maxAngle", 90.0);
            hough->set("angleStep", 2.0);
        }

        hough->setTemplate(templ);

        TEST_CYCLE() hough->detect(edges, dx, dy, positions);

        CPU_SANITY_CHECK(positions);
    }
}
