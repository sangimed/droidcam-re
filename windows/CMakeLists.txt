cmake_minimum_required(VERSION 3.24)
project(DroidCamCloneWindows LANGUAGES CXX)

add_executable(droidcam_clone WIN32
    src/main.cpp
    src/net/session_protocol.cpp
    src/net/session_server.cpp
    src/net/rtp_h264_receiver.cpp
    src/video/h264_decoder.cpp
)

target_compile_features(droidcam_clone PRIVATE cxx_std_20)
target_compile_definitions(droidcam_clone PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(droidcam_clone PRIVATE ws2_32 d3d11 dxgi mfplat mfuuid ole32 gdi32)
