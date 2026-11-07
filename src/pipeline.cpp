#include "pipeline.hpp"
#include "tasks/client_install.hpp"
#include "tasks/script_setting.hpp"
#include "tasks/map_download.hpp"
#include "ui/launcher_window.hpp"
#include "config.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <vector>

namespace pipeline {

void RunPipeline(const std::wstring& game_id,
                 const std::wstring& server_ip,
                 const std::wstring& server_port) {
    ui::PostStatus(L"Downloading Game...");

    if (!client::Installed()) {
        try { client::MigrateRootInstall(); } catch (...) {}
    }

    if (!client::Installed()) {
        client::ProgressFn progress = [&](uint64_t done, uint64_t total) {
            ui::PostProgress(done, total);
        };
        client::StatusFn status = [&](const std::wstring& s) { ui::PostStatus(s); };
        std::wstring err;
        if (!client::DownloadAndInstall(progress, status, &err)) {
            ui::PostStatus(L"Download failed: " + err);
            ui::PostClose(4000);
            return;
        }
        client::MoveLauncherAndRegisterUri(status);
    } else {
        ui::PostStatus(L"Game already installed");
    }
    Sleep(300);

    ui::PostStatus(L"Changing ScriptSetting...");
    std::wstring path;
    if (script::FindPath(&path)) {
        script::Row row;
        std::wstring serr;
        if (!script::Update(path, game_id, server_ip, server_port, &row, &serr)) {
            ui::PostStatus(L"ScriptSetting: " + serr);
        }
    } else {
        ui::PostStatus(L"ScriptSetting not found");
    }
    Sleep(300);

    std::wstring map_name;
    std::wstring map_err;
    maps::EnsureDownloaded(game_id, map_name,
                           [&](const std::wstring& s) { ui::PostStatus(s); },
                           &map_err);
    Sleep(300);

    ui::PostStatus(L"Launching Game.exe...");
    Sleep(500);
    std::wstring exe;
    if (client::FindGameExe(&exe)) {
        std::wstring dir = exe;
        size_t slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) dir.resize(slash);

        // Launch the way Explorer / `start ""` does. ShellExecute gives the
        // game a proper argv[0] plus its own working directory; the old
        // CreateProcess call passed an unquoted path, so the game read pieces
        // of its own executable path as arguments and exited.
        HINSTANCE launched = ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr,
                                           dir.c_str(), SW_SHOW);
        if (reinterpret_cast<INT_PTR>(launched) <= 32) {
            // Fallback: quote only argv[0], keep the application name separate.
            std::wstring quoted = L"\"" + exe + L"\"";
            std::vector<wchar_t> cmd(quoted.begin(), quoted.end());
            cmd.push_back(L'\0');
            STARTUPINFOW si = {};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi = {};
            if (CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0,
                               nullptr, dir.c_str(), &si, &pi)) {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            } else {
                ui::PostStatus(L"Could not start Game.exe");
            }
        }
    } else {
        ui::PostStatus(L"Game.exe not found");
    }
    Sleep(400);
    ui::PostClose(1500);
}

}  // namespace pipeline