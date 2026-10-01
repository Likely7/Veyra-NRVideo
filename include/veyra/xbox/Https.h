// SPDX-License-Identifier: GPL-3.0-only
// HTTPS for the Xbox streaming services: Microsoft sign-in, Xbox Live token services and the
// game-streaming (gssv) endpoints. All of them are ordinary public HTTPS, so the Windows HTTP stack
// (WinHTTP) does the TLS and certificate checks with the system's roots and proxy settings.
// The transport is an interface so the sign-in and session logic can be tested with scripted replies.
#pragma once
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>

namespace veyra::xbox {

struct HttpsRequest {
    std::string method = "GET";
    std::string host;                          // "login.microsoftonline.com"
    std::string path;                          // "/consumers/oauth2/v2.0/devicecode"
    std::map<std::string, std::string> headers;
    std::string body;
    int timeoutMs = 15000;
};

struct HttpsResponse {
    int status = 0;
    std::string body;
};

struct NetworkError : std::runtime_error {
    explicit NetworkError(const std::string& message) : std::runtime_error(message) {}
};

// A non-2xx answer from a service; `body` is kept for the error details the services send.
struct ServiceError : std::runtime_error {
    int status;
    std::string body;
    ServiceError(int code, std::string text, const std::string& what)
        : std::runtime_error(what), status(code), body(std::move(text)) {}
};

class HttpsTransport {
public:
    virtual ~HttpsTransport() = default;
    // Throws NetworkError for connection/TLS/timeout failures; any HTTP status is returned.
    virtual HttpsResponse send(const HttpsRequest& request) = 0;
};

// WinHTTP implementation. `cancel`, when set, aborts between steps of a request.
std::shared_ptr<HttpsTransport> makeWinHttpTransport(const std::atomic<bool>* cancel = nullptr);

// Form-urlencodes one value.
std::string urlEncode(const std::string& text);

} // namespace veyra::xbox
