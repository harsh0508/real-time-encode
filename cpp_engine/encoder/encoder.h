#ifndef VIDEO_ENCODER_H
#define VIDEO_ENCODER_H

#include <string>      // <change and reason> needed for std::string output URL
#include <cstdint>     // <change and reason> needed for int64_t pts_
#include <functional>  // <change and reason> needed because onPacket uses std::function

#include <opencv2/opencv.hpp> // <change and reason> frames are still coming from OpenCV as cv::Mat
#include <fstream>                 // <change and reason> required for std::ofstream out_ in LocalFile mode


extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
#include <libavformat/avformat.h> // <change and reason> required for AVFormatContext, AVStream, RTMP/FLV muxing
#include <libavutil/time.h>       // <change and reason> useful for FFmpeg timing/network streaming

}

// These functions are C-style, so extern "C" prevents C++ name mangling.

enum class OutputMode {
    LocalFile,
    RTMPStream
};

class VideoEncoder {
public:
    // <change and reason>
    // outputUrl can now be:
    // 1. rtmp://localhost/live/stream_720
    // 2. output.flv
    // It is no longer only a raw .h264 file path.
    VideoEncoder(const std::string& outPutTarget,
                 int width,
                 int height,
                 int fps,
                 int bitrate = 400000,
                OutputMode mode = OutputMode::LocalFile);

    ~VideoEncoder();

    void encodeFrame(const cv::Mat& bgrFrame);
    void flush();

    // <change and reason>
    // Optional callback if you still want access to encoded packet bytes.
    // RTMP streaming does not depend on this callback anymore.
    std::function<void(uint8_t*, int)> onPacket;

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

    OutputMode mode_;        // <change and reason> decides local save or RTMP stream
    std::ofstream out_;      // <change and reason> used only in LocalFile mode

    AVFormatContext* fmtCtx_; // <change and reason> replaces std::ofstream; manages RTMP/FLV output
    AVStream* stream_;        // <change and reason> represents the video stream inside the FLV/RTMP container

    bool flushed_;            // <change and reason> prevents flushing H.264 encoder twice

    static std::string ffmpegError(int err);
    static void check(int ret, const std::string& msg);

    void initCodec();
    void initOutput(const std::string& outpuxtUrl); // <change and reason> opens RTMP/FLV output
    void initFrame();
    void initScaler();
    void sendFrame(AVFrame* frame);
    void receivePackets();
    void closeOutput(); // <change and reason> writes trailer and closes RTMP/file output safely
};

#endif