#pragma once
// Small self-contained JSON parser (objects, arrays, strings incl \uXXXX,
// numbers, bool, null) plus a recursive key lookup.

#include <string>
#include <vector>
#include <utility>

namespace json {

struct Value {
    enum Type { kNull, kBool, kNumber, kString, kArray, kObject } type = kNull;
    bool boolean = false;
    double number = 0.0;
    std::wstring str;
    std::vector<Value> arr;
    std::vector<std::pair<std::wstring, Value>> members;

    const Value* FindKey(const std::wstring& wanted, int depth = 0) const;
    std::wstring AsString() const { return type == kString ? str : std::wstring(); }
};

// Parses UTF-8 JSON text into a Value tree. Invalid input collapses to a
// Null tree (FindKey then simply misses), so callers just handle absence.
Value Parse(const std::string& utf8);

}  // namespace json
