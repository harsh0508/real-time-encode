#pragma once

#include <opencv2/opencv.hpp>
#include <atomic>
#include <cstdint>
#include <string>

struct SharedFrameHeader {
    std::atomic<uint64_t> seq;
    std::atomic<uint64_t> timestampNs;
    int32_t width;
    int32_t height;
    int32_t channels;
    int32_t stride;
    int32_t frameBytes;
};

class SharedFrameWriter {
public:
    SharedFrameWriter(
        const std::string& path,
        int width,
        int height,
        int channels = 4
    );

    ~SharedFrameWriter();

    void writeBGRA(const cv::Mat& bgraFrame);

private:
    int fd = -1;
    void* memory = nullptr;
    size_t totalSize = 0;

    SharedFrameHeader* header = nullptr;
    uint8_t* pixels = nullptr;
};