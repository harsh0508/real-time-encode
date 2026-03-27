#include "encoder.h"

#include <stdexcept>

std::string VideoEncoder::ffmpegError(int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(err, buf, sizeof(buf));
    return std::string(buf);
}

void VideoEncoder::check(int ret, const std::string& msg) {
    if (ret < 0) {
        throw std::runtime_error(msg + ": " + ffmpegError(ret));
    }
}

VideoEncoder::VideoEncoder(const std::string& outputFile,
                           int width,
                           int height,
                           int fps,
                           int bitrate)
    : width_(width),
      height_(height),
      fps_(fps),
      bitrate_(bitrate),
      pts_(0),
      codec_(nullptr),
      codecCtx_(nullptr),
      frame_(nullptr),
      packet_(nullptr),
      swsCtx_(nullptr),
      out_(outputFile, std::ios::binary) {
    if (!out_) {
        throw std::runtime_error("Failed to open output file");
    }

    initCodec();
    initFrame();
    initScaler();
}

VideoEncoder::~VideoEncoder() {
    try {
        flush();
    } catch (...) {
    }

    if (swsCtx_) {
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
    }

    if (packet_) {
        av_packet_free(&packet_);
    }

    if (frame_) {
        av_frame_free(&frame_);
    }

    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
    }

    if (out_.is_open()) {
        out_.close();
    }
}

void VideoEncoder::initCodec() {
    codec_ = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec_) {
        throw std::runtime_error("H.264 encoder not found");
    }

    codecCtx_ = avcodec_alloc_context3(codec_);
    if (!codecCtx_) {
        throw std::runtime_error("Failed to allocate AVCodecContext");
    }

    codecCtx_->bit_rate = bitrate_;
    codecCtx_->width = width_;
    codecCtx_->height = height_;
    codecCtx_->time_base = AVRational{1, fps_};
    codecCtx_->framerate = AVRational{fps_, 1};
    codecCtx_->gop_size = 12;
    codecCtx_->max_b_frames = 1;
    codecCtx_->pix_fmt = AV_PIX_FMT_YUV420P;

    av_opt_set(codecCtx_->priv_data, "preset", "veryfast", 0);

    int ret = avcodec_open2(codecCtx_, codec_, nullptr);
    check(ret, "avcodec_open2 failed");

    packet_ = av_packet_alloc();
    if (!packet_) {
        throw std::runtime_error("Failed to allocate AVPacket");
    }
}

void VideoEncoder::initFrame() {
    frame_ = av_frame_alloc();
    if (!frame_) {
        throw std::runtime_error("Failed to allocate AVFrame");
    }

    frame_->format = codecCtx_->pix_fmt;
    frame_->width = codecCtx_->width;
    frame_->height = codecCtx_->height;

    int ret = av_frame_get_buffer(frame_, 32);
    check(ret, "av_frame_get_buffer failed");
}

void VideoEncoder::initScaler() {
    swsCtx_ = sws_getContext(
        width_,
        height_,
        AV_PIX_FMT_BGR24,
        width_,
        height_,
        AV_PIX_FMT_YUV420P,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr
    );

    if (!swsCtx_) {
        throw std::runtime_error("Failed to create SwsContext");
    }
}

void VideoEncoder::sendFrame(AVFrame* frame) {
    if (!codecCtx_) {
        throw std::runtime_error("Encoder context is null");
    }

    int ret = avcodec_send_frame(codecCtx_, frame);
    check(ret, "avcodec_send_frame failed");
}

void VideoEncoder::receivePackets() {
    if (!codecCtx_ || !packet_) {
        throw std::runtime_error("Encoder not initialized");
    }

    while (true) {
        int ret = avcodec_receive_packet(codecCtx_, packet_);

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }

        check(ret, "avcodec_receive_packet failed");

        out_.write(reinterpret_cast<const char*>(packet_->data), packet_->size);
        av_packet_unref(packet_);
    }
}

void VideoEncoder::encodeFrame(const cv::Mat& bgrFrame) {
    if (!codecCtx_ || !frame_ || !swsCtx_) {
        throw std::runtime_error("Encoder not initialized");
    }

    if (bgrFrame.empty()) {
        throw std::runtime_error("Input frame is empty");
    }

    if (bgrFrame.cols != width_ || bgrFrame.rows != height_) {
        throw std::runtime_error("Input frame size does not match encoder size");
    }

    if (bgrFrame.type() != CV_8UC3) {
        throw std::runtime_error("Expected CV_8UC3 BGR frame");
    }

    int ret = av_frame_make_writable(frame_);
    check(ret, "av_frame_make_writable failed");

    const uint8_t* srcSlice[1] = { bgrFrame.data };
    int srcStride[1] = { static_cast<int>(bgrFrame.step) };

    sws_scale(
        swsCtx_,
        srcSlice,
        srcStride,
        0,
        height_,
        frame_->data,
        frame_->linesize
    );

    frame_->pts = pts_++;

    sendFrame(frame_);
    receivePackets();
}

void VideoEncoder::flush() {
    if (!codecCtx_) {
        return;
    }

    sendFrame(nullptr);
    receivePackets();
}