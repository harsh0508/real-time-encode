#ifndef VIDEO_ENCODER_H
#define VIDEO_ENCODER_H

// this is guard and prevent file from duplication --> "only inclue the header once "

#include <fstream> //used for file writing std::ofstream out_;
#include <string> // used for std::string
#include <cstdint> // used for fixed width int type int64_t pts_;

#include <opencv2/opencv.hpp> // We use this because frames are coming from OpenCV 

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
}
// These functions are C-style, do not mangle their names.

class VideoEncoder {
public:
    // this is constructor
    // it needs :
    // the external parameter that we need to run everything in class
    VideoEncoder(const std::string& outputFile,
                 int width,
                 int height,
                 int fps,
                 int bitrate = 400000);

    // The destructor is down below 
    ~VideoEncoder();

    void encodeFrame(const cv::Mat& bgrFrame);
    void flush();
    // Need to understand this is detail I don't get it **
    // Encoders buffer frames internally.
    // Especially H.264.
    // So at the end, you must flush delayed packets out.
    // Without flushing, the last few frames may never be written.

private:
    int width_;
    int height_;
    int fps_;
    int bitrate_;
    int64_t pts_;

    const AVCodec* codec_;
    AVCodecContext* codecCtx_;
    AVFrame* frame_;
    AVPacket* packet_;
    SwsContext* swsCtx_;
    std::ofstream out_;

    static std::string ffmpegError(int err);
    static void check(int ret, const std::string& msg);

    void initCodec();
    void initFrame();
    void initScaler();
    void sendFrame(AVFrame* frame);
    void receivePackets();
};

#endif