#pragma once

#include "session_protocol.h"

#include <Winsock2.h>
#include <Windows.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

namespace clone_net {
struct SessionInfo {
    clone_protocol::Hello hello{};
    SOCKET videoSocket{INVALID_SOCKET}; // Owned by SessionServer.
    sockaddr_in peerAddress{};
    std::uint16_t videoPort{};
};

class SessionServer final {
public:
    using SessionCallback = std::function<bool(const SessionInfo&)>;

    SessionServer() = default;
    ~SessionServer();
    SessionServer(const SessionServer&) = delete;
    SessionServer& operator=(const SessionServer&) = delete;

    [[nodiscard]] bool Start(SessionCallback onAccepted, std::function<void()> onEnded);
    void Stop();
    [[nodiscard]] bool SendKeyframeRequest();
    [[nodiscard]] bool IsRunning() const noexcept { return running_.load(); }

private:
    void AcceptLoop();
    void HandleSession(SOCKET control, const sockaddr_in& peer);
    void CloseActiveSession();

    std::atomic_bool running_{false};
    SOCKET listener_{INVALID_SOCKET};
    SOCKET control_{INVALID_SOCKET};
    SOCKET video_{INVALID_SOCKET};
    bool winsockStarted_{};
    std::thread acceptThread_;
    std::mutex controlWriteMutex_;
    clone_protocol::Hello activeHello_{};
    sockaddr_in activePeer_{};
    SessionCallback onAccepted_;
    std::function<void()> onEnded_;
};
}
