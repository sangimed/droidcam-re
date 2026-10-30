#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cwchar>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>

#include "net/rtp_h264_receiver.h"
#include "net/session_server.h"
#include "video/h264_decoder.h"
#include "video/freshness_queue.h"
#include "virtual_camera/receiver_output.h"
#include "virtual_camera/registration.h"

namespace {
constexpr wchar_t kWindowClass[] = L"DroidCamCloneWindow";
constexpr UINT kFrameMessage = WM_APP + 1;
constexpr UINT kStatusMessage = WM_APP + 2;
constexpr UINT_PTR kStatsTimer = 1;

struct AppState {
    HWND window{};
    clone_net::SessionServer server;
    std::unique_ptr<clone_net::RtpH264Receiver> receiver;
    std::shared_ptr<clone_video::H264Decoder> decoder;
    std::shared_ptr<clone_video::FreshnessQueue> decodeQueue;
    std::unique_ptr<clone_vcam::ReceiverOutput> virtualOutput;
    std::atomic<std::uint64_t> sessionGeneration{0};
    std::atomic_bool virtualOutputReady{false};
    std::thread decodeThread;
    std::mutex frameMutex;
    std::shared_ptr<const clone_video::DecodedFrame> latestFrame;
    std::shared_ptr<const clone_video::DecodedFrame> displayedFrame;
    std::atomic_bool frameMessagePending{false};
    std::atomic<std::uint64_t> receivedPackets{0};
    std::atomic<std::uint64_t> lastPacketAgeMs{0};
    std::atomic<std::uint64_t> lostPackets{0};
    std::atomic<std::uint64_t> reorderedPackets{0};
    std::atomic<std::uint64_t> droppedAccessUnits{0};
    std::atomic<std::uint64_t> decodedFrames{0};
    std::atomic<std::uint64_t> lastAccessUnitSequence{0};
    std::atomic<std::uint64_t> decodeQueueDepth{0};
    std::atomic<std::uint64_t> decodeQueueAgeMs{0};
    std::atomic<std::uint64_t> decodeQueueOldestNs{0};
    std::atomic<std::uint64_t> decodeQueueDrops{0};
    std::atomic_bool waitingForIdr{false};
    std::atomic_bool streamStalled{false};
    std::atomic<std::uint64_t> decodeFpsMilli{0};
    std::atomic<std::uint64_t> decodeAgeMs{0};
    std::atomic<std::uint64_t> presentAgeMs{0};
    std::atomic<std::uint64_t> lastPresentedNs{0};
    std::atomic<std::uint32_t> decodeHresult{0};
    std::atomic<std::uint32_t> transformTimeMs{0};
    std::atomic<std::uint32_t> conversionTimeMs{0};
    std::atomic<std::uint64_t> processCpuMilliPercent{0};
    std::atomic_bool hardwareDecoder{false};
    std::atomic<std::uint64_t> lastFpsDecoded{0};
    std::atomic<int> selectedFps{0};
    std::atomic<int> selectedWidth{0};
    std::atomic<int> selectedHeight{0};
    std::uint64_t previousProcessCpu100ns{};
    std::chrono::steady_clock::time_point previousCpuSample{};
    bool suppressEndStatus{};
};
AppState g_app;

std::uint64_t FileTimeValue(const FILETIME& value) {
    ULARGE_INTEGER combined{};
    combined.LowPart = value.dwLowDateTime;
    combined.HighPart = value.dwHighDateTime;
    return combined.QuadPart;
}

void UpdateProcessCpuUsage() {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) return;

    const auto cpu100ns = FileTimeValue(kernel) + FileTimeValue(user);
    const auto now = std::chrono::steady_clock::now();
    if (g_app.previousProcessCpu100ns != 0) {
        const auto elapsed100ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now - g_app.previousCpuSample).count() / 100;
        if (elapsed100ns > 0 && cpu100ns >= g_app.previousProcessCpu100ns) {
            const double percentOfOneCore = 100.0 * static_cast<double>(cpu100ns - g_app.previousProcessCpu100ns) /
                static_cast<double>(elapsed100ns);
            g_app.processCpuMilliPercent.store(static_cast<std::uint64_t>(percentOfOneCore * 1000.0));
        }
    }
    g_app.previousProcessCpu100ns = cpu100ns;
    g_app.previousCpuSample = now;
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM, LPARAM lParam) {
    switch (message) {
    case kFrameMessage: {
        bool repost = false;
        {
            std::lock_guard lock(g_app.frameMutex);
            if (g_app.latestFrame) g_app.displayedFrame = std::move(g_app.latestFrame);
            g_app.frameMessagePending.store(false);
            if (g_app.latestFrame && !g_app.frameMessagePending.exchange(true)) repost = true;
        }
        g_app.lastPresentedNs.store(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count()));
        if (repost) PostMessageW(window, kFrameMessage, 0, 0);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case kStatusMessage:
        SetWindowTextW(window, reinterpret_cast<const wchar_t*>(lParam));
        delete[] reinterpret_cast<const wchar_t*>(lParam);
        InvalidateRect(window, nullptr, TRUE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        const clone_video::DecodedFrame* frame = nullptr;
        { std::lock_guard lock(g_app.frameMutex); frame = g_app.displayedFrame.get(); }
        if (frame && !frame->bgra.empty()) {
            const float scale = (std::min)(static_cast<float>(client.right) / frame->width,
                                            static_cast<float>(client.bottom) / frame->height);
            const int width = static_cast<int>(frame->width * scale);
            const int height = static_cast<int>(frame->height * scale);
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = static_cast<LONG>(frame->width);
            info.bmiHeader.biHeight = -static_cast<LONG>(frame->height);
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(dc, COLORONCOLOR);
            StretchDIBits(dc, (client.right - width) / 2, (client.bottom - height) / 2,
                          width, height, 0, 0, frame->width, frame->height,
                          frame->bgra.data(), &info, DIB_RGB_COLORS, SRCCOPY);
        }
        wchar_t line1[256]{};
        wchar_t line2[256]{};
        wchar_t line3[256]{};
        const double processCpuPercent = static_cast<double>(g_app.processCpuMilliPercent.load()) / 1000.0;
        const auto selectedFps = g_app.selectedFps.load();
        if (selectedFps > 0) {
            const auto lastPresented = g_app.lastPresentedNs.load();
            const auto nowNs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
            if (lastPresented != 0) {
                g_app.presentAgeMs.store((nowNs - lastPresented) / 1'000'000);
            }
            const auto oldestQueuedNs = g_app.decodeQueueOldestNs.load();
            g_app.decodeQueueAgeMs.store(oldestQueuedNs == 0 ? 0 : (nowNs - oldestQueuedNs) / 1'000'000);
            swprintf_s(line1, std::size(line1), L"%dx%d@%d H264 %s | decode %.1f fps | process CPU %.1f%%/core",
                g_app.selectedWidth.load(), g_app.selectedHeight.load(),
                selectedFps,
                g_app.hardwareDecoder.load() ? L"hardware" : L"software",
                static_cast<double>(g_app.decodeFpsMilli.load()) / 1000.0,
                processCpuPercent);
            swprintf_s(line2, std::size(line2), L"RTP %s %llums | rx %llu | loss %llu | reorder %llu | AU drops %llu",
                g_app.streamStalled.load() ? L"stalled" : L"live",
                static_cast<unsigned long long>(g_app.lastPacketAgeMs.load()),
                static_cast<unsigned long long>(g_app.receivedPackets.load()),
                static_cast<unsigned long long>(g_app.lostPackets.load()),
                static_cast<unsigned long long>(g_app.reorderedPackets.load()),
                static_cast<unsigned long long>(g_app.droppedAccessUnits.load()));
            swprintf_s(line3, std::size(line3), L"AU %llu | queue %llu/%llu ms | qdrop %llu | %s | decode 0x%08X age %llu ms | MFT %u ms convert %u ms",
                static_cast<unsigned long long>(g_app.lastAccessUnitSequence.load()),
                static_cast<unsigned long long>(g_app.decodeQueueDepth.load()),
                static_cast<unsigned long long>(g_app.decodeQueueAgeMs.load()),
                static_cast<unsigned long long>(g_app.decodeQueueDrops.load()),
                g_app.waitingForIdr.load() ? L"waiting for IDR" : L"live",
                g_app.decodeHresult.load(),
                static_cast<unsigned long long>(g_app.decodeAgeMs.load()),
                g_app.transformTimeMs.load(),
                g_app.conversionTimeMs.load());
        } else {
            swprintf_s(line1, std::size(line1), L"Waiting for phone on TCP port 48200");
            swprintf_s(line2, std::size(line2), L"Process CPU %.1f%% of one core", processCpuPercent);
        }
        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, RGB(0, 0, 0));
        SetTextColor(dc, RGB(255, 255, 255));
        TextOutW(dc, 12, 12, line1, static_cast<int>(std::wcslen(line1)));
        TextOutW(dc, 12, 30, line2, static_cast<int>(std::wcslen(line2)));
        if (line3[0] != L'\0') TextOutW(dc, 12, 48, line3, static_cast<int>(std::wcslen(line3)));
        EndPaint(window, &paint);
        return 0;
    }
    case WM_TIMER: {
        UpdateProcessCpuUsage();
        const auto decoded = g_app.decodedFrames.load();
        const auto previous = g_app.lastFpsDecoded.exchange(decoded);
        g_app.decodeFpsMilli.store((decoded - previous) * 1000);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_DESTROY:
        g_app.server.Stop();
        if (g_app.virtualOutput) g_app.virtualOutput->Stop();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, 0, lParam);
    }
}

void UpdateFreshnessStats(const std::shared_ptr<clone_video::FreshnessQueue>& queue) {
    const auto stats = queue->GetSnapshot();
    g_app.decodeQueueDepth.store(stats.depth);
    g_app.decodeQueueAgeMs.store(stats.oldestAgeMs);
    g_app.decodeQueueOldestNs.store(stats.oldestEnqueuedNs);
    g_app.decodeQueueDrops.store(stats.droppedAccessUnits);
    g_app.waitingForIdr.store(stats.waitingForIdr);
}

void ClearPreviewFrames() {
    bool notify = false;
    {
        std::lock_guard lock(g_app.frameMutex);
        g_app.latestFrame.reset();
        g_app.displayedFrame.reset();
        notify = !g_app.frameMessagePending.exchange(true);
    }
    g_app.lastPresentedNs.store(0);
    if (notify) PostMessageW(g_app.window, kFrameMessage, 0, 0);
}

bool StartReceiver(const clone_net::SessionInfo& info) {
    auto decoder = std::make_shared<clone_video::H264Decoder>();
    if (!decoder->Start(info.hello.width, info.hello.height)) return false;
    g_app.decoder = decoder;
    auto queue = std::make_shared<clone_video::FreshnessQueue>(3);
    g_app.decodeQueue = queue;
    const auto generation = g_app.sessionGeneration.fetch_add(1) + 1;
    if (g_app.virtualOutput) g_app.virtualOutput->BeginSession(generation);
    UpdateFreshnessStats(queue);
    g_app.decodeThread = std::thread([decoder, queue, generation] {
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        clone_net::H264AccessUnit unit;
        while (queue->Pop(unit)) {
            UpdateFreshnessStats(queue);
            clone_video::DecodedFrame frame{};
            const bool decoded = decoder->Decode(unit.annexB, frame, unit.timestamp90k);
            g_app.decodeHresult.store(static_cast<std::uint32_t>(decoder->LastDecodeStatus()));
            g_app.transformTimeMs.store(decoder->LastTransformTimeMs());
            g_app.conversionTimeMs.store(decoder->LastConversionTimeMs());
            if (decoded) {
                const auto decodeFinished = std::chrono::steady_clock::now();
                g_app.decodedFrames.fetch_add(1);
                g_app.decodeAgeMs.store(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(decodeFinished - unit.receivedAt).count()));
                auto sharedFrame = std::make_shared<clone_video::DecodedFrame>(std::move(frame));
                LARGE_INTEGER qpc{};
                QueryPerformanceCounter(&qpc);
                if (g_app.virtualOutput) {
                    g_app.virtualOutput->OnDecodedFrame(sharedFrame, generation,
                        static_cast<std::uint64_t>(qpc.QuadPart));
                }
                bool notify = false;
                {
                    std::lock_guard lock(g_app.frameMutex);
                    g_app.latestFrame = std::move(sharedFrame);
                    notify = !g_app.frameMessagePending.exchange(true);
                }
                if (notify) PostMessageW(g_app.window, kFrameMessage, 0, 0);
            }
            UpdateFreshnessStats(queue);
        }
        if (SUCCEEDED(comResult)) CoUninitialize();
    });
    g_app.receiver = std::make_unique<clone_net::RtpH264Receiver>(
        info.videoSocket, info.hello.ssrc,
        [](clone_net::H264AccessUnit unit) {
            g_app.lastAccessUnitSequence.store(unit.sequence);
            const auto result = g_app.decodeQueue->Push(std::move(unit));
            UpdateFreshnessStats(g_app.decodeQueue);
            if (result == clone_video::FreshnessQueue::PushResult::OverflowNeedsKeyframe) {
                (void)g_app.server.SendKeyframeRequest();
            }
        },
        [] { (void)g_app.server.SendKeyframeRequest(); }, info.peerAddress.sin_addr.s_addr,
        [](std::uint64_t packets, std::uint64_t lost, std::uint64_t reordered, std::uint64_t dropped,
           bool stalled, std::uint64_t packetAgeMs) {
            g_app.receivedPackets.store(packets);
            g_app.lostPackets.store(lost);
            g_app.reorderedPackets.store(reordered);
            g_app.droppedAccessUnits.store(dropped);
            const bool wasStalled = g_app.streamStalled.exchange(stalled);
            if (stalled && !wasStalled) {
                if (g_app.virtualOutput) g_app.virtualOutput->SetState(clone_vcam::FrameState::Reconnecting);
                ClearPreviewFrames();
            }
            g_app.lastPacketAgeMs.store(packetAgeMs);
        });
    g_app.hardwareDecoder.store(std::wstring(decoder->Mode()) == L"hardware");
    g_app.receiver->Start();
    return true;
}

void SetStatusAsync(std::wstring status) {
    auto* copy = new wchar_t[status.size() + 1];
    std::copy(status.begin(), status.end(), copy);
    copy[status.size()] = L'\0';
    if (!PostMessageW(g_app.window, kStatusMessage, 0, reinterpret_cast<LPARAM>(copy))) delete[] copy;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kWindowClass;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&windowClass)) return 1;
    g_app.window = CreateWindowExW(0, kWindowClass, L"DroidCam Clone",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 960, 600,
        nullptr, nullptr, instance, nullptr);
    if (!g_app.window) return 1;
    UpdateProcessCpuUsage();
    ShowWindow(g_app.window, showCommand);
    SetTimer(g_app.window, kStatsTimer, 1000, nullptr);

    g_app.virtualOutput = std::make_unique<clone_vcam::ReceiverOutput>();
    const auto installationId = clone_vcam::VirtualCameraRegistration::GetInstallationId();
    const HRESULT outputHr = installationId.empty() ? HRESULT_FROM_WIN32(ERROR_NOT_FOUND)
        : g_app.virtualOutput->Start(installationId);
    g_app.virtualOutputReady.store(SUCCEEDED(outputHr));
    if (FAILED(outputHr)) {
        wchar_t outputStatus[96]{};
        swprintf_s(outputStatus, std::size(outputStatus), L"Virtual camera unavailable (0x%08lX)", outputHr);
        SetWindowTextW(g_app.window, outputStatus);
    }

    if (!g_app.server.Start(
            [](const clone_net::SessionInfo& info) {
                g_app.selectedFps.store(info.hello.fps);
                g_app.selectedWidth.store(info.hello.width);
                g_app.selectedHeight.store(info.hello.height);
                g_app.decodedFrames.store(0);
                g_app.lastAccessUnitSequence.store(0);
                g_app.lastFpsDecoded.store(0);
                g_app.receivedPackets.store(0);
                g_app.lastPacketAgeMs.store(0);
                g_app.lostPackets.store(0);
                g_app.reorderedPackets.store(0);
                g_app.droppedAccessUnits.store(0);
                g_app.streamStalled.store(false);
                if (!StartReceiver(info)) {
                    g_app.suppressEndStatus = true;
                    SetStatusAsync(L"No H.264 decoder could be initialized");
                    return false;
                }
                wchar_t connectedStatus[128]{};
                swprintf_s(connectedStatus, std::size(connectedStatus), L"Connected · %ux%u@%u H.264 · virtual camera %s",
                    info.hello.width, info.hello.height, info.hello.fps,
                    g_app.virtualOutputReady.load() ? L"ready" : L"unavailable");
                SetStatusAsync(connectedStatus);
                return true;
            },
            [] {
                if (g_app.virtualOutput) g_app.virtualOutput->SetState(clone_vcam::FrameState::Disconnected);
                ClearPreviewFrames();
                if (g_app.receiver) { g_app.receiver->Stop(); g_app.receiver.reset(); }
                if (g_app.decodeQueue) g_app.decodeQueue->Stop();
                if (g_app.decodeThread.joinable()) g_app.decodeThread.join();
                g_app.decodeQueue.reset();
                g_app.decoder.reset();
                g_app.selectedFps.store(0);
                g_app.selectedWidth.store(0);
                g_app.selectedHeight.store(0);
                g_app.hardwareDecoder.store(false);
                g_app.decodeFpsMilli.store(0);
                g_app.decodeAgeMs.store(0);
                g_app.decodeHresult.store(0);
                g_app.presentAgeMs.store(0);
                g_app.lastPresentedNs.store(0);
                g_app.decodeQueueDepth.store(0);
                g_app.decodeQueueAgeMs.store(0);
                g_app.decodeQueueOldestNs.store(0);
                g_app.decodeQueueDrops.store(0);
                g_app.waitingForIdr.store(false);
                g_app.streamStalled.store(false);
                g_app.lastPacketAgeMs.store(0);
                if (g_app.suppressEndStatus) g_app.suppressEndStatus = false;
                else SetStatusAsync(L"Waiting on port 48200 for a phone...");
            })) {
        SetWindowTextW(g_app.window, L"DroidCam Clone - port 48200 unavailable");
    }
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
