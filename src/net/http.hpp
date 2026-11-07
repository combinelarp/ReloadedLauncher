#pragma once
// Minimal WinHTTP wrapper: GET / POST-json with progress callbacks.

#include <functional>
#include <string>
#include <vector>
#include <cstdint>

namespace net {

struct Request {
    enum Method { kGet, kPost } method = kGet;
    std::wstring url;
    std::string body;      // sent when kPost
    std::wstring content_type = L"application/json";
    unsigned timeout_ms = 30000;
};

using ProgressFn = std::function<void(uint64_t already, uint64_t total)>;

struct Result {
    bool ok = false;
    int status = 0;
    std::wstring error;         // human readable reason when ok is false
    std::vector<uint8_t> body;  // raw response bytes
};

// Synchronous request. progress (optional) is invoked while streaming down.
Result Send(const Request& req, const ProgressFn& progress = nullptr);

// Convenience: GET and return body as text (UTF-8).
Result Get(const std::wstring& url, const ProgressFn& progress = nullptr,
           unsigned timeout_ms = 30000);

// Convenience: POST application/json.
Result PostJson(const std::wstring& url, const std::string& body_json);

}  // namespace net
