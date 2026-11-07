#include "protocol.hpp"
#include "config.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#include <cwctype>
#include <algorithm>

#pragma comment(lib, "shlwapi.lib")

namespace protocol {

std::wstring ExePath() {
    wchar_t buf[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}

std::wstring ParseUri(int argc, wchar_t** argv) {
    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        std::wstring lower = arg;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
        if (lower.compare(0, 4, L"bmr:") == 0) return arg;
    }
    return L"";
}

void Register() {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Classes\\bmr",
                        0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    RegSetValueExW(key, L"URL Protocol", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(L""), sizeof(wchar_t));
    RegCloseKey(key);

    std::wstring cmd = L"\"" + ExePath() + L"\" \"%1\"";
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Classes\\bmr\\shell\\open\\command",
                        0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_WRITE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(key, nullptr, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(cmd.c_str()),
                       static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
    }
}

}  // namespace protocol