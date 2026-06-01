#include <opencv2/opencv.hpp>
#include <onnxruntime/core/session/onnxruntime_cxx_api.h>
#include <iostream>
#include <chrono>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <string_view>

#include "httplib.h"

#include "encoder.h"

#include "shm_frame.h"


constexpr float FIGHT_THRESHOLD {0.4f};


void httpServerThread(){
    constexpr int port {8080};
    httplib::Server server;

    server.Get("/streams", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({
            "240p": "' + RTMP_240_URL + '",
            "480p": "' + RTMP_480_URL + '",
            "720p": "' + RTMP_720_URL + '"
        })", "application/json");
    });

    server.listen("0.0.0.0", port);
}

struct FrameQueue{

    std::queue<cv::Mat> q;
    std::mutex m;
    std::condition_variable cv;
    std::atomic<bool> running{true};
    size_t maxSize = 5;

    void Pushframe(cv::Mat& frame){
        std::lock_guard<std::mutex> lock(m);
        // what does this mean ?

        if(q.size() >= maxSize){
            q.pop();
        }

        q.push(frame.clone());
        cv.notify_one();

    }

    bool popFrame(cv::Mat& frame){
        std::unique_lock<std::mutex> lock(m);

        cv.wait(lock,[&]{
            return !q.empty() || !running;
        });

        if(!running && q.empty()) return false;

        frame = std::move(q.front());
        q.pop();
        return true;
    }

    void stopQueue(){
        running = false;
        cv.notify_all();
    }

};


void EncoderWorker(
    FrameQueue& queue,
    const std::string& outFileName,
    int outW,
    int outH,
    int fps,
    int bitrate,
    OutputMode mode = OutputMode::LocalFile
){
    VideoEncoder encoder(outFileName, outW , outH , fps ,bitrate, mode);
    encoder.onPacket = [&](uint8_t* data, int size) {
            // send via websocket / rtmp
            // sendToRTMP(data, size);
    };
    cv::Mat frame;
    cv::Mat resize;

    while(queue.popFrame(frame)){
        cv::resize(frame, resize, cv::Size(outW, outH));
        encoder.encodeFrame(resize);
        

    }
    encoder.flush();
}


float runCNN(
    cv::Mat& img,
    Ort::Session& session,
    const char** inputNames,
    const char** outputNames,
    Ort::MemoryInfo& mem
)
{
    constexpr int cnnHeight {224};
    constexpr int cnnWidth  {224};
    constexpr int channels  {3};
    constexpr int batch     {1};

    constexpr int tensorSize = batch * cnnHeight * cnnWidth * channels;

    // Your ONNX model from TensorFlow/MobileNetV2 expects NHWC:
    // [1, 224, 224, 3]
    constexpr std::array<int64_t, 4> shape {
        batch,
        cnnHeight,
        cnnWidth,
        channels
    };

    cv::Mat input;

    // OpenCV camera frame is BGR, but your Python notebook converts BGR -> RGB.
    cv::cvtColor(img, input, cv::COLOR_BGR2RGB);

    // MobileNetV2 training size
    cv::resize(input, input, cv::Size(cnnWidth, cnnHeight));

    // Same as Python: img.astype(np.float32) / 255.0
    input.convertTo(input, CV_32F, 1.0 / 255.0);

    std::array<float, tensorSize> tensor {};

    int idx = 0;

    // NHWC layout: height -> width -> channel
    for(int y = 0; y < cnnHeight; y++)
    {
        for(int x = 0; x < cnnWidth; x++)
        {
            cv::Vec3f pixel = input.at<cv::Vec3f>(y, x);

            tensor[idx++] = pixel[0]; // R
            tensor[idx++] = pixel[1]; // G
            tensor[idx++] = pixel[2]; // B
        }
    }

    Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
        mem,
        tensor.data(),
        tensor.size(),
        shape.data(),
        shape.size()
    );

    auto output = session.Run(
        Ort::RunOptions{nullptr},
        inputNames,
        &inputTensor,
        1,
        outputNames,
        1
    );

    float* result = output.front().GetTensorMutableData<float>();

    float fightProb = result[0];

    return fightProb;
}
    // auto start = std::chrono::steady_clock::now();
    // auto end = std::chrono::steady_clock::now();
    // auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    // std::cout << "Time taken: " << duration.count() << " milliseconds" << std::endl;

int main(int argc, char* argv[])
{
    int fightCounter {0};
    bool useRTMP = false;
    std::string modelPath = "../onnx_model/improved_fight_model.onnx";


    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--rtmp") {
            useRTMP = true;
        }
        else if (arg == "--model" && i + 1 < argc) {
            modelPath = argv[++i];
        }
    }
    std::string out240 = useRTMP
        ? "rtmp://localhost/live/stream_240"
        : "output_240p.h264";

    std::string out480 = useRTMP
        ? "rtmp://localhost/live/stream_480"
        : "output_480p.h264";

    std::string out720 = useRTMP
        ? "rtmp://localhost/live/stream_720"
        : "output_720p.h264";

    OutputMode mode = useRTMP
        ? OutputMode::RTMPStream
        : OutputMode::LocalFile;

    short cnnFlag {0};
    cv::VideoCapture cam(0);

    if(!cam.isOpened())
        return -1;

    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "cnn");
    Ort::SessionOptions opts;
    opts.SetIntraOpNumThreads(4); // was 4 before **

    Ort::Session session(env, modelPath.c_str(), opts);

    Ort::AllocatorWithDefaultOptions allocator;
    auto inputNameAllocated = session.GetInputNameAllocated(0, allocator);
    auto outputNameAllocated = session.GetOutputNameAllocated(0, allocator);

    const char* inputNames[] = { inputNameAllocated.get() };
    const char* outputNames[] = { outputNameAllocated.get() };
    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(
        OrtArenaAllocator, OrtMemTypeDefault);

    const int width {static_cast<int>(cam.get(cv::CAP_PROP_FRAME_WIDTH))} ;
    const int height {static_cast<int>(cam.get(cv::CAP_PROP_FRAME_HEIGHT))};

    const std::string shmPath = "/tmp/realtime_encode_camera_frame.shm";

    SharedFrameWriter shmWriter(
        shmPath,
        width,
        height,
        4
    );

    // std::cout << "Shared frame memory ready: " << shmPath << std::endl;
    // std::cout << "Frame size: " << width << "x" << height << std::endl;

    int fps {static_cast<int>(cam.get(cv::CAP_PROP_FPS))};

    fps = (fps <=0) ? 25: fps;
    
    // VideoEncoder myEncoder("output.h264" , width , height , fps);  

    FrameQueue q240;
    FrameQueue q480;
    FrameQueue q720;

    std::thread t240(
        EncoderWorker,
        std::ref(q240),
        // "output_240p.h264",
        out240,
        426,
        240,
        fps,
        300000,
        mode
    );

    std::thread t480(
        EncoderWorker,
        std::ref(q480),
        // "output_480p.h264",
        out480,
        854,
        480,
        fps,
        800000,
        mode
    );

    std::thread t720(
        EncoderWorker,
        std::ref(q720),
        // "output_720p.h264",
        out720,
        1280,
        720,
        fps,
        2500000,
        mode
    );


    cv::Mat frame;
    float prob {0.0f};
    
    std::thread httpThread(httpServerThread);
    httpThread.detach();

    // cv::Mat infer;
    // need to clear after every loop

    // cv::Mat testImg = cv::imread("./i.jpg");

    // if (testImg.empty()) {
    //     std::cerr << "Could not load test image" << std::endl;
    // } else {
    //     float testProb = runCNN(testImg, session, inputNames, outputNames, mem);
    //     std::cout << "TEST IMAGE Fight Probability: " << testProb << std::endl;
    // }
    // return 0;
    while(true)
    {
        cam >> frame;
        if(frame.empty()) break;
        cv::Mat bgraFrame;
        cv::cvtColor(frame, bgraFrame, cv::COLOR_BGR2RGBA);

        shmWriter.writeBGRA(bgraFrame);

        if(cnnFlag >=12){
            prob = runCNN(frame, session , inputNames , outputNames , mem); // 15ms 
            cnnFlag = 0;
        }
        else{
            cnnFlag+=1;
        }

        std::cout << "Fight Probability: " << prob << ", fight counter: " << fightCounter << '\n';
        
        if(prob > FIGHT_THRESHOLD){
            fightCounter++; 
            if(fightCounter >= 5){
                frame.setTo(cv::Scalar(0,0,0)); // 156 microseconds
            }
        }else{
            fightCounter --;
            if(fightCounter < 0) fightCounter = 0;
        }

        q240.Pushframe(frame);
        q480.Pushframe(frame);
        q720.Pushframe(frame);

        if(cv::waitKey(1)==27)
            break;
    }
    
    q240.stopQueue();
    q480.stopQueue();
    q720.stopQueue();

    if(t240.joinable()) t240.join();
    if(t480.joinable()) t480.join();
    if(t720.joinable()) t720.join();


    cam.release();
    cv::destroyAllWindows();

    return 0;
}
