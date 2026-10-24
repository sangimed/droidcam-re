#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>

#include "net/rtp_h264_receiver.h"
#include "net/session_server.h"
#include "video/h264_decoder.h"

namespace {
constexpr wchar_t kWindowClass[] = L"DroidCamCloneWindow";
constexpr UINT kFrameMessage = WM_APP + 1;
constexpr UINT kStatusMessage = WM_APP + 2;

struct AppState {
    HWND window{};
    clone_net::SessionServer server;
    std::unique_ptr<clone_net::RtpH264Receiver> receiver;
    std::shared_ptr<clone_video::H264Decoder> decoder;
    std::mutex frameMutex;
    std::unique_ptr<clone_video::DecodedFrame> latestFrame;
    std::unique_ptr<clone_video::DecodedFrame> displayedFrame;
    std::atomic_bool frameMessagePending{false};
    bool suppressEndStatus{};
};
AppState g_app;

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
        } else {
            SetBkMode(dc, TRANSPARENT);
            const wchar_t text[] = L"Waiting for a phone on TCP port 48200";
            TextOutW(dc, 24, 24, text, static_cast<int>(std::size(text) - 1));
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DESTROY:
        g_app.server.Stop();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, 0, lParam);
    }
}

bool StartReceiver(const clone_net::SessionInfo& info) {
    auto decoder = std::make_shared<clone_video::H264Decoder>();
    if (!decoder->Start(info.hello.width, info.hello.height)) return false;
    g_app.decoder = decoder;
    g_app.receiver = std::make_unique<clone_net::RtpH264Receiver>(
        info.videoSocket, info.hello.ssrc,
        [decoder](clone_net::H264AccessUnit unit) {
            clone_video::DecodedFrame frame{};
            if (!decoder->Decode(unit.annexB, frame)) return;
            bool notify = false;
            {
                std::lock_guard lock(g_app.frameMutex);
                g_app.latestFrame = std::make_unique<clone_video::DecodedFrame>(std::move(frame));
                notify = !g_app.frameMessagePending.exchange(true);
            }
            if (notify) PostMessageW(g_app.window, kFrameMessage, 0, 0);
        },
        [] { (void)g_app.server.SendKeyframeRequest(); }, info.peerAddress.sin_addr.s_addr);
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
    ShowWindow(g_app.window, showCommand);

    if (!g_app.server.Start(
            [](const clone_net::SessionInfo& info) {
                if (!StartReceiver(info)) {
                    g_app.suppressEndStatus = true;
                    SetStatusAsync(L"No H.264 decoder could be initialized");
                    return false;
                }
                SetStatusAsync(info.hello.fps == 60 ? L"Connected · 1080p60 H.264" : L"Connected · 1080p30 H.264");
                return true;
            },
            [] {
                if (g_app.receiver) { g_app.receiver->Stop(); g_app.receiver.reset(); }
                g_app.decoder.reset();
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
