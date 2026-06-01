#include "shm_frame.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>

SharedFrameWriter::SharedFrameWriter(
    const std::string& path,
    int width,
    int height,
    int channels
) {
    const int stride = width * channels;
    const int frameBytes = stride * height;

    totalSize = sizeof(SharedFrameHeader) + frameBytes;

    fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        throw std::runtime_error("Failed to open shared memory file");
    }

    if (ftruncate(fd, totalSize) != 0) {
        throw std::runtime_error("Failed to resize shared memory file");
    }

    memory = mmap(
        nullptr,
        totalSize,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        0
    );

    if (memory == MAP_FAILED) {
        memory = nullptr;
        throw std::runtime_error("mmap failed");
    }

    header = reinterpret_cast<SharedFrameHeader*>(memory);
    pixels = reinterpret_cast<uint8_t*>(memory) + sizeof(SharedFrameHeader);

    header->seq.store(0, std::memory_order_release);
    header->timestampNs.store(0, std::memory_order_release);
    header->width = width;
    header->height = height;
    header->channels = channels;
    header->stride = stride;
    header->frameBytes = frameBytes;
}

SharedFrameWriter::~SharedFrameWriter() {
    if (memory) {
        munmap(memory, totalSize);
    }

    if (fd >= 0) {
        close(fd);
    }
}

void SharedFrameWriter::writeBGRA(const cv::Mat& bgraFrame) {
    if (!memory || bgraFrame.empty()) return;

    const uint64_t currentSeq = header->seq.load(std::memory_order_acquire);

    // Odd seq = writer is writing.
    header->seq.store(currentSeq + 1, std::memory_order_release);

    if (bgraFrame.isContinuous()) {
        std::memcpy(pixels, bgraFrame.data, header->frameBytes);
    } else {
        for (int y = 0; y < header->height; y++) {
            std::memcpy(
                pixels + y * header->stride,
                bgraFrame.ptr(y),
                header->stride
            );
        }
    }

    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const uint64_t ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();

    header->timestampNs.store(ns, std::memory_order_release);

    // Even seq = frame is stable.
    header->seq.store(currentSeq + 2, std::memory_order_release);
}