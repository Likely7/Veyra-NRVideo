// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/moonlight/Http.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string_view>
#include <new>

namespace veyra::moonlight {

namespace {

using Clock = std::chrono::steady_clock;

struct WsaInit {
    WsaInit() { WSADATA data; WSAStartup(MAKEWORD(2, 2), &data); }
    ~WsaInit() { WSACleanup(); }
};
void ensureWinsock() { static WsaInit init; }

struct Wait {
    Clock::time_point deadline;
    bool infinite = false;
    const std::atomic<bool>* cancel = nullptr;
};

void check(const Wait& w) {
    if (w.cancel && w.cancel->load(std::memory_order_relaxed)) throw TransportError(TransportError::Kind::Cancelled, "cancelled");
    if (!w.infinite && Clock::now() >= w.deadline) throw TransportError(TransportError::Kind::Timeout, "request timed out");
}

// Waits until the socket is ready (or has failed: a refused non-blocking connect
// is reported through the except set on Windows). Slices of 50 ms keep the
// cancel flag and the deadline responsive.
void waitSocket(SOCKET s, bool forRead, bool forWrite, const Wait& w) {
    for (;;) {
        check(w);
        fd_set readSet, writeSet, errorSet;
        FD_ZERO(&readSet); FD_ZERO(&writeSet); FD_ZERO(&errorSet);
        if (forRead) FD_SET(s, &readSet);
        if (forWrite) FD_SET(s, &writeSet);
        FD_SET(s, &errorSet);
        timeval slice{0, 50 * 1000};
        const int ready = select(0, &readSet, &writeSet, &errorSet, &slice);
        if (ready > 0) return;
        if (ready == SOCKET_ERROR) throw TransportError(TransportError::Kind::Connect, "select failed");
    }
}

struct Socket {
    SOCKET handle = INVALID_SOCKET;
    Socket() = default;
    explicit Socket(SOCKET s) : handle(s) {}
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    ~Socket() { if (handle != INVALID_SOCKET) closesocket(handle); }
};

struct AddrInfoFree { void operator()(addrinfo* p) const { freeaddrinfo(p); } };

SOCKET connectSocket(const std::string& host, uint16_t port, const Wait& overall) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* raw = nullptr;
    const std::string portText = std::to_string(port);
    if (getaddrinfo(host.c_str(), portText.c_str(), &hints, &raw) != 0 || !raw)
        throw TransportError(TransportError::Kind::Connect, "cannot resolve the host address");
    std::unique_ptr<addrinfo, AddrInfoFree> list(raw);
    size_t candidates = 0;
    for (addrinfo* ai = raw; ai; ai = ai->ai_next) ++candidates;
    for (addrinfo* ai = raw; ai; ai = ai->ai_next) {
        SOCKET s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        u_long nonBlocking = 1;
        ioctlsocket(s, FIONBIO, &nonBlocking);
        // With several candidate addresses one unreachable family must not eat the whole budget.
        Wait attempt = overall;
        if (candidates > 1) {
            const auto cap = Clock::now() + std::chrono::seconds(3);
            if (attempt.infinite || cap < attempt.deadline) { attempt.deadline = cap; attempt.infinite = false; }
        }
        const int result = connect(s, ai->ai_addr, int(ai->ai_addrlen));
        if (result == SOCKET_ERROR) {
            if (WSAGetLastError() != WSAEWOULDBLOCK) { closesocket(s); continue; }
            try {
                waitSocket(s, false, true, attempt);
            } catch (const TransportError& e) {
                closesocket(s);
                if (e.kind == TransportError::Kind::Timeout && candidates > 1 && Clock::now() < overall.deadline) continue;
                throw;
            }
            int error = 0;
            int length = sizeof(error);
            getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length);
            if (error != 0) { closesocket(s); continue; }
        }
        BOOL noDelay = TRUE;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
        return s;
    }
    throw TransportError(TransportError::Kind::Connect, "the host did not accept the connection");
}

struct CtxFree { void operator()(SSL_CTX* p) const { SSL_CTX_free(p); } };
struct SslFree { void operator()(SSL* p) const { SSL_free(p); } };
struct BioFree { void operator()(BIO* p) const { BIO_free_all(p); } };
struct X509Free { void operator()(X509* p) const { X509_free(p); } };
struct PkeyFree { void operator()(EVP_PKEY* p) const { EVP_PKEY_free(p); } };

std::unique_ptr<SSL_CTX, CtxFree> makeContext(const Identity& identity) {
    std::unique_ptr<SSL_CTX, CtxFree> ctx(SSL_CTX_new(TLS_client_method()));
    if (!ctx) throw std::bad_alloc();
    SSL_CTX_set_min_proto_version(ctx.get(), TLS1_2_VERSION);
    // The host's certificate is self-signed: trust comes from the pinned copy,
    // compared right after the handshake and before anything is sent.
    SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_NONE, nullptr);
    std::unique_ptr<BIO, BioFree> certBio(BIO_new_mem_buf(identity.certPem.data(), int(identity.certPem.size())));
    std::unique_ptr<BIO, BioFree> keyBio(BIO_new_mem_buf(identity.keyPem.data(), int(identity.keyPem.size())));
    std::unique_ptr<X509, X509Free> cert(certBio ? PEM_read_bio_X509(certBio.get(), nullptr, nullptr, nullptr) : nullptr);
    std::unique_ptr<EVP_PKEY, PkeyFree> key(keyBio ? PEM_read_bio_PrivateKey(keyBio.get(), nullptr, nullptr, nullptr) : nullptr);
    if (!cert || !key || SSL_CTX_use_certificate(ctx.get(), cert.get()) != 1 || SSL_CTX_use_PrivateKey(ctx.get(), key.get()) != 1)
        throw TransportError(TransportError::Kind::Tls, "the client identity could not be loaded");
    return ctx;
}

// One connection, plain or TLS, read and written under the same deadline.
class Connection {
public:
    Connection(SOCKET socket, SSL* ssl, const Wait& wait) : socket_(socket), ssl_(ssl), wait_(wait) {}

    void writeAll(std::string_view data) {
        size_t sent = 0;
        while (sent < data.size()) {
            check(wait_);
            const int chunk = int(std::min<size_t>(data.size() - sent, 1 << 16));
            if (ssl_) {
                const int n = SSL_write(ssl_, data.data() + sent, chunk);
                if (n > 0) { sent += size_t(n); continue; }
                const int error = SSL_get_error(ssl_, n);
                if (error == SSL_ERROR_WANT_READ) waitSocket(socket_, true, false, wait_);
                else if (error == SSL_ERROR_WANT_WRITE) waitSocket(socket_, false, true, wait_);
                else throw TransportError(TransportError::Kind::Tls, "TLS write failed");
            } else {
                const int n = send(socket_, data.data() + sent, chunk, 0);
                if (n > 0) { sent += size_t(n); continue; }
                if (WSAGetLastError() == WSAEWOULDBLOCK) waitSocket(socket_, false, true, wait_);
                else throw TransportError(TransportError::Kind::Connect, "send failed");
            }
        }
    }

    // 0 means the peer closed the connection.
    size_t read(char* buffer, size_t capacity) {
        for (;;) {
            check(wait_);
            if (ssl_) {
                const int n = SSL_read(ssl_, buffer, int(std::min<size_t>(capacity, 1 << 16)));
                if (n > 0) { receivedAny_ = true; return size_t(n); }
                const int error = SSL_get_error(ssl_, n);
                if (error == SSL_ERROR_WANT_READ) waitSocket(socket_, true, false, wait_);
                else if (error == SSL_ERROR_WANT_WRITE) waitSocket(socket_, false, true, wait_);
                else if (error == SSL_ERROR_ZERO_RETURN) return 0;
                // A host that closes without a TLS close_notify after answering is still a complete answer.
                else if (error == SSL_ERROR_SYSCALL && receivedAny_) return 0;
                else throw TransportError(TransportError::Kind::Tls, "TLS read failed");
            } else {
                const int n = recv(socket_, buffer, int(std::min<size_t>(capacity, 1 << 16)), 0);
                if (n > 0) { receivedAny_ = true; return size_t(n); }
                if (n == 0) return 0;
                if (WSAGetLastError() == WSAEWOULDBLOCK) waitSocket(socket_, true, false, wait_);
                else if (receivedAny_) return 0;
                else throw TransportError(TransportError::Kind::Connect, "receive failed");
            }
        }
    }

private:
    SOCKET socket_;
    SSL* ssl_;
    Wait wait_;
    bool receivedAny_ = false;
};

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}

std::string trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return std::string(text);
}

HttpResponse readResponse(Connection& connection, size_t maxBody) {
    std::string buffer;
    char chunk[8192];
    size_t headerEnd = std::string::npos;
    while ((headerEnd = buffer.find("\r\n\r\n")) == std::string::npos) {
        if (buffer.size() > (1u << 16)) throw TransportError(TransportError::Kind::Protocol, "response headers too large");
        const size_t n = connection.read(chunk, sizeof(chunk));
        if (n == 0) throw TransportError(TransportError::Kind::Protocol, "connection closed before the response headers ended");
        buffer.append(chunk, n);
    }
    HttpResponse response;
    const std::string head = buffer.substr(0, headerEnd);
    std::string body = buffer.substr(headerEnd + 4);

    const auto firstLineEnd = head.find("\r\n");
    const std::string statusLine = head.substr(0, firstLineEnd);
    if (statusLine.rfind("HTTP/1.", 0) != 0 || statusLine.size() < 12)
        throw TransportError(TransportError::Kind::Protocol, "malformed HTTP status line");
    response.status = std::atoi(statusLine.c_str() + 9);

    long long contentLength = -1;
    bool chunked = false;
    size_t position = firstLineEnd == std::string::npos ? head.size() : firstLineEnd + 2;
    while (position < head.size()) {
        const auto end = head.find("\r\n", position);
        const std::string line = head.substr(position, end == std::string::npos ? std::string::npos : end - position);
        position = end == std::string::npos ? head.size() : end + 2;
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string name = lower(line.substr(0, colon));
        const std::string value = trim(std::string_view(line).substr(colon + 1));
        if (name == "content-length") contentLength = std::atoll(value.c_str());
        else if (name == "transfer-encoding" && lower(value).find("chunked") != std::string::npos) chunked = true;
    }
    if (contentLength > 0 && size_t(contentLength) > maxBody) throw TransportError(TransportError::Kind::Protocol, "response body too large");

    if (chunked) {
        std::string decoded;
        size_t cursor = 0;
        for (;;) {
            // Make sure a full chunk-size line is buffered.
            size_t lineEnd;
            while ((lineEnd = body.find("\r\n", cursor)) == std::string::npos) {
                const size_t n = connection.read(chunk, sizeof(chunk));
                if (n == 0) throw TransportError(TransportError::Kind::Protocol, "connection closed inside a chunked body");
                body.append(chunk, n);
            }
            const size_t size = std::strtoull(body.substr(cursor, lineEnd - cursor).c_str(), nullptr, 16);
            cursor = lineEnd + 2;
            if (size == 0) break;
            if (decoded.size() + size > maxBody) throw TransportError(TransportError::Kind::Protocol, "response body too large");
            while (body.size() < cursor + size + 2) {
                const size_t n = connection.read(chunk, sizeof(chunk));
                if (n == 0) throw TransportError(TransportError::Kind::Protocol, "connection closed inside a chunked body");
                body.append(chunk, n);
            }
            decoded.append(body, cursor, size);
            cursor += size + 2;
        }
        response.body = std::move(decoded);
    } else if (contentLength >= 0) {
        while (body.size() < size_t(contentLength)) {
            const size_t n = connection.read(chunk, sizeof(chunk));
            if (n == 0) throw TransportError(TransportError::Kind::Protocol, "connection closed before the body was complete");
            body.append(chunk, n);
        }
        body.resize(size_t(contentLength));
        response.body = std::move(body);
    } else {
        for (;;) {
            const size_t n = connection.read(chunk, sizeof(chunk));
            if (n == 0) break;
            if (body.size() + n > maxBody) throw TransportError(TransportError::Kind::Protocol, "response body too large");
            body.append(chunk, n);
        }
        response.body = std::move(body);
    }
    return response;
}

} // namespace

HttpClient::HttpClient(const Identity* identity, std::string pinnedServerCertPem)
    : identity_(identity), pinned_(std::move(pinnedServerCertPem)) {}

HttpResponse HttpClient::get(const HttpTarget& target, const std::string& pathAndQuery, const HttpOptions& options) const {
    ensureWinsock();
    Wait wait;
    wait.infinite = options.timeoutMs <= 0;
    wait.deadline = Clock::now() + std::chrono::milliseconds(std::max(0, options.timeoutMs));
    wait.cancel = options.cancel;
    if (target.tls && (!identity_ || pinned_.empty()))
        throw TransportError(TransportError::Kind::Tls, "HTTPS needs a client identity and a pinned server certificate");

    Socket socket(connectSocket(target.host, target.port, wait));

    std::unique_ptr<SSL_CTX, CtxFree> context;
    std::unique_ptr<SSL, SslFree> ssl;
    if (target.tls) {
        context = makeContext(*identity_);
        ssl.reset(SSL_new(context.get()));
        if (!ssl) throw std::bad_alloc();
        SSL_set_fd(ssl.get(), static_cast<int>(socket.handle));
        SSL_set_connect_state(ssl.get());
        for (;;) {
            const int result = SSL_connect(ssl.get());
            if (result == 1) break;
            const int error = SSL_get_error(ssl.get(), result);
            if (error == SSL_ERROR_WANT_READ) waitSocket(socket.handle, true, false, wait);
            else if (error == SSL_ERROR_WANT_WRITE) waitSocket(socket.handle, false, true, wait);
            else throw TransportError(TransportError::Kind::Tls, "TLS handshake failed");
        }
        std::unique_ptr<X509, X509Free> peer(SSL_get1_peer_certificate(ssl.get()));
        std::unique_ptr<BIO, BioFree> pinBio(BIO_new_mem_buf(pinned_.data(), int(pinned_.size())));
        std::unique_ptr<X509, X509Free> pin(pinBio ? PEM_read_bio_X509(pinBio.get(), nullptr, nullptr, nullptr) : nullptr);
        if (!peer || !pin || X509_cmp(peer.get(), pin.get()) != 0)
            throw TransportError(TransportError::Kind::Tls, "the server certificate does not match the paired one");
    }

    const bool defaultPort = (target.tls && target.port == 443) || (!target.tls && target.port == 80);
    const bool ipv6 = target.host.find(':') != std::string::npos;
    std::string hostHeader = ipv6 ? "[" + target.host + "]" : target.host;
    if (!defaultPort) hostHeader += ":" + std::to_string(target.port);
    const std::string request = "GET " + pathAndQuery + " HTTP/1.1\r\nHost: " + hostHeader +
        "\r\nConnection: close\r\nAccept: */*\r\nUser-Agent: Veyra\r\n\r\n";

    Connection connection(socket.handle, ssl.get(), wait);
    connection.writeAll(request);
    return readResponse(connection, options.maxBodyBytes);
}

} // namespace veyra::moonlight
