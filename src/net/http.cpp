#include "http.hpp"
#include "config.hpp"

#include <wininet.h>
#include <windows.h>
#include <string>
#include <vector>

#pragma comment(lib, "wininet.lib")

namespace net {

namespace {

struct UrlBits {
    std::wstring host;
    INTERNET_PORT port = 80;
    std::wstring path;
    bool https = false;
};

// Turns the last Win32/WinINet error into a message the launcher can show.
std::wstring LastErrorText() {
    DWORD e = GetLastError();
    if (!e) return L"network error";
    wchar_t buf[256] = {0};
    if (!FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                        nullptr, e, 0, buf, 255, nullptr))
        return L"network error " + cfg::ToWString(e);
    std::wstring msg = buf;
    while (!msg.empty() && (msg.back() == L'\r' || msg.back() == L'\n' || msg.back() == L' '))
        msg.pop_back();
    if (msg.empty()) return L"network error " + cfg::ToWString(e);
    return msg;
}

// Without these WinINet blocks forever on a stalled server, which looks like
// an endless 0% download.
void ApplyTimeouts(HINTERNET h, unsigned timeout_ms) {
    DWORD ms = timeout_ms ? static_cast<DWORD>(timeout_ms) : 30000;
    InternetSetOptionW(h, INTERNET_OPTION_CONNECT_TIMEOUT, &ms, sizeof(ms));
    InternetSetOptionW(h, INTERNET_OPTION_SEND_TIMEOUT, &ms, sizeof(ms));
    InternetSetOptionW(h, INTERNET_OPTION_RECEIVE_TIMEOUT, &ms, sizeof(ms));
}

bool Crack(const std::wstring& url, UrlBits* out) {
    wchar_t host[256] = {0};
    wchar_t path[2048] = {0};
    URL_COMPONENTSW uc = {};
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host;
    uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2047;
    if (!InternetCrackUrlW(url.c_str(), 0, ICU_DECODE, &uc)) return false;
    out->host = host;
    out->port = uc.nPort;
    out->path = path;
    out->https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    return true;
}

}  // namespace

Result Send(const Request& req, const ProgressFn& progress) {
    Result res;

    HINTERNET session = InternetOpenW(cfg::kUserAgent,
                                      INTERNET_OPEN_TYPE_PRECONFIG,
                                      nullptr, nullptr, 0);
    if (!session) {
        res.error = LastErrorText();
        return res;
    }
    ApplyTimeouts(session, req.timeout_ms);

    HINTERNET request = nullptr;
    do {
        // Hand WinINet the complete URL. Passing a cracked, scheme-less path
        // here makes it fail with ERROR_INTERNET_UNRECOGNIZED_SCHEME (12006)
        // for plain http URLs, and would also drop any query string.
        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_COOKIES;
        request = InternetOpenUrlW(session, req.url.c_str(), nullptr, 0, flags, 0);
        if (!request) {
            res.error = LastErrorText();
            break;
        }
        DWORD code = 0, size = sizeof(code);
        if (!HttpQueryInfoW(request, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                            &code, &size, 0)) {
            res.error = LastErrorText();
            break;
        }
        res.status = static_cast<int>(code);

        DWORD content_len = 0, cl_size = sizeof(content_len);
        HttpQueryInfoW(request, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER,
                       &content_len, &cl_size, 0);
        uint64_t total = content_len;
        uint64_t done = 0;
        res.body.reserve(total ? static_cast<size_t>(total) : 64 * 1024);
        uint8_t chunk[65536];
        uint64_t last_report = 0;
        bool read_failed = false;
        for (;;) {
            DWORD got = 0;
            if (!InternetReadFile(request, chunk, sizeof(chunk), &got)) {
                read_failed = true;
                break;
            }
            if (got == 0) break;
            res.body.insert(res.body.end(), chunk, chunk + got);
            done += got;
            // Throttle UI updates; one message per chunk floods the window.
            uint64_t now = GetTickCount64();
            if (progress && now - last_report >= 100) {
                last_report = now;
                progress(done, total);
            }
        }
        if (read_failed) {
            res.error = LastErrorText();
            break;
        }
        if (progress) progress(done, total);
        res.ok = true;
    } while (false);

    if (request) InternetCloseHandle(request);
    InternetCloseHandle(session);
    return res;
}

Result Get(const std::wstring& url, const ProgressFn& progress, unsigned timeout_ms) {
    Request req;
    req.method = Request::kGet;
    req.url = url;
    req.timeout_ms = timeout_ms;
    return Send(req, progress);
}

Result PostJson(const std::wstring& url, const std::string& body_json) {
    Result res;
    UrlBits u;
    if (!Crack(url, &u)) return res;
    HINTERNET session = InternetOpenW(cfg::kUserAgent,
                                      INTERNET_OPEN_TYPE_PRECONFIG,
                                      nullptr, nullptr, 0);
    if (!session) return res;
    HINTERNET connect = InternetConnectW(session, u.host.c_str(), u.port, 0, 0,
                                         INTERNET_SERVICE_HTTP, 0, 0);
    if (!connect) { InternetCloseHandle(session); return res; }
    ApplyTimeouts(connect, 30000);
    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_COOKIES;
    if (u.https) flags |= INTERNET_FLAG_SECURE;
    HINTERNET request = HttpOpenRequestW(connect, L"POST", u.path.c_str(), nullptr,
                                         nullptr, nullptr, flags, 0);
    if (!request) {
        InternetCloseHandle(connect);
        InternetCloseHandle(session);
        return res;
    }
    std::wstring headers = L"Content-Type: application/json\r\n";
    BOOL sent = HttpSendRequestW(request, headers.c_str(),
                                 static_cast<DWORD>(headers.size()),
                                 const_cast<char*>(body_json.data()),
                                 static_cast<DWORD>(body_json.size()));
    if (sent) {
        DWORD code = 0, size = sizeof(code);
        if (HttpQueryInfoW(request, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                           &code, &size, 0))
            res.status = static_cast<int>(code);
        uint8_t chunk[65536];
        DWORD got = 0;
        while (InternetReadFile(request, chunk, sizeof(chunk), &got) && got) {
            res.body.insert(res.body.end(), chunk, chunk + got);
        }
        res.ok = true;
    }
    InternetCloseHandle(request);
    InternetCloseHandle(connect);
    InternetCloseHandle(session);
    return res;
}

}  // namespace net
