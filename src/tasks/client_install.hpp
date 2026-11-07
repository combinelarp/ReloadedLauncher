#pragma once
#include <functional>
#include <string>
#include <cstdint>

namespace client {

using ProgressFn = std::function<void(uint64_t done, uint64_t total)>;
using StatusFn = std::function<void(const std::wstring&)>;

bool Installed();
void MigrateRootInstall();
// Downloads cfg zip and extracts everything into client\. Throws status text
// on failure via fun, returns false on error.
bool DownloadAndInstall(const ProgressFn& progress, const StatusFn& status,
                        std::wstring* error);
bool FindGameExe(std::wstring* out_path);

// Move the launcher exe to AppData and register the bmr:// URI scheme pointing there.
// Returns true on success. User can then delete the original launcher in Downloads.
bool MoveLauncherAndRegisterUri(const StatusFn& status);

}  // namespace client
