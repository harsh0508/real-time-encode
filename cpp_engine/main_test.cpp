#include <opencv2/opencv.hpp>
#include <onnxruntime/core/session/onnxruntime_cxx_api.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <condition_variable>
#include "encoder.h"

constexpr float THRESHOLD = 0.9f;
constexpr int BLACKOUT_FRAMES = 12;

struct FramePacket {
    cv::Mat frame;
    uint64_t frameId;
};

template<typename T>
class SafeQueue {
private:
    std::queue<T> q;
    std::mutex m;
    std::condition_variable cv;

public:
    void push(const T& item) {
        {
            std::lock_guard<std::mutex> lock(m);
            q.push(item);
        }
        cv.notify_one();
    }

    bool pop(T& item, std::atomic<bool>& running) {
        std::unique_lock<std::mutex> lock(m);
        cv.wait(lock, [&]() { return !q.empty() || !running.load(); });

        if (q.empty()) return false;

        item = std::move(q.front());
        q.pop();
        return true;
    }

    void notifyAll() {
        cv.notify_all();
    }
};

float runCNN(cv::Mat& img,
             Ort::Session& session,
             const char** inputNames,
             const char** outputNames,
             Ort::MemoryInfo& mem)
{
    constexpr int cnnHeight = 240;
    constexpr int cnnWidth = 320;
    constexpr int channels = 3;
    constexpr int batch = 1;
    constexpr int planeSize = cnnHeight * cnnWidth;
    constexpr int tensorSize = batch * channels * planeSize;
    constexpr std::array<int64_t, 4> shape = {batch, channels, cnnHeight, cnnWidth};

    cv::Mat input;
    cv::resize(img, input, cv::Size(cnnWidth, cnnHeight));
    input.convertTo(input, CV_32F, 1.0 / 255.0);

    std::array<float, tensorSize> tensor{};

    for (int y = 0; y < cnnHeight; y++) {
        const cv::Vec3f* row = input.ptr<cv::Vec3f>(y);
        for (int x = 0; x < cnnWidth; x++) {
            tensor[0 * planeSize + y * cnnWidth + x] = row[x][0];
            tensor[1 * planeSize + y * cnnWidth + x] = row[x][1];
            tensor[2 * planeSize + y * cnnWidth + x] = row[x][2];
        }
    }

    Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
        mem, tensor.data(), tensor.size(), shape.data(), shape.size());

    auto output = session.Run(
        Ort::RunOptions{nullptr},
        inputNames,
        &inputTensor,
        1,
        outputNames,
        1);

    const float* prob = output.front().GetTensorData<float>();

    constexpr int numAnchors = 4420;
    float maxFaceProb = 0.0f;

    for (int i = 0; i < numAnchors; i++) {
        float faceProb = prob[i * 2 + 1];
        if (faceProb > maxFaceProb)
            maxFaceProb = faceProb;
    }

    return maxFaceProb;
}

int main()
{
    std::atomic<bool> running = true;
    std::atomic<int> blackoutCountdown = 0;

    SafeQueue<FramePacket> inferQueue;
    SafeQueue<FramePacket> encodeQueue;

    cv::VideoCapture cam(0);
    if (!cam.isOpened()) return -1;

    const int width = static_cast<int>(cam.get(cv::CAP_PROP_FRAME_WIDTH));
    const int height = static_cast<int>(cam.get(cv::CAP_PROP_FRAME_HEIGHT));
    int fps = static_cast<int>(cam.get(cv::CAP_PROP_FPS));
    fps = (fps <= 0) ? 25 : fps;

    VideoEncoder myEncoder("output.h264", width, height, fps);

    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "cnn");
    Ort::SessionOptions opts;
    opts.SetIntraOpNumThreads(4);
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    Ort::Session session(env, "./onnx_model/face-det.onnx", opts);

    Ort::AllocatorWithDefaultOptions allocator;
    auto inputNameAllocated = session.GetInputNameAllocated(0, allocator);
    auto outputNameAllocated = session.GetOutputNameAllocated(0, allocator);

    const char* inputNames[] = { inputNameAllocated.get() };
    const char* outputNames[] = { outputNameAllocated.get() };

    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(
        OrtArenaAllocator, OrtMemTypeDefault);

    std::thread captureThread([&]() {
        uint64_t frameId = 0;
        int cnnFlag = 0;

        while (running.load()) {
            cv::Mat frame;
            cam >> frame;

            if (frame.empty()) {
                running.store(false);
                inferQueue.notifyAll();
                encodeQueue.notifyAll();
                break;
            }

            FramePacket pkt;
            pkt.frame = frame.clone();
            pkt.frameId = frameId;

            encodeQueue.push(pkt);

            if (cnnFlag >= 12) {
                FramePacket inferPkt;
                inferPkt.frame = frame.clone();
                inferPkt.frameId = frameId;
                inferQueue.push(inferPkt);
                cnnFlag = 0;
            } else {
                cnnFlag++;
            }

            frameId++;
        }
    });

    std::thread inferThread([&]() {
        while (running.load()) {
            FramePacket pkt;
            if (!inferQueue.pop(pkt, running)) break;

            float prob = runCNN(pkt.frame, session, inputNames, outputNames, mem);

            if (prob > THRESHOLD) {
                blackoutCountdown.store(BLACKOUT_FRAMES);
            }
        }
    });

    std::thread encodeThread([&]() {
        while (running.load()) {
            FramePacket pkt;
            if (!encodeQueue.pop(pkt, running)) break;

            int count = blackoutCountdown.load();
            if (count > 0) {
                pkt.frame.setTo(cv::Scalar(0, 0, 0));
                blackoutCountdown.fetch_sub(1);
            }

            myEncoder.encodeFrame(pkt.frame);

            if (cv::waitKey(1) == 27) {
                running.store(false);
                inferQueue.notifyAll();
                encodeQueue.notifyAll();
                break;
            }
        }
    });

    captureThread.join();
    inferThread.join();
    encodeThread.join();

    myEncoder.flush();
    cam.release();
    cv::destroyAllWindows();

    return 0;
}