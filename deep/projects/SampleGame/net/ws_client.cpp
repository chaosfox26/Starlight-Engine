// ws_client.cpp — implementation. See ws_client.h for scope.
#include "ws_client.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <array>
#include <vector>
#include <string>

#pragma comment(lib, "ws2_32.lib")

namespace net {

// ---- base64 ---------------------------------------------------------------
static std::string b64encode(const uint8_t* data, size_t len) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 2 < len; i += 3) {
        uint32_t n = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | data[i + 2];
        out.push_back(T[(n >> 18) & 63]);
        out.push_back(T[(n >> 12) & 63]);
        out.push_back(T[(n >> 6) & 63]);
        out.push_back(T[n & 63]);
    }
    if (i < len) {
        uint32_t n = uint32_t(data[i]) << 16;
        bool two = (i + 1 < len);
        if (two) n |= uint32_t(data[i + 1]) << 8;
        out.push_back(T[(n >> 18) & 63]);
        out.push_back(T[(n >> 12) & 63]);
        out.push_back(two ? T[(n >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

// ---- SHA-1 (for Sec-WebSocket-Accept verification) ------------------------
static void sha1(const uint8_t* msg, size_t len, uint8_t out[20]) {
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
    uint64_t ml = uint64_t(len) * 8;
    std::vector<uint8_t> data(msg, msg + len);
    data.push_back(0x80);
    while (data.size() % 64 != 56) data.push_back(0x00);
    for (int i = 7; i >= 0; --i) data.push_back(uint8_t((ml >> (i * 8)) & 0xFF));

    auto rol = [](uint32_t v, int c) { return (v << c) | (v >> (32 - c)); };
    for (size_t chunk = 0; chunk < data.size(); chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t(data[chunk + i * 4]) << 24) | (uint32_t(data[chunk + i * 4 + 1]) << 16) |
                   (uint32_t(data[chunk + i * 4 + 2]) << 8) | uint32_t(data[chunk + i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | ((~b) & d);            k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d;                       k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d);     k = 0x8F1BBCDC; }
            else             { f = b ^ c ^ d;                       k = 0xCA62C1D6; }
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }
    uint32_t hs[5] = { h0, h1, h2, h3, h4 };
    for (int i = 0; i < 5; ++i) {
        out[i * 4]     = uint8_t((hs[i] >> 24) & 0xFF);
        out[i * 4 + 1] = uint8_t((hs[i] >> 16) & 0xFF);
        out[i * 4 + 2] = uint8_t((hs[i] >> 8) & 0xFF);
        out[i * 4 + 3] = uint8_t(hs[i] & 0xFF);
    }
}

// --------------------------------------------------------------------------
WsClient::~WsClient() { close(); }

bool WsClient::sendRaw(const char* buf, size_t len) {
    SOCKET s = (SOCKET)sock_;
    size_t sent = 0;
    while (sent < len) {
        int n = ::send(s, buf + sent, (int)(len - sent), 0);
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}

bool WsClient::connect(const std::string& host, uint16_t port, const std::string& path) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { log("WSAStartup failed"); return false; }
    wsaInit_ = true;

    char portStr[16];
    std::snprintf(portStr, sizeof(portStr), "%u", (unsigned)port);
    addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM; hints.ai_protocol = IPPROTO_TCP;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), portStr, &hints, &res) != 0 || !res) {
        log("getaddrinfo failed for " + host); return false;
    }
    SOCKET s = INVALID_SOCKET;
    for (addrinfo* ai = res; ai; ai = ai->ai_next) {
        s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        if (::connect(s, ai->ai_addr, (int)ai->ai_addrlen) == 0) break;
        closesocket(s); s = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    if (s == INVALID_SOCKET) { log("TCP connect failed to " + host + ":" + portStr); return false; }
    sock_ = (uintptr_t)s;

    // ---- handshake: random 16-byte key -> base64 ----
    std::srand((unsigned)time(nullptr) ^ (unsigned)GetCurrentThreadId());
    uint8_t keyBytes[16];
    for (auto& b : keyBytes) b = (uint8_t)(std::rand() & 0xFF);
    std::string key = b64encode(keyBytes, 16);

    std::string req =
        "GET " + path + " HTTP/1.1\r\n"
        "Host: " + host + ":" + portStr + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: " + key + "\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";
    if (!sendRaw(req.data(), req.size())) { log("handshake send failed"); shutdownSocket(); return false; }

    // ---- read response headers up to \r\n\r\n ----
    std::string resp;
    char buf[512];
    for (;;) {
        int n = ::recv(s, buf, sizeof(buf), 0);
        if (n <= 0) { log("handshake recv failed"); shutdownSocket(); return false; }
        resp.append(buf, buf + n);
        auto pos = resp.find("\r\n\r\n");
        if (pos != std::string::npos) {
            // Anything after the header terminator is the start of the first frame.
            leftover_.assign(resp.begin() + (pos + 4), resp.end());
            resp.resize(pos);
            break;
        }
        if (resp.size() > 16384) { log("handshake response too large"); shutdownSocket(); return false; }
    }

    if (resp.find(" 101") == std::string::npos) {
        log("server did not return 101 Switching Protocols:\n" + resp);
        shutdownSocket(); return false;
    }

    // ---- verify Sec-WebSocket-Accept = base64(sha1(key + GUID)) ----
    const std::string GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string concat = key + GUID;
    uint8_t digest[20];
    sha1((const uint8_t*)concat.data(), concat.size(), digest);
    std::string expected = b64encode(digest, 20);
    // case-insensitive header search
    std::string low = resp;
    for (auto& c : low) c = (char)tolower((unsigned char)c);
    auto ap = low.find("sec-websocket-accept:");
    if (ap != std::string::npos) {
        size_t vs = ap + strlen("sec-websocket-accept:");
        while (vs < resp.size() && (resp[vs] == ' ' || resp[vs] == '\t')) ++vs;
        size_t ve = resp.find("\r\n", vs);
        std::string got = resp.substr(vs, (ve == std::string::npos ? resp.size() : ve) - vs);
        if (got != expected) {
            log("Sec-WebSocket-Accept mismatch (got '" + got + "' expected '" + expected + "')");
            shutdownSocket(); return false;
        }
    } else {
        log("no Sec-WebSocket-Accept header (proceeding anyway)");
    }

    open_.store(true);
    stop_.store(false);
    reader_ = std::thread(&WsClient::readLoop, this);
    log("websocket connected to ws://" + host + ":" + portStr + path);
    return true;
}

// ---- build + send a client-masked frame -----------------------------------
bool WsClient::sendFrame(uint8_t opcode, const void* data, size_t len) {
    std::lock_guard<std::mutex> lk(sendMu_);
    if (!open_.load()) return false;
    std::vector<uint8_t> f;
    f.push_back(0x80 | (opcode & 0x0F)); // FIN + opcode
    uint8_t maskBit = 0x80;              // clients MUST mask
    if (len < 126) {
        f.push_back(maskBit | (uint8_t)len);
    } else if (len <= 0xFFFF) {
        f.push_back(maskBit | 126);
        f.push_back((uint8_t)((len >> 8) & 0xFF));
        f.push_back((uint8_t)(len & 0xFF));
    } else {
        f.push_back(maskBit | 127);
        for (int i = 7; i >= 0; --i) f.push_back((uint8_t)((uint64_t(len) >> (i * 8)) & 0xFF));
    }
    uint8_t mask[4];
    for (auto& m : mask) m = (uint8_t)(std::rand() & 0xFF);
    f.insert(f.end(), mask, mask + 4);
    const uint8_t* p = (const uint8_t*)data;
    size_t base = f.size();
    f.resize(base + len);
    for (size_t i = 0; i < len; ++i) f[base + i] = p[i] ^ mask[i & 3];
    return sendRaw((const char*)f.data(), f.size());
}

bool WsClient::sendText(const std::string& text) {
    return sendFrame(0x1, text.data(), text.size());
}

// ---- read loop: reassemble frames, dispatch text --------------------------
void WsClient::readLoop() {
    SOCKET s = (SOCKET)sock_;
    std::vector<uint8_t> rx(leftover_.begin(), leftover_.end());
    leftover_.clear();
    std::string fragMsg;      // accumulates a fragmented text/binary message
    uint8_t     fragOpcode = 0;
    char netbuf[4096];

    auto need = [&](size_t n) -> bool {
        while (rx.size() < n) {
            int got = ::recv(s, netbuf, sizeof(netbuf), 0);
            if (got <= 0) return false;
            rx.insert(rx.end(), netbuf, netbuf + got);
        }
        return true;
    };

    while (!stop_.load()) {
        if (!need(2)) break;
        uint8_t b0 = rx[0], b1 = rx[1];
        bool    fin = (b0 & 0x80) != 0;
        uint8_t opcode = b0 & 0x0F;
        bool    masked = (b1 & 0x80) != 0;   // server->client is normally unmasked
        uint64_t plen = b1 & 0x7F;
        size_t   hdr = 2;
        if (plen == 126) {
            if (!need(4)) break;
            plen = (uint64_t(rx[2]) << 8) | rx[3];
            hdr = 4;
        } else if (plen == 127) {
            if (!need(10)) break;
            plen = 0;
            for (int i = 0; i < 8; ++i) plen = (plen << 8) | rx[2 + i];
            hdr = 10;
        }
        uint8_t mask[4] = { 0,0,0,0 };
        if (masked) {
            if (!need(hdr + 4)) break;
            for (int i = 0; i < 4; ++i) mask[i] = rx[hdr + i];
            hdr += 4;
        }
        if (!need(hdr + (size_t)plen)) break;

        std::string payload;
        payload.resize((size_t)plen);
        for (uint64_t i = 0; i < plen; ++i) {
            uint8_t byte = rx[hdr + (size_t)i];
            payload[(size_t)i] = (char)(masked ? (byte ^ mask[i & 3]) : byte);
        }
        rx.erase(rx.begin(), rx.begin() + hdr + (size_t)plen);

        switch (opcode) {
            case 0x0: // continuation
                fragMsg += payload;
                if (fin) {
                    if (fragOpcode == 0x1 && onMsg_) onMsg_(fragMsg);
                    fragMsg.clear(); fragOpcode = 0;
                }
                break;
            case 0x1: // text
            case 0x2: // binary
                if (fin) {
                    if (opcode == 0x1 && onMsg_) onMsg_(payload);
                } else {
                    fragOpcode = opcode; fragMsg = payload;
                }
                break;
            case 0x8: // close
                log("server sent CLOSE");
                sendFrame(0x8, payload.data(), payload.size());
                stop_.store(true);
                break;
            case 0x9: // ping -> pong
                sendFrame(0xA, payload.data(), payload.size());
                break;
            case 0xA: // pong
                break;
            default:
                break;
        }
    }
    open_.store(false);
    log("read loop ended");
}

void WsClient::shutdownSocket() {
    if (sock_ != ~static_cast<uintptr_t>(0)) {
        closesocket((SOCKET)sock_);
        sock_ = ~static_cast<uintptr_t>(0);
    }
    if (wsaInit_) { WSACleanup(); wsaInit_ = false; }
}

void WsClient::close() {
    if (open_.exchange(false)) {
        // best-effort CLOSE frame
        uint8_t code[2] = { 0x03, 0xE8 }; // 1000 normal
        sendFrame(0x8, code, 2);
    }
    stop_.store(true);
    if (reader_.joinable()) {
        // unblock a recv() stuck on the socket
        if (sock_ != ~static_cast<uintptr_t>(0)) shutdown((SOCKET)sock_, SD_BOTH);
        reader_.join();
    }
    shutdownSocket();
}

} // namespace net
