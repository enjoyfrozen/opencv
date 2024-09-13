// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html
#include "test_precomp.hpp"

namespace opencv_test { namespace {

#ifdef HAVE_WEBP

TEST(Imgcodecs_WebP, encode_decode_lossless_webp)
{
    const string root = cvtest::TS::ptr()->get_data_path();
    string filename = root + "../cv/shared/lena.png";
    cv::Mat img = cv::imread(filename);
    ASSERT_FALSE(img.empty());

    string output = cv::tempfile(".webp");
    EXPECT_NO_THROW(cv::imwrite(output, img)); // lossless

    cv::Mat img_webp = cv::imread(output);

    std::vector<unsigned char> buf;

    FILE * wfile = NULL;

    wfile = fopen(output.c_str(), "rb");
    if (wfile != NULL)
    {
        fseek(wfile, 0, SEEK_END);
        size_t wfile_size = ftell(wfile);
        fseek(wfile, 0, SEEK_SET);

        buf.resize(wfile_size);

        size_t data_size = fread(&buf[0], 1, wfile_size, wfile);

        if(wfile)
        {
            fclose(wfile);
        }

        if (data_size != wfile_size)
        {
            EXPECT_TRUE(false);
        }
    }

    EXPECT_EQ(0, remove(output.c_str()));

    cv::Mat decode = cv::imdecode(buf, IMREAD_COLOR);
    ASSERT_FALSE(decode.empty());
    EXPECT_TRUE(cvtest::norm(decode, img_webp, NORM_INF) == 0);

    cv::Mat decode_rgb = cv::imdecode(buf, IMREAD_COLOR_RGB);
    ASSERT_FALSE(decode_rgb.empty());

    cvtColor(decode_rgb, decode_rgb, COLOR_RGB2BGR);
    EXPECT_TRUE(cvtest::norm(decode_rgb, img_webp, NORM_INF) == 0);

    ASSERT_FALSE(img_webp.empty());

    EXPECT_TRUE(cvtest::norm(img, img_webp, NORM_INF) == 0);
}

TEST(Imgcodecs_WebP, encode_decode_lossy_webp)
{
    const string root = cvtest::TS::ptr()->get_data_path();
    std::string input = root + "../cv/shared/lena.png";
    cv::Mat img = cv::imread(input);
    ASSERT_FALSE(img.empty());

    for(int q = 100; q>=0; q-=20)
    {
        std::vector<int> params;
        params.push_back(IMWRITE_WEBP_QUALITY);
        params.push_back(q);
        string output = cv::tempfile(".webp");

        EXPECT_NO_THROW(cv::imwrite(output, img, params));
        cv::Mat img_webp = cv::imread(output);
        EXPECT_EQ(0, remove(output.c_str()));
        EXPECT_FALSE(img_webp.empty());
        EXPECT_EQ(3,   img_webp.channels());
        EXPECT_EQ(512, img_webp.cols);
        EXPECT_EQ(512, img_webp.rows);
    }
}

TEST(Imgcodecs_WebP, encode_decode_with_alpha_webp)
{
    const string root = cvtest::TS::ptr()->get_data_path();
    std::string input = root + "../cv/shared/lena.png";
    cv::Mat img = cv::imread(input);
    ASSERT_FALSE(img.empty());

    std::vector<cv::Mat> imgs;
    cv::split(img, imgs);
    imgs.push_back(cv::Mat(imgs[0]));
    imgs[imgs.size() - 1] = cv::Scalar::all(128);
    cv::merge(imgs, img);

    string output = cv::tempfile(".webp");

    EXPECT_NO_THROW(cv::imwrite(output, img));
    cv::Mat img_webp = cv::imread(output, IMREAD_UNCHANGED);
    cv::Mat img_webp_bgr = cv::imread(output); // IMREAD_COLOR by default
    EXPECT_EQ(0, remove(output.c_str()));
    EXPECT_FALSE(img_webp.empty());
    EXPECT_EQ(4,   img_webp.channels());
    EXPECT_EQ(512, img_webp.cols);
    EXPECT_EQ(512, img_webp.rows);
    EXPECT_FALSE(img_webp_bgr.empty());
    EXPECT_EQ(3,   img_webp_bgr.channels());
    EXPECT_EQ(512, img_webp_bgr.cols);
    EXPECT_EQ(512, img_webp_bgr.rows);
}

TEST(Imgcodecs_WebP, load_save_animation)
{
    const string root = cvtest::TS::ptr()->get_data_path();
    const string filename = root + "readwrite/OpenCV_logo_white.png";
    Animation l_animation, s_animation;

    Mat image = imread(filename, IMREAD_UNCHANGED);
    s_animation.frames.push_back(image.clone());
    Mat roi = image(Rect(0, 170, 164, 47));
    int timestamp = 100;
    s_animation.timestamps.push_back(timestamp);
    s_animation.bgcolor = 0xffffffff;
    s_animation.loop_count = 0xffff; // MAX_LOOP_COUNT

    for (int i = 0; i < 15; i++)
    {
        roi = roi - Scalar(0, 0, 0, 20);
        s_animation.frames.push_back(image.clone());
        s_animation.timestamps.push_back(timestamp);
    }

    string output = cv::tempfile(".webp");

    EXPECT_EQ(true, imwriteanimation(output, s_animation));
    EXPECT_EQ(true, imreadanimation(output, l_animation));

    // Since the last three images are identical, only one image was inserted as the last frame,
    // and its duration was calculated by libwebp.
    size_t expected_frame_count = s_animation.frames.size() - 2;
    EXPECT_EQ(imcount(output), expected_frame_count);
    EXPECT_EQ(l_animation.frames.size(), expected_frame_count);
    EXPECT_EQ(l_animation.bgcolor, s_animation.bgcolor);
    EXPECT_EQ(l_animation.loop_count, s_animation.loop_count);

    for (size_t i = 1; i < l_animation.frames.size()-1; i++)
        EXPECT_EQ(s_animation.timestamps[i], l_animation.timestamps[i]);

    EXPECT_EQ(true, imwrite(output, s_animation.frames));
    vector<Mat> webp_frames;
    EXPECT_EQ(true, imreadmulti(output, webp_frames));
    expected_frame_count = 1;
    EXPECT_EQ(webp_frames.size(), expected_frame_count);

    std::vector<uchar> buf;
    EXPECT_EQ(true, imencode(".webp", webp_frames, buf));
    EXPECT_EQ(true, imdecodemulti(buf, IMREAD_COLOR_RGB, webp_frames));
    EXPECT_EQ(0, remove(output.c_str()));
}

#endif // HAVE_WEBP

}} // namespace
