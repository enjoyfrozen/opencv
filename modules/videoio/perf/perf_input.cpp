// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html
#include "perf_precomp.hpp"
#include <thread>
namespace opencv_test
{
using namespace perf;

typedef perf::TestBaseWithParam<std::string> VideoCapture_Reading;

const string bunny_files[] = {
    "highgui/video/big_buck_bunny.avi",
    "highgui/video/big_buck_bunny.mov",
    "highgui/video/big_buck_bunny.mp4",
#ifndef HAVE_MSMF
    // MPEG2 is not supported by Media Foundation yet
    // http://social.msdn.microsoft.com/Forums/en-US/mediafoundationdevelopment/thread/39a36231-8c01-40af-9af5-3c105d684429
    "highgui/video/big_buck_bunny.mpg",
#endif
    "highgui/video/big_buck_bunny.wmv"
};

PERF_TEST_P(VideoCapture_Reading, ReadFile, testing::ValuesIn(bunny_files) )
{
  string filename = getDataPath(GetParam());

  VideoCapture cap;

  TEST_CYCLE() cap.open(filename);

  SANITY_CHECK_NOTHING();
}

PERF_TEST(, GetReadFrame)
{
    VideoCapture cap1(0, CAP_V4L);
    VideoCapture cap2(2, CAP_V4L);

    ASSERT_TRUE(cap1.isOpened());
    ASSERT_TRUE(cap2.isOpened());

    int ITERATION_COUNT = 50; //number of expected frames from all cameras

    Mat frame1, frame2,  processed1, processed2;

    //false start
    EXPECT_TRUE(cap1.read(frame1));
    EXPECT_TRUE(cap2.read(frame2));

    TEST_CYCLE() {
        auto func = [&]()
        {
            for(int i = 0; i < 50; ++i)//50-load
            {
                cv::Canny(frame1, processed1, 400, 1000, 5);
            }
        };
        for(int j = 0; j < ITERATION_COUNT; ++j)
        {
            EXPECT_TRUE(cap1.read(frame1));
            std::thread th1(func);

            EXPECT_TRUE(cap2.read(frame2));
            for(int i = 0; i < 50; ++i)//50-load
            {
                cv::Canny(frame2, processed2, 400, 1000, 5);
            }
            th1.join();
        }
    };
    SANITY_CHECK_NOTHING();
}

PERF_TEST(, GetWaitAnySyncFrame)
{
    VideoCapture cap1(0, CAP_V4L);
    VideoCapture cap2(2, CAP_V4L);

    ASSERT_TRUE(cap1.isOpened());
    ASSERT_TRUE(cap2.isOpened());

    std::vector<VideoCapture> VCM;

    VCM.push_back(cap1);
    VCM.push_back(cap2);

    std::vector<int> state(VCM.size());

    Mat frame1, frame2, processed1, processed2;
    std::vector<Mat> forMAt = {frame1, frame2};
    std::vector<Mat> forProc = {processed1, processed2};
    int TIMEOUT = -1;
    int FRAME_COUNT = 500;//number of expected frames from all cameras

    //false start
    EXPECT_TRUE(cap1.read(frame1));
    EXPECT_TRUE(cap2.read(frame2));

    TEST_CYCLE() {
        int COUNTER = 0;
        do
        {
            VideoCapture::waitAny(VCM, state, TIMEOUT);
            EXPECT_EQ(VCM.size(), state.size());
            for(unsigned int i = 0; i < VCM.size(); ++i)
            {
                if(state[i] == CAP_CAM_READY)
                {
                  EXPECT_TRUE(VCM[i].retrieve(forMAt[i]));
                  for(int e = 0; e < 50; ++e)//50-load
                  {
                      cv::Canny(forMAt[i], forProc[i], 400, 1000, 5);
                  }
                  ++COUNTER;
                }
            }
        }
        while(COUNTER != FRAME_COUNT);
    };
    SANITY_CHECK_NOTHING();
}

PERF_TEST(, GetWaitAnyAsyncFrame)
{
    VideoCapture cap1(0, CAP_V4L);
    VideoCapture cap2(2, CAP_V4L);

    ASSERT_TRUE(cap1.isOpened());
    ASSERT_TRUE(cap2.isOpened());

    std::vector<VideoCapture> VCM;

    VCM.push_back(cap1);
    VCM.push_back(cap2);

    std::vector<int> state;

    Mat frame1, frame2, processed1, processed2;
    std::vector<Mat> forMAt = {frame1, frame2};
    std::vector<Mat> forProc = {processed1, processed2};
    int TIMEOUT = -1;
    int FRAME_COUNT = 500;

    //false start
    cap1>>frame1;
    cap2>>frame2;

    TEST_CYCLE() {
        int COUNTER = 0, numCam = 2;
        vector<int> nums(numCam, -1);
        auto func = [&](int _st)
        {
                cv::Canny(forMAt[_st], forProc[_st], 400, 1000, 5);
                ++COUNTER;
        };
        VideoCapture::waitAny(VCM, state, TIMEOUT);
        do
        {
            EXPECT_EQ(VCM.size(), state.size());
            for(unsigned int i = 0; i < VCM.size(); ++i)
            {
               if(state[i] == CAP_CAM_READY)
               {
                   EXPECT_TRUE(VCM[i].retrieve(forMAt[i]));
                   nums[i] = i;
               }
            }
            vector<std::thread> threads;

            for(int nc = 0; nc < numCam; ++nc)
            {
                if(nums[nc] != -1)
                   {
                    threads.push_back(std::thread(func, nums[nc]));
                    nums[nc] = -1;
                   }
            }
            VideoCapture::waitAny(VCM, state, TIMEOUT);

            for(unsigned int nc = 0; nc < threads.size(); ++nc)
            {
                 threads[nc].join();
            }
        }
        while(COUNTER != FRAME_COUNT);
    };
    SANITY_CHECK_NOTHING();
}

PERF_TEST(, GetWaitAnyMultiThFrame)
{
    VideoCapture cap1(0, CAP_V4L);
    VideoCapture cap2(2, CAP_V4L);
    VideoCapture cap3(4, CAP_V4L);

    ASSERT_TRUE(cap1.isOpened());
    ASSERT_TRUE(cap2.isOpened());
    ASSERT_TRUE(cap3.isOpened());

    EXPECT_TRUE(cap1.set(CAP_PROP_FPS, 30));
    EXPECT_TRUE(cap2.set(CAP_PROP_FPS, 30));
    EXPECT_TRUE(cap3.set(CAP_PROP_FPS, 30));

    std::vector<VideoCapture> VCM;

    VCM.push_back(cap1);
    VCM.push_back(cap2);
    VCM.push_back(cap3);

    std::vector<int> state(VCM.size());

    Mat frame1, frame2, frame3, processed1, processed2, processed3;
    std::vector<Mat> forMAt = {frame1, frame2, frame3};
    std::vector<Mat> forProc = {processed1, processed2, processed3};

    int TIMEOUT = -1;
    int FRAME_COUNT = 50;//number of expected frames from all cameras

    //false start
    EXPECT_TRUE(cap1.read(frame1));
    EXPECT_TRUE(cap2.read(frame2));
    EXPECT_TRUE(cap3.read(frame3));

    TEST_CYCLE() {
        int COUNTER = 0, NUM_THREAD = 3;
        vector<int> nums(VCM.size(), -1);
        vector<std::thread> threads(NUM_THREAD);
        vector<int> threadState(NUM_THREAD, 0);

        auto func = [&](int _st, int th_s)
        {
            for(int i = 0; i < 50; ++i)//50-load
            {
                cv::Canny(forMAt[_st], forProc[_st], 400, 1000, 5);
            }

            threadState[th_s] = 2;
            ++COUNTER;
        };
        VideoCapture::waitAny(VCM, state, TIMEOUT);

        do
        {
            EXPECT_EQ(VCM.size(), state.size());
            for(unsigned int i = 0; i < VCM.size(); ++i)
            {
               nums[i] = -1;
               if(state[i] == CAP_CAM_READY)
               {
                   EXPECT_TRUE(VCM[i].retrieve(forMAt[i]));
                   nums[i] = i;
               }
            }

            for(unsigned int nc = 0; nc < VCM.size(); ++nc)
            {
                 if(nums[nc] != -1)
                 {
                     for(int nt = 0; nt < NUM_THREAD; ++nt)
                     {
                         if(!threadState[nt])
                         {
                             threads[nt] = std::thread(func, nums[nc], nt);
                             printf("th %d  ncam %d\n", nt+1, nc+1);
                             threadState[nt] = 1;
                             break;
                         }
                     }
                 }
            }
            VideoCapture::waitAny(VCM, state, TIMEOUT);

            for(int nt = 0; nt < NUM_THREAD; ++nt)
            {
               if(threadState[nt] == 2)
               {
                    threads[nt].join();
                    threadState[nt] = 0;
               }
            }
            if(COUNTER >= FRAME_COUNT)
            {
                for(int nt = 0; nt < NUM_THREAD; ++nt)
                {
                    if(threadState[nt])
                    {
                        threads[nt].join();
                    }
                }
            }
        }
        while(COUNTER <= FRAME_COUNT);
    };
    SANITY_CHECK_NOTHING();
}


typedef tuple<int, int> Threads_Number;
typedef perf::TestBaseWithParam<Threads_Number> MultiThreadFrame;

PERF_TEST_P(MultiThreadFrame, GetWaitAnyMultiThreadFrame, testing::Combine(
                            testing::Values(2, 4, 6, 8, 10), testing::Values(1)))
{
    int NUM_THREAD = get<0>(GetParam()),
            TIMEOUT = -1,
            FRAME_COUNT = 50; //number of expected frames from all cameras

#ifndef WINRT
    char* datapath_dir = getenv("OPENCV_TEST_CAMERA_LIST");
#else
    char* datapath_dir = OPENCV_TEST_CAMERA_LIST;
#endif

    std::vector<VideoCapture> VCM;
    int step = 0; string path;
    while(true)
    {
        if(datapath_dir[step] == ':' || datapath_dir[step] == '\0')
        {
            VCM.push_back(VideoCapture(path, CAP_V4L));
            path.clear();
            if(datapath_dir[step] != '\0')
                ++step;
        }
        if(datapath_dir[step] == '\0')
            break;
        path += datapath_dir[step];
        ++step;
    }
    std::vector<int> state(VCM.size());

    Mat frame1, frame2, frame3, frame4, frame5, frame6, frame7, frame8, frame9, frame10,
        processed1, processed2, processed3, processed4, processed5, processed6, processed7, processed8, processed9, processed10;

    std::vector<Mat> forMAt = {frame1, frame2, frame3, frame4, frame5,
                                               frame6, frame7, frame8, frame9, frame10};

    std::vector<Mat> forProc = {processed1, processed2, processed3, processed4, processed5,
                                            processed6, processed7, processed8, processed9, processed10};

    for(size_t vc = 0; vc < VCM.size(); ++vc)
    {
        ASSERT_TRUE(VCM[vc].isOpened());
        //false start
        EXPECT_TRUE(VCM[vc].read(forMAt[vc]));
    }

    EXPECT_TRUE(VCM[0].set(CAP_PROP_FPS, 30));
    EXPECT_TRUE(VCM[1].set(CAP_PROP_FPS, 30));
    //EXPECT_TRUE(cap3.set(CAP_PROP_FPS, 30));

    TEST_CYCLE() {
       int COUNTER = 0;
       vector<int> nums(VCM.size(), -1);
       vector<std::thread> threads(NUM_THREAD);
       vector<int> threadState(NUM_THREAD, 0);

       auto func = [&](int _st, int th_s)
       {
           for(int i = 0; i < 50; ++i)//50-load
           {
               cv::Canny(forMAt[_st], forProc[_st], 400, 1000, 5);
           }
           threadState[th_s] = 2;
           ++COUNTER;
       };
       VideoCapture::waitAny(VCM, state, TIMEOUT);
       do
       {

           for(unsigned int i = 0; i < VCM.size(); ++i)
           {
              if(state[i] == CAP_CAM_READY)
              {
                  for(int nt = 0; nt < NUM_THREAD; ++nt)
                  {
                      if(!threadState[nt])
                      {
                           EXPECT_TRUE(VCM[i].retrieve(forMAt[nt]));
                           threads[nt] = std::thread(func, nt, nt);
                           threadState[nt] = 1;

                      }
                  }
              }
           }

           VideoCapture::waitAny(VCM, state, TIMEOUT);

           for(int nt = 0; nt < NUM_THREAD; ++nt)
           {
              if(threadState[nt] == 2)
              {
                   threads[nt].join();
                   threadState[nt] = 0;
              }
           }
           if(COUNTER >= FRAME_COUNT)
           {
               for(int nt = 0; nt < NUM_THREAD; ++nt)
               {
                   if(threadState[nt])
                   {
                       threads[nt].join();
                   }
               }
           }
       }
       while(COUNTER <= FRAME_COUNT);
    };
    SANITY_CHECK_NOTHING();
}

} // namespace
