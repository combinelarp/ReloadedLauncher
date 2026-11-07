#include "client_install.hpp"
#include "config.hpp"
#include "../net/http.hpp"
#include "../zip/extract.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#include <algorithm>
#include <cwctype>

#pragma comment(lib, "shlwapi.lib")

namespace fs {
    // std::filesystem is heavy to pull; small dir helpers suffice here.
    bool DirExists(const std::wstring& p) {
        DWORD a = GetFileAttributesW(p.c_str());
        return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
    }
    void CreateDirs(const std::wstring& p) {
        std::wstring built;
        for (wchar_t c : p) {
            if (c == L':' ) { built += c; continue; }
            built += c;
            if (built.size() >= 2 && iswalpha(built[0]) && built.substr(1, 1) == L":" )
                continue;
            if (c == L'\\' || c == L'/') CreateDirectoryW(built.c_str(), nullptr);
        }
        CreateDirectoryW(p.c_str(), nullptr);
    }
    bool FileExists(const std::wstring& p) {
        DWORD a = GetFileAttributesW(p.c_str());
        return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
    }
}  // namespace fs

namespace client {

bool FindGameExeIn(const std::wstring& root, std::wstring* found, bool* stop) {
    if (*stop) return true;
    WIN32_FIND_DATAW fd;
    std::wstring pattern = root + L"\\*.*";
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        std::wstring full = root + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (FindGameExeIn(full, found, stop)) {
                *stop = true;
                return true;
            }
        } else if (_wcsicmp(fd.cFileName, cfg::kGameExe) == 0) {
            *found = full;
            *stop = true;
            return true;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return false;
}

bool FindGameExe(std::wstring* out_path) {
    bool stop = false;
    std::wstring found;
    bool hit = FindGameExeIn(cfg::ClientDir(), &found, &stop);
    if (!hit) hit = FindGameExeIn(cfg::BaseDir(), &found, &stop);
    if (hit && out_path) *out_path = found;
    return hit;
}

namespace {

bool HasRootGameExe() {
    if (fs::FileExists(std::wstring(cfg::BaseDir()) + L"\\" + cfg::kGameExe))
        return true;
    // Also accept a Game.exe inside any first-level folder.
    WIN32_FIND_DATAW fd;
    std::wstring pattern = cfg::BaseDir() + L"\\*";
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0 ||
            !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        if (lstrcmpW(fd.cFileName, cfg::kSysDirName) == 0) continue;
        std::wstring nested = std::wstring(cfg::BaseDir()) + L"\\" + fd.cFileName +
                              L"\\" + cfg::kGameExe;
        if (fs::FileExists(nested)) { FindClose(h); return true; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return false;
}

}  // namespace

bool Installed() { return FindGameExe(nullptr); }

void MigrateRootInstall() {
    const std::wstring base = cfg::BaseDir();
    const std::wstring client = cfg::ClientDir();
    if (!fs::DirExists(base)) return;
    if (Installed()) return;
    fs::CreateDirs(client);
    if (!HasRootGameExe()) return;

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((base + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L".." || lstrcmpW(name.c_str(), cfg::kSysDirName) == 0)
            continue;
        std::wstring src = base + L"\\" + name;
        std::wstring dst = client + L"\\" + name;
        if (fs::FileExists(dst) && !DeleteFileW(dst.c_str())) continue;
        if (!MoveFileW(src.c_str(), dst.c_str()))
            fs::DirExists(src) ? RemoveDirectoryW(src.c_str()) : DeleteFileW(src.c_str());
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

bool DownloadAndInstall(const ProgressFn& progress, const StatusFn& status,
                        std::wstring* error) {
    fs::CreateDirs(cfg::ClientDir());
    status(L"Downloading Game...");

    net::ProgressFn net_progress = [&](uint64_t done, uint64_t total) {
        if (progress) progress(done, total);
    };
    if (progress) progress(0, 0);  // size unknown until the server answers
    net::Result res = net::Get(cfg::kClientZipUrl, net_progress, 60000);
    if (!res.ok || res.status / 100 != 2) {
        if (error) {
            if (!res.error.empty())
                *error = L"Download failed: " + res.error;
            else if (res.status > 0)
                *error = L"Download failed: HTTP " + cfg::ToWString(res.status);
            else
                *error = L"Download failed: no response";
        }
        return false;
    }
    status(L"Extracting Game...");
    if (progress) progress(0, 0);
    std::wstring temp_zip;
    if (!shell::WriteTempFile(cfg::BaseDir(), L"blockmango.zip", res.body.data(),
                              res.body.size(), &temp_zip)) {
        if (error) *error = L"Failed to write staging zip";
        return false;
    }
    std::wstring err;
    // Extract using libzip.
    if (!ziputil::Extract(temp_zip, cfg::ClientDir(), &err, net_progress)) {
        if (error) *error = err;
        DeleteFileW(temp_zip.c_str());
        return false;
    }
    DeleteFileW(temp_zip.c_str());
    return true;
}

}  // namespace client
