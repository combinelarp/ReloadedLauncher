#include "extract.hpp"
#include "config.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#include <algorithm>
#include <cstdint>
#include <vector>
#include <string>

namespace shell {

namespace {

bool ReadAt(HANDLE file, uint64_t offset, void* buf, DWORD len) {
    LARGE_INTEGER pos;
    pos.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(file, pos, nullptr, FILE_BEGIN)) return false;
    DWORD got = 0;
    return ReadFile(file, buf, len, &got, nullptr) && got == len;
}

// Sums the uncompressed sizes from the ZIP central directory, so extraction can
// be shown as a percentage instead of an endless unknown.
uint64_t ZipUncompressedSize(const std::wstring& zip_path) {
    HANDLE f = CreateFileW(zip_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return 0;

    uint64_t total = 0;
    LARGE_INTEGER fsize;
    if (GetFileSizeEx(f, &fsize) && fsize.QuadPart > 22) {
        DWORD tail_len = static_cast<DWORD>(std::min<LONGLONG>(fsize.QuadPart, 128 * 1024));
        std::vector<unsigned char> tail(tail_len);
        uint64_t tail_off = static_cast<uint64_t>(fsize.QuadPart) - tail_len;
        if (ReadAt(f, tail_off, tail.data(), tail_len)) {
            // End of central directory record: PK\x05\x06 + 18 bytes of fields.
            int i = static_cast<int>(tail_len) - 22;
            for (; i >= 0; --i) {
                if (tail[i] == 0x50 && tail[i + 1] == 0x4b &&
                    tail[i + 2] == 0x05 && tail[i + 3] == 0x06)
                    break;
            }
            if (i >= 0) {
                auto u32 = [&](int at) {
                    return static_cast<uint32_t>(tail[at]) |
                           (static_cast<uint32_t>(tail[at + 1]) << 8) |
                           (static_cast<uint32_t>(tail[at + 2]) << 16) |
                           (static_cast<uint32_t>(tail[at + 3]) << 24);
                };
                uint32_t count = static_cast<uint32_t>(tail[i + 10]) | (static_cast<uint32_t>(tail[i + 11]) << 8);
                uint32_t cd_size = u32(i + 12);
                uint32_t cd_off = u32(i + 16);
                if (cd_size > 0 && cd_size <= 64u * 1024 * 1024) {
                    std::vector<unsigned char> cd(cd_size);
                    if (ReadAt(f, cd_off, cd.data(), cd_size)) {
                        size_t p = 0;
                        for (uint32_t n = 0; n < count && p + 46 <= cd.size(); ++n) {
                            if (!(cd[p] == 0x50 && cd[p + 1] == 0x4b &&
                                  cd[p + 2] == 0x01 && cd[p + 3] == 0x02))
                                break;
                            uint32_t usize = static_cast<uint32_t>(cd[p + 24]) |
                                             (static_cast<uint32_t>(cd[p + 25]) << 8) |
                                             (static_cast<uint32_t>(cd[p + 26]) << 16) |
                                             (static_cast<uint32_t>(cd[p + 27]) << 24);
                            uint32_t nlen = static_cast<uint32_t>(cd[p + 28]) | (static_cast<uint32_t>(cd[p + 29]) << 8);
                            uint32_t elen = static_cast<uint32_t>(cd[p + 30]) | (static_cast<uint32_t>(cd[p + 31]) << 8);
                            uint32_t clen = static_cast<uint32_t>(cd[p + 32]) | (static_cast<uint32_t>(cd[p + 33]) << 8);
                            if (usize != 0xFFFFFFFFu) total += usize;
                            p += 46 + nlen + elen + clen;
                        }
                    }
                }
            }
        }
    }
    CloseHandle(f);
    return total;
}

// Total size of everything already extracted into dir.
uint64_t DirSize(const std::wstring& dir) {
    uint64_t total = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        std::wstring full = dir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            total += DirSize(full);
        else
            total += (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return total;
}

void DrainPipe(HANDLE pipe) {
    if (!pipe) return;
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &avail, nullptr) || avail == 0)
            return;
        char buf[4096];
        DWORD want = avail < sizeof(buf) ? avail : static_cast<DWORD>(sizeof(buf));
        DWORD got = 0;
        if (!ReadFile(pipe, buf, want, &got, nullptr) || got == 0) return;
    }
}

}  // namespace

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

int ExtractZipTo(const std::wstring& zip_path, const std::wstring& dest_dir,
                 const ProgressFn& progress) {
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

    UINT64 total = progress ? ZipUncompressedSize(zip_path) : 0;
    for (;;) {
        DrainPipe(out_rd);
        if (progress) {
            uint64_t done = DirSize(dest_dir);
            if (total && done > total) done = total;
            progress(done, total);
        }
        DWORD wait = WaitForSingleObject(pi.hProcess, 250);
        if (wait != WAIT_TIMEOUT) break;
    }
    DrainPipe(out_rd);
    if (out_rd) CloseHandle(out_rd);

    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(code);
}

bool ExtractZip(const std::wstring& zip_path, const std::wstring& dest_dir,
                std::wstring* err, const ProgressFn& progress) {
    CreateDirectoryW(dest_dir.c_str(), nullptr);
    int rc = ExtractZipTo(zip_path, dest_dir, progress);
    if (rc != 0) {
        if (err) *err = L"Extract failed (code " + cfg::ToWString(rc) + L")";
        return false;
    }
    return true;
}

}  // namespace shell
