#pragma once
// Create a stored (uncompressed) ZIP archive from a list of files.

#include <string>
#include <vector>
#include <utility>

namespace ziputil {

// Creates a stored (no compression) ZIP archive at `zip_path` containing the
// files specified in `files`. Each entry is a pair of (full_local_path,
// archive_name). Returns false on error.
bool CreateStored(const std::wstring& zip_path,
                  const std::vector<std::pair<std::wstring, std::wstring>>& files);

}  // namespace ziputil