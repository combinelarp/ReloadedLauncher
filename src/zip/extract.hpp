#pragma once
// ZIP extraction via libzip (replaces the old PowerShell Expand-Archive path).

#include <functional>
#include <string>

namespace shell {

// Writes a temporary file to `dir` with the given `name` and `data`.
// Returns true on success and fills `out_path` with the full path.
bool WriteTempFile(const std::wstring& dir, const wchar_t* name,
                   const void* data, size_t size, std::wstring* out_path);

}  // namespace shell

namespace ziputil {

using ProgressFn = std::function<void(uint64_t done, uint64_t total)>;

// Extract every entry of `zip_path` into `dest_dir`. Creates `dest_dir` and
// any subdirectories as needed. Returns false and fills `error` on failure.
bool Extract(const std::wstring& zip_path, const std::wstring& dest_dir,
             std::wstring* error, const ProgressFn& progress = nullptr);

}  // namespace ziputil