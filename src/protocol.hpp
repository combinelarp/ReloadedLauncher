// Registers the bmr:// URL scheme so the site can fire the launcher.
// Also parses the command line for a bmr:// URI.

#pragma once
#include <windows.h>
#include <string>

namespace protocol {

std::wstring ParseUri(int argc, wchar_t** argv);

// Returns the path to the running exe.
std::wstring ExePath();

void Register();

}  // namespace protocol