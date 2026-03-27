#include <opencv2/opencv.hpp>
#include <onnxruntime/core/session/onnxruntime_cxx_api.h>
#include <iostream>
#include "encoder.h"

const float THRESHOLD = 0.7;

float runCNN(cv::Mat &img, Ort::Session &session)
{
    cv::Mat input;
    cv::resize(img, input, cv::Size(224,224));
    input.convertTo(input, CV_32F, 1.0/255);

    std::vector<float> tensor(1*3*224*224);

    for(int c=0;c<3;c++)
        for(int y=0;y<224;y++)
            for(int x=0;x<224;x++)
                tensor[c*224*224 + y*224 + x] =
                    input.at<cv::Vec3f>(y,x)[c];

    std::array<int64_t,4> shape = {1,3,224,224};

    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(
        OrtArenaAllocator, OrtMemTypeDefault);

    Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
        mem, tensor.data(), tensor.size(), shape.data(), 4);

    const char* inputNames[] = {"input"};
    const char* outputNames[] = {"output"};

    auto output = session.Run(
        Ort::RunOptions{nullptr},
        inputNames,
        &inputTensor,
        1,
        outputNames,
        1);

    float* prob = output.front().GetTensorMutableData<float>();

    return prob[0];
}

int main()
{
    std::cout << "started" << std::flush;
    cv::VideoCapture cam(0);

    if(!cam.isOpened())
        return -1;

    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "cnn");
    Ort::SessionOptions opts;
    opts.SetIntraOpNumThreads(4);

    Ort::Session session(env, "mobilenet.onnx", opts);

    cv::Mat frame;

    while(true)
    {
        cam >> frame;
        if(frame.empty()) break;

        // create 240p frame for inference
        cv::Mat infer;
        cv::resize(frame, infer, cv::Size(426,240));

        float prob = runCNN(infer, session);

        if(prob > THRESHOLD)
            frame.setTo(cv::Scalar(0,0,0));

        // ENCODER SHOULD RUN HERE
        // encodeFrame(frame);

        cv::imshow("stream", frame);

        if(cv::waitKey(1)==27)
            break;
    }

    return 0;
}