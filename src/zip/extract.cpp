#include "extract.hpp"
#include "../libzip/lib/zip.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
#include <string>

namespace shell {

bool WriteTempFile(const std::wstring& dir, const wchar_t* name,
                   const void* data, size_t size, std::wstring* out_path) {
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring path = dir + L"\\" + name;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(h, data, static_cast<DWORD>(size), &written, nullptr);
    CloseHandle(h);
    if (!ok || written != size) {
        DeleteFileW(path.c_str());
        return false;
    }
    if (out_path) *out_path = path;
    return true;
}

}  // namespace shell

namespace {

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    if (n) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, 0);
    if (n) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

void CreateDirsForFile(const std::wstring& filepath) {
    std::wstring path = filepath;
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i] == L'/' || path[i] == L'\\') {
            path[i] = L'\\';
            std::wstring sub = path.substr(0, i);
            if (sub.size() > 2 && sub[sub.size() - 1] != L':') {
                CreateDirectoryW(sub.c_str(), nullptr);
            }
        }
    }
}

void EnsureDirExists(const std::wstring& dir) {
    std::wstring path = dir;
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i] == L'/' || path[i] == L'\\') {
            path[i] = L'\\';
            std::wstring sub = path.substr(0, i);
            if (sub.size() > 2 && sub[sub.size() - 1] != L':') {
                CreateDirectoryW(sub.c_str(), nullptr);
            }
        }
    }
    CreateDirectoryW(dir.c_str(), nullptr);
}

}  // namespace

namespace ziputil {

bool Extract(const std::wstring& zip_path, const std::wstring& dest_dir,
             std::wstring* error, const ProgressFn& progress) {
    std::string path_u8 = WideToUtf8(zip_path);
    int err = 0;
    zip_t* za = zip_open(path_u8.c_str(), ZIP_RDONLY, &err);
    if (!za) {
        if (error) *error = L"Failed to open zip archive (error " + std::to_wstring(err) + L")";
        return false;
    }

    zip_int64_t num_entries = zip_get_num_entries(za, 0);
    uint64_t total_bytes = 0;
    std::vector<zip_stat_t> stats(num_entries);
    for (zip_int64_t i = 0; i < num_entries; ++i) {
        zip_stat_init(&stats[i]);
        zip_stat_index(za, (zip_uint64_t)i, 0, &stats[i]);
        if (stats[i].valid & ZIP_STAT_SIZE) {
            total_bytes += stats[i].size;
        }
    }

    uint64_t extracted_bytes = 0;
    if (progress) progress(0, total_bytes);

    // Ensure dest_dir exists
    EnsureDirExists(dest_dir);

    bool success = true;
    for (zip_int64_t i = 0; i < num_entries; ++i) {
        const char* name = zip_get_name(za, (zip_uint64_t)i, 0);
        if (!name) continue;

        std::wstring rel_name = Utf8ToWide(name);
        // Normalize slashes
        for (auto& c : rel_name) {
            if (c == L'/') c = L'\\';
        }

        std::wstring full_path = dest_dir + L"\\" + rel_name;

        if (name[0] != '\0' && (name[strlen(name) - 1] == '/' || name[strlen(name) - 1] == '\\')) {
            CreateDirsForFile(full_path + L"\\dummy");
            continue;
        }

        CreateDirsForFile(full_path);

        zip_file_t* zf = zip_fopen_index(za, (zip_uint64_t)i, 0);
        if (!zf) {
            success = false;
            if (error) *error = L"Failed to open entry in zip: " + rel_name;
            break;
        }

        HANDLE hf = CreateFileW(full_path.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hf == INVALID_HANDLE_VALUE) {
            zip_fclose(zf);
            success = false;
            if (error) *error = L"Failed to create file on disk: " + full_path;
            break;
        }

        char buf[65536];
        zip_int64_t n;
        while ((n = zip_fread(zf, buf, sizeof(buf))) > 0) {
            DWORD written = 0;
            WriteFile(hf, buf, (DWORD)n, &written, nullptr);
            extracted_bytes += written;
            if (progress) progress(extracted_bytes, total_bytes);
        }

        CloseHandle(hf);
        zip_fclose(zf);
    }

    zip_close(za);
    return success;
}

}  // namespace ziputil