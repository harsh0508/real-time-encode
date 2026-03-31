#include <opencv2/opencv.hpp>
#include <onnxruntime/core/session/onnxruntime_cxx_api.h>
#include <iostream>
#include "encoder.h"
#include <chrono>

constexpr float THRESHOLD {0.7};

float runCNN(cv::Mat &img, Ort::Session &session , 
    const char** inputNames , const char** outputNames ,
    Ort::MemoryInfo &mem)
{
    constexpr int cnnHeight { 240};
    constexpr int cnnWidth {320};
    constexpr int channels {3};
    constexpr int batch  {1};
    constexpr int planeSize { cnnHeight * cnnWidth};
    constexpr int tensorSize {batch * channels * planeSize};
    constexpr std::array<int64_t, 4> shape {batch, channels, cnnHeight, cnnWidth};

    cv::Mat input;
    cv::resize(img, input, cv::Size(cnnWidth,cnnHeight));
    input.convertTo(input, CV_32F, 1.0/255);

    std::array<float , tensorSize> tensor {};
    // insted of std::vector<float> tensor {tensorSize}; -- uses stack -- cuts 2 ms

    for(int c=0;c<channels;c++)
        for(int y=0;y<cnnHeight;y++)
            for(int x=0;x<cnnWidth;x++)
                tensor[c * planeSize + y * cnnWidth + x] =
                    input.at<cv::Vec3f>(y,x)[c];
                    // check what is at. bound/type handling 
                    // and how to make better loops 
    Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
        mem, tensor.data(), tensor.size(), shape.data(), shape.size());

    auto output = session.Run(
        Ort::RunOptions{nullptr},
        inputNames,
        &inputTensor,
        1,
        outputNames,
        1);

    float* prob = output.front().GetTensorMutableData<float>();
    constexpr int numAnchors = 4420;
    float maxFaceProb = 0.0f;

    for(int i = 0; i < numAnchors; i++) {
        float faceProb = prob[i * 2 + 1];
        if(faceProb > maxFaceProb)
            maxFaceProb = faceProb;
    }
    return maxFaceProb;
}
    // auto start = std::chrono::steady_clock::now();
    // auto end = std::chrono::steady_clock::now();
    // auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    // std::cout << "Time taken: " << duration.count() << " milliseconds" << std::endl;

int main()
{
    short cnnFlag {0};
    cv::VideoCapture cam(0);

    if(!cam.isOpened())
        return -1;

    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "cnn");
    Ort::SessionOptions opts;
    opts.SetIntraOpNumThreads(4);

    Ort::Session session(env, "./onnx_model/face-det.onnx", opts);

    Ort::AllocatorWithDefaultOptions allocator;
    auto inputNameAllocated = session.GetInputNameAllocated(0, allocator);
    auto outputNameAllocated = session.GetOutputNameAllocated(0, allocator);

    const char* inputNames[] = { inputNameAllocated.get() };
    const char* outputNames[] = { outputNameAllocated.get() };
    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(
        OrtArenaAllocator, OrtMemTypeDefault);

    const int width {static_cast<int>(cam.get(cv::CAP_PROP_FRAME_WIDTH))} ;
    const int height {static_cast<int>(cam.get(cv::CAP_PROP_FRAME_HEIGHT))};
    int fps {static_cast<int>(cam.get(cv::CAP_PROP_FPS))};

    fps = (fps <=0) ? 25: fps;
    
    VideoEncoder myEncoder("output.h264" , width , height , fps);

    cv::Mat frame;
    float prob {0.0f};
    cv::Mat infer;
    // need to clear after every loop
    while(true)
    {
        cam >> frame;
        if(frame.empty()) break;

        cv::resize(frame, infer, cv::Size(426,240));
        
        if(cnnFlag >=12){
            prob = runCNN(infer, session , inputNames , outputNames , mem); // 15ms if imshow removed 
            cnnFlag = 0;
        }
        else{
            cnnFlag+=1;
        }
        
        if(prob > 0.9){
            frame.setTo(cv::Scalar(0,0,0));
        }
            
        // ENCODER SHOULD RUN HERE
        myEncoder.encodeFrame(frame); // 3ms
        // cv::imshow("stream", frame);

        if(cv::waitKey(1)==27)
            break;
    }
    
    myEncoder.flush();

    cam.release();
    cv::destroyAllWindows();

    return 0;
}