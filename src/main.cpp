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
        // First run: ensure the client is downloaded before opening the site
        if (!client::Installed()) {
            try { client::MigrateRootInstall(); } catch (...) {}
        }
        if (!client::Installed()) {
            client::ProgressFn progress = [](uint64_t, uint64_t) {};
            client::StatusFn status = [](const std::wstring&) {};
            std::wstring err;
            client::DownloadAndInstall(progress, status, &err);
        }
        // Set the URI: register the bmr:// scheme so the site can fire it later
        protocol::Register();
        // Open the site
        ShellExecuteW(nullptr, L"open", cfg::kSiteUrl, nullptr, nullptr, SW_SHOW);
        return 0;
    }

    protocol::Register();

    // bmr://GameId:ServerIP:ServerPort
    std::wstring body = uri;
    if (body.compare(0, 6, L"bmr://") == 0) body = body.substr(6);
    else if (body.compare(0, 4, L"bmr:") == 0) body = body.substr(4);
    while (!body.empty() && (body.front() == L'/' || body.front() == L':'))
        body.erase(body.begin());

    std::wstring game_id, server_ip, server_port;
    for (size_t i = 0; i < body.size(); ++i) {
        if (body[i] == L':') {
            if (server_ip.empty()) {
                game_id = body.substr(0, i);
                server_ip = body.substr(0, i);
            } else {
                server_port = body.substr(i + 1);
                break;
            }
        }
    }
    if (game_id.empty()) game_id = body;

    std::function<void()> pipeline = [game_id, server_ip, server_port]() {
        pipeline::RunPipeline(game_id, server_ip, server_port);
    };

    ui::Run(cfg::kTitle, 500, 330, pipeline);
    return 0;
}