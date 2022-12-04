/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                        Intel License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2008, 2011, Nils Hasler, all rights reserved.
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
//   * The name of Intel Corporation may not be used to endorse or promote products
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

/*!
 * \file cap_gstreamer.cpp
 * \author Nils Hasler <hasler@mpi-inf.mpg.de>
 *         Max-Planck-Institut Informatik
 * \author Dirk Van Haerenborgh <vhdirk@gmail.com>
 *
 * \brief Use GStreamer to read/write video
 */
#include "precomp.hpp"

#include <opencv2/core/utils/logger.hpp>
#include <opencv2/core/utils/filesystem.hpp>

#include <iostream>
#include <fstream>
#include <string.h>
#include <thread>
#include <queue>
#include <deque>
#include <cstddef>

#include <gst/gst.h>
#include <gst/gstbuffer.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/riff/riff-media.h>
#include <gst/pbutils/missing-plugins.h>

#define VERSION_NUM(major, minor, micro) (major * 1000000 + minor * 1000 + micro)
#define FULL_GST_VERSION VERSION_NUM(GST_VERSION_MAJOR, GST_VERSION_MINOR, GST_VERSION_MICRO)

#include <gst/pbutils/encoding-profile.h>
//#include <gst/base/gsttypefindhelper.h>

#define CV_WARN(...) CV_LOG_WARNING(NULL, "OpenCV | GStreamer warning: " << __VA_ARGS__)

#define COLOR_ELEM "videoconvert"
#define COLOR_ELEM_NAME COLOR_ELEM

#define CV_GST_FORMAT(format) (format)


namespace cv {

static void toFraction(double decimal, CV_OUT int& numerator, CV_OUT int& denominator);
static void handleMessage(GstElement * pipeline);


namespace {

#if defined __clang__
# pragma clang diagnostic push
# pragma clang diagnostic ignored "-Wunused-function"
#endif

template<typename T> static inline void GSafePtr_addref(T* ptr)
{
    if (ptr)
        g_object_ref_sink(ptr);
}

template<typename T> static inline void GSafePtr_release(T** pPtr);

template<> inline void GSafePtr_release<GError>(GError** pPtr) { g_clear_error(pPtr); }
template<> inline void GSafePtr_release<GstElement>(GstElement** pPtr) { if (pPtr) { gst_object_unref(G_OBJECT(*pPtr)); *pPtr = NULL; } }
template<> inline void GSafePtr_release<GstElementFactory>(GstElementFactory** pPtr) { if (pPtr) { gst_object_unref(G_OBJECT(*pPtr)); *pPtr = NULL; } }
template<> inline void GSafePtr_release<GstPad>(GstPad** pPtr) { if (pPtr) { gst_object_unref(G_OBJECT(*pPtr)); *pPtr = NULL; } }
template<> inline void GSafePtr_release<GstCaps>(GstCaps** pPtr) { if (pPtr) { gst_caps_unref(*pPtr); *pPtr = NULL; } }
template<> inline void GSafePtr_release<GstBuffer>(GstBuffer** pPtr) { if (pPtr) { gst_buffer_unref(*pPtr); *pPtr = NULL; } }
template<> inline void GSafePtr_release<GstSample>(GstSample** pPtr) { if (pPtr) { gst_sample_unref(*pPtr); *pPtr = NULL; } }
template<> inline void GSafePtr_release<GstBus>(GstBus** pPtr) { if (pPtr) { gst_object_unref(G_OBJECT(*pPtr)); *pPtr = NULL; } }
template<> inline void GSafePtr_release<GstMessage>(GstMessage** pPtr) { if (pPtr) { gst_message_unref(*pPtr); *pPtr = NULL; } }
template<> inline void GSafePtr_release<GMainLoop>(GMainLoop** pPtr) { if (pPtr) { g_main_loop_unref(*pPtr); *pPtr = NULL; } }

template<> inline void GSafePtr_release<GstEncodingVideoProfile>(GstEncodingVideoProfile** pPtr) { if (pPtr) { gst_encoding_profile_unref(*pPtr); *pPtr = NULL; } }
template<> inline void GSafePtr_release<GstEncodingContainerProfile>(GstEncodingContainerProfile** pPtr) { if (pPtr) { gst_object_unref(G_OBJECT(*pPtr)); *pPtr = NULL; } }

template<> inline void GSafePtr_addref<char>(char* pPtr);  // declaration only. not defined. should not be used
template<> inline void GSafePtr_release<char>(char** pPtr) { if (pPtr) { g_free(*pPtr); *pPtr = NULL; } }

#if defined __clang__
# pragma clang diagnostic pop
#endif

template <typename T>
class GSafePtr
{
protected:
    T* ptr;
public:
    inline GSafePtr() CV_NOEXCEPT : ptr(NULL) { }
    inline GSafePtr(T* p) : ptr(p) { }
    inline ~GSafePtr() CV_NOEXCEPT { release(); }
    inline void release() CV_NOEXCEPT
    {
#if 0
        printf("release: %s:%d: %p\n", CV__TRACE_FUNCTION, __LINE__, ptr);
        if (ptr) {
            printf("    refcount: %d\n", (int)GST_OBJECT_REFCOUNT_VALUE(ptr)); \
        }
#endif
        if (ptr)
            GSafePtr_release<T>(&ptr);
    }

    inline operator T* () CV_NOEXCEPT { return ptr; }
    inline operator /*const*/ T* () const CV_NOEXCEPT { return (T*)ptr; }  // there is no const correctness in Gst C API

    T* get() { CV_Assert(ptr); return ptr; }
    /*const*/ T* get() const { CV_Assert(ptr); return (T*)ptr; }  // there is no const correctness in Gst C API

    const T* operator -> () const { CV_Assert(ptr); return ptr; }
    inline operator bool () const CV_NOEXCEPT { return ptr != NULL; }
    inline bool operator ! () const CV_NOEXCEPT { return ptr == NULL; }

    T** getRef() { CV_Assert(ptr == NULL); return &ptr; }

    inline GSafePtr& reset(T* p) CV_NOEXCEPT // pass result of functions with "transfer floating" ownership
    {
        //printf("reset: %s:%d: %p\n", CV__TRACE_FUNCTION, __LINE__, p);
        release();
        if (p)
        {
            GSafePtr_addref<T>(p);
            ptr = p;
        }
        return *this;
    }

    inline GSafePtr& attach(T* p) CV_NOEXCEPT  // pass result of functions with "transfer full" ownership
    {
        //printf("attach: %s:%d: %p\n", CV__TRACE_FUNCTION, __LINE__, p);
        release(); ptr = p; return *this;
    }
    inline T* detach() CV_NOEXCEPT { T* p = ptr; ptr = NULL; return p; }

    inline void swap(GSafePtr& o) CV_NOEXCEPT { std::swap(ptr, o.ptr); }
private:
    GSafePtr(const GSafePtr&); // = disabled
    GSafePtr& operator=(const T*); // = disabled
};

class ScopeGuardGstMapInfo
{
    GstBuffer* buf_;
    GstMapInfo* info_;
public:
    ScopeGuardGstMapInfo(GstBuffer* buf, GstMapInfo* info)
        : buf_(buf), info_(info)
    {}
    ~ScopeGuardGstMapInfo()
    {
        gst_buffer_unmap(buf_, info_);
    }
};

class ScopeGuardGstVideoFrame
{
    GstVideoFrame* frame_;
public:
    ScopeGuardGstVideoFrame(GstVideoFrame* frame)
        : frame_(frame)
    {}
    ~ScopeGuardGstVideoFrame()
    {
        gst_video_frame_unmap(frame_);
    }
};

} // namespace

/*!
 * \brief The gst_initializer class
 * Initializes gstreamer once in the whole process
 */
class gst_initializer
{
public:
    static gst_initializer& init()
    {
        static gst_initializer g_init;
        if (g_init.isFailed)
            CV_Error(Error::StsError, "Can't initialize GStreamer");
        return g_init;
    }
private:
    bool isFailed;
    bool call_deinit;
    bool start_loop;
    GSafePtr<GMainLoop> loop;
    std::thread thread;

    gst_initializer() :
        isFailed(false)
    {
        call_deinit = utils::getConfigurationParameterBool("OPENCV_VIDEOIO_GSTREAMER_CALL_DEINIT", false);
        start_loop = utils::getConfigurationParameterBool("OPENCV_VIDEOIO_GSTREAMER_START_MAINLOOP", false);

        GSafePtr<GError> err;
        gboolean gst_init_res = gst_init_check(NULL, NULL, err.getRef());
        if (!gst_init_res)
        {
            CV_WARN("Can't initialize GStreamer: " << (err ? err->message : "<unknown reason>"));
            isFailed = true;
            return;
        }
        guint major, minor, micro, nano;
        gst_version(&major, &minor, &micro, &nano);
        if (GST_VERSION_MAJOR != major)
        {
            CV_WARN("incompatible GStreamer version");
            isFailed = true;
            return;
        }

        if (start_loop)
        {
            loop.attach(g_main_loop_new (NULL, FALSE));
            thread = std::thread([this](){
                g_main_loop_run (loop);
            });
        }
    }
    ~gst_initializer()
    {
        if (call_deinit)
        {
            // Debug leaks: GST_LEAKS_TRACER_STACK_TRACE=1 GST_DEBUG="GST_TRACER:7" GST_TRACERS="leaks"
            gst_deinit();
        }

        if (start_loop)
        {
            g_main_loop_quit(loop);
            thread.join();
        }
    }
};

inline static
std::string get_gst_propname(int propId)
{
    switch (propId)
    {
    case CV_CAP_PROP_BRIGHTNESS: return "brightness";
    case CV_CAP_PROP_CONTRAST: return "contrast";
    case CV_CAP_PROP_SATURATION: return "saturation";
    case CV_CAP_PROP_HUE: return "hue";
    default: return std::string();
    }
}

inline static
bool is_gst_element_exists(const std::string& name)
{
    GSafePtr<GstElementFactory> testfac; testfac.attach(gst_element_factory_find(name.c_str()));
    return (bool)testfac;
}

static void find_hw_element(const GValue *item, gpointer va_type)
{
    GstElement *element = GST_ELEMENT(g_value_get_object(item));
    const gchar *name = g_type_name(G_OBJECT_TYPE(element));
    if (name) {
        std::string name_lower = toLowerCase(name);
        if (name_lower.find("vaapi") != std::string::npos) {
            *(int*)va_type = VIDEO_ACCELERATION_VAAPI;
        } else if (name_lower.find("mfx") != std::string::npos || name_lower.find("msdk") != std::string::npos) {
            *(int*)va_type = VIDEO_ACCELERATION_MFX;
        } else if (name_lower.find("d3d11") != std::string::npos) {
            *(int*)va_type = VIDEO_ACCELERATION_D3D11;
        }
    }
}

//==================================================================================================

class GStreamerCapture CV_FINAL : public IVideoCapture
{
public:
    struct StreamData
    {
        GstStreamCollection* collection;
        GstEvent*            ev;
        guint                notifyID;
        gint                 videoStream;
        gint                 audioStream;
        gint                 nbVideoStream;
        gint                 nbAudioStream;

        GSafePtr<GstElement> pipeline;

        StreamData() : collection(NULL), ev(NULL), notifyID(0), videoStream(0), audioStream(-1), nbVideoStream(0), nbAudioStream(0) {};
    };
private:
    GSafePtr<GstElement> audiopipeline;
    GSafePtr<GstElement> pipeline;
    GSafePtr<GstElement> v4l2src;
    GSafePtr<GstElement> sink;
    GSafePtr<GstElement> audiosink;
    GSafePtr<GstSample>  impendingVideoSample;
    GSafePtr<GstSample>  usedVideoSample;
    GSafePtr<GstSample>  impendingAudioSample;
    GSafePtr<GstSample>  audioSample;
    std::vector<GstSample*> audioSamples;
    GSafePtr<GstCaps>    caps;
    GSafePtr<GstCaps>    audiocaps;

    gint64        duration;
    gint64        audioTime;
    gint64        bufferedAudioDuration;
    gint64        requiredAudioTime;
    gint64        audioStartOffset;
    gint64        videoStartOffset;
    gint64        impendingVideoSampleTime;
    gint64        usedVideoSampleTime;
    gint64        videoSampleDuration;
    gint64        audioSampleDuration;
    gint64        impendingAudioSampleTime;
    gint64        audioSampleTime;
    gint64        chunkLengthOfBytes;
    gint64        givenAudioTime;
    gint64        numberOfAdditionalAudioBytes;
    gint64        audioSamplePos;
    gint          width;
    gint          height;
    double        fps;
    bool          isPosFramesSupported;
    bool          isPosFramesEmulated;
    bool          vEOS;
    bool          aEOS;
    bool          syncLastFrame;
    bool          lastFrame;
    gint64        emulatedFrameNumber;
    gint          outputAudioFormat;
    gint          audioBitPerSample;
    gint          audioBaseIndex;
    gint          nAudioChannels;
    gint          audioSamplesPerSecond;

    Mat audioFrame;
    std::deque<uint8_t> bufferAudioData;

    StreamData streamData;
    VideoAccelerationType va_type;
    int hw_device;
public:
    GStreamerCapture();
    virtual ~GStreamerCapture() CV_OVERRIDE;
    virtual bool grabFrame() CV_OVERRIDE;
    virtual bool retrieveFrame(int /*unused*/, OutputArray dst) CV_OVERRIDE;
    bool configureAudioFrame();
    bool grabVideoFrame();
    bool grabAudioFrame();
    bool retrieveVideoFrame(int /*unused*/, OutputArray dst);
    bool retrieveAudioFrame(int /*unused*/, OutputArray dst);
    virtual double getProperty(int propId) const CV_OVERRIDE;
    virtual bool setProperty(int propId, double value) CV_OVERRIDE;
    virtual bool isOpened() const CV_OVERRIDE { return (bool)pipeline; }
    virtual int getCaptureDomain() CV_OVERRIDE { return cv::CAP_GSTREAMER; }
    static void print_tag_foreach(const GstTagList * tags, const gchar * tag, gpointer user_data);
    static void streamNotifyCB(GstStreamCollection * collection, GstStream * stream, GParamSpec * pspec, guint * val);
    static GstBusSyncReply busMessage(GstBus * /*bus*/, GstMessage * message, StreamData * data);
    static bool switchStreams(StreamData * data);
    bool open(int id, const cv::VideoCaptureParameters& params);
    bool open(const String &filename_, const cv::VideoCaptureParameters& params);
    static void newPad(GstElement * /*elem*/, GstPad     *pad, gpointer    data);
    bool configureHW(const cv::VideoCaptureParameters&);
    bool configureStreams(const cv::VideoCaptureParameters&);
    bool setAudioProperties(const cv::VideoCaptureParameters&);

protected:
    bool isPipelinePlaying();
    void startPipeline();
    void stopPipeline();
    void restartPipeline();
    void setFilter(const char *prop, int type, int v1, int v2);
    void removeFilter(const char *filter);
};

GStreamerCapture::GStreamerCapture() :
    duration(-1),
    audioTime(0),
    bufferedAudioDuration(0),
    requiredAudioTime(0),
    audioStartOffset(-1), videoStartOffset(-1),
    impendingVideoSampleTime(0),
    usedVideoSampleTime(0),
    videoSampleDuration(0), audioSampleDuration(0),
    impendingAudioSampleTime(0),
    audioSampleTime(0),
    chunkLengthOfBytes(0),
    givenAudioTime(0),
    numberOfAdditionalAudioBytes(0),
    audioSamplePos(0),
    width(-1), height(-1), fps(-1),
    isPosFramesSupported(false),
    isPosFramesEmulated(false),
    vEOS(false),
    aEOS(false),
    syncLastFrame(true),
    lastFrame(false),
    emulatedFrameNumber(-1),
    outputAudioFormat(CV_16S),
    audioBitPerSample(16),
    audioBaseIndex(1),
    nAudioChannels(0),
    audioSamplesPerSecond(44100)
    , va_type(VIDEO_ACCELERATION_NONE)
    , hw_device(-1)
{}

/*!
 * \brief CvCapture_GStreamer::close
 * Closes the pipeline and destroys all instances
 */
GStreamerCapture::~GStreamerCapture()
{
    if (isPipelinePlaying())
        stopPipeline();
    if (pipeline && GST_IS_ELEMENT(pipeline.get()))
    {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        pipeline.release();
    }
}

bool GStreamerCapture::configureHW(const cv::VideoCaptureParameters& params)
{
    if (params.has(CAP_PROP_HW_ACCELERATION))
    {
        va_type = params.get<VideoAccelerationType>(CAP_PROP_HW_ACCELERATION);
    }
    if (params.has(CAP_PROP_HW_DEVICE))
    {
        hw_device = params.get<int>(CAP_PROP_HW_DEVICE);
        if (va_type == VIDEO_ACCELERATION_NONE && hw_device != -1)
        {
            CV_LOG_ERROR(NULL, "VIDEOIO/GStreamer: Invalid usage of CAP_PROP_HW_DEVICE without requested H/W acceleration. Bailout");
            return false;
        }
        if (va_type == VIDEO_ACCELERATION_ANY && hw_device != -1)
        {
            CV_LOG_ERROR(NULL, "VIDEOIO/GStreamer: Invalid usage of CAP_PROP_HW_DEVICE with 'ANY' H/W acceleration. Bailout");
            return false;
        }
        if (hw_device != -1)
        {
            CV_LOG_ERROR(NULL, "VIDEOIO/GStreamer: CAP_PROP_HW_DEVICE is not supported. Specify -1 (auto) value. Bailout");
            return false;
        }
    }
    return true;
}

bool GStreamerCapture::configureStreams(const cv::VideoCaptureParameters& params)
{
    if (params.has(CAP_PROP_VIDEO_STREAM))
    {
        double value = params.get<double>(CAP_PROP_VIDEO_STREAM);
        if (value == -1 || value == 0)
            streamData.videoStream = static_cast<gint>(value);
        else
        {
            CV_LOG_ERROR(NULL, "VIDEOIO/MSMF: CAP_PROP_VIDEO_STREAM parameter value is invalid/unsupported: " << value);
            return false;
        }
    }
    if (params.has(CAP_PROP_AUDIO_STREAM))
    {
        double value = params.get<double>(CAP_PROP_AUDIO_STREAM);
        if (value == -1 || value > -1)
            streamData.audioStream = static_cast<gint>(value);
        else
        {
            CV_LOG_ERROR(NULL, "VIDEOIO/MSMF: CAP_PROP_AUDIO_STREAM parameter value is invalid/unsupported: " << value);
            return false;
        }
    }
    return true;
}

bool GStreamerCapture::setAudioProperties(const cv::VideoCaptureParameters& params)
{
    if (params.has(CAP_PROP_AUDIO_DATA_DEPTH))
    {
        gint value = static_cast<gint>(params.get<double>(CAP_PROP_AUDIO_DATA_DEPTH));
        if (value != CV_8S && value != CV_16S && value != CV_32S && value != CV_32F)
        {
            CV_LOG_ERROR(NULL, "VIDEOIO/MSMF: CAP_PROP_AUDIO_DATA_DEPTH parameter value is invalid/unsupported: " << value);
            return false;
        }
        else
        {
            outputAudioFormat = value;
        }
    }
    if (params.has(CAP_PROP_AUDIO_SAMPLES_PER_SECOND))
    {
        int value = static_cast<int>(params.get<double>(CAP_PROP_AUDIO_SAMPLES_PER_SECOND));
        if (value < 0)
        {
            CV_LOG_ERROR(NULL, "VIDEOIO/MSMF: CAP_PROP_AUDIO_SAMPLES_PER_SECOND parameter can't be negative: " << value);
            return false;
        }
        else
        {
            audioSamplesPerSecond = value;
        }
    }
    if (params.has(CAP_PROP_AUDIO_SYNCHRONIZE))
    {
        int value = static_cast<uint32_t>(params.get<double>(CAP_PROP_AUDIO_SYNCHRONIZE));
        syncLastFrame = (value != 0) ? true : false;
    }
    return true;
}

/*!
 * \brief CvCapture_GStreamer::grabFrame
 * \return
 * Grabs a sample from the pipeline, awaiting consumation by retrieveFrame.
 * The pipeline is started if it was not running yet
 */
bool GStreamerCapture::grabFrame()
{
    //std::cout << "--------------------------------------" << std::endl;
    if (!pipeline || !GST_IS_ELEMENT(pipeline.get()))
        return false;

    // start the pipeline if it was not in playing state yet
    if (!this->isPipelinePlaying())
        this->startPipeline();
    // bail out if EOS

    if (vEOS)
        return false;

    bool returnFlag = true;

    if (streamData.videoStream >= 0)
    {
        if (!vEOS)
            returnFlag &= grabVideoFrame();
        if (!returnFlag)
            return false;
    }

    if (streamData.audioStream >= 0)
    {
        bufferedAudioDuration = (gint64)(1e9*(((double)bufferAudioData.size()/((audioBitPerSample/8)*nAudioChannels))/audioSamplesPerSecond));
        //std::cout << "bufferAudioData.size() " << bufferAudioData.size() << std::endl;
        audioFrame.release();
        if (!aEOS)
            returnFlag &= grabAudioFrame();
    }

    return returnFlag;
}

bool GStreamerCapture::grabVideoFrame()
{
    //std::cout << "GStreamerCapture::grabVideoFrame" << std::endl;
    usedVideoSample.release();

    bool returnFlag = false;
    bool stopFlag = false;

    if (streamData.audioStream != -1)
    {
        usedVideoSample.swap(impendingVideoSample);
        std::swap(usedVideoSampleTime, impendingVideoSampleTime);
    }

    while (!stopFlag)
    {
        if (gst_app_sink_is_eos(GST_APP_SINK(sink.get())))
        {
            vEOS = true;
            lastFrame = true;
            stopFlag = true;
            if (streamData.audioStream == -1)
            {
                returnFlag = false;
            }
            else if (usedVideoSample)
            {
                gst_element_query_position(sink.get(), CV_GST_FORMAT(GST_FORMAT_TIME), &impendingVideoSampleTime);
                videoSampleDuration = impendingVideoSampleTime - usedVideoSampleTime;
                requiredAudioTime = impendingVideoSampleTime - givenAudioTime;
                givenAudioTime += requiredAudioTime;
                returnFlag = true;
            }
        }
        else 
        {
            impendingVideoSample.attach(gst_app_sink_pull_sample(GST_APP_SINK(sink.get())));
            if (!impendingVideoSample)
            {
                //std::cout << "video sample empty!" << std::endl;
                CV_LOG_DEBUG(NULL, "videoio(MSMF): gst_app_sink_pull_sample() method is not succeeded");
                return false;
            }
            gst_element_query_position(sink.get(), CV_GST_FORMAT(GST_FORMAT_TIME), &impendingVideoSampleTime);

            if (streamData.audioStream != -1)
            {
                if (!usedVideoSample)
                {
                    usedVideoSample.swap(impendingVideoSample);
                    std::swap(usedVideoSampleTime, impendingVideoSampleTime);
                    videoStartOffset = usedVideoSampleTime;
                }
                else
                {
                    stopFlag = true;
                }
                if (impendingVideoSample)
                {
                    //if (isPosFramesEmulated)
                        emulatedFrameNumber++;
                }
                videoSampleDuration = impendingVideoSampleTime - usedVideoSampleTime;
                requiredAudioTime = impendingVideoSampleTime - givenAudioTime;
                givenAudioTime += requiredAudioTime;
                std::cout << "videoSampleDuration " << videoSampleDuration << std::endl;
                //std::cout << "requiredAudioTime " << requiredAudioTime << std::endl;
                //std::cout << "usedVideoSampleTime " << usedVideoSampleTime << std::endl;
                //std::cout << "impendingVideoSampleTime " << impendingVideoSampleTime << std::endl;
            }
            else
            {
                usedVideoSample.swap(impendingVideoSample);
                std::swap(usedVideoSampleTime, impendingVideoSampleTime);
                stopFlag = true;
                //if (isPosFramesEmulated)
                    emulatedFrameNumber++;
            }
            returnFlag = true;
        }
    }

    return returnFlag;
}

bool GStreamerCapture::grabAudioFrame()
{
    //std::cout << "GStreamerCapture::grabAudioFrame" << std::endl;
    audioSample.reset(NULL);
    for (uint i = 0; i < audioSamples.size(); i++)
    {
        gst_sample_unref(audioSamples[i]);
    }
    audioSamples.clear();

    bool returnFlag = false;
    audioTime = bufferedAudioDuration;

    if (bufferedAudioDuration > requiredAudioTime)
        return true;

    while ((!vEOS) ? audioTime <= requiredAudioTime : !aEOS)
    {
        std::cout << "new iteration" << std::endl;
        /*audioSample.swap(impendingAudioSample);
        std::swap(audioSampleTime, impendingAudioSampleTime);*/

        if (audioStartOffset - usedVideoSampleTime > videoSampleDuration)
            return true;
    
        if (gst_app_sink_is_eos(GST_APP_SINK(audiosink.get())))
        {
            aEOS = true;
            if (streamData.videoStream != -1 && !vEOS)
                returnFlag = true;
            if (streamData.videoStream == -1)
                audioSamplePos += chunkLengthOfBytes/((audioBitPerSample/8)*nAudioChannels);
            break;
        }
        else
        {
            audioSample.attach(gst_app_sink_pull_sample(GST_APP_SINK(audiosink.get())));
            if (!audioSample)
            {
                CV_LOG_DEBUG(NULL, "videoio(MSMF): gst_app_sink_pull_sample() method is not succeeded");
                return false;
            }
            gst_element_query_position(audiosink.get(), CV_GST_FORMAT(GST_FORMAT_TIME), &audioSampleTime);

            /*if (!audioSample)
            {
                audioSample.swap(impendingAudioSample);
                std::swap(audioSampleTime, impendingAudioSampleTime);
            }

            if (!impendingAudioSample)
            {
                std::cout << "second pull" << std::endl;
                impendingAudioSample.attach(gst_app_sink_pull_sample(GST_APP_SINK(audiosink.get())));
                if (!impendingAudioSample)
                {
                    CV_LOG_DEBUG(NULL, "videoio(MSMF): gst_app_sink_pull_sample() method is not succeeded");
                    return false;
                }
                gst_element_query_position(audiosink.get(), CV_GST_FORMAT(GST_FORMAT_TIME), &impendingAudioSampleTime);
            }*/

            GstMapInfo map_info = {};
            GstBuffer* buf = gst_sample_get_buffer(audioSample);
            if (!buf)
                return false;
            if (!gst_buffer_map(buf, &map_info, GST_MAP_READ))
            {
                CV_LOG_ERROR(NULL, "GStreamer: Failed to map GStreamer buffer to system memory");
                return false;
            }
            ScopeGuardGstMapInfo map_guard(buf, &map_info);
            audioSampleDuration = 1e9*(((double)map_info.size/((audioBitPerSample/8)*nAudioChannels))/audioSamplesPerSecond);
            //std::cout << "audioSampleTime " << audioSampleTime << std::endl;
            //audioSampleDuration = impendingAudioSampleTime - audioSampleTime;
            audioSamples.push_back(audioSample.get());
            audioSample.detach();
            audioSample.reset(NULL);
            
            CV_LOG_DEBUG(NULL, "videoio(MSMF): got audio frame with timestamp=" << audioSampleTime << "  duration=" << audioSampleDuration);
            //std::cout << "bufferedAudioDuration " << bufferedAudioDuration << std::endl;
            //std::cout << "audioTime " << audioTime << std::endl;
            audioTime += (int64_t)(audioSampleDuration);
            if (emulatedFrameNumber == 1 && audioStartOffset == -1)
            {
                //audioStartOffset = audioSampleTime - audioSampleDuration;
                audioStartOffset = 0;
                //std::cout << "audioStartOffset " << audioStartOffset << std::endl;
                requiredAudioTime -= audioStartOffset;
            }
            //std::cout << "audioSampleDuration " << audioSampleDuration << std::endl;
            //std::cout << "audioTime " << audioTime << std::endl;
            //std::cout << "bufferedAudioDuration " << bufferedAudioDuration << std::endl;
            //std::cout << "audioStartOffset " << audioStartOffset << std::endl;
            returnFlag = true;
        }  
    }

    returnFlag &= configureAudioFrame();
    return returnFlag;
}

bool GStreamerCapture::configureAudioFrame()
{
    //std::cout << "GStreamerCapture::configureAudioFrame" << std::endl;
    if (!audioSamples.empty() || (!bufferAudioData.empty() && aEOS))
    {
        std::vector<uint8_t> audioDataInUse;
        GstMapInfo map_info = {};

        GstCaps* frame_caps = gst_sample_get_caps(audioSamples[0]);  // no lifetime transfer
        if (!frame_caps)
        {
            CV_LOG_ERROR(NULL, "GStreamer: gst_sample_get_caps() returns NULL");
            return false;
        }
        if (!GST_CAPS_IS_SIMPLE(frame_caps))
        {
            // bail out in no caps
            CV_LOG_ERROR(NULL, "GStreamer: GST_CAPS_IS_SIMPLE(frame_caps) check is failed");
            return false;
        }

        GstAudioInfo info = {};
        gboolean audio_info_res = gst_audio_info_from_caps(&info, frame_caps);
        if (!audio_info_res)
        {
            CV_Error(Error::StsError, "GStreamer: gst_audio_info_from_caps() is failed. Can't handle unknown layout");
        }
        int bpf = GST_AUDIO_INFO_BPF(&info);
        CV_CheckGT(bpf, 0, "");

        GstStructure* structure = gst_caps_get_structure(frame_caps, 0);  // no lifetime transfer
        if (!structure)
        {
            CV_LOG_ERROR(NULL, "GStreamer: Can't query 'structure'-0 from GStreamer sample");
            return false;
        }

        const gchar* name_ = gst_structure_get_name(structure);
        if (!name_)
        {
            CV_LOG_ERROR(NULL, "GStreamer: Can't query 'name' from GStreamer sample");
            return false;
        }
        std::string name = toLowerCase(std::string(name_));

        for (uint i = 0; i < audioSamples.size(); i++)
        {
            GstBuffer* buf = gst_sample_get_buffer(audioSamples[i]);
            if (!buf)
                return false;
            if (!gst_buffer_map(buf, &map_info, GST_MAP_READ))
            {
                CV_LOG_ERROR(NULL, "GStreamer: Failed to map GStreamer buffer to system memory");
                return false;
            }
            ScopeGuardGstMapInfo map_guard(buf, &map_info);
            
            gsize lastSize = bufferAudioData.size();
            bufferAudioData.resize(lastSize+map_info.size);
            for (gsize j = 0; j < map_info.size; j++)
            {
                bufferAudioData[lastSize+j]=*(map_info.data + j);
            }
        }

        //std::cout << "audio Time " << audioTime << std::endl;
        //std::cout << "bufferedAudioDuration " << bufferedAudioDuration << std::endl;
        //std::cout << "requiredAudioTime " << requiredAudioTime << std::endl;
        //std::cout << "videoSampleDuration " << videoSampleDuration << std::endl;

        audioSamplePos += chunkLengthOfBytes/((audioBitPerSample/8)*nAudioChannels);
        chunkLengthOfBytes = (streamData.videoStream != -1) ? (int64_t)((requiredAudioTime * 1e-9 * audioSamplesPerSecond*nAudioChannels*(audioBitPerSample)/8)) : map_info.size;
        if ((streamData.videoStream != -1) && (chunkLengthOfBytes % ((int)(audioBitPerSample)/8* (int)nAudioChannels) != 0))
        {
            if ( (double)audioSamplePos/audioSamplesPerSecond + audioStartOffset * 1e-9 - usedVideoSampleTime * 1e-9 >= 0 ) 
                chunkLengthOfBytes -= numberOfAdditionalAudioBytes;
            numberOfAdditionalAudioBytes = ((int)(audioBitPerSample)/8* (int)nAudioChannels)
                                        - chunkLengthOfBytes % ((int)(audioBitPerSample)/8* (int)nAudioChannels);
            chunkLengthOfBytes += numberOfAdditionalAudioBytes;
        }
        if ((lastFrame && !syncLastFrame) || (aEOS && !vEOS))
        {
            chunkLengthOfBytes = bufferAudioData.size();
            audioSamplePos += chunkLengthOfBytes/((audioBitPerSample/8)*nAudioChannels);
        }
        CV_Check((double)chunkLengthOfBytes, chunkLengthOfBytes >= INT_MIN || chunkLengthOfBytes <= INT_MAX, "GSTREAMER: The chunkLengthOfBytes is out of the allowed range");
        copy(bufferAudioData.begin(), bufferAudioData.begin() + (int)chunkLengthOfBytes, std::back_inserter(audioDataInUse));
        std::cout << "chunkLengthOfBytes " << chunkLengthOfBytes << std::endl;
        std::cout << "before bufferAudioData size " << bufferAudioData.size() << std::endl;
        bufferAudioData.erase(bufferAudioData.begin(), bufferAudioData.begin() + (int)chunkLengthOfBytes);
        //std::cout << "after bufferAudioData size " << bufferAudioData.size() << std::endl;

        if (name == "audio/x-raw")
        {
            const gchar* format_ = gst_structure_get_string(structure, "format");
            if (!format_)
            {
                CV_LOG_ERROR(NULL, "GStreamer: Can't query 'format' of 'video/x-raw'");
                return false;
            }
            std::string format = toUpperCase(std::string(format_));
            cv::Mat data;

            /*std::fstream file("/home/bin/gst_default.bin", std::ios::out | std::ios::app | std::ios::binary);
            char buffer[map_info.size];
            for (gsize i = 0; i < map_info.size; i++)
            {
                buffer[i] = *(map_info.data + i);
            }*/
            /*file.write(reinterpret_cast<char*>(buffer), sizeof(buffer));
            file.close();*/

            if (format != "S8" && format != "S16LE" && format != "S32LE" && format != "F32LE")
            {
                CV_Error_(Error::StsNotImplemented, ("Unsupported GStreamer audio format: %s", format.c_str()));
                return false;
            }
            if (format == "S8")
            {
                Mat((int)chunkLengthOfBytes/bpf, nAudioChannels, CV_8S, audioDataInUse.data()).copyTo(audioFrame);
                return true;
            }
            if (format == "S16LE")
            {
                Mat((int)chunkLengthOfBytes/bpf, nAudioChannels, CV_16S, audioDataInUse.data()).copyTo(audioFrame);
                
                return true;
            }
            if (format == "S32LE")
            {
                Mat((int)chunkLengthOfBytes/bpf, nAudioChannels, CV_32S, audioDataInUse.data()).copyTo(audioFrame);
                return true;
            }
            if (format == "F32LE")
            {
                Mat((int)chunkLengthOfBytes/bpf, nAudioChannels, CV_32F, audioDataInUse.data()).copyTo(audioFrame);
                return true;
            }

            audioDataInUse.clear();
            audioDataInUse.shrink_to_fit();
            return true;
        }
        CV_Error_(Error::StsNotImplemented, ("Unsupported GStreamer layer type: %s", name.c_str()));
    }
    else
    {
        return false;
    }
}

bool GStreamerCapture::retrieveAudioFrame(int index, OutputArray dst)
{
    //std::cout << "GStreamerCapture::retrieveAudioFrame" << std::endl;
    if (audioStartOffset - usedVideoSampleTime > videoSampleDuration)
    {
        dst.release();
        return true;
    }
    if (audioFrame.empty())
    {
        dst.release();
        if (aEOS)
            return true;
    }
    CV_Check(index, index >= audioBaseIndex && index < audioBaseIndex + nAudioChannels, "");
    index -= audioBaseIndex;

    CV_CheckType(outputAudioFormat,
        outputAudioFormat == CV_8S ||
        outputAudioFormat == CV_16S ||
        outputAudioFormat == CV_32S ||
        outputAudioFormat == CV_32F,
        "");

    dst.create(1, audioFrame.rows, outputAudioFormat);
    Mat data = dst.getMat();
    switch (outputAudioFormat)
    {
        case CV_8S:
            for (int i = 0; i < audioFrame.rows; i++)
                data.at<char>(i) = audioFrame.at<char>(i, index);
            return true;
        case CV_16S:
            for (int i = 0; i < audioFrame.rows; i++)
                data.at<short>(i) = audioFrame.at<short>(i, index);
            return true;
        case CV_32S:
            for (int i = 0; i < audioFrame.rows; i++)
                data.at<int>(i) = audioFrame.at<int>(i, index);
            return true;
        case CV_32F:
            for (int i = 0; i < audioFrame.rows; i++)
                data.at<float>(i) = audioFrame.at<float>(i, index);
            return true;
    }

    dst.release();
    return false;
}

bool GStreamerCapture::retrieveVideoFrame(int, OutputArray dst)
{
    //std::cout << "GStreamerCapture::retrieveVideoFrame" << std::endl;
    GstCaps* frame_caps = gst_sample_get_caps(usedVideoSample);  // no lifetime transfer
    if (!frame_caps)
    {
        CV_LOG_ERROR(NULL, "GStreamer: gst_sample_get_caps() returns NULL");
        return false;
    }

    if (!GST_CAPS_IS_SIMPLE(frame_caps))
    {
        // bail out in no caps
        CV_LOG_ERROR(NULL, "GStreamer: GST_CAPS_IS_SIMPLE(frame_caps) check is failed");
        return false;
    }

    GstVideoInfo info = {};
    gboolean video_info_res = gst_video_info_from_caps(&info, frame_caps);
    if (!video_info_res)
    {
        CV_Error(Error::StsError, "GStreamer: gst_video_info_from_caps() is failed. Can't handle unknown layout");
    }

    // gstreamer expects us to handle the memory at this point
    // so we can just wrap the raw buffer and be done with it
    GstBuffer* buf = gst_sample_get_buffer(usedVideoSample);  // no lifetime transfer
    if (!buf)
        return false;

    // at this point, the gstreamer buffer may contain a video meta with special
    // stride and plane locations. We __must__ consider in order to correctly parse
    // the data. The gst_video_frame_map will parse the meta for us, or default to
    // regular strides/offsets if no meta is present.
    GstVideoFrame frame = {};
    GstMapFlags flags = static_cast<GstMapFlags>(GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF);
    if (!gst_video_frame_map(&frame, &info, buf, flags))
    {
        CV_LOG_ERROR(NULL, "GStreamer: Failed to map GStreamer buffer to system memory");
        return false;
    }

    ScopeGuardGstVideoFrame frame_guard(&frame);  // call gst_video_frame_unmap(&frame) on scope leave

    int frame_width = GST_VIDEO_FRAME_COMP_WIDTH(&frame, 0);
    int frame_height = GST_VIDEO_FRAME_COMP_HEIGHT(&frame, 0);
    if (frame_width <= 0 || frame_height <= 0)
    {
        CV_LOG_ERROR(NULL, "GStreamer: Can't query frame size from GStreamer sample");
        return false;
    }

    GstStructure* structure = gst_caps_get_structure(frame_caps, 0);  // no lifetime transfer
    if (!structure)
    {
        CV_LOG_ERROR(NULL, "GStreamer: Can't query 'structure'-0 from GStreamer sample");
        return false;
    }

    const gchar* name_ = gst_structure_get_name(structure);
    if (!name_)
    {
        CV_LOG_ERROR(NULL, "GStreamer: Can't query 'name' from GStreamer sample");
        return false;
    }
    std::string name = toLowerCase(std::string(name_));

    // we support these types of data:
    //     video/x-raw, format=BGR   -> 8bit, 3 channels
    //     video/x-raw, format=GRAY8 -> 8bit, 1 channel
    //     video/x-raw, format=UYVY  -> 8bit, 2 channel
    //     video/x-raw, format=YUY2  -> 8bit, 2 channel
    //     video/x-raw, format=YVYU  -> 8bit, 2 channel
    //     video/x-raw, format=NV12  -> 8bit, 1 channel (height is 1.5x larger than true height)
    //     video/x-raw, format=NV21  -> 8bit, 1 channel (height is 1.5x larger than true height)
    //     video/x-raw, format=YV12  -> 8bit, 1 channel (height is 1.5x larger than true height)
    //     video/x-raw, format=I420  -> 8bit, 1 channel (height is 1.5x larger than true height)
    //     video/x-bayer             -> 8bit, 1 channel
    //     image/jpeg                -> 8bit, mjpeg: buffer_size x 1 x 1
    //     video/x-raw, format=GRAY16_LE (BE) -> 16 bit, 1 channel
    //     video/x-raw, format={BGRA, RGBA, BGRx, RGBx} -> 8bit, 4 channels
    // bayer data is never decoded, the user is responsible for that
    Size sz = Size(frame_width, frame_height);
    guint n_planes = GST_VIDEO_FRAME_N_PLANES(&frame);

    if (name == "video/x-raw")
    {
        const gchar* format_ = frame.info.finfo->name;
        if (!format_)
        {
            CV_LOG_ERROR(NULL, "GStreamer: Can't query 'format' of 'video/x-raw'");
            return false;
        }
        std::string format = toUpperCase(std::string(format_));

        if (format == "BGR")
        {
            CV_CheckEQ((int)n_planes, 1, "");
            size_t step = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0);
            CV_CheckGE(step, (size_t)frame_width * 3, "");
            Mat src(sz, CV_8UC3, GST_VIDEO_FRAME_PLANE_DATA(&frame, 0), step);
            src.copyTo(dst);
            return true;
        }
        else if (format == "GRAY8")
        {
            CV_CheckEQ((int)n_planes, 1, "");
            size_t step = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0);
            CV_CheckGE(step, (size_t)frame_width, "");
            Mat src(sz, CV_8UC1, GST_VIDEO_FRAME_PLANE_DATA(&frame, 0), step);
            src.copyTo(dst);
            return true;
        }
        else if (format == "GRAY16_LE" || format == "GRAY16_BE")
        {
            CV_CheckEQ((int)n_planes, 1, "");
            size_t step = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0);
            CV_CheckGE(step, (size_t)frame_width, "");
            Mat src(sz, CV_16UC1, GST_VIDEO_FRAME_PLANE_DATA(&frame, 0), step);
            src.copyTo(dst);
            return true;
        }
        else if (format == "BGRA" || format == "RGBA" || format == "BGRX" || format == "RGBX")
        {
            CV_CheckEQ((int)n_planes, 1, "");
            size_t step = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0);
            CV_CheckGE(step, (size_t)frame_width, "");
            Mat src(sz, CV_8UC4, GST_VIDEO_FRAME_PLANE_DATA(&frame, 0), step);
            src.copyTo(dst);
            return true;
        }
        else if (format == "UYVY" || format == "YUY2" || format == "YVYU")
        {
            CV_CheckEQ((int)n_planes, 1, "");
            size_t step = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0);
            CV_CheckGE(step, (size_t)frame_width * 2, "");
            Mat src(sz, CV_8UC2, GST_VIDEO_FRAME_PLANE_DATA(&frame, 0), step);
            src.copyTo(dst);
            return true;
        }
        else if (format == "NV12" || format == "NV21")
        {
            CV_CheckEQ((int)n_planes, 2, "");
            size_t stepY = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0);
            CV_CheckGE(stepY, (size_t)frame_width, "");
            size_t stepUV = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 1);
            CV_CheckGE(stepUV, (size_t)frame_width, "");

            dst.create(Size(frame_width, frame_height * 3 / 2), CV_8UC1);
            Mat dst_ = dst.getMat();
            Mat srcY(sz, CV_8UC1, GST_VIDEO_FRAME_PLANE_DATA(&frame,0), stepY);
            Mat srcUV(Size(frame_width, frame_height / 2), CV_8UC1, GST_VIDEO_FRAME_PLANE_DATA(&frame,1), stepUV);
            srcY.copyTo(dst_(Rect(0, 0, frame_width, frame_height)));
            srcUV.copyTo(dst_(Rect(0, frame_height, frame_width, frame_height / 2)));
            return true;
        }
        else if (format == "YV12" || format == "I420")
        {
            CV_CheckEQ((int)n_planes, 3, "");
            size_t step0 = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0);
            CV_CheckGE(step0, (size_t)frame_width, "");
            size_t step1 = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 1);
            CV_CheckGE(step1, (size_t)frame_width / 2, "");
            size_t step2 = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 2);
            CV_CheckGE(step2, (size_t)frame_width / 2, "");

            dst.create(Size(frame_width, frame_height * 3 / 2), CV_8UC1);
            Mat dst_ = dst.getMat();
            Mat srcY(sz, CV_8UC1, GST_VIDEO_FRAME_PLANE_DATA(&frame,0), step0);
            Size sz2(frame_width / 2, frame_height / 2);
            Mat src1(sz2, CV_8UC1, GST_VIDEO_FRAME_PLANE_DATA(&frame,1), step1);
            Mat src2(sz2, CV_8UC1, GST_VIDEO_FRAME_PLANE_DATA(&frame,2), step2);
            srcY.copyTo(dst_(Rect(0, 0, frame_width, frame_height)));
            src1.copyTo(Mat(sz2, CV_8UC1, dst_.ptr<uchar>(frame_height)));
            src2.copyTo(Mat(sz2, CV_8UC1, dst_.ptr<uchar>(frame_height) + src1.total()));
            return true;
        }
        else
        {
            CV_Error_(Error::StsNotImplemented, ("Unsupported GStreamer 'video/x-raw' format: %s", format.c_str()));
        }
    }
    else if (name == "video/x-bayer")
    {
        CV_CheckEQ((int)n_planes, 0, "");
        Mat src = Mat(sz, CV_8UC1, frame.map[0].data);
        src.copyTo(dst);
        return true;
    }
    else if (name == "image/jpeg")
    {
        CV_CheckEQ((int)n_planes, 0, "");
        Mat src = Mat(Size(frame.map[0].size, 1), CV_8UC1, frame.map[0].data);
        src.copyTo(dst);
        return true;
    }

    CV_Error_(Error::StsNotImplemented, ("Unsupported GStreamer layer type: %s", name.c_str()));
}

bool GStreamerCapture::retrieveFrame(int index, OutputArray dst)
{
    if (index < 0)
        return false;

    if ((gint)index < audioBaseIndex)
    {
        if (streamData.videoStream == -1)
        {
            dst.release();
            return false;
        }
        else
        {
            CV_CheckGE(streamData.videoStream, 0, "No video stream configured");
            return retrieveVideoFrame(index, dst);
        }
    }
    else
    {
        if (streamData.audioStream == -1)
        {
            dst.release();
            return false;
        }
        else
        {
            CV_CheckGE(streamData.audioStream, 0, "No audio stream configured");
            return retrieveAudioFrame(index, dst);
        }
    }

    CV_LOG_ERROR(NULL, "GStreamer(retrive): unrecognized index=" << index);
    return false;
}

bool GStreamerCapture::isPipelinePlaying()
{
    if (!pipeline || !GST_IS_ELEMENT(pipeline.get()))
    {
        CV_WARN("GStreamer: pipeline have not been created");
        return false;
    }
    GstState current, pending;
    GstClockTime timeout = 5*GST_SECOND;
    GstStateChangeReturn ret = gst_element_get_state(pipeline, &current, &pending, timeout);
    if (!ret)
    {
        CV_WARN("unable to query pipeline state");
        return false;
    }
    return current == GST_STATE_PLAYING;
}

/*!
 * \brief CvCapture_GStreamer::startPipeline
 * Start the pipeline by setting it to the playing state
 */
void GStreamerCapture::startPipeline()
{
    if (!pipeline || !GST_IS_ELEMENT(pipeline.get()))
    {
        CV_WARN("GStreamer: pipeline have not been created");
        return;
    }
    GstStateChangeReturn status = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (status == GST_STATE_CHANGE_ASYNC)
    {
        // wait for status update
        status = gst_element_get_state(pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
    }
    if (status == GST_STATE_CHANGE_FAILURE)
    {
        handleMessage(pipeline);
        pipeline.release();
        CV_WARN("unable to start pipeline");
        return;
    }

    if (isPosFramesEmulated)
        emulatedFrameNumber = 0;

    handleMessage(pipeline);
}

void GStreamerCapture::stopPipeline()
{
    if (!pipeline || !GST_IS_ELEMENT(pipeline.get()))
    {
        CV_WARN("GStreamer: pipeline have not been created");
        return;
    }
    if (gst_element_set_state(pipeline, GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE)
    {
        CV_WARN("unable to stop pipeline");
        pipeline.release();
    }
}

/*!
 * \brief CvCapture_GStreamer::restartPipeline
 * Restart the pipeline
 */
void GStreamerCapture::restartPipeline()
{
    handleMessage(pipeline);

    this->stopPipeline();
    this->startPipeline();
}

/*!
 * \brief CvCapture_GStreamer::setFilter
 * \param prop the property name
 * \param type glib property type
 * \param v1 the value
 * \param v2 second value of property type requires it, else NULL
 * Filter the output formats by setting appsink caps properties
 */
void GStreamerCapture::setFilter(const char *prop, int type, int v1, int v2)
{
    if (!caps || !(GST_IS_CAPS(caps.get())))
    {
        if (type == G_TYPE_INT)
        {
            caps.attach(gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "BGR", prop, type, v1, NULL));
        }
        else
        {
            caps.attach(gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "BGR", prop, type, v1, v2, NULL));
        }
    }
    else
    {
        if (!gst_caps_is_writable(caps.get()))
            caps.attach(gst_caps_make_writable(caps.detach()));
        if (type == G_TYPE_INT)
        {
            gst_caps_set_simple(caps, prop, type, v1, NULL);
        }
        else
        {
            gst_caps_set_simple(caps, prop, type, v1, v2, NULL);
        }
    }

    caps.attach(gst_caps_fixate(caps.detach()));

    gst_app_sink_set_caps(GST_APP_SINK(sink.get()), caps);
    GST_LOG("filtering with caps: %" GST_PTR_FORMAT, caps.get());
}

/*!
 * \brief CvCapture_GStreamer::removeFilter
 * \param filter filter to remove
 * remove the specified filter from the appsink template caps
 */
void GStreamerCapture::removeFilter(const char *filter)
{
    if(!caps)
        return;

    if (!gst_caps_is_writable(caps.get()))
        caps.attach(gst_caps_make_writable(caps.detach()));

    GstStructure *s = gst_caps_get_structure(caps, 0);  // no lifetime transfer
    gst_structure_remove_field(s, filter);

    caps.attach(gst_caps_fixate(caps.detach()));

    gst_app_sink_set_caps(GST_APP_SINK(sink.get()), caps);
}

/*!
 * \brief CvCapture_GStreamer::newPad link dynamic padd
 * \param pad
 * \param data
 * decodebin creates pads based on stream information, which is not known upfront
 * on receiving the pad-added signal, we connect it to the colorspace conversion element
 */
void GStreamerCapture::newPad(GstElement *, GstPad *pad, gpointer data)
{
    GSafePtr<GstPad> sinkpad;
    GstElement* color = (GstElement*)data;
    /*if (true)
    {
        gchar *pad_name = gst_pad_get_name (pad);
        const gchar *sink_pad = NULL;

        GST_DEBUG_OBJECT (pad, "New pad ! Link to playsink !");
        if (!g_ascii_strncasecmp (pad_name, "video_", 6))
            sink_pad = "video_sink";
        else if (!g_ascii_strncasecmp (pad_name, "audio_", 6))
            sink_pad = "audio_sink";
        else if (!g_ascii_strncasecmp (pad_name, "text_", 5))
            sink_pad = "text_sink";
        else
            GST_WARNING_OBJECT (pad, "non audio/video/text pad");

        g_free (pad_name);

        if (sink_pad)
        {

            sinkpad.attach(gst_element_get_request_pad(color, sink_pad));
            if (sinkpad)
                gst_pad_link (pad, sinkpad.get());
        }
    }*/
    sinkpad.attach(gst_element_get_static_pad(color, "sink"));
    if (!sinkpad) {
        CV_WARN("no pad named sink");
        return;
    }

    gst_pad_link(pad, sinkpad.get());
}

/*void GStreamerCapture::print_tag_foreach(const GstTagList * tags, const gchar * tag, gpointer user_data)
{
    GValue val = { 0, };
    gchar *str;
    gint depth = GPOINTER_TO_INT (user_data);

    if (!gst_tag_list_copy_value (&val, tags, tag))
        return;

    if (G_VALUE_HOLDS_STRING (&val))
        str = g_value_dup_string (&val);
    else
        str = gst_value_serialize (&val);

    gst_print ("%*s%s: %s\n", 2 * depth, " ", gst_tag_get_nick (tag), str);
    g_free (str);

    g_value_unset (&val);
}*/

/*void GStreamerCapture::streamNotifyCB(GstStreamCollection *, GstStream * stream, GParamSpec * pspec, guint * val)
{
    if (g_str_equal (pspec->name, "caps"))
    {
        GstCaps *capsTMP = gst_stream_get_caps(stream);
        gchar *caps_str = gst_caps_to_string(capsTMP);
        g_free(caps_str);
        gst_caps_unref(capsTMP);
    }
}*/

GstBusSyncReply GStreamerCapture::busMessage(GstBus * /*bus*/, GstMessage * message, StreamData * data)
{
    //GstObject *src = GST_MESSAGE_SRC (message);
    switch (GST_MESSAGE_TYPE (message))
    {
        case GST_MESSAGE_ERROR:
        {
            //std::cout << "GST_MESSAGE_ERROR" << std::endl;
            break;
        }
        case GST_MESSAGE_EOS:
        {
            //std::cout << "GST_MESSAGE_EOS" << std::endl;
            break;
        }
        case GST_MESSAGE_STREAM_COLLECTION:
        {
            GstStreamCollection *collectionTMP = NULL;
            gst_message_parse_stream_collection(message, &collectionTMP);
            if (collectionTMP)
            {
                /*GstTagList *tags;
                for (guint i = 0; i < gst_stream_collection_get_size(collectionTMP); i++)
                {
                    GstStream *stream = gst_stream_collection_get_stream (collectionTMP, i);
                    gst_print (" Stream %u type %s flags 0x%x\n", i, gst_stream_type_get_name (gst_stream_get_stream_type (stream)), gst_stream_get_stream_flags (stream));
                    gst_print ("  ID: %s\n", gst_stream_get_stream_id (stream));

                    GstCaps* capsTMP = gst_stream_get_caps (stream);
                    if (capsTMP)
                    {
                        gchar *caps_str = gst_caps_to_string (capsTMP);
                        gst_print ("caps: %s\n", caps_str);
                        g_free (caps_str);
                        gst_caps_unref (capsTMP);
                    }
                    tags = gst_stream_get_tags(stream);
                    if (tags)
                    {
                        gst_print("tags:\n");
                        gst_tag_list_foreach(tags, print_tag_foreach, GUINT_TO_POINTER (3));
                        gst_tag_list_unref(tags);
                    }
                }*/
                /*if (data->collection && data->notifyID) 
                {
                    g_signal_handler_disconnect(data->collection, data->notifyID);
                    data->notifyID = 0;
                }*/
                gst_object_replace((GstObject **) & data->collection, (GstObject *) collectionTMP);
                /*if (data->collection)
                {
                    data->notifyID = g_signal_connect(data->collection, "stream-notify", (GCallback) streamNotifyCB, data);
                }*/
                if (!switchStreams(data))
                {
                    std::cout << "error" << std::endl;
                }
                gst_object_unref(collectionTMP);
            }
            break;
        }
    default:
        break;
    }

    return GST_BUS_PASS;
}

bool GStreamerCapture::switchStreams(StreamData * data)
{
    guint i, nb_streams;
    std::vector<GstStream*> videos;
    std::vector<GstStream*> audios;
    GList* streams = NULL;
    //GstEvent* ev;

    g_print ("Switching Streams...\n");
    nb_streams = gst_stream_collection_get_size(data->collection);
    for (i = 0; i < nb_streams; i++)
    {
        GstStream* stream = gst_stream_collection_get_stream (data->collection, i);
        GstStreamType stype = gst_stream_get_stream_type (stream);
        if (stype == GST_STREAM_TYPE_VIDEO)
        {
            videos.push_back(stream);
            data->nbVideoStream += 1;
        }
        else if (stype == GST_STREAM_TYPE_AUDIO)
        {
            audios.push_back(stream);
            data->nbAudioStream += 1;
        }
    }
    /*if (data->videoStream < data->nbVideoStream)
    {
        streams = g_list_append(streams, (gchar *) gst_stream_get_stream_id (videos[0]));
        g_print ("  Selecting video channel #%d : %s\n", data->videoStream, gst_stream_get_stream_id (videos[0]));
    }
    else
    {
        CV_LOG_WARNING(NULL, "GStreamer: can't set video stream: " << data->videoStream);
        return false;
    }*/
    if (data->audioStream < data->nbAudioStream)
    {
        streams = g_list_append(streams, (gchar *) gst_stream_get_stream_id (audios[data->audioStream]));
        g_print (" Selecting audio channel #%d : %s\n", data->audioStream, gst_stream_get_stream_id (audios[data->audioStream]));
    }
    else
    {
        CV_LOG_WARNING(NULL, "GStreamer: can't set audio stream: " << data->audioStream);
        return false;
    }

    data->ev = gst_event_new_select_streams(streams);
    //std::cout << GST_EVENT_TYPE_NAME(data->ev) << std::endl;
    //gst_event_type_get_name
    //gst_element_send_event (pipeline.get(), ev);
    gboolean ret = gst_element_send_event(data->pipeline.get(), data->ev);
    gst_println ("Sent select-streams event ret %d", ret);

    return ret;
}

/*!
 * \brief Create GStreamer pipeline
 * \param filename Filename to open in case of CV_CAP_GSTREAMER_FILE
 * \return boolean. Specifies if opening was successful.
 *
 * In case of camera 'index', a pipeline is constructed as follows:
 *    v4l2src ! autoconvert ! appsink
 *
 *
 * The 'filename' parameter is not limited to filesystem paths, and may be one of the following:
 *
 *  - a normal filesystem path:
 *        e.g. video.avi or /path/to/video.avi or C:\\video.avi
 *  - an uri:
 *        e.g. file:///path/to/video.avi or rtsp:///path/to/stream.asf
 *  - a gstreamer pipeline description:
 *        e.g. videotestsrc ! videoconvert ! appsink
 *        the appsink name should be either 'appsink0' (the default) or 'opencvsink'
 *
 *  GStreamer will not drop frames if the grabbing interval larger than the framerate period.
 *  To support dropping for live streams add appsink 'drop' parameter into your custom pipeline.
 *
 *  The pipeline will only be started whenever the first frame is grabbed. Setting pipeline properties
 *  is really slow if we need to restart the pipeline over and over again.
 *
 */
bool GStreamerCapture::open(int id, const cv::VideoCaptureParameters& params)
{
    gst_initializer::init();

    if (!is_gst_element_exists("v4l2src"))
        return false;
    std::ostringstream desc;
    desc << "v4l2src device=/dev/video" << id
             << " ! " << COLOR_ELEM
             << " ! appsink drop=true";
    return open(desc.str(), params);
}

bool GStreamerCapture::open(const String &filename_, const cv::VideoCaptureParameters& params)
{
    gst_initializer::init();

    if (!configureHW(params))
    {
        CV_LOG_WARNING(NULL, "GStreamer: can't configure HW encoding/decoding support");
        return false;
    }

    if (!configureStreams(params))
    {
        CV_LOG_WARNING(NULL, "GStreamer: can't configure streams");
        return false;
    }
    /*if ((streamData.videoStream >= 0 && streamData.audioStream >= 0) || (streamData.videoStream < 0 && streamData.audioStream < 0))
    {
        CV_LOG_ERROR(NULL, "GStreamer backend supports audio-only or video-only capturing. Only one of the properties CAP_PROP_AUDIO_STREAM=" << streamData.audioStream << " and CAP_PROP_VIDEO_STREAM=" <<  streamData.videoStream << " should be >= 0");
        return false;
    }
    if (streamData.audioStream > 0)
    {
        CV_LOG_ERROR(NULL, "GStreamer backend supports the first audio stream only. CAP_PROP_AUDIO_STREAM=" << streamData.audioStream);
        return false;
    }*/

    if (!setAudioProperties(params))
    {
        CV_LOG_WARNING(NULL, "GStreamer: can't configure audio properties");
        return false;
    }

    const gchar* filename = filename_.c_str();

    bool file = false;
    bool manualpipeline = false;
    GSafePtr<char> uri;
    GSafePtr<GstBus> bus;

    GSafePtr<GstElement> uridecodebin;
    GSafePtr<GstElement> audioUridecodebin;
    GSafePtr<GstElement> audioqueue;
    GSafePtr<GstElement> videoqueue;
    GSafePtr<GstElement> color;
    GSafePtr<GstElement> convert;
    GSafePtr<GstElement> resample;

    GstStateChangeReturn status;

    // test if we have a valid uri. If so, open it with an uridecodebin
    // else, we might have a file or a manual pipeline.
    // if gstreamer cannot parse the manual pipeline, we assume we were given and
    // ordinary file path.
    CV_LOG_INFO(NULL, "OpenCV | GStreamer: " << filename);
    if (!gst_uri_is_valid(filename))
    {
        if (utils::fs::exists(filename_))
        {
            GSafePtr<GError> err;
            uri.attach(gst_filename_to_uri(filename, err.getRef()));
            if (uri)
            {
                file = true;
            }
            else
            {
                CV_WARN("Error opening file: " << filename << " (" << (err ? err->message : "<unknown reason>") << ")");
                return false;
            }
        }
        else
        {
            GSafePtr<GError> err;
            uridecodebin.attach(gst_parse_launch(filename, err.getRef()));
            if (!uridecodebin)
            {
                CV_WARN("Error opening bin: " << (err ? err->message : "<unknown reason>"));
                return false;
            }
            manualpipeline = true;
        }
    }
    else
    {
        uri.attach(g_strdup(filename));
    }
    CV_LOG_INFO(NULL, "OpenCV | GStreamer: mode - " << (file ? "FILE" : manualpipeline ? "MANUAL" : "URI"));
    bool element_from_uri = false;
    if (!uridecodebin)
    {
        if (streamData.videoStream >= 0)
        {
            // At this writing, the v4l2 element (and maybe others too) does not support caps renegotiation.
            // This means that we cannot use an uridecodebin when dealing with v4l2, since setting
            // capture properties will not work.
            // The solution (probably only until gstreamer 1.2) is to make an element from uri when dealing with v4l2.
            GSafePtr<gchar> protocol_; protocol_.attach(gst_uri_get_protocol(uri));
            CV_Assert(protocol_);
            std::string protocol = toLowerCase(std::string(protocol_.get()));
            if (protocol == "v4l2")
            {
                uridecodebin.reset(gst_element_make_from_uri(GST_URI_SRC, uri.get(), "src", NULL));
                CV_Assert(uridecodebin);
                element_from_uri = true;
            }
            else
            {
                uridecodebin.reset(gst_element_factory_make("uridecodebin", NULL));
                CV_Assert(uridecodebin);
                g_object_set(G_OBJECT(uridecodebin.get()), "uri", uri.get(), NULL);
            }
            if (!uridecodebin)
            {
                CV_WARN("Can not parse GStreamer URI bin");
                return false;
            }
        }
        if (streamData.audioStream >= 0)
        {
            audioUridecodebin.reset(gst_element_factory_make("uridecodebin3", NULL));
            CV_Assert(audioUridecodebin);
            g_object_set(G_OBJECT(audioUridecodebin.get()), "uri", uri.get(), NULL);
        }
    }

    if (manualpipeline)
    {
        GstIterator *it = gst_bin_iterate_elements(GST_BIN(uridecodebin.get()));

        gboolean done = false;
        GValue value = G_VALUE_INIT;

        while (!done)
        {
            GstElement *element = NULL;
            GSafePtr<gchar> name;
            switch (gst_iterator_next (it, &value))
            {
            case GST_ITERATOR_OK:
                element = GST_ELEMENT (g_value_get_object (&value));
                name.attach(gst_element_get_name(element));
                if (name)
                {
                    if (strstr(name, "opencvsink") != NULL || strstr(name, "appsink") != NULL)
                    {
                        sink.attach(GST_ELEMENT(gst_object_ref(element)));
                    }
                    else if (strstr(name, COLOR_ELEM_NAME) != NULL)
                    {
                        color.attach(GST_ELEMENT(gst_object_ref(element)));
                    }
                    else if (strstr(name, "v4l") != NULL)
                    {
                        v4l2src.attach(GST_ELEMENT(gst_object_ref(element)));
                    }
                    name.release();

                    done = sink && color && v4l2src;
                }
                g_value_unset (&value);
                break;
            case GST_ITERATOR_RESYNC:
                gst_iterator_resync (it);
                break;
            case GST_ITERATOR_ERROR:
            case GST_ITERATOR_DONE:
                done = TRUE;
                break;
            }
        }
        gst_iterator_free (it);

        if (!sink)
        {
            CV_WARN("cannot find appsink in manual pipeline");
            return false;
        }

        pipeline.swap(uridecodebin);
    }
    else
    {
        pipeline.reset(gst_pipeline_new(NULL));
        CV_Assert(pipeline);

        if (streamData.videoStream >= 0)
        {
            sink.reset(gst_element_factory_make("appsink", NULL));
            CV_Assert(sink);
            // videoconvert (in 0.10: ffmpegcolorspace, in 1.x autovideoconvert)
            //automatically selects the correct colorspace conversion based on caps.
            color.reset(gst_element_factory_make(COLOR_ELEM, NULL));
            CV_Assert(color);

            gst_bin_add_many(GST_BIN(pipeline.get()), uridecodebin.get(), color.get(), sink.get(), NULL);

            if (element_from_uri)
            {
                if(!gst_element_link(uridecodebin, color.get()))
                {
                    CV_WARN("GStreamer(video): cannot link color -> sink");
                    pipeline.release();
                    return false;
                }
            }
            else
            {
                g_signal_connect(uridecodebin, "pad-added", G_CALLBACK(newPad), color.get());
            }

            if (!gst_element_link(color.get(), sink.get()))
            {
                CV_WARN("GStreamer(video): cannot link color -> sink");
                pipeline.release();
                return false;
            }  
        }
        if (streamData.audioStream >= 0)
        {
            convert.reset(gst_element_factory_make("audioconvert", NULL));
            resample.reset(gst_element_factory_make("audioresample", NULL));
            audiosink.reset(gst_element_factory_make("appsink", NULL));
            CV_Assert(convert);
            CV_Assert(resample);
            CV_Assert(audiosink);

            gst_bin_add_many (GST_BIN (pipeline.get()), audioUridecodebin.get(), convert.get(), resample.get(), audiosink.get(), NULL);
            if (!gst_element_link_many (convert.get(), resample.get(), audiosink.get(), NULL))
            {
                CV_WARN("GStreamer(audio): cannot link convert -> resample -> sink");
                pipeline.release();
                return false;
            }
            g_signal_connect (audioUridecodebin, "pad-added", G_CALLBACK (newPad), convert.get());
            /*gst_bin_add_many (GST_BIN (pipeline.get()), audioUridecodebin.get(), convert.get(), audiosink.get(), NULL);
            if (!gst_element_link_many (convert.get(), audiosink.get(), NULL))
            {
                CV_WARN("GStreamer(audio): cannot link convert -> resample -> sink");
                pipeline.release();
                return false;
            }
            g_signal_connect (audioUridecodebin, "pad-added", G_CALLBACK (newPad), audiosink.get());*/
            /*gst_element_sync_state_with_parent (convert.get());
            gst_element_sync_state_with_parent (resample.get());
            gst_element_sync_state_with_parent (audiosink.get());*/
        }
    }

    if (!manualpipeline || strstr(filename, " max-buffers=") == NULL)
    {
        //TODO: is 1 single buffer really high enough?
        if (streamData.videoStream >= 0)
            gst_app_sink_set_max_buffers(GST_APP_SINK(sink.get()), 1);
        if (streamData.audioStream >= 0)
            gst_app_sink_set_max_buffers(GST_APP_SINK(audiosink.get()), 1);
    }
    if (!manualpipeline)
    {
        if (streamData.videoStream >= 0)
            gst_base_sink_set_sync(GST_BASE_SINK(sink.get()), FALSE);
        if (streamData.audioStream >= 0)
            gst_base_sink_set_sync(GST_BASE_SINK(audiosink.get()), FALSE);
    }
    if (streamData.videoStream >= 0)
    {
        //do not emit signals: all calls will be synchronous and blocking
        gst_app_sink_set_emit_signals (GST_APP_SINK(sink.get()), FALSE);
        caps.attach(gst_caps_from_string("video/x-raw, format=(string){BGR, GRAY8}; video/x-bayer,format=(string){rggb,bggr,grbg,gbrg}; image/jpeg"));
    }
    if (streamData.audioStream >= 0)
    {
        gst_app_sink_set_emit_signals(GST_APP_SINK(audiosink.get()), FALSE);
        std::string audioFormat;
        switch (outputAudioFormat)
        {
        case CV_8S:
        {
            audioBitPerSample = 8;
            audioFormat = "S8";
            break;
        }
        case CV_16S:
        {
            audioBitPerSample = 16;
            audioFormat = "S16LE";
            break;
        }
        case CV_32S:
        {
            audioBitPerSample = 32;
            audioFormat = "S32LE";
            break;
        }
        case CV_32F:
        {
            audioBitPerSample = 32;
            audioFormat = "F32LE";
            break;
        }
        default:
            audioFormat = "S16LE";
            break;
        }
        std::string stringCaps = "audio/x-raw, format=(string)" + audioFormat + ", rate=(int)" + std::to_string(audioSamplesPerSecond) + ", channels=(int){1, 2}, layout=(string)interleaved";
        audiocaps.attach(gst_caps_from_string(stringCaps.c_str()));

        gst_app_sink_set_caps(GST_APP_SINK(audiosink.get()), audiocaps);
        audiocaps.release();
    }
    if (manualpipeline)
    {
        GSafePtr<GstCaps> peer_caps;
        GSafePtr<GstPad> sink_pad;
        sink_pad.attach(gst_element_get_static_pad(sink, "sink"));
        peer_caps.attach(gst_pad_peer_query_caps(sink_pad, NULL));
        if (!gst_caps_can_intersect(caps, peer_caps))
        {
            caps.attach(gst_caps_from_string("video/x-raw, format=(string){UYVY,YUY2,YVYU,NV12,NV21,YV12,I420,BGRA,RGBA,BGRx,RGBx,GRAY16_LE,GRAY16_BE}"));
            CV_Assert(caps);
        }
    }
    if (streamData.videoStream >= 0)
    {
        gst_app_sink_set_caps(GST_APP_SINK(sink.get()), caps);
        caps.release();
    }
    if (streamData.audioStream >= 0)
    {
        bus.reset(gst_pipeline_get_bus(GST_PIPELINE(pipeline.get())));
        streamData.pipeline.reset(pipeline.get());
        gst_bus_set_sync_handler(bus, (GstBusSyncHandler) busMessage, &streamData, NULL);
    }
    {
        GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline.get()), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline-init");

        status = gst_element_set_state(GST_ELEMENT(pipeline.get()),
                                    file ? GST_STATE_PAUSED : GST_STATE_PLAYING);
        if (status == GST_STATE_CHANGE_ASYNC)
        {
            // wait for status update
            status = gst_element_get_state(pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
        }
        if (status == GST_STATE_CHANGE_FAILURE)
        {
            GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline.get()), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline-error");
            handleMessage(pipeline);
            pipeline.release();
            CV_WARN("unable to start pipeline");
            return false;
        }

        if (streamData.videoStream >= 0)
        {
            GSafePtr<GstPad> pad;
            pad.attach(gst_element_get_static_pad(sink, "sink"));

            GSafePtr<GstCaps> buffer_caps;
            buffer_caps.attach(gst_pad_get_current_caps(pad));

            GstFormat format;
            
            format = GST_FORMAT_DEFAULT;
            if(!gst_element_query_duration(sink, format, &duration))
            {
                handleMessage(pipeline);
                CV_WARN("unable to query duration of stream");
                duration = -1;
            }

            handleMessage(pipeline);
            
            const GstStructure *structure = gst_caps_get_structure(buffer_caps, 0);  // no lifetime transfer
            if (!gst_structure_get_int (structure, "width", &width) ||
                !gst_structure_get_int (structure, "height", &height))
            {
                CV_WARN("cannot query video width/height");
            }

            gint num = 0, denom=1;
            if (!gst_structure_get_fraction(structure, "framerate", &num, &denom))
            {
                CV_WARN("cannot query video fps");
            }

            fps = (double)num/(double)denom;

            {
                GstFormat format_;
                gint64 value_ = -1;
                gboolean status_;

                format_ = GST_FORMAT_DEFAULT;

                status_ = gst_element_query_position(sink, CV_GST_FORMAT(format_), &value_);
                if (!status_ || value_ != 0 || duration < 0)
                {
                    CV_WARN("Cannot query video position: status=" << status_ << ", value=" << value_ << ", duration=" << duration);
                    isPosFramesSupported = false;
                    isPosFramesEmulated = true;
                    emulatedFrameNumber = 0;
                }
                else
                    isPosFramesSupported = true;
            }
        }

        if (streamData.audioStream >= 0)
        {
            GSafePtr<GstPad> pad;
            pad.attach(gst_element_get_static_pad(audiosink, "sink"));

            GSafePtr<GstCaps> buffer_caps;
            buffer_caps.attach(gst_pad_get_current_caps(pad));

            GstAudioInfo info = {};
            if (gst_audio_info_from_caps(&info, buffer_caps))
            {
                nAudioChannels = GST_AUDIO_INFO_CHANNELS(&info);
                audioSamplesPerSecond = GST_AUDIO_INFO_RATE(&info);
            }
            else
            {
                CV_WARN("cannot query audio nChannels and SamplesPerSecond");
            }
        }
        GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline.get()), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline");
    }

    std::vector<int> unused_params = params.getUnused();
    for (int key : unused_params)
    {
        if (!setProperty(key, params.get<double>(key)))
        {
            CV_LOG_ERROR(NULL, "VIDEOIO/GStreamer: can't set property " << key);
            return false;
        }
    }

    if (pipeline)
    {
        VideoAccelerationType actual_va_type = VIDEO_ACCELERATION_NONE;
        GstIterator *iter = gst_bin_iterate_recurse(GST_BIN (pipeline.get()));
        gst_iterator_foreach(iter, find_hw_element, (gpointer)&actual_va_type);
        gst_iterator_free(iter);
        if (va_type != VIDEO_ACCELERATION_NONE && va_type != VIDEO_ACCELERATION_ANY)
        {
            if (va_type != actual_va_type)
            {
                CV_LOG_ERROR(NULL, "VIDEOIO/GStreamer: Can't select requested video acceleration through CAP_PROP_HW_ACCELERATION: "
                        << va_type << " (actual is " << actual_va_type << "). Bailout");
                return false;
            }
        }
        else
        {
            va_type = actual_va_type;
        }
    }

    return true;
}

/*!
 * \brief CvCapture_GStreamer::getProperty retrieve the requested property from the pipeline
 * \param propId requested property
 * \return property value
 *
 * There are two ways the properties can be retrieved. For seek-based properties we can query the pipeline.
 * For frame-based properties, we use the caps of the last receivef sample. This means that some properties
 * are not available until a first frame was received
 */
double GStreamerCapture::getProperty(int propId) const
{
    GstFormat format;
    gint64 value;
    gboolean status;

    if(!pipeline) {
        CV_WARN("GStreamer: no pipeline");
        return 0;
    }

    switch(propId)
    {
    case CV_CAP_PROP_POS_MSEC:
        CV_LOG_ONCE_WARNING(NULL, "OpenCV | GStreamer: CAP_PROP_POS_MSEC property result may be unrealiable: "
                                  "https://github.com/opencv/opencv/issues/19025");
        if (streamData.audioStream != -1)
        {
            return usedVideoSampleTime * 1e-6;
        }
        format = GST_FORMAT_TIME;
        status = gst_element_query_position(sink.get(), CV_GST_FORMAT(format), &value);
        if(!status) {
            handleMessage(pipeline);
            CV_WARN("GStreamer: unable to query position of stream");
            return 0;
        }
        return value * 1e-6; // nano seconds to milli seconds
    case CV_CAP_PROP_POS_FRAMES:
        if (!isPosFramesSupported)
        {
            if (isPosFramesEmulated)
                return emulatedFrameNumber;
            return 0; // TODO getProperty() "unsupported" value should be changed
        }
        format = GST_FORMAT_DEFAULT;
        status = gst_element_query_position(sink.get(), CV_GST_FORMAT(format), &value);
        if(!status) {
            handleMessage(pipeline);
            CV_WARN("GStreamer: unable to query position of stream");
            return 0;
        }
        return value;
    case CV_CAP_PROP_POS_AVI_RATIO:
        format = GST_FORMAT_PERCENT;
        status = gst_element_query_position(sink.get(), CV_GST_FORMAT(format), &value);
        if(!status) {
            handleMessage(pipeline);
            CV_WARN("GStreamer: unable to query position of stream");
            return 0;
        }
        return ((double) value) / GST_FORMAT_PERCENT_MAX;
    case CV_CAP_PROP_FRAME_WIDTH:
        return width;
    case CV_CAP_PROP_FRAME_HEIGHT:
        return height;
    case CV_CAP_PROP_FPS:
        return fps;
    case CV_CAP_PROP_FRAME_COUNT:
        return duration;
    case CV_CAP_PROP_BRIGHTNESS:
    case CV_CAP_PROP_CONTRAST:
    case CV_CAP_PROP_SATURATION:
    case CV_CAP_PROP_HUE:
        if (v4l2src)
        {
            std::string propName = get_gst_propname(propId);
            if (!propName.empty())
            {
                gint32 val = 0;
                g_object_get(G_OBJECT(v4l2src.get()), propName.c_str(), &val, NULL);
                return static_cast<double>(val);
            }
        }
        break;
    case CAP_PROP_HW_ACCELERATION:
        return static_cast<double>(va_type);
    case CAP_PROP_HW_DEVICE:
        return static_cast<double>(hw_device);
    case CV_CAP_GSTREAMER_QUEUE_LENGTH:
        if(!sink)
        {
            CV_WARN("there is no sink yet");
            return 0;
        }
        return gst_app_sink_get_max_buffers(GST_APP_SINK(sink.get()));
    case CAP_PROP_AUDIO_TOTAL_CHANNELS:
        return nAudioChannels;
    case CAP_PROP_AUDIO_SAMPLES_PER_SECOND:
        return audioSamplesPerSecond;
    case CAP_PROP_AUDIO_DATA_DEPTH:
        return outputAudioFormat;
    case CAP_PROP_AUDIO_BASE_INDEX:
        return audioBaseIndex;
    case CAP_PROP_AUDIO_TOTAL_STREAMS:
        return streamData.nbAudioStream;
    case CAP_PROP_AUDIO_POS:
        return audioSamplePos;
    case CAP_PROP_AUDIO_SHIFT_NSEC:
        return (double)(audioStartOffset - videoStartOffset)*1e-6;
    default:
        CV_WARN("unhandled property: " << propId);
        break;
    }

    return 0;
}

/*!
 * \brief CvCapture_GStreamer::setProperty
 * \param propId
 * \param value
 * \return success
 * Sets the desired property id with val. If the pipeline is running,
 * it is briefly stopped and started again after the property was set
 */
bool GStreamerCapture::setProperty(int propId, double value)
{
    const GstSeekFlags flags = (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE);

    if(!pipeline)
    {
        CV_WARN("no pipeline");
        return false;
    }

    bool wasPlaying = this->isPipelinePlaying();
    if (wasPlaying)
        this->stopPipeline();

    switch(propId)
    {
    case CV_CAP_PROP_POS_MSEC:
        if(!gst_element_seek_simple(GST_ELEMENT(pipeline.get()), GST_FORMAT_TIME,
                                    flags, (gint64) (value * GST_MSECOND))) {
            handleMessage(pipeline);
            CV_WARN("GStreamer: unable to seek");
        }
        else
        {
            if (isPosFramesEmulated)
            {
                if (value == 0)
                {
                    emulatedFrameNumber = 0;
                    return true;
                }
                else
                {
                    isPosFramesEmulated = false; // reset frame counter emulation
                }
            }
        }
        break;
    case CV_CAP_PROP_POS_FRAMES:
    {
        if (!isPosFramesSupported)
        {
            if (isPosFramesEmulated)
            {
                if (value == 0)
                {
                    restartPipeline();
                    emulatedFrameNumber = 0;
                    return true;
                }
            }
            return false;
            CV_WARN("unable to seek");
        }
        if(!gst_element_seek_simple(GST_ELEMENT(pipeline.get()), GST_FORMAT_DEFAULT,
                                    flags, (gint64) value)) {
            handleMessage(pipeline);
            CV_WARN("GStreamer: unable to seek");
            break;
        }
        // wait for status update
        gst_element_get_state(pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
        return true;
    }
    case CV_CAP_PROP_POS_AVI_RATIO:
        if(!gst_element_seek_simple(GST_ELEMENT(pipeline.get()), GST_FORMAT_PERCENT,
                                    flags, (gint64) (value * GST_FORMAT_PERCENT_MAX))) {
            handleMessage(pipeline);
            CV_WARN("GStreamer: unable to seek");
        }
        else
        {
            if (isPosFramesEmulated)
            {
                if (value == 0)
                {
                    emulatedFrameNumber = 0;
                    return true;
                }
                else
                {
                    isPosFramesEmulated = false; // reset frame counter emulation
                }
            }
        }
        break;
    case CV_CAP_PROP_FRAME_WIDTH:
        if(value > 0)
            setFilter("width", G_TYPE_INT, (int) value, 0);
        else
            removeFilter("width");
        break;
    case CV_CAP_PROP_FRAME_HEIGHT:
        if(value > 0)
            setFilter("height", G_TYPE_INT, (int) value, 0);
        else
            removeFilter("height");
        break;
    case CV_CAP_PROP_FPS:
        if(value > 0) {
            int num = 0, denom = 1;
            toFraction(value, num, denom);
            setFilter("framerate", GST_TYPE_FRACTION, value, denom);
        } else
            removeFilter("framerate");
        break;
    case CV_CAP_PROP_BRIGHTNESS:
    case CV_CAP_PROP_CONTRAST:
    case CV_CAP_PROP_SATURATION:
    case CV_CAP_PROP_HUE:
        if (v4l2src)
        {
            std::string propName = get_gst_propname(propId);
            if (!propName.empty())
            {
                gint32 val = cv::saturate_cast<gint32>(value);
                g_object_set(G_OBJECT(v4l2src.get()), propName.c_str(), &val, NULL);
                return true;
            }
        }
        return false;
    case CV_CAP_PROP_GAIN:
    case CV_CAP_PROP_CONVERT_RGB:
        break;
    case cv::CAP_PROP_HW_ACCELERATION:
        return false; // open-only
    case cv::CAP_PROP_HW_DEVICE:
        return false; // open-only
    case CV_CAP_GSTREAMER_QUEUE_LENGTH:
    {
        if(!sink)
        {
            CV_WARN("there is no sink yet");
            return false;
        }
        gst_app_sink_set_max_buffers(GST_APP_SINK(sink.get()), (guint) value);
        return true;
    }
    default:
        CV_WARN("GStreamer: unhandled property");
    }

    if (wasPlaying)
        this->startPipeline();

    return false;
}


Ptr<IVideoCapture> createGStreamerCapture_file(const String& filename, const cv::VideoCaptureParameters& params)
{
    Ptr<GStreamerCapture> cap = makePtr<GStreamerCapture>();
    if (cap && cap->open(filename, params))
        return cap;
    return Ptr<IVideoCapture>();
}

Ptr<IVideoCapture> createGStreamerCapture_cam(int index, const cv::VideoCaptureParameters& params)
{
    Ptr<GStreamerCapture> cap = makePtr<GStreamerCapture>();
    if (cap && cap->open(index, params))
        return cap;
    return Ptr<IVideoCapture>();
}

//==================================================================================================

/*!
 * \brief The CvVideoWriter_GStreamer class
 * Use GStreamer to write video
 */
class CvVideoWriter_GStreamer : public CvVideoWriter
{
public:
    CvVideoWriter_GStreamer()
        : ipl_depth(CV_8U)
        , input_pix_fmt(0), num_frames(0), framerate(0)
        , va_type(VIDEO_ACCELERATION_NONE), hw_device(0)
    {
    }
    virtual ~CvVideoWriter_GStreamer() CV_OVERRIDE
    {
        try
        {
            close();
        }
        catch (const std::exception& e)
        {
            CV_WARN("C++ exception in writer destructor: " << e.what());
        }
        catch (...)
        {
            CV_WARN("Unknown exception in writer destructor. Ignore");
        }
    }

    int getCaptureDomain() const CV_OVERRIDE { return cv::CAP_GSTREAMER; }

    bool open(const std::string &filename, int fourcc,
              double fps, const Size &frameSize, const VideoWriterParameters& params );
    void close();
    bool writeFrame( const IplImage* image ) CV_OVERRIDE;

    int getIplDepth() const { return ipl_depth; }

    virtual double getProperty(int) const CV_OVERRIDE;

protected:
    const char* filenameToMimetype(const char* filename);
    GSafePtr<GstElement> pipeline;
    GSafePtr<GstElement> source;
    int ipl_depth;
    int input_pix_fmt;
    int num_frames;
    double framerate;

    VideoAccelerationType va_type;
    int hw_device;

    void close_();
};

/*!
 * \brief CvVideoWriter_GStreamer::close
 * ends the pipeline by sending EOS and destroys the pipeline and all
 * elements afterwards
 */
void CvVideoWriter_GStreamer::close_()
{
    GstStateChangeReturn status;
    if (pipeline)
    {
        handleMessage(pipeline);

        if (!(bool)source)
        {
            CV_WARN("No source in GStreamer pipeline. Ignore");
        }
        else if (gst_app_src_end_of_stream(GST_APP_SRC(source.get())) != GST_FLOW_OK)
        {
            CV_WARN("Cannot send EOS to GStreamer pipeline");
        }
        else
        {
            //wait for EOS to trickle down the pipeline. This will let all elements finish properly
            GSafePtr<GstBus> bus; bus.attach(gst_element_get_bus(pipeline));
            if (bus)
            {
                GSafePtr<GstMessage> msg; msg.attach(gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS)));
                if (!msg || GST_MESSAGE_TYPE(msg.get()) == GST_MESSAGE_ERROR)
                {
                    CV_WARN("Error during VideoWriter finalization");
                    handleMessage(pipeline);
                }
            }
            else
            {
                CV_WARN("can't get GstBus");
            }
        }

        status = gst_element_set_state (pipeline, GST_STATE_NULL);
        if (status == GST_STATE_CHANGE_ASYNC)
        {
            // wait for status update
            GstState st1;
            GstState st2;
            status = gst_element_get_state(pipeline, &st1, &st2, GST_CLOCK_TIME_NONE);
        }
        if (status == GST_STATE_CHANGE_FAILURE)
        {
            handleMessage (pipeline);
            CV_WARN("Unable to stop writer pipeline");
        }
    }
}

void CvVideoWriter_GStreamer::close()
{
    close_();
    source.release();
    pipeline.release();
    va_type = VIDEO_ACCELERATION_NONE;
    hw_device = -1;
}

/*!
 * \brief filenameToMimetype
 * \param filename
 * \return mimetype
 * Returns a container mime type for a given filename by looking at it's extension
 */
const char* CvVideoWriter_GStreamer::filenameToMimetype(const char *filename)
{
    //get extension
    const char *ext_ = strrchr(filename, '.');
    if (!ext_ || ext_ == filename)
        return NULL;
    ext_ += 1; //exclude the dot

    std::string ext(ext_);
    ext = toLowerCase(ext);

    // return a container mime based on the given extension.
    // gstreamer's function returns too much possibilities, which is not useful to us

    //return the appropriate mime
    if (ext == "avi")
        return "video/x-msvideo";

    if (ext == "mkv" || ext == "mk3d" || ext == "webm")
        return "video/x-matroska";

    if (ext == "wmv")
        return "video/x-ms-asf";

    if (ext == "mov")
        return "video/x-quicktime";

    if (ext == "ogg" || ext == "ogv")
        return "application/ogg";

    if (ext == "rm")
        return "vnd.rn-realmedia";

    if (ext == "swf")
        return "application/x-shockwave-flash";

    if (ext == "mp4")
        return "video/x-quicktime, variant=(string)iso";

    //default to avi
    return "video/x-msvideo";
}

/*!
 * \brief CvVideoWriter_GStreamer::open
 * \param filename filename to output to
 * \param fourcc desired codec fourcc
 * \param fps desired framerate
 * \param frameSize the size of the expected frames
 * \param params other parameters
 * \return success
 *
 * We support 2 modes of operation. Either the user enters a filename and a fourcc
 * code, or enters a manual pipeline description like in CvVideoCapture_GStreamer.
 * In the latter case, we just push frames on the appsink with appropriate caps.
 * In the former case, we try to deduce the correct container from the filename,
 * and the correct encoder from the fourcc profile.
 *
 * If the file extension did was not recognize, an avi container is used
 *
 */
bool CvVideoWriter_GStreamer::open( const std::string &filename, int fourcc,
                                    double fps, const cv::Size &frameSize,
                                    const VideoWriterParameters& params )
{
    // check arguments
    CV_Assert(!filename.empty());
    CV_Assert(fps > 0);
    CV_Assert(frameSize.width > 0 && frameSize.height > 0);

    const bool is_color = params.get(VIDEOWRITER_PROP_IS_COLOR, true);
    const int depth = params.get(VIDEOWRITER_PROP_DEPTH, CV_8U);

    if (params.has(VIDEOWRITER_PROP_HW_ACCELERATION))
    {
        va_type = params.get<VideoAccelerationType>(VIDEOWRITER_PROP_HW_ACCELERATION);
    }
    if (params.has(VIDEOWRITER_PROP_HW_DEVICE))
    {
        hw_device = params.get<int>(VIDEOWRITER_PROP_HW_DEVICE);
        if (va_type == VIDEO_ACCELERATION_NONE && hw_device != -1)
        {
            CV_LOG_ERROR(NULL, "VIDEOIO/GStreamer: Invalid usage of VIDEOWRITER_PROP_HW_DEVICE without requested H/W acceleration. Bailout");
            return false;
        }
        if (va_type == VIDEO_ACCELERATION_ANY && hw_device != -1)
        {
            CV_LOG_ERROR(NULL, "VIDEOIO/GStreamer: Invalid usage of VIDEOWRITER_PROP_HW_DEVICE with 'ANY' H/W acceleration. Bailout");
            return false;
        }
        if (hw_device != -1)
        {
            CV_LOG_ERROR(NULL, "VIDEOIO/GStreamer: VIDEOWRITER_PROP_HW_DEVICE is not supported. Specify -1 (auto) value. Bailout");
            return false;
        }
    }

    if (params.warnUnusedParameters())
    {
        CV_LOG_ERROR(NULL, "VIDEOIO/GStreamer: unsupported parameters in VideoWriter, see logger INFO channel for details");
        return false;
    }

    // init gstreamer
    gst_initializer::init();

    // init vars
    GSafePtr<GstElement> file;
    GSafePtr<GstElement> encodebin;

    bool manualpipeline = true;
    int  bufsize = 0;
    GSafePtr<GError> err;
    GstStateChangeReturn stateret;

    GSafePtr<GstCaps> caps;

    GstIterator* it = NULL;
    gboolean done = FALSE;

    // we first try to construct a pipeline from the given string.
    // if that fails, we assume it is an ordinary filename

    encodebin.attach(gst_parse_launch(filename.c_str(), err.getRef()));
    manualpipeline = (bool)encodebin;

    if (manualpipeline)
    {
        if (err)
        {
            CV_WARN("error opening writer pipeline: " << err->message);
            if (encodebin)
            {
                gst_element_set_state(encodebin, GST_STATE_NULL);
            }
            handleMessage(encodebin);
            encodebin.release();
            return false;
        }
        it = gst_bin_iterate_sources (GST_BIN(encodebin.get()));

        while (!done)
        {
          GValue value = G_VALUE_INIT;
          GSafePtr<gchar> name;
          GstElement* element = NULL;
          switch (gst_iterator_next (it, &value)) {
            case GST_ITERATOR_OK:
              element = GST_ELEMENT (g_value_get_object (&value));  // no lifetime transfer
              name.attach(gst_element_get_name(element));
              if (name)
              {
                if (strstr(name.get(), "opencvsrc") != NULL || strstr(name.get(), "appsrc") != NULL)
                {
                  source.attach(GST_ELEMENT(gst_object_ref(element)));
                  done = TRUE;
                }
              }
              g_value_unset(&value);

              break;
            case GST_ITERATOR_RESYNC:
              gst_iterator_resync (it);
              break;
            case GST_ITERATOR_ERROR:
            case GST_ITERATOR_DONE:
              done = TRUE;
              break;
          }
        }
        gst_iterator_free (it);

        if (!source){
            CV_WARN("GStreamer: cannot find appsrc in manual pipeline\n");
            return false;
        }
        pipeline.swap(encodebin);
    }
    else
    {
        err.release();
        pipeline.reset(gst_pipeline_new(NULL));

        // we just got a filename and a fourcc code.
        // first, try to guess the container from the filename

        //proxy old non existing fourcc ids. These were used in previous opencv versions,
        //but do not even exist in gstreamer any more
        if (fourcc == CV_FOURCC('M','P','1','V')) fourcc = CV_FOURCC('M', 'P', 'G' ,'1');
        if (fourcc == CV_FOURCC('M','P','2','V')) fourcc = CV_FOURCC('M', 'P', 'G' ,'2');
        if (fourcc == CV_FOURCC('D','R','A','C')) fourcc = CV_FOURCC('d', 'r', 'a' ,'c');


        //create encoder caps from fourcc
        GSafePtr<GstCaps> videocaps;
        videocaps.attach(gst_riff_create_video_caps(fourcc, NULL, NULL, NULL, NULL, NULL));
        if (!videocaps)
        {
            CV_WARN("OpenCV backend does not support passed FOURCC value");
            return false;
        }

        //create container caps from file extension
        const char* mime = filenameToMimetype(filename.c_str());
        if (!mime)
        {
            CV_WARN("OpenCV backend does not support this file type (extension): " << filename);
            return false;
        }

        //create pipeline elements
        encodebin.reset(gst_element_factory_make("encodebin", NULL));

        GSafePtr<GstCaps> containercaps;
        GSafePtr<GstEncodingContainerProfile> containerprofile;
        GSafePtr<GstEncodingVideoProfile> videoprofile;

        containercaps.attach(gst_caps_from_string(mime));

        //create encodebin profile
        containerprofile.attach(gst_encoding_container_profile_new("container", "container", containercaps.get(), NULL));
        videoprofile.reset(gst_encoding_video_profile_new(videocaps.get(), NULL, NULL, 1));
        gst_encoding_container_profile_add_profile(containerprofile.get(), (GstEncodingProfile*)videoprofile.get());

        g_object_set(G_OBJECT(encodebin.get()), "profile", containerprofile.get(), NULL);

        source.reset(gst_element_factory_make("appsrc", NULL));
        file.reset(gst_element_factory_make("filesink", NULL));
        g_object_set(G_OBJECT(file.get()), "location", (const char*)filename.c_str(), NULL);
    }

    int fps_num = 0, fps_denom = 1;
    toFraction(fps, fps_num, fps_denom);

    if (fourcc == CV_FOURCC('M','J','P','G') && frameSize.height == 1)
    {
        CV_Assert(depth == CV_8U);
        ipl_depth = IPL_DEPTH_8U;
        input_pix_fmt = GST_VIDEO_FORMAT_ENCODED;
        caps.attach(gst_caps_new_simple("image/jpeg",
                                        "framerate", GST_TYPE_FRACTION, int(fps_num), int(fps_denom),
                                        NULL));
        caps.attach(gst_caps_fixate(caps.detach()));
    }
    else if (is_color)
    {
        CV_Assert(depth == CV_8U);
        ipl_depth = IPL_DEPTH_8U;
        input_pix_fmt = GST_VIDEO_FORMAT_BGR;
        bufsize = frameSize.width * frameSize.height * 3;

        caps.attach(gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, "BGR",
                                        "width", G_TYPE_INT, frameSize.width,
                                        "height", G_TYPE_INT, frameSize.height,
                                        "framerate", GST_TYPE_FRACTION, gint(fps_num), gint(fps_denom),
                                        NULL));
        CV_Assert(caps);
        caps.attach(gst_caps_fixate(caps.detach()));
        CV_Assert(caps);
    }
    else if (!is_color && depth == CV_8U)
    {
        ipl_depth = IPL_DEPTH_8U;
        input_pix_fmt = GST_VIDEO_FORMAT_GRAY8;
        bufsize = frameSize.width * frameSize.height;

        caps.attach(gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, "GRAY8",
                                        "width", G_TYPE_INT, frameSize.width,
                                        "height", G_TYPE_INT, frameSize.height,
                                        "framerate", GST_TYPE_FRACTION, gint(fps_num), gint(fps_denom),
                                        NULL));
        caps.attach(gst_caps_fixate(caps.detach()));
    }
    else if (!is_color && depth == CV_16U)
    {
        ipl_depth = IPL_DEPTH_16U;
        input_pix_fmt = GST_VIDEO_FORMAT_GRAY16_LE;
        bufsize = frameSize.width * frameSize.height * 2;

        caps.attach(gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, "GRAY16_LE",
                                        "width", G_TYPE_INT, frameSize.width,
                                        "height", G_TYPE_INT, frameSize.height,
                                        "framerate", GST_TYPE_FRACTION, gint(fps_num), gint(fps_denom),
                                        NULL));
        caps.attach(gst_caps_fixate(caps.detach()));
    }
    else
    {
        CV_WARN("unsupported depth=" << depth <<", and is_color=" << is_color << " combination");
        pipeline.release();
        return false;
    }

    gst_app_src_set_caps(GST_APP_SRC(source.get()), caps);
    gst_app_src_set_stream_type(GST_APP_SRC(source.get()), GST_APP_STREAM_TYPE_STREAM);
    gst_app_src_set_size (GST_APP_SRC(source.get()), -1);

    g_object_set(G_OBJECT(source.get()), "format", GST_FORMAT_TIME, NULL);
    g_object_set(G_OBJECT(source.get()), "block", 1, NULL);
    g_object_set(G_OBJECT(source.get()), "is-live", 0, NULL);


    if (!manualpipeline)
    {
        g_object_set(G_OBJECT(file.get()), "buffer-size", bufsize, NULL);
        gst_bin_add_many(GST_BIN(pipeline.get()), source.get(), encodebin.get(), file.get(), NULL);
        if (!gst_element_link_many(source.get(), encodebin.get(), file.get(), NULL))
        {
            CV_WARN("cannot link elements");
            pipeline.release();
            return false;
        }
    }

    GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline.get()), GST_DEBUG_GRAPH_SHOW_ALL, "write-pipeline");

    stateret = gst_element_set_state(GST_ELEMENT(pipeline.get()), GST_STATE_PLAYING);
    if (stateret == GST_STATE_CHANGE_FAILURE)
    {
        handleMessage(pipeline);
        CV_WARN("GStreamer: cannot put pipeline to play\n");
        pipeline.release();
        return false;
    }

    framerate = fps;
    num_frames = 0;

    handleMessage(pipeline);

    if (pipeline)
    {
        VideoAccelerationType actual_va_type = VIDEO_ACCELERATION_NONE;
        GstIterator *iter = gst_bin_iterate_recurse(GST_BIN (pipeline.get()));
        gst_iterator_foreach(iter, find_hw_element, (gpointer)&actual_va_type);
        gst_iterator_free(iter);
        if (va_type != VIDEO_ACCELERATION_NONE && va_type != VIDEO_ACCELERATION_ANY)
        {
            if (va_type != actual_va_type)
            {
                CV_LOG_ERROR(NULL, "VIDEOIO/GStreamer: Can't select requested VideoWriter acceleration through VIDEOWRITER_PROP_HW_ACCELERATION: "
                        << va_type << " (actual is " << actual_va_type << "). Bailout");
                close();
                return false;
            }
        }
        else
        {
            va_type = actual_va_type;
        }
    }

    return true;
}


/*!
 * \brief CvVideoWriter_GStreamer::writeFrame
 * \param image
 * \return
 * Pushes the given frame on the pipeline.
 * The timestamp for the buffer is generated from the framerate set in open
 * and ensures a smooth video
 */
bool CvVideoWriter_GStreamer::writeFrame( const IplImage * image )
{
    GstClockTime duration, timestamp;
    GstFlowReturn ret;
    int size;

    handleMessage(pipeline);

    if (input_pix_fmt == GST_VIDEO_FORMAT_ENCODED) {
        if (image->nChannels != 1 || image->depth != IPL_DEPTH_8U || image->height != 1) {
            CV_WARN("cvWriteFrame() needs images with depth = IPL_DEPTH_8U, nChannels = 1 and height = 1.");
            return false;
        }
    }
    else
    if(input_pix_fmt == GST_VIDEO_FORMAT_BGR) {
        if (image->nChannels != 3 || image->depth != IPL_DEPTH_8U) {
            CV_WARN("cvWriteFrame() needs images with depth = IPL_DEPTH_8U and nChannels = 3.");
            return false;
        }
    }
    else if (input_pix_fmt == GST_VIDEO_FORMAT_GRAY8) {
        if (image->nChannels != 1 || image->depth != IPL_DEPTH_8U) {
            CV_WARN("cvWriteFrame() needs images with depth = IPL_DEPTH_8U and nChannels = 1.");
            return false;
        }
    }
    else if (input_pix_fmt == GST_VIDEO_FORMAT_GRAY16_LE) {
        if (image->nChannels != 1 || image->depth != IPL_DEPTH_16U) {
            CV_WARN("cvWriteFrame() needs images with depth = IPL_DEPTH_16U and nChannels = 1.");
            return false;
        }
    }
    else {
        CV_WARN("cvWriteFrame() needs BGR or grayscale images\n");
        return false;
    }

    size = image->imageSize;
    duration = ((double)1/framerate) * GST_SECOND;
    timestamp = num_frames * duration;

    //gst_app_src_push_buffer takes ownership of the buffer, so we need to supply it a copy
    GstBuffer *buffer = gst_buffer_new_allocate(NULL, size, NULL);
    GstMapInfo info;
    gst_buffer_map(buffer, &info, (GstMapFlags)GST_MAP_READ);
    memcpy(info.data, (guint8*)image->imageData, size);
    gst_buffer_unmap(buffer, &info);
    GST_BUFFER_DURATION(buffer) = duration;
    GST_BUFFER_PTS(buffer) = timestamp;
    GST_BUFFER_DTS(buffer) = timestamp;
    //set the current number in the frame
    GST_BUFFER_OFFSET(buffer) = num_frames;

    ret = gst_app_src_push_buffer(GST_APP_SRC(source.get()), buffer);
    if (ret != GST_FLOW_OK)
    {
        CV_WARN("Error pushing buffer to GStreamer pipeline");
        return false;
    }

    //GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline");

    ++num_frames;

    return true;
}


double CvVideoWriter_GStreamer::getProperty(int propId) const
{
    if (propId == VIDEOWRITER_PROP_HW_ACCELERATION)
    {
        return static_cast<double>(va_type);
    }
    else if (propId == VIDEOWRITER_PROP_HW_DEVICE)
    {
        return static_cast<double>(hw_device);
    }
    return 0;
}

Ptr<IVideoWriter> create_GStreamer_writer(const std::string& filename, int fourcc, double fps,
                                          const cv::Size& frameSize, const VideoWriterParameters& params)
{
    CvVideoWriter_GStreamer* wrt = new CvVideoWriter_GStreamer;
    try
    {
        if (wrt->open(filename, fourcc, fps, frameSize, params))
            return makePtr<LegacyWriter>(wrt);
        delete wrt;
    }
    catch (...)
    {
        delete wrt;
        throw;
    }
    return 0;
}

// utility functions

void toFraction(const double decimal, int &numerator_i, int &denominator_i)
{
    double err = 1.0;
    int denominator = 1;
    double numerator = 0;
    for (int check_denominator = 1; ; check_denominator++)
    {
        double check_numerator = (double)check_denominator * decimal;
        double dummy;
        double check_err = modf(check_numerator, &dummy);
        if (check_err < err)
        {
            err = check_err;
            denominator = check_denominator;
            numerator = check_numerator;
            if (err < FLT_EPSILON)
                break;
        }
        if (check_denominator == 100)  // limit
            break;
    }
    numerator_i = cvRound(numerator);
    denominator_i = denominator;
    //printf("%g: %d/%d    (err=%g)\n", decimal, numerator_i, denominator_i, err);
}


/*!
 * \brief handleMessage
 * Handles gstreamer bus messages. Mainly for debugging purposes and ensuring clean shutdown on error
 */
void handleMessage(GstElement * pipeline)
{
    GSafePtr<GstBus> bus;
    GstStreamStatusType tp;
    GstElement * elem = NULL;

    bus.attach(gst_element_get_bus(pipeline));

    while (gst_bus_have_pending(bus))
    {
        GSafePtr<GstMessage> msg;
        msg.attach(gst_bus_pop(bus));
        if (!msg || !GST_IS_MESSAGE(msg.get()))
            continue;

        if (gst_is_missing_plugin_message(msg))
        {
            CV_WARN("your GStreamer installation is missing a required plugin");
        }
        else
        {
            switch (GST_MESSAGE_TYPE (msg)) {
            case GST_MESSAGE_STATE_CHANGED:
                GstState oldstate, newstate, pendstate;
                gst_message_parse_state_changed(msg, &oldstate, &newstate, &pendstate);
                break;
            case GST_MESSAGE_ERROR:
            {
                GSafePtr<GError> err;
                GSafePtr<gchar> debug;
                gst_message_parse_error(msg, err.getRef(), debug.getRef());
                GSafePtr<gchar> name; name.attach(gst_element_get_name(GST_MESSAGE_SRC (msg)));
                CV_WARN("Embedded video playback halted; module " << name.get() <<
                        " reported: " << (err ? err->message : "<unknown reason>"));
                CV_LOG_DEBUG(NULL, "GStreamer debug: " << debug.get());

                gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);
                break;
            }
            case GST_MESSAGE_EOS:
                break;
            case GST_MESSAGE_STREAM_STATUS:
                gst_message_parse_stream_status(msg,&tp,&elem);
                break;
            default:
                break;
            }
        }
    }
}

}  // namespace cv

//==================================================================================================

#if defined(BUILD_PLUGIN)

#define CAPTURE_ABI_VERSION 1
#define CAPTURE_API_VERSION 1
#include "plugin_capture_api.hpp"
#define WRITER_ABI_VERSION 1
#define WRITER_API_VERSION 1
#include "plugin_writer_api.hpp"

namespace cv {

static
CvResult CV_API_CALL cv_capture_open_with_params(
        const char* filename, int camera_index,
        int* params, unsigned n_params,
        CV_OUT CvPluginCapture* handle
)
{
    if (!handle)
        return CV_ERROR_FAIL;
    *handle = NULL;
    if (!filename)
        return CV_ERROR_FAIL;
    GStreamerCapture *cap = 0;
    try
    {
        cv::VideoCaptureParameters parameters(params, n_params);
        cap = new GStreamerCapture();
        bool res;
        if (filename)
            res = cap->open(std::string(filename), parameters);
        else
            res = cap->open(camera_index, parameters);
        if (res)
        {
            *handle = (CvPluginCapture)cap;
            return CV_ERROR_OK;
        }
    }
    catch (const std::exception& e)
    {
        CV_LOG_WARNING(NULL, "GStreamer: Exception is raised: " << e.what());
    }
    catch (...)
    {
        CV_LOG_WARNING(NULL, "GStreamer: Unknown C++ exception is raised");
    }
    if (cap)
        delete cap;
    return CV_ERROR_FAIL;
}

static
CvResult CV_API_CALL cv_capture_open(const char* filename, int camera_index, CV_OUT CvPluginCapture* handle)
{
    return cv_capture_open_with_params(filename, camera_index, NULL, 0, handle);
}

static
CvResult CV_API_CALL cv_capture_release(CvPluginCapture handle)
{
    if (!handle)
        return CV_ERROR_FAIL;
    GStreamerCapture* instance = (GStreamerCapture*)handle;
    delete instance;
    return CV_ERROR_OK;
}


static
CvResult CV_API_CALL cv_capture_get_prop(CvPluginCapture handle, int prop, CV_OUT double* val)
{
    if (!handle)
        return CV_ERROR_FAIL;
    if (!val)
        return CV_ERROR_FAIL;
    try
    {
        GStreamerCapture* instance = (GStreamerCapture*)handle;
        *val = instance->getProperty(prop);
        return CV_ERROR_OK;
    }
    catch (const std::exception& e)
    {
        CV_LOG_WARNING(NULL, "GStreamer: Exception is raised: " << e.what());
        return CV_ERROR_FAIL;
    }
    catch (...)
    {
        CV_LOG_WARNING(NULL, "GStreamer: Unknown C++ exception is raised");
        return CV_ERROR_FAIL;
    }
}

static
CvResult CV_API_CALL cv_capture_set_prop(CvPluginCapture handle, int prop, double val)
{
    if (!handle)
        return CV_ERROR_FAIL;
    try
    {
        GStreamerCapture* instance = (GStreamerCapture*)handle;
        return instance->setProperty(prop, val) ? CV_ERROR_OK : CV_ERROR_FAIL;
    }
    catch (const std::exception& e)
    {
        CV_LOG_WARNING(NULL, "GStreamer: Exception is raised: " << e.what());
        return CV_ERROR_FAIL;
    }
    catch (...)
    {
        CV_LOG_WARNING(NULL, "GStreamer: Unknown C++ exception is raised");
        return CV_ERROR_FAIL;
    }
}

static
CvResult CV_API_CALL cv_capture_grab(CvPluginCapture handle)
{
    if (!handle)
        return CV_ERROR_FAIL;
    try
    {
        GStreamerCapture* instance = (GStreamerCapture*)handle;
        return instance->grabFrame() ? CV_ERROR_OK : CV_ERROR_FAIL;
    }
    catch (const std::exception& e)
    {
        CV_LOG_WARNING(NULL, "GStreamer: Exception is raised: " << e.what());
        return CV_ERROR_FAIL;
    }
    catch (...)
    {
        CV_LOG_WARNING(NULL, "GStreamer: Unknown C++ exception is raised");
        return CV_ERROR_FAIL;
    }
}

static
CvResult CV_API_CALL cv_capture_retrieve(CvPluginCapture handle, int stream_idx, cv_videoio_capture_retrieve_cb_t callback, void* userdata)
{
    if (!handle)
        return CV_ERROR_FAIL;
    try
    {
        GStreamerCapture* instance = (GStreamerCapture*)handle;
        Mat img;
        // TODO: avoid unnecessary copying - implement lower level GStreamerCapture::retrieve
        if (instance->retrieveFrame(stream_idx, img))
            return callback(stream_idx, img.data, img.step, img.cols, img.rows, img.type(), userdata);
        return CV_ERROR_FAIL;
    }
    catch (const std::exception& e)
    {
        CV_LOG_WARNING(NULL, "GStreamer: Exception is raised: " << e.what());
        return CV_ERROR_FAIL;
    }
    catch (...)
    {
        CV_LOG_WARNING(NULL, "GStreamer: Unknown C++ exception is raised");
        return CV_ERROR_FAIL;
    }
}

static
CvResult CV_API_CALL cv_writer_open_with_params(
        const char* filename, int fourcc, double fps, int width, int height,
        int* params, unsigned n_params,
        CV_OUT CvPluginWriter* handle)
{
    CvVideoWriter_GStreamer* wrt = 0;
    try
    {
        CvSize sz = { width, height };
        VideoWriterParameters parameters(params, n_params);
        wrt = new CvVideoWriter_GStreamer();
        if (wrt && wrt->open(filename, fourcc, fps, sz, parameters))
        {
            *handle = (CvPluginWriter)wrt;
            return CV_ERROR_OK;
        }
    }
    catch (const std::exception& e)
    {
        CV_LOG_WARNING(NULL, "GStreamer: Exception is raised: " << e.what());
    }
    catch (...)
    {
        CV_LOG_WARNING(NULL, "GStreamer: Unknown C++ exception is raised");
    }
    if (wrt)
        delete wrt;
    return CV_ERROR_FAIL;
}

static
CvResult CV_API_CALL cv_writer_open(const char* filename, int fourcc, double fps, int width, int height, int isColor,
    CV_OUT CvPluginWriter* handle)
{
    int params[2] = { VIDEOWRITER_PROP_IS_COLOR, isColor };
    return cv_writer_open_with_params(filename, fourcc, fps, width, height, params, 1, handle);
}

static
CvResult CV_API_CALL cv_writer_release(CvPluginWriter handle)
{
    if (!handle)
        return CV_ERROR_FAIL;
    CvVideoWriter_GStreamer* instance = (CvVideoWriter_GStreamer*)handle;
    delete instance;
    return CV_ERROR_OK;
}

static
CvResult CV_API_CALL cv_writer_get_prop(CvPluginWriter handle, int prop, CV_OUT double* val)
{
    if (!handle)
        return CV_ERROR_FAIL;
    if (!val)
        return CV_ERROR_FAIL;
    try
    {
        CvVideoWriter_GStreamer* instance = (CvVideoWriter_GStreamer*)handle;
        *val = instance->getProperty(prop);
        return CV_ERROR_OK;
    }
    catch (...)
    {
        return CV_ERROR_FAIL;
    }
}


static
CvResult CV_API_CALL cv_writer_set_prop(CvPluginWriter /*handle*/, int /*prop*/, double /*val*/)
{
    return CV_ERROR_FAIL;
}

static
CvResult CV_API_CALL cv_writer_write(CvPluginWriter handle, const unsigned char *data, int step, int width, int height, int cn)
{
    if (!handle)
        return CV_ERROR_FAIL;
    try
    {
        CvVideoWriter_GStreamer* instance = (CvVideoWriter_GStreamer*)handle;
        CvSize sz = { width, height };
        IplImage img;
        cvInitImageHeader(&img, sz, instance->getIplDepth(), cn);
        cvSetData(&img, const_cast<unsigned char*>(data), step);
        return instance->writeFrame(&img) ? CV_ERROR_OK : CV_ERROR_FAIL;
    }
    catch (const std::exception& e)
    {
        CV_LOG_WARNING(NULL, "GStreamer: Exception is raised: " << e.what());
        return CV_ERROR_FAIL;
    }
    catch (...)
    {
        CV_LOG_WARNING(NULL, "GStreamer: Unknown C++ exception is raised");
        return CV_ERROR_FAIL;
    }
}

static const OpenCV_VideoIO_Capture_Plugin_API capture_api =
{
    {
        sizeof(OpenCV_VideoIO_Capture_Plugin_API), CAPTURE_ABI_VERSION, CAPTURE_API_VERSION,
        CV_VERSION_MAJOR, CV_VERSION_MINOR, CV_VERSION_REVISION, CV_VERSION_STATUS,
        "GStreamer OpenCV Video I/O Capture plugin"
    },
    {
        /*  1*/CAP_GSTREAMER,
        /*  2*/cv_capture_open,
        /*  3*/cv_capture_release,
        /*  4*/cv_capture_get_prop,
        /*  5*/cv_capture_set_prop,
        /*  6*/cv_capture_grab,
        /*  7*/cv_capture_retrieve,
    },
    {
        /*  8*/cv_capture_open_with_params,
    }
};

static const OpenCV_VideoIO_Writer_Plugin_API writer_api =
{
    {
        sizeof(OpenCV_VideoIO_Writer_Plugin_API), WRITER_ABI_VERSION, WRITER_API_VERSION,
        CV_VERSION_MAJOR, CV_VERSION_MINOR, CV_VERSION_REVISION, CV_VERSION_STATUS,
        "GStreamer OpenCV Video I/O Writer plugin"
    },
    {
        /*  1*/CAP_GSTREAMER,
        /*  2*/cv_writer_open,
        /*  3*/cv_writer_release,
        /*  4*/cv_writer_get_prop,
        /*  5*/cv_writer_set_prop,
        /*  6*/cv_writer_write
    },
    {
        /*  7*/cv_writer_open_with_params
    }
};

} // namespace

const OpenCV_VideoIO_Capture_Plugin_API* opencv_videoio_capture_plugin_init_v1(int requested_abi_version, int requested_api_version, void* /*reserved=NULL*/) CV_NOEXCEPT
{
    if (requested_abi_version == CAPTURE_ABI_VERSION && requested_api_version <= CAPTURE_API_VERSION)
        return &cv::capture_api;
    return NULL;
}

const OpenCV_VideoIO_Writer_Plugin_API* opencv_videoio_writer_plugin_init_v1(int requested_abi_version, int requested_api_version, void* /*reserved=NULL*/) CV_NOEXCEPT
{
    if (requested_abi_version == WRITER_ABI_VERSION && requested_api_version <= WRITER_API_VERSION)
        return &cv::writer_api;
    return NULL;
}

#endif // BUILD_PLUGIN
