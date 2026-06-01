cmake_minimum_required(VERSION 3.16)
project(cpp_engine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)

set(VCPKG_ROOT "$ENV{HOME}/vcpkg")
set(VCPKG_TRIPLET "arm64-osx")
set(VCPKG_INSTALLED "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET}")

add_executable(cpp_engine
    main.cpp
    encoder/encoder.cpp
    SM/shm_frame.cpp
)

target_include_directories(cpp_engine PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${VCPKG_INSTALLED}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/encoder
    ${CMAKE_CURRENT_SOURCE_DIR}/SM
    ${CMAKE_CURRENT_SOURCE_DIR}/http
)

target_link_directories(cpp_engine PRIVATE
    ${VCPKG_INSTALLED}/lib
    ${VCPKG_INSTALLED}/debug/lib
)

target_link_libraries(cpp_engine PRIVATE
    ${OpenCV_LIBS}
    onnxruntime_session
    onnxruntime_framework
    onnxruntime_graph
    onnxruntime_common
    onnxruntime_util
    onnxruntime_mlas
    onnxruntime_providers
    onnxruntime_flatbuffers
    onnxruntime_optimizer
    onnxruntime_lora
    onnx
    onnx_proto
    re2
    cpuinfo
    avcodec
    avformat
    avutil
    swscale
    swresample

    "-framework VideoToolbox"
    "-framework AudioToolbox"
    "-framework CoreMedia"
    "-framework CoreVideo"
    "-framework CoreFoundation"
)