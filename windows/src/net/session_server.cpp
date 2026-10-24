#include "session_server.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <objbase.h>
#include <utility>
#include <vector>

namespace clone_net {
namespace {
bool ReceiveExact(SOCKET socket, std::uint8_t* output, std::size_t size) {
    std::size_t offset = 0;
    while (offset < size) {
        const int received = recv(socket, reinterpret_cast<char*>(output + offset),
                                  static_cast<int>(size - offset), 0);
        if (received <= 0) return false;
        offset += static_cast<std::size_t>(received);
    }
    return true;
}

bool SendExact(SOCKET socket, const std::uint8_t* data, std::size_t size) {
    std::size_t offset = 0;
    while (offset < size) {
        const int sent = send(socket, reinterpret_cast<const char*>(data + offset),
                              static_cast<int>(size - offset), 0);
        if (sent <= 0) return false;
        offset += static_cast<std::size_t>(sent);
    }
    return true;
}

bool ReadRecord(SOCKET socket, std::vector<std::uint8_t>& record) {
    std::array<std::uint8_t, 4> prefix{};
    if (!ReceiveExact(socket, prefix.data(), prefix.size())) return false;
    const std::uint32_t length = (static_cast<std::uint32_t>(prefix[0]) << 24) |
                                 (static_cast<std::uint32_t>(prefix[1]) << 16) |
                                 (static_cast<std::uint32_t>(prefix[2]) << 8) |
                                 static_cast<std::uint32_t>(prefix[3]);
    if (length < 2 || length > clone_protocol::kMaxRecordLength) return false;
    record.resize(static_cast<std::size_t>(length) + prefix.size());
    std::copy(prefix.begin(), prefix.end(), record.begin());
    return ReceiveExact(socket, record.data() + prefix.size(), length);
}

bool SendReject(SOCKET socket, std::uint16_t reason) {
    const std::array<std::uint8_t, 8> record{
        0, 0, 0, 4, clone_protocol::kVersion,
        static_cast<std::uint8_t>(clone_protocol::MessageType::Reject),
        static_cast<std::uint8_t>(reason >> 8), static_cast<std::uint8_t>(reason),
    };
    return SendExact(socket, record.data(), record.size());
}
}

SessionServer::~SessionServer() { Stop(); }

bool SessionServer::Start(SessionCallback onAccepted, std::function<void()> onEnded) {
    if (running_.load()) return false;
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
    winsockStarted_ = true;

    listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener_ == INVALID_SOCKET) {
        Stop();
        return false;
    }
    BOOL reuse = TRUE;
    setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(clone_protocol::kControlPort);
    if (bind(listener_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
        listen(listener_, 1) == SOCKET_ERROR) {
        Stop();
        return false;
    }

    onAccepted_ = std::move(onAccepted);
    onEnded_ = std::move(onEnded);
    running_.store(true);
    acceptThread_ = std::thread(&SessionServer::AcceptLoop, this);
    return true;
}

void SessionServer::Stop() {
    if (!running_.exchange(false) && !winsockStarted_) return;
    if (listener_ != INVALID_SOCKET) {
        shutdown(listener_, SD_BOTH);
        closesocket(listener_);
        listener_ = INVALID_SOCKET;
    }
    {
        std::lock_guard lock(controlWriteMutex_);
        if (control_ != INVALID_SOCKET) shutdown(control_, SD_BOTH);
        if (video_ != INVALID_SOCKET) shutdown(video_, SD_BOTH);
    }
    if (acceptThread_.joinable() && acceptThread_.get_id() != std::this_thread::get_id()) acceptThread_.join();
    CloseActiveSession();
    if (winsockStarted_) {
        WSACleanup();
        winsockStarted_ = false;
    }
}

bool SessionServer::SendKeyframeRequest() {
    std::lock_guard lock(controlWriteMutex_);
    if (control_ == INVALID_SOCKET) return false;
    std::vector<std::uint8_t> record{0, 0, 0, 18, clone_protocol::kVersion,
        static_cast<std::uint8_t>(clone_protocol::MessageType::KeyframeRequest)};
    record.insert(record.end(), activeHello_.sessionId.begin(), activeHello_.sessionId.end());
    return SendExact(control_, record.data(), record.size());
}

void SessionServer::AcceptLoop() {
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    while (running_.load()) {
        sockaddr_in peer{};
        int peerSize = sizeof(peer);
        SOCKET client = accept(listener_, reinterpret_cast<sockaddr*>(&peer), &peerSize);
        if (client == INVALID_SOCKET) {
            if (!running_.load()) break;
            continue;
        }
        {
            std::lock_guard lock(controlWriteMutex_);
            control_ = client;
        }
        HandleSession(client, peer);
    }
    if (SUCCEEDED(comResult)) CoUninitialize();
}

void SessionServer::HandleSession(SOCKET control, const sockaddr_in& peer) {
    std::vector<std::uint8_t> record;
    clone_protocol::Hello hello{};
    std::string error;
    if (!ReadRecord(control, record) || !clone_protocol::ParseHelloRecord(record, hello, error)) {
        SendReject(control, error == "unsupported profile" ? 2 : 1);
        CloseActiveSession();
        return;
    }

    SOCKET video = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (video == INVALID_SOCKET) {
        SendReject(control, 3);
        CloseActiveSession();
        return;
    }
    sockaddr_in videoAddress{};
    videoAddress.sin_family = AF_INET;
    videoAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    videoAddress.sin_port = 0;
    if (bind(video, reinterpret_cast<const sockaddr*>(&videoAddress), sizeof(videoAddress)) == SOCKET_ERROR) {
        closesocket(video);
        SendReject(control, 3);
        CloseActiveSession();
        return;
    }
    int addressSize = sizeof(videoAddress);
    if (getsockname(video, reinterpret_cast<sockaddr*>(&videoAddress), &addressSize) == SOCKET_ERROR) {
        closesocket(video);
        SendReject(control, 3);
        CloseActiveSession();
        return;
    }

    {
        std::lock_guard lock(controlWriteMutex_);
        video_ = video;
        activeHello_ = hello;
        activePeer_ = peer;
    }
    clone_protocol::Accept acceptMessage{hello.sessionId, ntohs(videoAddress.sin_port),
                                         clone_protocol::kMaxRtpPayload};
    const auto acceptRecord = clone_protocol::EncodeAcceptRecord(acceptMessage);
    if (!SendExact(control, acceptRecord.data(), acceptRecord.size())) {
        CloseActiveSession();
        return;
    }

    if (onAccepted_) {
        SessionInfo info{hello, video, peer, ntohs(videoAddress.sin_port)};
        if (!onAccepted_(info)) {
            CloseActiveSession();
            return;
        }
    }

    while (running_.load()) {
        if (!ReadRecord(control, record)) break;
        std::array<std::uint8_t, 16> sessionId{};
        error.clear();
        if (!clone_protocol::ParseSessionMessage(record, clone_protocol::MessageType::Close, sessionId, error) ||
            sessionId != hello.sessionId) {
            break;
        }
        break;
    }
    CloseActiveSession();
}

void SessionServer::CloseActiveSession() {
    SOCKET control = INVALID_SOCKET;
    SOCKET video = INVALID_SOCKET;
    {
        std::lock_guard lock(controlWriteMutex_);
        control = std::exchange(control_, INVALID_SOCKET);
        video = std::exchange(video_, INVALID_SOCKET);
    }
    if (control != INVALID_SOCKET) {
        shutdown(control, SD_BOTH);
        closesocket(control);
    }
    if (video != INVALID_SOCKET) {
        shutdown(video, SD_BOTH);
        closesocket(video);
    }
    if (onEnded_) onEnded_();
}
}
