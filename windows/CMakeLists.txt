cmake_minimum_required(VERSION 3.24)
project(DroidCamCloneWindows LANGUAGES CXX)

add_executable(droidcam_clone WIN32
    src/main.cpp
    src/net/session_protocol.cpp
    src/net/session_server.cpp
    src/net/rtp_h264_receiver.cpp
    src/video/h264_decoder.cpp
    src/video/nv12_to_bgra.cpp
)

target_compile_features(droidcam_clone PRIVATE cxx_std_20)
target_compile_definitions(droidcam_clone PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(droidcam_clone PRIVATE ws2_32 d3d11 dxgi mfplat mfuuid ole32 gdi32)

enable_testing()
add_executable(droidcam_clone_video_tests tests/nv12_to_bgra_test.cpp src/video/nv12_to_bgra.cpp)
target_compile_features(droidcam_clone_video_tests PRIVATE cxx_std_20)
target_include_directories(droidcam_clone_video_tests PRIVATE src)
add_test(NAME nv12_to_bgra COMMAND droidcam_clone_video_tests)

add_executable(droidcam_clone_queue_tests tests/freshness_queue_test.cpp)
target_compile_features(droidcam_clone_queue_tests PRIVATE cxx_std_20)
target_compile_definitions(droidcam_clone_queue_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(droidcam_clone_queue_tests PRIVATE src)
add_test(NAME freshness_queue COMMAND droidcam_clone_queue_tests)

add_executable(droidcam_clone_rtp_tests tests/rtp_h264_receiver_test.cpp src/net/rtp_h264_receiver.cpp)
target_compile_features(droidcam_clone_rtp_tests PRIVATE cxx_std_20)
target_compile_definitions(droidcam_clone_rtp_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(droidcam_clone_rtp_tests PRIVATE src)
target_link_libraries(droidcam_clone_rtp_tests PRIVATE ws2_32 ole32)
add_test(NAME rtp_h264_receiver COMMAND droidcam_clone_rtp_tests)
