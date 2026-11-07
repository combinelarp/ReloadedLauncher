# ReloadedLauncher - BMGO Engine 10068 PC Launcher Recreation (C++ / Win32).

- Multi-file layout: config, util/json, net/http, zip/extract, zip/store,
- tasks/client_install, tasks/script_setting, tasks/map_download,
- ui/launcher_window, protocol, main.
- libzip integration for ZIP extraction (replaces PowerShell Expand-Archive)

# How to build?
- Have MSVC (Visual Studio 2022) installed
# Step 1
- run this "python waf configure"
# Step 2
- run this "python waf build"
- Output will be in build/

# Dependencies
- libzip (embedded in src/libzip/) - ZIP archive manipulation
- zlib (embedded in src/zlib/) - DEFLATE compression
- bcrypt.lib (Windows SDK) - cryptography for libzip

# Changes from old version
- Replaced PowerShell-based ZIP extraction (shell::ExtractZip) with libzip-based extraction (ziputil::Extract)
- Added ziputil::CreateStored for creating uncompressed ZIP archives
- Added shell::WriteTempFile for writing temporary files
- Fixed missing config.h and zipconf.h for libzip MSVC build
- Added zip_err_str.c for libzip error string tables

# make sure u have 10068 apis for this and have a blockmango.zip with the 10068 client in ur cdn folder at path files/blockmango.zip