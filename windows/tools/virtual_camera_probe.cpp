#include <Windows.h>
#include <mfapi.h>
#include <cstdio>
#include <chrono>
#include <string>
#include <thread>
#include <atomic>

#include "virtual_camera/frame_bridge.h"
#include "virtual_camera/frame_contract.h"
#include "virtual_camera/registration.h"

namespace {

std::atomic<bool> g_running{true};

BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT) {
        g_running = false;
        return TRUE;
    }
    return FALSE;
}

void PrintUsage() {
    std::puts("Usage: virtual_camera_probe.exe <mode> [options]");
    std::puts("Modes:");
    std::puts("  --register              Register the virtual camera for current user");
    std::puts("  --remove                Remove the virtual camera registration");
    std::puts("  --stop                  Stop the virtual camera");
    std::puts("  --synthetic [options]   Produce synthetic test frames at 30 fps");
    std::puts("Options for --synthetic:");
    std::puts("  --duration <seconds>    Run for N seconds (default: infinite until Ctrl+C)");
}

int RunSynthetic(int durationSeconds) {
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    std::wstring installId = clone_vcam::VirtualCameraRegistration::GetInstallationId();
    if (installId.empty()) {
        std::fprintf(stderr, "Error: Virtual camera is not registered (missing installation ID). Run --register first.\n");
        return 1;
    }

    std::wprintf(L"Starting synthetic producer for installation ID: %s\n", installId.c_str());

    clone_vcam::FramePublisher publisher;
    HRESULT hr = publisher.Open(installId);
    if (FAILED(hr)) {
        std::fprintf(stderr, "Failed to open frame publisher: 0x%08lX\n", hr);
        return 2;
    }

    const auto startTime = std::chrono::steady_clock::now();
    uint64_t sequence = 0;
    const uint64_t generation = 1;

    std::vector<uint8_t> nv12(clone_vcam::kOutputFrameBytes, 128);
    constexpr std::size_t yBytes = static_cast<std::size_t>(clone_vcam::kOutputWidth) * clone_vcam::kOutputHeight;

    int boxX = 100;
    int boxY = 100;
    int dirX = 10;
    int dirY = 8;
    constexpr int boxW = 160;
    constexpr int boxH = 120;

    std::puts("Publishing synthetic 1080p30 NV12 frames. Press Ctrl+C to stop...");

    while (g_running) {
        if (durationSeconds > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - startTime).count();
            if (elapsed >= durationSeconds) {
                break;
            }
        }

        // Draw background luma
        std::fill_n(nv12.begin(), yBytes, static_cast<uint8_t>(32));
        // Neutral chroma (U=128, V=128)
        std::fill(nv12.begin() + yBytes, nv12.end(), static_cast<uint8_t>(128));

        // Animate moving box
        boxX += dirX;
        boxY += dirY;
        if (boxX <= 0 || boxX + boxW >= static_cast<int>(clone_vcam::kOutputWidth)) dirX = -dirX;
        if (boxY <= 0 || boxY + boxH >= static_cast<int>(clone_vcam::kOutputHeight)) dirY = -dirY;

        for (int y = boxY; y < boxY + boxH; ++y) {
            for (int x = boxX; x < boxX + boxW; ++x) {
                nv12[static_cast<std::size_t>(y) * clone_vcam::kOutputWidth + x] = 235; // Bright white box
            }
        }

        LARGE_INTEGER qpc;
        QueryPerformanceCounter(&qpc);

        clone_vcam::OutputFrame frame;
        frame.metadata.generation = generation;
        frame.metadata.sequence = sequence++;
        frame.metadata.capturedQpc = static_cast<uint64_t>(qpc.QuadPart);
        frame.metadata.state = clone_vcam::FrameState::Live;
        frame.nv12 = nv12;

        if (!publisher.Publish(frame)) {
            std::fprintf(stderr, "Warning: Failed to publish frame %llu\n", sequence);
        }

        if (sequence % 30 == 0) {
            std::printf("Published frame %llu\n", sequence);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    publisher.Close();
    std::puts("Synthetic producer stopped.");
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    std::wstring mode = argv[1];

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);


    int exitCode = 0;

    if (mode == L"--register") {
        HRESULT hr = clone_vcam::VirtualCameraRegistration::EnsureInstalled();
        if (SUCCEEDED(hr)) {
            std::wprintf(L"Successfully registered virtual camera. Installation ID: %s\n",
                         clone_vcam::VirtualCameraRegistration::GetInstallationId().c_str());
        } else {
            std::fprintf(stderr, "Failed to register virtual camera: HRESULT 0x%08lX\n", hr);
            exitCode = 2;
        }
    } else if (mode == L"--remove") {
        HRESULT hr = clone_vcam::VirtualCameraRegistration::Remove();
        if (SUCCEEDED(hr)) {
            std::puts("Successfully removed virtual camera.");
        } else {
            std::fprintf(stderr, "Failed to remove virtual camera: HRESULT 0x%08lX\n", hr);
            exitCode = 2;
        }
    } else if (mode == L"--stop") {
        HRESULT hr = clone_vcam::VirtualCameraRegistration::Stop();
        if (SUCCEEDED(hr)) {
            std::puts("Successfully stopped virtual camera.");
        } else {
            std::fprintf(stderr, "Failed to stop virtual camera: HRESULT 0x%08lX\n", hr);
            exitCode = 2;
        }
    } else if (mode == L"--synthetic") {
        int duration = 0;
        for (int i = 2; i < argc; ++i) {
            if (std::wstring(argv[i]) == L"--duration" && i + 1 < argc) {
                duration = _wtoi(argv[++i]);
            }
        }
        exitCode = RunSynthetic(duration);
    } else {
        PrintUsage();
        exitCode = 1;
    }

    MFShutdown();
    CoUninitialize();
    return exitCode;
}
