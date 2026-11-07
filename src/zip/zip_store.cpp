#include "zip_store.hpp"
#include <cstdint>
#include <cstdio>
#include <ctime>

namespace zip {

namespace {

uint32_t Crc32(const std::vector<uint8_t>& data) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t b : data) crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

void Put16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(v & 0xFF);
    out.push_back((v >> 8) & 0xFF);
}
void Put32(std::vector<uint8_t>& out, uint32_t v) {
    Put16(out, static_cast<uint16_t>(v & 0xFFFF));
    Put16(out, static_cast<uint16_t>(v >> 16));
}

void WriteStringBytes(std::vector<uint8_t>& out, const std::string& s) {
    out.insert(out.end(), s.begin(), s.end());
}

std::string NarrowUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
                                static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                        &s[0], n, nullptr, nullptr);
    return s;
}

}  // namespace

bool CreateStored(const std::wstring& zip_path,
                  const std::vector<std::pair<std::wstring, std::wstring>>& files) {
    HANDLE out = CreateFileW(zip_path.c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) return false;

    struct Central {
        std::vector<uint8_t> rec;
        uint32_t offset = 0;
    };
    std::vector<Central> centrals;
    uint32_t running_offset = 0;
    bool ok = true;

    auto LocalHeader = [&](uint16_t flags, uint16_t method, uint16_t time,
                           uint16_t date, uint32_t crc, uint32_t size,
                           const std::string& name) {
        std::vector<uint8_t> h;
        Put32(h, 0x04034B50);
        Put16(h, 20);        // version needed
        Put16(h, 0x0800);    // utf-8 name flag
        Put16(h, 0);         // file attrs none
        Put16(h, time);
        Put16(h, date);
        Put32(h, crc);
        Put32(h, size);
        Put32(h, size);
        Put16(h, static_cast<uint16_t>(name.size()));
        Put16(h, 0);         // extra len
        WriteStringBytes(h, name);
        DWORD written = 0;
        return WriteFile(out, h.data(), static_cast<DWORD>(h.size()), &written, nullptr) == TRUE;
    };

    for (const auto& pair : files) {
        const std::wstring& src = pair.first;
        std::string name = NarrowUtf8(pair.second);
        HANDLE f = CreateFileW(src.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, 0, nullptr);
        if (f == INVALID_HANDLE_VALUE) { ok = false; continue; }
        LARGE_INTEGER sz = {};
        GetFileSizeEx(f, &sz);
        std::vector<uint8_t> data(static_cast<size_t>(sz.QuadPart));
        DWORD read = 0, got = 0;
        while (read < data.size()) {
            if (!ReadFile(f, data.data() + read, static_cast<DWORD>(data.size() - read),
                          &got, nullptr) || got == 0) break;
            read += got;
        }
        CloseHandle(f);
        if (read != data.size()) { ok = false; continue; }

        uint32_t crc = Crc32(data);
        // MS-DOS style time, stored UTC-ish; contents irrelevant for consumers.
        SYSTEMTIME st;
        GetLocalTime(&st);
        uint16_t dos_time = static_cast<uint16_t>((st.wHour << 11) | (st.wMinute << 5) | (st.wSecond >> 1));
        uint16_t dos_date = static_cast<uint16_t>(((st.wMonth) << 5) | (st.wDay) | ((st.wYear - 1980) << 9));

        DWORD written = 0;
        if (!LocalHeader(0x0800, 0, dos_time, dos_date, crc,
                         static_cast<uint32_t>(data.size()), name) ||
            !WriteFile(out, data.data(), static_cast<DWORD>(data.size()), &written, nullptr)) {
            ok = false; continue;
        }

        Central cen;
        std::vector<uint8_t>& c = cen.rec;
        Put32(c, 0x02014B50);
        Put16(c, 10);       // version made by (fat)
        Put16(c, 20);       // version needed
        Put16(c, 0x0800);
        Put16(c, 0);
        Put16(c, dos_time);
        Put16(c, dos_date);
        Put32(c, crc);
        Put32(c, static_cast<uint32_t>(data.size()));
        Put32(c, static_cast<uint32_t>(data.size()));
        Put16(c, static_cast<uint16_t>(name.size()));
        Put16(c, 0);   // extra
        Put16(c, 0);   // comment
        Put16(c, 0);   // disk
        Put16(c, 0);   // internal attrs
        Put32(c, 0);   // external attrs
        Put32(c, running_offset);
        WriteStringBytes(c, name);
        cen.offset = running_offset;
        running_offset += static_cast<uint32_t>(name.size()) + 30 +
                          static_cast<uint32_t>(data.size());
        centrals.push_back(std::move(cen));
    }

    // central directory
    uint32_t cd_offset = running_offset;
    uint32_t cd_size = 0;
    for (Central& c : centrals) {
        DWORD written = 0;
        if (!WriteFile(out, c.rec.data(), static_cast<DWORD>(c.rec.size()), &written, nullptr)) {
            ok = false;
            break;
        }
        cd_size += static_cast<uint32_t>(c.rec.size());
    }

    // EOCD
    std::vector<uint8_t> eocd;
    Put32(eocd, 0x06054B50);
    Put16(eocd, 0);
    Put16(eocd, 0);
    Put16(eocd, static_cast<uint16_t>(centrals.size()));
    Put16(eocd, static_cast<uint16_t>(centrals.size()));
    Put32(eocd, cd_size);
    Put32(eocd, cd_offset);
    Put16(eocd, 0);
    DWORD written = 0;
    if (!WriteFile(out, eocd.data(), static_cast<DWORD>(eocd.size()), &written, nullptr))
        ok = false;
    CloseHandle(out);
    return ok && centrals.size() == files.size();
}

}  // namespace zip
