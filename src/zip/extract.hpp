#pragma once
// Zip extraction via hidden PowerShell Expand-Archive (zero extra deps).

#include <functional>
#include <string>
#include <cstdint>

namespace shell {

// Reports bytes already written into the destination plus the archive's total
// uncompressed size (0 when it cannot be determined).
using ProgressFn = std::function<void(uint64_t done, uint64_t total)>;

// Extracts winZipPath into destDir (created if needed). Returns exit code 0 on
// success; -1 if the process could not start; otherwise PowerShell's code.
int ExtractZipTo(const std::wstring& zip_path, const std::wstring& dest_dir,
                 const ProgressFn& progress = nullptr);

// Extracts zip returning log for debug strings (same signature as above).
bool ExtractZip(const std::wstring& zip_path, const std::wstring& dest_dir,
                std::wstring* err, const ProgressFn& progress = nullptr);

// Writes bytes to a temporary .zip file next to dest (auto cleanup deferred).
bool WriteTempFile(const std::wstring& dest_dir, const wchar_t* name,
                   const unsigned char* data, size_t len, std::wstring* path_out);

}  // namespace shell
