#pragma once
#include <string>

namespace pipeline {

// Runs the whole pipeline from a worker thread. Posts UI events via ui::.
void RunPipeline(const std::wstring& game_id,
                 const std::wstring& server_ip,
                 const std::wstring& server_port);

}  // namespace pipeline