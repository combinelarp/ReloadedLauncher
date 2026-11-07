#pragma once
#include <windows.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ui {

// Thread-safe UI event queue (worker thread -> UI thread).
enum MsgType { kStatus, kProgress, kIndeterminate, kClose };
struct UiMsg {
    MsgType type;
    uint64_t a = 0;
    uint64_t b = 0;
    std::wstring text;
};

void Push(const UiMsg& msg);
void PostStatus(const std::wstring& text);
void PostProgress(uint64_t done, uint64_t total);
void PostIndeterminate(uint64_t done);
void PostClose(int delay_ms);

// Registers a pipeline function (called from a worker thread) and runs the
// window message loop. Returns when the window is closed.
void Run(const std::wstring& title, int width, int height,
         const std::function<void()>& pipeline);

}  // namespace ui