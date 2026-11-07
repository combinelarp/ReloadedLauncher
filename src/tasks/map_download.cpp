#include "map_download.hpp"
#include "config.hpp"
#include "../net/http.hpp"
#include "../util/json.hpp"
#include "../zip/extract.hpp"
#include "../zip/zip_store.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cwctype>
#include <algorithm>

namespace maps {

namespace {

std::wstring ToWide(const std::string& utf8) {
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring w(n, 0);
    if (n)
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                            static_cast<int>(utf8.size()), &w[0], n);
    return w;
}

std::string JsonEscape(const std::wstring& w) {
    std::string utf8;
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
                                static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    utf8.resize(n);
    if (n)
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
                            static_cast<int>(w.size()), &utf8[0], n, nullptr, nullptr);
    std::string out = "\"";
    for (char c : utf8) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (static_cast<unsigned char>(c) < 0x20) {
            char buf[8];
            sprintf(buf, "\\u%04x", c);
            out += buf;
        } else out += c;
    }
    out += "\"";
    return out;
}

std::string JsonEscapeUtf8(const std::string& utf8) {
    std::string out = "\"";
    for (unsigned char c : utf8) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c < 0x20) {
            char buf[8];
            sprintf(buf, "\\u%04x", c);
            out += buf;
        } else out += c;
    }
    out += "\"";
    return out;
}

bool DirExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

void CreateDirs(const std::wstring& p) {
    std::wstring built;
    for (wchar_t c : p) {
        built += c;
        if (c == L'\\' && built.size() > 3)
            CreateDirectoryW(built.c_str(), nullptr);
    }
    CreateDirectoryW(p.c_str(), nullptr);
}

std::vector<std::wstring> SplitPathParts(const std::wstring& p) {
    std::vector<std::wstring> parts;
    std::wstring cur;
    for (wchar_t c : p) {
        if (c == L'\\' || c == L'/') {
            if (!cur.empty()) parts.push_back(cur);
            cur.clear();
        } else cur += c;
    }
    if (!cur.empty()) parts.push_back(cur);
    return parts;
}

void AppendTreeToZip(std::vector<std::pair<std::wstring, std::wstring>>& files,
                     const std::wstring& root_dir, const std::wstring& arc_prefix) {
    struct Stack { std::wstring dir; std::wstring arc; };
    std::vector<Stack> stack;
    stack.push_back({root_dir, arc_prefix});
    while (!stack.empty()) {
        Stack top = stack.back();
        stack.pop_back();
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((top.dir + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                continue;
            std::wstring full = top.dir + L"\\" + fd.cFileName;
            std::wstring arc = top.arc.empty() ? fd.cFileName
                                               : top.arc + L"/" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                stack.push_back({full, arc});
            else
                files.emplace_back(full, arc);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}

bool AnyEntries(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool any = false;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        any = true;
        break;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return any;
}

void PackDebugZip(const std::wstring& mid_dir, const StatusFn& status) {
    if (!DirExists(mid_dir) || !AnyEntries(mid_dir)) return;
    std::wstring debug_dir = mid_dir + L"\\map_debug";
    CreateDirs(debug_dir);
    std::wstring zip_path = debug_dir + L"\\map-debug.zip";
    HANDLE ef = CreateFileW(zip_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, 0, nullptr);
    if (ef != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER sz;
        bool nonempty = GetFileSizeEx(ef, &sz) && sz.QuadPart > 0;
        CloseHandle(ef);
        if (nonempty) return;
    }
    DeleteFileW(zip_path.c_str());
    status(L"Packing map-debug.zip");
    std::vector<std::pair<std::wstring, std::wstring>> files;
    AppendTreeToZip(files, mid_dir, L"");
    std::vector<std::pair<std::wstring, std::wstring>> kept;
    for (auto& p : files) {
        size_t cut = p.second.find(L'/');
        std::wstring head = cut == std::wstring::npos ? p.second : p.second.substr(0, cut);
        if (head == L"map_debug") continue;
        kept.push_back(p);
    }
    zip::CreateStored(zip_path, kept);
}

}  // namespace

void EnsureDownloaded(const std::wstring& game_id,
                      const std::wstring& fallback_map_name,
                      const StatusFn& status, std::wstring* error) {
    if (game_id.empty()) return;
    std::wstring map_name = fallback_map_name;

    // 1) presence check via games API (also returns canonical mapName)
    net::Result info = net::Get(std::wstring(cfg::kGameApiUrl) + game_id, nullptr);
    if (info.ok && info.status / 100 == 2) {
        std::string utf8(reinterpret_cast<const char*>(info.body.data()), info.body.size());
        json::Value root = json::Parse(utf8);
        const json::Value* mp = root.FindKey(L"mapName");
        if (mp && mp->type == json::Value::kString && !mp->str.empty())
            map_name = mp->str;
    }

    status(L"Downloading Map...");
    
    // Gid/mid preparation
    std::wstring game_ref = game_id;
    int n = WideCharToMultiByte(CP_UTF8, 0, game_ref.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string gid_utf8(n - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, game_ref.c_str(), -1, &gid_utf8[0], n - 1, nullptr, nullptr);
    
    n = WideCharToMultiByte(CP_UTF8, 0, map_name.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string map_utf8(n - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, map_name.c_str(), -1, &map_utf8[0], n - 1, nullptr, nullptr);
    
    std::string body = "{\"gameId\":" + JsonEscapeUtf8(gid_utf8) +
                       ",\"id\":" + JsonEscapeUtf8(gid_utf8) +
                       ",\"gameType\":" + JsonEscapeUtf8(gid_utf8) +
                       ",\"mapName\":" + JsonEscapeUtf8(map_utf8) + "}";
    net::Result dispatch = net::PostJson(cfg::kDispatchUrl, body);
    if (!dispatch.ok || dispatch.status >= 400) {
        std::wstring query = std::wstring(L"?gameId=") + game_id;
        dispatch = net::Get(std::wstring(cfg::kDispatchUrl) + query, nullptr);
    }
    if (!dispatch.ok || dispatch.status >= 400) {
        if (error) *error = L"dispatch failed (HTTP " +
                            cfg::ToWString(dispatch.status) + L")";
        return;
    }

    json::Value dispatch_root = json::Parse(
        std::string(reinterpret_cast<const char*>(dispatch.body.data()),
                    dispatch.body.size()));

    const json::Value* downurl = dispatch_root.FindKey(L"downurl");
    if (!downurl || downurl->type != json::Value::kString ||
        downurl->str.empty()) {
        if (error) *error = L"No map download server";
        return;
    }
    std::wstring down_url = downurl->str;

    // mid: dispatch mid / mname / mapName / map, else mapName tail, else zip stem
    std::wstring mid;
    for (const wchar_t* key : {L"mid", L"mname", L"mapName", L"map"}) {
        const json::Value* v = dispatch_root.FindKey(key);
        if (v && v->type == json::Value::kString && !v->str.empty()) {
            mid = v->str;
            break;
        }
    }
    if (mid.empty()) {
        // tail of the ScriptSetting map name (maps/g1048/m1048_1 -> m1048_1)
        auto parts = SplitPathParts(map_name);
        if (parts.size() >= 3) mid = parts[2];
        else if (parts.size() == 2) mid = parts[1];
    }
    if (mid.empty()) {
        auto parts = SplitPathParts(down_url);
        std::wstring zip_name = parts.empty() ? L"" : parts.back();
        size_t q = zip_name.find(L'?');
        if (q != std::wstring::npos) zip_name.resize(q);
        size_t dot = zip_name.find(L'.');
        if (dot != std::wstring::npos) zip_name.resize(dot);
        mid = zip_name;
    }
    if (mid.empty()) {
        if (error) *error = L"No map name server";
        return;
    }
    auto mid_parts = SplitPathParts(mid);
    mid = mid_parts.empty() ? mid : mid_parts.back();

    // gid: dispatch "type" else 2nd part of mapName (maps/g1048/...) else group
    std::wstring gid;
    const json::Value* typev = dispatch_root.FindKey(L"type");
    if (typev && typev->type == json::Value::kString && !typev->str.empty())
        gid = typev->str;
    if (gid.empty()) {
        auto parts = SplitPathParts(map_name);
        if (parts.size() >= 3 && lstrcmpW(parts[0].c_str(), L"maps") == 0)
            gid = parts[1];
    }
    if (gid.empty()) gid = game_id;
    auto gid_parts = SplitPathParts(gid);
    gid = gid_parts.empty() ? gid : gid_parts.back();

    std::wstring maps_root = cfg::MapsRoot();
    std::wstring gid_dir = maps_root + L"\\" + gid;
    std::wstring mid_dir = gid_dir + L"\\" + mid;

    if (DirExists(mid_dir) && AnyEntries(mid_dir)) {
        PackDebugZip(mid_dir, status);
        status(L"Map already downloaded");
        return;
    }

    status(L"Downloading Map...");
    CreateDirs(gid_dir);
    net::Result map_res = net::Get(down_url, nullptr, 60000);
    if (!map_res.ok || map_res.status / 100 != 2) {
        if (error)
            *error = L"Map download failed (HTTP " + cfg::ToWString(map_res.status) + L")";
        return;
    }

    status(L"Extracting Map...");
    std::wstring staging = maps_root + L"\\_staging_" + mid + L".zip";
    if (!shell::WriteTempFile(maps_root, (std::wstring(L"_staging_") + mid + L".zip").c_str(),
                              map_res.body.data(), map_res.body.size(), &staging)) {
        if (error) *error = L"Failed to write staging zip";
        return;
    }

    // if the zip root folder is named like mid, extract one level up
    std::wstring err;
    bool root_named_mid = false;
    // cheap check: peek at first local file header name using a temp read
    {
        HANDLE f = CreateFileW(staging.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            uint8_t hdr[32];
            DWORD read = 0;
            const DWORD kLocalSig = 0x04034B50;
            DWORD sig = 0;
            // find first local header signature
            SetFilePointer(f, 0, nullptr, FILE_BEGIN);
            ReadFile(f, hdr, 4, &read, nullptr);
            if (read == 4) {
                sig = hdr[0] | (hdr[1] << 8) | (hdr[2] << 16) |
                      (static_cast<DWORD>(hdr[3]) << 24);
                if (sig == kLocalSig) {
                    LARGE_INTEGER off;
                    off.QuadPart = 30;  // skip to name
                    DWORD name_len = 0, extra_len = 0;
                    uint8_t meta[26];
                    ReadFile(f, meta, 26, &read, nullptr);
                    if (read == 26) {
                        name_len = meta[24] | (meta[25] << 8);
                        std::string name(name_len, ' ');
                        ReadFile(f, &name[0], name_len, &read, nullptr);
                        if (read == name_len) {
                            int wlen = MultiByteToWideChar(CP_UTF8, 0, name.c_str(),
                                                           static_cast<int>(name.size()),
                                                           nullptr, 0);
                            std::wstring wname(wlen, 0);
                            MultiByteToWideChar(CP_UTF8, 0, name.c_str(),
                                                static_cast<int>(name.size()),
                                                &wname[0], wlen);
                            auto parts = SplitPathParts(wname);
                            if (!parts.empty() &&
                                lstrcmpW(parts[0].c_str(), mid.c_str()) == 0)
                                root_named_mid = true;
                        }
                    }
                }
            }
            CloseHandle(f);
        }
    }

    std::wstring dest = root_named_mid ? gid_dir : mid_dir;
    CreateDirs(dest);
    if (!shell::ExtractZip(staging, dest, &err)) {
        DeleteFileW(staging.c_str());
        if (error) *error = err;
        return;
    }
    CreateDirs(mid_dir);

    std::wstring debug_dir = mid_dir + L"\\map_debug";
    CreateDirs(debug_dir);
    if (!shell::ExtractZip(staging, debug_dir, &err) && error)
        *error = err;  // duplicate extraction failure is non-fatal
    DeleteFileW(staging.c_str());
    PackDebugZip(mid_dir, status);
    status(L"Map downloaded");
}

}  // namespace maps
