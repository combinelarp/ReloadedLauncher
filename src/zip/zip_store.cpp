#include "zip_store.hpp"
#include "../libzip/lib/zip.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
#include <string>

namespace {

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    if (n) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

}  // namespace

namespace ziputil {

bool CreateStored(const std::wstring& zip_path,
                  const std::vector<std::pair<std::wstring, std::wstring>>& files) {
    std::string path_u8 = WideToUtf8(zip_path);
    int err = 0;
    zip_t* za = zip_open(path_u8.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    if (!za) {
        return false;
    }

    bool success = true;
    for (const auto& entry : files) {
        std::string local_path_u8 = WideToUtf8(entry.first);
        std::string arc_name_u8 = WideToUtf8(entry.second);

        zip_source_t* src = zip_source_file(za, local_path_u8.c_str(), 0, -1);
        if (!src) {
            success = false;
            break;
        }

        if (zip_file_add(za, arc_name_u8.c_str(), src, ZIP_FL_OVERWRITE) < 0) {
            zip_source_free(src);
            success = false;
            break;
        }

        zip_int64_t idx = zip_name_locate(za, arc_name_u8.c_str(), 0);
        if (idx >= 0) {
            zip_set_file_compression(za, (zip_uint64_t)idx, ZIP_CM_STORE, 0);
        }
    }

    if (zip_close(za) < 0) {
        success = false;
    }

    return success;
}

}  // namespace ziputil