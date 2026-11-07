#include "pipeline.hpp"
#include "tasks/client_install.hpp"
#include "tasks/script_setting.hpp"
#include "tasks/map_download.hpp"
#include "ui/launcher_window.hpp"
#include "config.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        si.wShowWindow = SW_SHOW;
        PROCESS_INFORMATION pi = {};
        std::vector<wchar_t> cmd(exe.begin(), exe.end());
        cmd.push_back(L'\0');
        CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0,
                       nullptr, nullptr, &si, &pi);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        ui::PostStatus(L"Game.exe not found");
    }
    Sleep(400);
    ui::PostClose(1500);
}

}  // namespace pipeline