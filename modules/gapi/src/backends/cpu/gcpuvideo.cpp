// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
//
// Copyright (C) 2020 Intel Corporation


#include "precomp.hpp"

#include <opencv2/gapi/video.hpp>
#include <opencv2/gapi/cpu/video.hpp>
#include <opencv2/gapi/cpu/gcpukernel.hpp>

#ifdef HAVE_OPENCV_VIDEO
#include <opencv2/video.hpp>
#endif // HAVE_OPENCV_VIDEO

#ifdef HAVE_OPENCV_VIDEO

GAPI_OCV_KERNEL(GCPUBuildOptFlowPyramid, cv::gapi::video::GBuildOptFlowPyramid)
{
    static void run(const cv::Mat              &img,
                    const cv::Size             &winSize,
                    const cv::Scalar           &maxLevel,
                          bool                  withDerivatives,
                          int                   pyrBorder,
                          int                   derivBorder,
                          bool                  tryReuseInputImage,
                          std::vector<cv::Mat> &outPyr,
                          cv::Scalar           &outMaxLevel)
    {
        outMaxLevel = cv::buildOpticalFlowPyramid(img, outPyr, winSize,
                                                  static_cast<int>(maxLevel[0]),
                                                  withDerivatives, pyrBorder,
                                                  derivBorder, tryReuseInputImage);
    }
};

GAPI_OCV_KERNEL(GCPUCalcOptFlowLK, cv::gapi::video::GCalcOptFlowLK)
{
    static void run(const cv::Mat                  &prevImg,
                    const cv::Mat                  &nextImg,
                    const std::vector<cv::Point2f> &prevPts,
                    const std::vector<cv::Point2f> &predPts,
                    const cv::Size                 &winSize,
                    const cv::Scalar               &maxLevel,
                    const cv::TermCriteria         &criteria,
                          int                       flags,
                          double                    minEigThresh,
                          std::vector<cv::Point2f> &outPts,
                          std::vector<uchar>       &status,
                          std::vector<float>       &err)
    {
        if (flags & cv::OPTFLOW_USE_INITIAL_FLOW)
            outPts = predPts;
        cv::calcOpticalFlowPyrLK(prevImg, nextImg, prevPts, outPts, status, err, winSize,
                                 static_cast<int>(maxLevel[0]), criteria, flags, minEigThresh);
    }
};

GAPI_OCV_KERNEL(GCPUCalcOptFlowLKForPyr, cv::gapi::video::GCalcOptFlowLKForPyr)
{
    static void run(const std::vector<cv::Mat>     &prevPyr,
                    const std::vector<cv::Mat>     &nextPyr,
                    const std::vector<cv::Point2f> &prevPts,
                    const std::vector<cv::Point2f> &predPts,
                    const cv::Size                 &winSize,
                    const cv::Scalar               &maxLevel,
                    const cv::TermCriteria         &criteria,
                          int                       flags,
                          double                    minEigThresh,
                          std::vector<cv::Point2f> &outPts,
                          std::vector<uchar>       &status,
                          std::vector<float>       &err)
    {
        if (flags & cv::OPTFLOW_USE_INITIAL_FLOW)
            outPts = predPts;
        cv::calcOpticalFlowPyrLK(prevPyr, nextPyr, prevPts, outPts, status, err, winSize,
                                 static_cast<int>(maxLevel[0]), criteria, flags, minEigThresh);
    }
};

GAPI_OCV_KERNEL_ST(GCPUBackgroundSubtractor,
                   cv::gapi::video::GBackgroundSubtractor,
                   cv::BackgroundSubtractor)
{
    static void setup(const cv::GMatDesc&, const cv::gapi::video::BackgroundSubtractorParams& bsParams,
                      std::shared_ptr<cv::BackgroundSubtractor>& state,
                      const cv::GCompileArgs&)
    {
        if (bsParams.operation == cv::gapi::video::TYPE_BS_MOG2)
            state = cv::createBackgroundSubtractorMOG2(bsParams.history,
                                                       bsParams.threshold,
                                                       bsParams.detectShadows);
        else if (bsParams.operation == cv::gapi::video::TYPE_BS_KNN)
            state = cv::createBackgroundSubtractorKNN(bsParams.history,
                                                      bsParams.threshold,
                                                      bsParams.detectShadows);

        GAPI_Assert(state);
    }

    static void run(const cv::Mat& in, const cv::gapi::video::BackgroundSubtractorParams& bsParams,
                    cv::Mat &out, cv::BackgroundSubtractor& state)
    {
        state.apply(in, out, bsParams.learningRate);
    }
};

GAPI_OCV_KERNEL_ST(GCPUKalmanFilter, cv::gapi::video::GKalmanFilter, cv::KalmanFilter)
{
    static void setup(const cv::GMatDesc&, const cv::GOpaqueDesc&,
                      const cv::GMatDesc&, const cv::gapi::video::KalmanParams& kfParams,
                      std::shared_ptr<cv::KalmanFilter> &state, const cv::GCompileArgs&)
    {

       // auto kfParams = cv::gapi::getCompileArg<cv::gapi::video::KalmanParams>(compileArgs)
         //               .value_or(cv::gapi::video::KalmanParams{});

        GAPI_Assert(kfParams.type == CV_32F || kfParams.type == CV_64F);
        state = std::make_shared<cv::KalmanFilter>(kfParams.dpDim, kfParams.mpDim, kfParams.ctrlDim, kfParams.type);

        // initial state
        kfParams.statePre.copyTo(state->statePre);
        kfParams.errorCovPre.copyTo(state->errorCovPre);

        // dynamic system initialization
        kfParams.controlMatrix.copyTo(state->controlMatrix);

        kfParams.measurementMatrix.copyTo(state->measurementMatrix);

        if (cv::norm(kfParams.transitionMatrix, cv::NORM_INF) != 0)
            kfParams.transitionMatrix.copyTo(state->transitionMatrix);

        if (cv::norm(kfParams.processNoiseCov, cv::NORM_INF) != 0)
            kfParams.processNoiseCov.copyTo(state->processNoiseCov);

        if (cv::norm(kfParams.measurementNoiseCov, cv::NORM_INF) != 0)
            kfParams.measurementNoiseCov.copyTo(state->measurementNoiseCov);

        GAPI_Assert(state);
    }

    static void run(const cv::Mat& measurements, bool haveMeasurement,
                    const cv::Mat& control, const cv::gapi::video::KalmanParams&,
                    cv::Mat &out, cv::KalmanFilter& state)
    {
        cv::Mat pre;

        if (!control.empty() && cv::norm(control, cv::NORM_INF) != 0)
            pre = state.predict(control);
        else
            pre = state.predict();

        if (haveMeasurement && !measurements.empty())
            state.correct(measurements).copyTo(out);
        else
            pre.copyTo(out);
    }
};

cv::gapi::GKernelPackage cv::gapi::video::cpu::kernels()
{
    static auto pkg = cv::gapi::kernels
        < GCPUBuildOptFlowPyramid
        , GCPUCalcOptFlowLK
        , GCPUCalcOptFlowLKForPyr
        , GCPUBackgroundSubtractor
        , GCPUKalmanFilter
        >();
    return pkg;
}

#else

cv::gapi::GKernelPackage cv::gapi::video::cpu::kernels()
{
    return GKernelPackage();
}

#endif // HAVE_OPENCV_VIDEO
