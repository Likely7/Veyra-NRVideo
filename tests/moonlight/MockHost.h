// SPDX-License-Identifier: GPL-3.0-only
// A fake Sunshine host on loopback for the Moonlight tests: serverinfo, applist, launch and the host side
// of the five-step pairing (with real TLS and client certificate pinning). It shares the client's protocol
// understanding, so it proves the client is self-consistent, not that a real host accepts it.
#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>

#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "veyra/moonlight/Client.h"
#include "veyra/moonlight/Crypto.h"
#include "veyra/moonlight/Xml.h"

namespace veyra::moonlight::mock {

// ---------------------------------------------------------------- fixtures

inline const char* kServerInfoXml =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<root status_code=\"200\"><hostname>DESKTOP-TEST</hostname><appversion>7.1.431.-1</appversion>"
    "<GfeVersion>3.23.0.74</GfeVersion><uniqueid>ABCD1234EF567890</uniqueid><HttpsPort>48000</HttpsPort>"
    "<ExternalPort>47999</ExternalPort><MaxLumaPixelsHEVC>1869449984</MaxLumaPixelsHEVC><mac>AA:BB:CC:DD:EE:FF</mac>"
    "<LocalIP>192.168.1.20</LocalIP><ServerCodecModeSupport>197377</ServerCodecModeSupport>"
    "<SupportedDisplayMode><DisplayMode><Width>3840</Width><Height>2160</Height><RefreshRate>60</RefreshRate></DisplayMode>"
    "<DisplayMode><Width>1920</Width><Height>1080</Height><RefreshRate>60</RefreshRate></DisplayMode></SupportedDisplayMode>"
    "<PairStatus>1</PairStatus><currentgame>7</currentgame><state>SUNSHINE_SERVER_BUSY</state></root>";

inline const char* kAppListXml =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<root status_code=\"200\">"
    "<App><IsHdrSupported>1</IsHdrSupported><AppTitle>Desktop</AppTitle><ID>881448767</ID></App>"
    "<App><IsHdrSupported>0</IsHdrSupported><AppTitle>Tom &amp; Jerry &lt;Deluxe&gt; &#x4E2D;</AppTitle><ID>12</ID></App>"
    "<App><AppTitle/><ID>13</ID></App>"
    "</root>";

// ---------------------------------------------------------------- mock host

inline std::string hexOf(const std::string& bytes) { return xml::toHex(bytes); }

struct Query {
    std::string command;
    std::map<std::string, std::string> args;
};

inline Query parseRequestLine(const std::string& request) {
    Query q;
    const auto space1 = request.find(' ');
    const auto space2 = request.find(' ', space1 + 1);
    std::string target = request.substr(space1 + 1, space2 - space1 - 1);
    const auto mark = target.find('?');
    q.command = target.substr(1, mark == std::string::npos ? std::string::npos : mark - 1);
    if (mark != std::string::npos) {
        size_t pos = mark + 1;
        while (pos < target.size()) {
            const auto amp = target.find('&', pos);
            const std::string pair = target.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
            const auto eq = pair.find('=');
            q.args[pair.substr(0, eq)] = eq == std::string::npos ? "" : pair.substr(eq + 1);
            pos = amp == std::string::npos ? target.size() : amp + 1;
        }
    }
    return q;
}

class MockHost {
public:
    explicit MockHost(const std::string& pin) : pin_(pin) {
        identity = createIdentity();
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
        httpListen_ = listenOnLoopback(httpPort);
        httpsListen_ = listenOnLoopback(httpsPort);
        ctx_ = SSL_CTX_new(TLS_server_method());
        BIO* certBio = BIO_new_mem_buf(identity.certPem.data(), int(identity.certPem.size()));
        BIO* keyBio = BIO_new_mem_buf(identity.keyPem.data(), int(identity.keyPem.size()));
        X509* cert = PEM_read_bio_X509(certBio, nullptr, nullptr, nullptr);
        EVP_PKEY* key = PEM_read_bio_PrivateKey(keyBio, nullptr, nullptr, nullptr);
        SSL_CTX_use_certificate(ctx_, cert);
        SSL_CTX_use_PrivateKey(ctx_, key);
        X509_free(cert); EVP_PKEY_free(key); BIO_free(certBio); BIO_free(keyBio);
        SSL_CTX_set_verify(ctx_, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
        // Only the client certificate that completed pairing may use HTTPS.
        SSL_CTX_set_cert_verify_callback(ctx_, [](X509_STORE_CTX* store, void* arg) -> int {
            auto* self = static_cast<MockHost*>(arg);
            X509* peer = X509_STORE_CTX_get0_cert(store);
            unsigned char* der = nullptr;
            const int length = i2d_X509(peer, &der);
            std::string presented(reinterpret_cast<char*>(der), size_t(length));
            OPENSSL_free(der);
            std::lock_guard lock(self->mutex_);
            return !self->pairedClientDer_.empty() && presented == self->pairedClientDer_ ? 1 : 0;
        }, this);
        httpThread_ = std::thread([this] { acceptLoop(httpListen_, false); });
        httpsThread_ = std::thread([this] { acceptLoop(httpsListen_, true); });
    }

    ~MockHost() {
        stop_ = true;
        closesocket(httpListen_);
        closesocket(httpsListen_);
        if (httpThread_.joinable()) httpThread_.join();
        if (httpsThread_.joinable()) httpsThread_.join();
        SSL_CTX_free(ctx_);
    }

    void enterPin() { pinEntered_ = true; }
    // The PIN the user would type on the host; the pairing step that derives the key waits for it.
    void setPin(const std::string& pin) {
        { std::lock_guard lock(mutex_); pin_ = pin; }
        pinEntered_ = true;
    }
    std::atomic<int> currentGame{7};
    void forgetPairing() {
        std::lock_guard lock(mutex_);
        pairedClientDer_.clear();
    }
    void presentDifferentCertificate() {
        // Swap to a new identity the client has never pinned.
        Identity other = createIdentity();
        BIO* certBio = BIO_new_mem_buf(other.certPem.data(), int(other.certPem.size()));
        BIO* keyBio = BIO_new_mem_buf(other.keyPem.data(), int(other.keyPem.size()));
        X509* cert = PEM_read_bio_X509(certBio, nullptr, nullptr, nullptr);
        EVP_PKEY* key = PEM_read_bio_PrivateKey(keyBio, nullptr, nullptr, nullptr);
        SSL_CTX_use_certificate(ctx_, cert);
        SSL_CTX_use_PrivateKey(ctx_, key);
        X509_free(cert); EVP_PKEY_free(key); BIO_free(certBio); BIO_free(keyBio);
    }

    Identity identity;
    uint16_t httpPort = 0, httpsPort = 0;
    std::atomic<bool> busyPairing{false};
    std::atomic<int> unpairCalls{0};
    std::atomic<int> launchCalls{0};
    std::string lastLaunch;   // guarded by mutex_

    std::string launchQuery() { std::lock_guard lock(mutex_); return lastLaunch; }

private:
    SOCKET listenOnLoopback(uint16_t& port) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        bind(s, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        listen(s, 8);
        int length = sizeof(address);
        getsockname(s, reinterpret_cast<sockaddr*>(&address), &length);
        port = ntohs(address.sin_port);
        return s;
    }

    void acceptLoop(SOCKET listener, bool tls) {
        while (!stop_) {
            SOCKET client = accept(listener, nullptr, nullptr);
            if (client == INVALID_SOCKET) return;
            std::thread([this, client, tls] { serve(client, tls); }).detach();
        }
    }

    void serve(SOCKET client, bool tls) {
        SSL* ssl = nullptr;
        if (tls) {
            ssl = SSL_new(ctx_);
            SSL_set_fd(ssl, static_cast<int>(client));
            if (SSL_accept(ssl) != 1) { SSL_free(ssl); closesocket(client); return; }
        }
        auto readBytes = [&](char* buffer, int size) { return ssl ? SSL_read(ssl, buffer, size) : recv(client, buffer, size, 0); };
        auto writeBytes = [&](const std::string& data) {
            size_t sent = 0;
            while (sent < data.size()) {
                const int n = ssl ? SSL_write(ssl, data.data() + sent, int(data.size() - sent)) : send(client, data.data() + sent, int(data.size() - sent), 0);
                if (n <= 0) return;
                sent += size_t(n);
            }
        };
        std::string request;
        char chunk[2048];
        while (request.find("\r\n\r\n") == std::string::npos) {
            const int n = readBytes(chunk, sizeof(chunk));
            if (n <= 0) break;
            request.append(chunk, size_t(n));
        }
        if (request.find("\r\n\r\n") != std::string::npos) {
            const Query q = parseRequestLine(request);
            std::string body = route(q, tls);
            writeBytes(respond(q, tls, body));
        }
        if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); }
        closesocket(client);
    }

    // The three framings a real host uses must all parse: content-length, chunked, close-delimited.
    std::string respond(const Query& q, bool tls, const std::string& body) {
        if (!tls && q.command == "serverinfo") {
            std::string out = "HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n";
            const size_t half = body.size() / 2;
            char size[16];
            std::snprintf(size, sizeof(size), "%zx\r\n", half);
            out += size + body.substr(0, half) + "\r\n";
            std::snprintf(size, sizeof(size), "%zx\r\n", body.size() - half);
            out += size + body.substr(half) + "\r\n0\r\n\r\n";
            return out;
        }
        if (q.command == "applist") return "HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\nConnection: close\r\n\r\n" + body;
        return "HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    }

    static std::string ok(const std::string& inner) { return "<?xml version=\"1.0\" encoding=\"utf-8\"?><root status_code=\"200\">" + inner + "</root>"; }
    static std::string fail(int code, const std::string& message) {
        return "<?xml version=\"1.0\" encoding=\"utf-8\"?><root status_code=\"" + std::to_string(code) + "\" status_message=\"" + message + "\"></root>";
    }

    std::string route(const Query& q, bool tls) {
        std::lock_guard lock(mutex_);
        if (q.command == "serverinfo") {
            std::string info = kServerInfoXml;
            // Report the real ports and the real pairing state of this mock.
            const auto replace = [&](const std::string& from, const std::string& to) { const auto at = info.find(from); if (at != std::string::npos) info.replace(at, from.size(), to); };
            replace("<HttpsPort>48000</HttpsPort>", "<HttpsPort>" + std::to_string(httpsPort) + "</HttpsPort>");
            replace("<currentgame>7</currentgame>", "<currentgame>" + std::to_string(currentGame.load()) + "</currentgame>");
            replace("<PairStatus>1</PairStatus>", std::string("<PairStatus>") + (pairedClientDer_.empty() ? "0" : "1") + "</PairStatus>");
            return info;
        }
        if (q.command == "applist") return tls ? kAppListXml : fail(401, "The client is not authorized");
        if (q.command == "unpair") { ++unpairCalls; pairedClientDer_.clear(); stage_ = 0; return ok(""); }
        if (q.command == "launch" || q.command == "resume") { ++launchCalls; lastLaunch = q.command; for (const auto& [k, v] : q.args) lastLaunch += "&" + k + "=" + v; return ok("<sessionUrl0>rtsp://127.0.0.1:48010</sessionUrl0><gamesession>1</gamesession>"); }
        if (q.command == "cancel") return ok("<cancel>1</cancel>");
        if (q.command == "appasset") return "\x89PNG-MOCK";
        if (q.command == "pair") return pair(q, tls);
        return fail(404, "unknown");
    }

    // The host side of the five stage pairing handshake.
    std::string pair(const Query& q, bool tls) {
        const bool sha256 = true;   // appversion 7.x
        const size_t hashLength = 32;
        const auto get = [&](const char* name) { const auto it = q.args.find(name); return it == q.args.end() ? std::string() : it->second; };
        if (!tls && get("phrase") == "getservercert") {
            if (busyPairing) return ok("<paired>1</paired><plaincert></plaincert>");
            mutex_.unlock();   // the PIN is typed by a human; do not hold the lock while waiting
            for (int i = 0; i < 100 && !pinEntered_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
            mutex_.lock();
            if (!pinEntered_) return fail(500, "pin timeout");
            salt_ = *xml::fromHex(get("salt"));
            clientCertPem_ = *xml::fromHex(get("clientcert"));
            aesKey_ = crypto::digest(salt_ + pin_, sha256).substr(0, 16);
            stage_ = 1;
            return ok("<paired>1</paired><plaincert>" + hexOf(identity.certPem) + "</plaincert>");
        }
        if (!tls && !get("clientchallenge").empty() && stage_ == 1) {
            const std::string challenge = crypto::aesEcbDecrypt(*xml::fromHex(get("clientchallenge")), aesKey_);
            serverSecret_ = crypto::randomBytes(16);
            serverChallenge_ = crypto::randomBytes(16);
            const std::string hash = crypto::digest(challenge + crypto::certificateSignature(identity.certPem) + serverSecret_, sha256);
            stage_ = 2;
            return ok("<paired>1</paired><challengeresponse>" + hexOf(crypto::aesEcbEncrypt(hash + serverChallenge_, aesKey_)) + "</challengeresponse>");
        }
        if (!tls && !get("serverchallengeresp").empty() && stage_ == 2) {
            clientHash_ = crypto::aesEcbDecrypt(*xml::fromHex(get("serverchallengeresp")), aesKey_).substr(0, hashLength);
            stage_ = 3;
            return ok("<paired>1</paired><pairingsecret>" + hexOf(serverSecret_ + crypto::sign(identity, serverSecret_)) + "</pairingsecret>");
        }
        if (!tls && !get("clientpairingsecret").empty() && stage_ == 3) {
            const std::string blob = *xml::fromHex(get("clientpairingsecret"));
            const std::string clientSecret = blob.substr(0, 16);
            const bool signatureOk = crypto::verify(clientSecret, blob.substr(16), clientCertPem_);
            const bool hashOk = crypto::digest(serverChallenge_ + crypto::certificateSignature(clientCertPem_) + clientSecret, sha256) == clientHash_;
            if (!signatureOk || !hashOk) { stage_ = 0; return ok("<paired>0</paired>"); }
            pairedClientDer_ = crypto::certificateDer(clientCertPem_);
            stage_ = 4;
            return ok("<paired>1</paired>");
        }
        if (tls && get("phrase") == "pairchallenge") return ok("<paired>1</paired>");
        return fail(400, "unexpected pairing request");
    }

    std::string pin_;
    std::atomic<bool> pinEntered_{false};
    std::atomic<bool> stop_{false};
    SOCKET httpListen_ = INVALID_SOCKET, httpsListen_ = INVALID_SOCKET;
    SSL_CTX* ctx_ = nullptr;
    std::thread httpThread_, httpsThread_;
    std::mutex mutex_;
    int stage_ = 0;
    std::string salt_, aesKey_, clientCertPem_, pairedClientDer_, serverSecret_, serverChallenge_, clientHash_;
};

} // namespace veyra::moonlight::mock
