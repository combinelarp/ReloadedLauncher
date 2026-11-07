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
    UrlBits u;
    if (!Crack(req.url, &u)) return res;

    HINTERNET session = InternetOpenW(cfg::kUserAgent,
                                      INTERNET_OPEN_TYPE_PRECONFIG,
                                      nullptr, nullptr, 0);
    if (!session) return res;
    HINTERNET connect = InternetConnectW(session, u.host.c_str(), u.port, 0, 0,
                                         INTERNET_SERVICE_HTTP, 0, 0);
    HINTERNET request = nullptr;
    do {
        if (!connect) break;
        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_COOKIES;
        if (u.https) flags |= INTERNET_FLAG_SECURE;
        request = InternetOpenUrlW(connect, u.path.c_str(), nullptr, 0, flags, 0);
        if (!request) break;
        DWORD code = 0, size = sizeof(code);
        if (!HttpQueryInfoW(request, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                            &code, &size, 0)) break;
        res.status = static_cast<int>(code);

        DWORD content_len = 0, cl_size = sizeof(content_len);
        HttpQueryInfoW(request, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER,
                       &content_len, &cl_size, 0);
        uint64_t total = content_len;
        uint64_t done = 0;
        res.body.reserve(64 * 1024);
        uint8_t chunk[65536];
        for (;;) {
            DWORD got = 0;
            if (!InternetReadFile(request, chunk, sizeof(chunk), &got)) break;
            if (got == 0) break;
            res.body.insert(res.body.end(), chunk, chunk + got);
            done += got;
            if (progress && (done < 4096 || (done % (64 * 1024)) < 65536))
                progress(done, total);
        }
        if (progress) progress(done, total);
        res.ok = true;
    } while (false);

    if (request) InternetCloseHandle(request);
    if (connect) InternetCloseHandle(connect);
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
