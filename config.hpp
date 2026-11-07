// Central paths / URLs for ReloadedLauncher (Blockman.Launcher clone).

#include <shlobj.h>
#include <cstdint>
#include <string>

namespace cfg {

constexpr const wchar_t* kTitle = L"BlockmanReloaded";
constexpr const wchar_t* kUserAgent = L"ReloadedLauncher/1.0";
constexpr const wchar_t* kClientZipUrl = L"http://localhost:18080/files/blockmango.zip";
constexpr const wchar_t* kGameApiUrl = L"http://localhost:18080/game/api/v1/games/";
constexpr const wchar_t* kDispatchUrl = L"http://localhost:18080/v1/dispatch";
constexpr const wchar_t* kSiteUrl = L"http://localhost:18080/";
constexpr const wchar_t* kIconName = L"unnamed.png";
constexpr const wchar_t* kGameExe = L"Game.exe";
constexpr const wchar_t* kSysDirName = L"client";
constexpr const wchar_t* kMapsRootRel = L"map\\maps";
constexpr const wchar_t* kScriptSetting = L"ScriptSetting.csv";

inline std::wstring BaseDir() {
    wchar_t buf[MAX_PATH] = {0};
    if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, buf) != S_OK)
        return std::wstring();
    std::wstring dir = buf;
    dir += L"\\Engine-10068-Blockman-Reloaded";
    return dir;
}

inline std::wstring ClientDir() { return BaseDir() + L"\\" + kSysDirName; }
inline std::wstring MapsRoot() { return ClientDir() + L"\\" + kMapsRootRel; }

inline std::wstring ScriptSettingPath() {
    return ClientDir() + L"\\Media\\Scripts\\" + kScriptSetting;
}
inline std::wstring ScriptSettingFallback() {
    return BaseDir() + L"\\Media\\Scripts\\" + kScriptSetting;
}
inline std::wstring LegacyScriptSettingPath(int which) {
    if (which == 0) return ClientDir() + L"\\Media\\Script\\" + kScriptSetting;
    return BaseDir() + L"\\Media\\Script\\" + kScriptSetting;
}

inline std::wstring IconPath() {
    wchar_t exe[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    size_t slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dir.resize(slash);
    return dir + L"\\" + kIconName;
}

inline std::wstring ToWString(uint64_t v) {
    wchar_t buf[32];
    int i = 32;
    buf[--i] = 0;
    if (v == 0) buf[--i] = L'0';
    while (v) {
        buf[--i] = L'0' + static_cast<wchar_t>(v % 10);
        v /= 10;
    }
    return buf + i;
}

}  // namespace cfg