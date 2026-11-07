#pragma once
#include <functional>
#include <string>

// Per-game map pipeline: resolve gid/mid from the server, download the map
// zip into client\map\maps\<gid>\<mid>, extract a duplicate copy plus a
// store-only map-debug.zip inside <mid>\map_debug.

namespace maps {

using StatusFn = std::function<void(const std::wstring&)>;

void EnsureDownloaded(const std::wstring& game_id,
                      const std::wstring& fallback_map_name,
                      const StatusFn& status, std::wstring* error);

}  // namespace maps
