#include "json.hpp"

namespace json {

const Value* Value::FindKey(const std::wstring& wanted, int depth) const {
    if (depth > 8) return nullptr;
    if (type == kObject) {
        for (const auto& kv : members) {
            if (kv.first == wanted) return &kv.second;
        }
        for (const auto& kv : members) {
            const Value* hit = kv.second.FindKey(wanted, depth + 1);
            if (hit) return hit;
        }
    } else if (type == kArray) {
        for (const auto& item : arr) {
            const Value* hit = item.FindKey(wanted, depth + 1);
            if (hit) return hit;
        }
    }
    return nullptr;
}

namespace {

struct Parser {
    const char* p;
    const char* end;
    bool ok = false;
    std::wstring str;

    void SkipWs() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }

    bool Eat(char c) {
        if (p < end && *p == c) { ++p; return true; }
        return false;
    }

    void AppendUtf8(wchar_t ch) { str.push_back(ch); }

    void ReadString() {
        str.clear();
        Eat('"');  // caller checked open quote
        while (p < end) {
            char c = *p++;
            if (c == '"') return;
            if (c != '\\') {
                // Decode UTF-8 multibyte sequences into wide chars.
                unsigned char b0 = static_cast<unsigned char>(c);
                if (b0 < 0x80) { AppendUtf8(c); continue; }
                wchar_t w = 0; int extra = 0;
                if ((b0 & 0xE0) == 0xC0) { w = b0 & 0x1F; extra = 1; }
                else if ((b0 & 0xF0) == 0xE0) { w = b0 & 0x0F; extra = 2; }
                else if ((b0 & 0xF8) == 0xF0) { w = b0 & 0x07; extra = 3; }
                else { AppendUtf8(L'\uFFFD'); continue; }
                bool good = true;
                for (int i = 0; i < extra && p < end; ++i) {
                    unsigned char bc = static_cast<unsigned char>(*p++);
                    if ((bc & 0xC0) != 0x80) { good = false; break; }
                    w = (w << 6) | (bc & 0x3F);
                }
                if (good && extra == 3 && w >= 0xD800 && w < 0xDC00 && p + 1 < end) {
                    // surrogate pair
                    const char* save = p;
                    unsigned char s0 = static_cast<unsigned char>(*p++);
                    unsigned char s1 = static_cast<unsigned char>(*p++);
                    if (s0 == 0xED && (s1 & 0xF0) == 0xB0 && p < end) {
                        unsigned char s2 = static_cast<unsigned char>(*p++);
                        unsigned int lo = 0xD000 | ((s1 & 0x0F) << 6) | (s2 & 0x3F);
                        w = 0x10000 + ((w - 0xD800) << 10) + (lo - 0xDC00);
                        w &= 0xFFFF;  // keep in wchar_t space; UTF-16 build only
                        AppendUtf8(w);
                    } else {
                        p = save;
                        AppendUtf8(w);
                        AppendUtf8(L'\uFFFD');
                    }
                } else if (good) {
                    AppendUtf8(w);
                } else {
                    AppendUtf8(L'\uFFFD');
                }
                continue;
            }
            if (p >= end) return;
            char esc = *p++;
            switch (esc) {
                case '"': AppendUtf8(L'"'); break;
                case '\\': AppendUtf8(L'\\'); break;
                case '/': AppendUtf8(L'/'); break;
                case 'b': AppendUtf8(L'\b'); break;
                case 'f': AppendUtf8(L'\f'); break;
                case 'n': AppendUtf8(L'\n'); break;
                case 'r': AppendUtf8(L'\r'); break;
                case 't': AppendUtf8(L'\t'); break;
                case 'u': {
                    unsigned cp = 0;
                    if (end - p < 4) return;
                    for (int i = 0; i < 4; ++i) {
                        char h = *p++;
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= (h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                        else return;
                    }
                    AppendUtf8(static_cast<wchar_t>(cp));
                    break;
                }
                default: AppendUtf8(L'\uFFFD'); break;
            }
        }
    }

    void ReadNumber() {
        str.clear();
        while (p < end && (*p == '-' || *p == '+' || *p == '.' ||
                           *p == 'e' || *p == 'E' ||
                           (*p >= '0' && *p <= '9'))) {
            unsigned char b = static_cast<unsigned char>(*p);
            if (b < 0x80) str.push_back(static_cast<wchar_t>(*p++));
            else break;
        }
    }
};

Value ParseValue(Parser& ps) {
    Value v;
    if (!ps.ok) return v;
    ps.SkipWs();
    if (ps.p >= ps.end) { ps.ok = false; return v; }

    char c = *ps.p;
    if (c == '{') {
        ++ps.p;
        v.type = Value::kObject;
        ps.SkipWs();
        if (ps.Eat('}')) return v;
        while (true) {
            ps.SkipWs();
            if (ps.p >= ps.end || *ps.p != '"') { ps.ok = false; return v; }
            ps.ReadString();
            std::wstring key = ps.str;
            ps.SkipWs();
            if (!ps.Eat(':')) { ps.ok = false; return v; }
            Value item = ParseValue(ps);
            v.members.emplace_back(key, std::move(item));
            ps.SkipWs();
            if (ps.Eat(',')) continue;
            if (ps.Eat('}')) return v;
            ps.ok = false;
            return v;
        }
    }
    if (c == '[') {
        ++ps.p;
        v.type = Value::kArray;
        ps.SkipWs();
        if (ps.Eat(']')) return v;
        while (true) {
            Value item = ParseValue(ps);
            v.arr.push_back(std::move(item));
            ps.SkipWs();
            if (ps.Eat(',')) continue;
            if (ps.Eat(']')) return v;
            ps.ok = false;
            return v;
        }
    }
    if (c == '"') {
        ps.ReadString();
        v.type = Value::kString;
        v.str = ps.str;
        return v;
    }
    if (c == 't') {
        if (ps.end - ps.p >= 4 && ps.p[1] == 'r' && ps.p[2] == 'u' && ps.p[3] == 'e') {
            ps.p += 4;
            v.type = Value::kBool; v.boolean = true;
            return v;
        }
        ps.ok = false; return v;
    }
    if (c == 'f') {
        if (ps.end - ps.p >= 5 && ps.p[1] == 'a' && ps.p[2] == 'l' &&
            ps.p[3] == 's' && ps.p[4] == 'e') {
            ps.p += 5;
            v.type = Value::kBool; v.boolean = false;
            return v;
        }
        ps.ok = false; return v;
    }
    if (c == 'n') {
        if (ps.end - ps.p >= 4 && ps.p[1] == 'u' && ps.p[2] == 'l' && ps.p[3] == 'l') {
            ps.p += 4;
            v.type = Value::kNull;
            return v;
        }
        ps.ok = false; return v;
    }
    // number
    {
        const char* start = ps.p;
        if (c == '-' || c == '+') ++ps.p;
        bool digits = false;
        while (ps.p < ps.end && *ps.p >= '0' && *ps.p <= '9') { ++ps.p; digits = true; }
        if (ps.p < ps.end && *ps.p == '.') {
            ++ps.p;
            while (ps.p < ps.end && *ps.p >= '0' && *ps.p <= '9') ++ps.p;
        }
        if (ps.p < ps.end && (*ps.p == 'e' || *ps.p == 'E')) {
            ++ps.p;
            if (ps.p < ps.end && (*ps.p == '-' || *ps.p == '+')) ++ps.p;
            while (ps.p < ps.end && *ps.p >= '0' && *ps.p <= '9') ++ps.p;
        }
        if (!digits) { ps.ok = false; return v; }
        v.type = Value::kNumber;
        std::string text(start, ps.p);
        wchar_t* endp = nullptr;
        v.number = wcstod(std::wstring(text.begin(), text.end()).c_str(), &endp);
        return v;
    }
}

}  // namespace

Value Parse(const std::string& utf8) {
    Parser ps;
    ps.p = utf8.c_str();
    ps.end = ps.p + utf8.size();
    ps.ok = true;
    Value root = ParseValue(ps);
    if (ps.p != ps.end) ps.ok = false;
    if (!ps.ok) return Value();
    return root;
}

}  // namespace json
