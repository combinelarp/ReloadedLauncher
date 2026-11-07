#pragma once
#include <string>

namespace script {

struct Row {
    std::wstring game_type;
    std::wstring group_dir;
    std::wstring name;
    std::wstring map_name;
};

// Locates the live ScriptSetting.csv (Media\Scripts first).
bool FindPath(std::wstring* out_path);

// same_game_id: exact match or digits-with-'g'-stripped match (1048 == g1048).
bool SameGameId(const std::wstring& row_game_id, const std::wstring& game_id);

bool Update(const std::wstring& path,
            const std::wstring& game_id,
            const std::wstring& server_ip,
            const std::wstring& server_port,
            Row* out_row,          // filled with the enabled row (optional)
            std::wstring* error);  // set when the game id was not found

}  // namespace script
