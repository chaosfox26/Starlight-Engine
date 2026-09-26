// ws_client.h — minimal RFC6455 WebSocket client over Winsock2 for ws:// (no TLS).
//
// Scope: receive-focused. It performs the HTTP Upgrade handshake (client masking
// on the frames it *sends*), then reads server TEXT frames on a background thread,
// handing each complete UTF-8 message to a callback. It answers server PING with
// PONG and CLOSE with CLOSE, and can send masked TEXT (for JOIN/INPUT) and CLOSE.
//
// This is the transport half of the net module; message *meaning* lives in
// world_state.h. TLS (wss://) is intentionally out of scope — the slice server is
// local ws://. Standalone: NEW file, does not touch main.cpp.
#pragma once
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstdint>

namespace net {

class WsClient {
public:
    using MessageCb = std::function<void(const std::string& text)>;
    using LogCb     = std::function<void(const std::string& line)>;

    WsClient() = default;
    ~WsClient();

    WsClient(const WsClient&) = delete;
    WsClient& operator=(const WsClient&) = delete;

    // host e.g. "127.0.0.1", port e.g. 8090, path e.g. "/". Blocks until the
    // handshake completes (or fails); on success spawns the read thread.
    // Returns true if connected + upgraded.
    bool connect(const std::string& host, uint16_t port, const std::string& path = "/");

    // Send a TEXT frame (client-masked). Safe to call from any thread. No-op if not open.
    bool sendText(const std::string& text);

    // Send a CLOSE frame and stop the read thread. Idempotent.
    void close();

    bool isOpen() const { return open_.load(); }

    void onMessage(MessageCb cb) { onMsg_ = std::move(cb); }
    void onLog(LogCb cb)         { onLog_ = std::move(cb); }

private:
    void   readLoop();
    bool   sendFrame(uint8_t opcode, const void* data, size_t len);
    bool   sendRaw(const char* buf, size_t len);
    void   log(const std::string& s) const { if (onLog_) onLog_(s); }
    void   shutdownSocket();

    uintptr_t        sock_ = ~static_cast<uintptr_t>(0); // SOCKET (INVALID_SOCKET)
    std::atomic<bool> open_{false};
    std::atomic<bool> stop_{false};
    std::thread      reader_;
    std::mutex       sendMu_;
    std::string      leftover_;   // bytes read past the handshake terminator
    MessageCb        onMsg_;
    LogCb            onLog_;
    bool             wsaInit_ = false;
};

} // namespace net
