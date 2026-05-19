#include "encoder.h"

#include <stdexcept>
#include <iostream>

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

VideoEncoder::VideoEncoder(const std::string& outputTarget,
                           int width,
                           int height,
                           int fps,
                           int bitrate,
                           OutputMode mode)
    : width_(width),
      height_(height),
      fps_(fps),
      bitrate_(bitrate),
      mode_(mode),
      pts_(0),
      codec_(nullptr),
      codecCtx_(nullptr),
      frame_(nullptr),
      packet_(nullptr),
      swsCtx_(nullptr),
      fmtCtx_(nullptr),   // <change and reason> RTMP/FLV output context starts empty
      stream_(nullptr),   // <change and reason> stream is created after codec is ready
      flushed_(false) {   // <change and reason> avoid double flush

    // <change and reason>
    // Needed for network protocols such as RTMP.
    // Safe to call even if output is local FLV.
    if (mode_ == OutputMode::RTMPStream) {
        avformat_network_init(); // <change and reason> needed for RTMP network output
    }

    avformat_network_init();

    initCodec();
    if(mode_ == OutputMode::RTMPStream) {
        initOutput(outputTarget);
    } else {
        out_.open(outputTarget, std::ios::binary); // <change and reason> local file output
        if(!out_) {
            throw std::runtime_error("Failed to open output file");
        }
    }
    // initOutput(outputTarget, mode_);
    initFrame();
    initScaler();
}

VideoEncoder::~VideoEncoder() {
    try {
        flush();
        closeOutput();
    } catch (...) {
        // <change and reason>
        // Destructors should not throw.
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

    avformat_network_deinit(); // <change and reason> cleanup FFmpeg network layer
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
    codecCtx_->max_b_frames = 0; // <change and reason> lower latency for live RTMP streaming
    codecCtx_->pix_fmt = AV_PIX_FMT_YUV420P;

    // <change and reason>
    // Important for container-based output such as FLV/RTMP.
    // Raw .h264 writing did not need this, but RTMP muxing benefits from it.
    codecCtx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    // <change and reason>
    // x264 low-latency settings for live streaming.
    av_opt_set(codecCtx_->priv_data, "preset", "veryfast", 0);
    av_opt_set(codecCtx_->priv_data, "tune", "zerolatency", 0);

    int ret = avcodec_open2(codecCtx_, codec_, nullptr);
    check(ret, "avcodec_open2 failed");

    packet_ = av_packet_alloc();
    if (!packet_) {
        throw std::runtime_error("Failed to allocate AVPacket");
    }
}

void VideoEncoder::initOutput(const std::string& outputUrl) {
    // <change and reason>
    // For RTMP, FFmpeg commonly uses FLV container.
    // Example outputUrl:
    // rtmp://localhost/live/stream_720
    int ret = avformat_alloc_output_context2(
        &fmtCtx_,
        nullptr,
        "flv",
        outputUrl.c_str()
    );

    if (ret < 0 || !fmtCtx_) {
        check(ret, "avformat_alloc_output_context2 failed");
    }

    stream_ = avformat_new_stream(fmtCtx_, nullptr);
    if (!stream_) {
        throw std::runtime_error("Failed to create output stream");
    }

    stream_->time_base = codecCtx_->time_base;

    // <change and reason>
    // Copy encoder settings into the output stream.
    ret = avcodec_parameters_from_context(stream_->codecpar, codecCtx_);
    check(ret, "avcodec_parameters_from_context failed");

    stream_->codecpar->codec_tag = 0;

    // <change and reason>
    // Opens RTMP URL or local output file.
    // This replaces std::ofstream out_.
    if (!(fmtCtx_->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&fmtCtx_->pb, outputUrl.c_str(), AVIO_FLAG_WRITE);
        check(ret, "avio_open failed");
    }

    // <change and reason>
    // Writes FLV/RTMP header before packets are sent.
    ret = avformat_write_header(fmtCtx_, nullptr);
    check(ret, "avformat_write_header failed");
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

        if (mode_ == OutputMode::RTMPStream) {
            packet_->stream_index = stream_->index;

            av_packet_rescale_ts(
                packet_,
                codecCtx_->time_base,
                stream_->time_base
            );

            int writeRet = av_interleaved_write_frame(fmtCtx_, packet_);
            check(writeRet, "av_interleaved_write_frame failed");
        } 
        else {
            out_.write(
                reinterpret_cast<const char*>(packet_->data),
                packet_->size
            );
        }
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
    if (!codecCtx_ || flushed_) {
        return;
    }

    flushed_ = true;

    // <change and reason>
    // Sends null frame to drain delayed H.264 packets.
    sendFrame(nullptr);
    receivePackets();
}

void VideoEncoder::closeOutput() {
    if (!fmtCtx_) {
        return;
    }

    // <change and reason>
    // Writes FLV trailer and closes RTMP/file output.
    av_write_trailer(fmtCtx_);

    if (!(fmtCtx_->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&fmtCtx_->pb);
    }

    avformat_free_context(fmtCtx_);
    fmtCtx_ = nullptr;
    stream_ = nullptr;
}