#include "extract.hpp"
#include "config.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#include <vector>
#include <string>

namespace shell {

bool WriteTempFile(const std::wstring& dest_dir, const wchar_t* name,
                   const unsigned char* data, size_t len,
                   std::wstring* path_out) {
    CreateDirectoryW(dest_dir.c_str(), nullptr);
    std::wstring path = dest_dir + L"\\" + name;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = data == nullptr || len == 0 ||
              WriteFile(f, data, static_cast<DWORD>(len), &written, nullptr);
    CloseHandle(f);
    if (ok && path_out) *path_out = path;
    return ok && written == len;
}

int ExtractZipTo(const std::wstring& zip_path, const std::wstring& dest_dir) {
    // -Force keeps behavior identical to Zigzag's extractall (overwrite-in-place).
    std::wstring cmd;
    cmd += L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"";
    cmd += L"Expand-Archive -LiteralPath '";
    cmd += zip_path;
    cmd += L"' -DestinationPath '";
    cmd += dest_dir;
    cmd += L"' -Force\"";

    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    HANDLE out_rd = nullptr, out_wr = nullptr;
    if (CreatePipe(&out_rd, &out_wr, &sa, 0)) {
        SetHandleInformation(out_rd, HANDLE_FLAG_INHERIT, 0);
    }

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (out_rd) { si.dwFlags |= STARTF_USESTDHANDLES; si.hStdOutput = out_wr; si.hStdError = out_wr; }

    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> cmdline(cmd.begin(), cmd.end());
    cmdline.push_back(L'\0');
    BOOL started = CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr,
                                  out_rd != nullptr ? TRUE : FALSE, CREATE_NO_WINDOW,
                                  nullptr, nullptr, &si, &pi);
    if (!started) {
        if (out_rd) CloseHandle(out_rd);
        if (out_wr) CloseHandle(out_wr);
        return -1;
    }
    if (out_wr) CloseHandle(out_wr);

    std::string log;
    if (out_rd) {
        char buf[512];
        DWORD got = 0;
        while (ReadFile(out_rd, buf, sizeof(buf) - 1, &got, nullptr) && got) {
            buf[got] = 0;
            log += buf;
        }
        CloseHandle(out_rd);
    }
    WaitForSingleObject(pi.hProcess, 10 * 60 * 1000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(code);
}

bool ExtractZip(const std::wstring& zip_path, const std::wstring& dest_dir,
                std::wstring* err) {
    CreateDirectoryW(dest_dir.c_str(), nullptr);
    int rc = ExtractZipTo(zip_path, dest_dir);
    if (rc != 0) {
        if (err) *err = L"Expand-Archive failed with code " + cfg::ToWString(rc);
        return false;
    }
    return true;
}

}  // namespace shell
