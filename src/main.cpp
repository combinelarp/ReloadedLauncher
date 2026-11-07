// ReloadedLauncher entry point.
// - With a bmr:// URI argument: register scheme, run the GUI pipeline.
// - Without arguments: open the Blockman GO site in the browser and exit.

#include "protocol.hpp"
#include "ui/launcher_window.hpp"
#include "pipeline.hpp"
#include "tasks/client_install.hpp"
#include "config.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <cwctype>
#include <algorithm>

#pragma comment(lib, "shell32.lib")

int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    std::wstring uri;
    if (argv) {
        uri = protocol::ParseUri(argc, argv);
        LocalFree(argv);
    }

    if (uri.empty()) {
        // First run: show the GUI, download the client in the background,
        // then open the site once installation is confirmed.
        std::function<void()> pipeline = []() {
            if (!client::Installed()) {
                try { client::MigrateRootInstall(); } catch (...) {}
            }
            if (!client::Installed()) {
                // Forward real download progress to the window (this used to be
                // discarded, which kept the bar at a permanent 0%).
                client::ProgressFn progress = [](uint64_t done, uint64_t total) {
                    ui::PostProgress(done, total);
                };
                client::StatusFn status = [](const std::wstring& s) { ui::PostStatus(s); };
                std::wstring err;
                if (!client::DownloadAndInstall(progress, status, &err)) {
                    ui::PostStatus(err.empty() ? L"Download failed" : err);
                    ui::PostClose(8000);
                    return;
                }
            }
            // Move launcher to AppData and register URI scheme pointing there
            client::MoveLauncherAndRegisterUri([](const std::wstring& s) { ui::PostStatus(s); });
            
            // Only open the site once the client is confirmed installed.
            if (client::Installed()) {
                ui::PostStatus(L"Starting Blockman GO...");
                Sleep(600);
                ShellExecuteW(nullptr, L"open", cfg::kSiteUrl, nullptr, nullptr, SW_SHOW);
                ui::PostClose(1500);
            } else {
                ui::PostStatus(L"Game.exe was not found after extracting");
                ui::PostClose(8000);
            }
        };
        ui::Run(cfg::kTitle, 500, 330, pipeline);
        return 0;
    }

    // URI launched from browser - already registered to AppData location

    // bmr://GameId:ServerIP:ServerPort
    std::wstring body = uri;
    if (body.compare(0, 6, L"bmr://") == 0) body = body.substr(6);
    else if (body.compare(0, 4, L"bmr:") == 0) body = body.substr(4);
    while (!body.empty() && (body.front() == L'/' || body.front() == L':'))
        body.erase(body.begin());

    // GameId:ServerIP:ServerPort - the IP used to be filled with the GameId,
    // so ScriptSetting got a bogus server address.
    std::wstring game_id, server_ip, server_port;
    size_t p1 = body.find(L':');
    if (p1 == std::wstring::npos) {
        game_id = body;
    } else {
        game_id = body.substr(0, p1);
        size_t p2 = body.find(L':', p1 + 1);
        if (p2 == std::wstring::npos) {
            server_ip = body.substr(p1 + 1);
        } else {
            server_ip = body.substr(p1 + 1, p2 - p1 - 1);
            server_port = body.substr(p2 + 1);
        }
    }
    if (game_id.empty()) game_id = body;

    std::function<void()> pipeline = [game_id, server_ip, server_port]() {
        pipeline::RunPipeline(game_id, server_ip, server_port);
    };

    ui::Run(cfg::kTitle, 500, 330, pipeline);
    return 0;
}