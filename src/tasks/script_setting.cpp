#include "script_setting.hpp"
#include "config.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>
#include <vector>

namespace script {

namespace {

bool ReadBytes(const std::wstring& path, std::string* out) {
    std::ifstream f(path.c_str(), std::ios::binary | std::ios::in);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    *out = ss.str();
    return true;
}

bool WriteBytes(const std::wstring& path, const std::string& bytes) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL w = TRUE;
    if (!bytes.empty())
        w = WriteFile(f, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(f);
    return w && written == bytes.size();
}

char DetectSep(const std::string& data) {
    // first non-empty line that isn't the ASCII header
    size_t pos = 0;
    while (pos < data.size()) {
        size_t eol = data.find('\n', pos);
        std::string line = data.substr(pos, (eol == std::string::npos ? data.size() : eol) - pos);
        pos = (eol == std::string::npos) ? data.size() : eol + 1;
        if (line.empty() || line == "\r") continue;
        if (line.compare(0, 8, "GameType") == 0) continue;
        if (line.find('\t') != std::string::npos) return '\t';
        if (line.find(',') != std::string::npos) return ',';
        return '\t';
    }
    return '\t';
}

}  // namespace

bool SameGameId(const std::wstring& row_game_id, const std::wstring& game_id) {
    if (row_game_id == game_id) return true;
    auto strip = [](std::wstring v) {
        while (!v.empty() && (v.front() == L'g' || v.front() == L'G' || v.front() == L'"'))
            v.erase(v.begin());
        while (!v.empty() && (v.back() == L'"' || iswspace(v.back())))
            v.pop_back();
        return v;
    };
    std::wstring row = strip(row_game_id);
    std::wstring ask = strip(game_id);
    if (row.empty() || ask.empty())
        return false;
    if (!std::all_of(row.begin(), row.end(), ::iswdigit)) return false;
    if (!std::all_of(ask.begin(), ask.end(), ::iswdigit)) return false;
    return row == ask;
}

bool FindPath(std::wstring* out_path) {
    const std::wstring candidates[] = {
        cfg::ScriptSettingPath(),
        cfg::LegacyScriptSettingPath(0),
        cfg::ScriptSettingFallback(),
        cfg::LegacyScriptSettingPath(1),
    };
    for (const std::wstring& path : candidates) {
        DWORD a = GetFileAttributesW(path.c_str());
        if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) {
            if (out_path) *out_path = path;
            return true;
        }
    }
    return false;
}

bool Update(const std::wstring& path,
            const std::wstring& game_id,
            const std::wstring& server_ip,
            const std::wstring& server_port,
            Row* out_row, std::wstring* error) {
    std::string data;
    if (!ReadBytes(path, &data) || data.empty()) {
        if (error) *error = L"ScriptSetting.csv missing or empty at " + path;
        return false;
    }
    char sep = DetectSep(data);

    std::vector<std::string> lines;
    {
        size_t pos = 0;
        while (pos <= data.size()) {
            size_t eol = data.find('\n', pos);
            if (eol == std::string::npos) eol = data.size();
            std::string line = data.substr(pos, eol - pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
            if (eol == data.size()) break;
            pos = eol + 1;
        }
    }

    std::string out;
    bool has_target = false;
    bool changed = false;
    for (std::string& line : lines) {
        if (line.empty()) { out += line; out += "\r\n"; continue; }
        std::vector<std::string> cols;
        size_t startp = 0;
        while (startp <= line.size()) {
            size_t cut = line.find(sep, startp);
            if (cut == std::string::npos) cut = line.size();
            cols.push_back(line.substr(startp, cut - startp));
            if (cut == line.size()) break;
            startp = cut + 1;
        }
        if (cols.size() < 7) { out += line; out += "\r\n"; continue; }

        // convert cols <-> wstring only where comparisons need it
        auto ToW = [](const std::string& s) {
            int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                        static_cast<int>(s.size()), nullptr, 0);
            std::wstring w(n, 0);
            if (n)
                MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                    static_cast<int>(s.size()), &w[0], n);
            return w;
        };
        std::wstring col0 = ToW(cols[0]);
        std::wstring col2 = ToW(cols[2]);
        bool is_target = !game_id.empty() &&
                         (SameGameId(col0, game_id) || SameGameId(col2, game_id));

        if (is_target) {
            auto ToBytes = [](const std::wstring& w) {
                int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
                                            static_cast<int>(w.size()),
                                            nullptr, 0, nullptr, nullptr);
                std::string s(n, 0);
                if (n)
                    WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
                                        static_cast<int>(w.size()),
                                        &s[0], n, nullptr, nullptr);
                return s;
            };
            if (!server_ip.empty()) cols[5] = ToBytes(server_ip);
            if (!server_port.empty()) cols[6] = ToBytes(server_port);
            const std::string& en = cols[3];
            bool en_digit = !en.empty() &&
                            std::all_of(en.begin(), en.end(),
                                        [](char c) { return c >= '0' && c <= '9'; });
            if (en_digit) cols[3] = "1";

            std::string joined;
            for (size_t i = 0; i < cols.size(); ++i) {
                if (i) joined += sep;
                joined += cols[i];
            }
            out += joined;
            out += "\r\n";
            changed = true;
            has_target = true;
            if (out_row) {
                out_row->game_type = ToW(cols[0]);
                out_row->group_dir = ToW(cols[1]);
                out_row->name = ToW(cols[2]);
                out_row->map_name = ToW(cols.size() > 4 ? cols[4] : "");
            }
        } else {
            const std::string& en = cols[3];
            bool en_digit = !en.empty() &&
                            std::all_of(en.begin(), en.end(),
                                        [](char c) { return c >= '0' && c <= '9'; });
            if (en_digit && !(en.size() == 1 && en[0] == '0')) {
                cols[3] = "0";
                std::string joined;
                for (size_t i = 0; i < cols.size(); ++i) {
                    if (i) joined += sep;
                    joined += cols[i];
                }
                out += joined;
                changed = true;
            } else {
                out += line;
            }
            out += "\r\n";
        }
    }

    if (!has_target) {
        if (error)
            *error = L"GameId '" + game_id + L"' not found in ScriptSetting";
        return false;
    }
    if (!WriteBytes(path, out)) {
        (void)changed;
        if (error) *error = L"Failed to write ScriptSetting";
        return false;
    }
    return true;
}

}  // namespace script
