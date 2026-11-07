#pragma once
// Writes uncompressed ("stored") ZIP archives - enough for map-debug.zip.

#include <windows.h>
#include <string>
#include <vector>

namespace zip {

bool CreateStored(const std::wstring& zip_path,
                  const std::vector<std::pair<std::wstring, std::wstring>>& files);
// files: {full_source_path, archive_name} (forward slashes)

}  // namespace zip
