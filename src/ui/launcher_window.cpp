#include "launcher_window.hpp"
#include "config.hpp"
#include "../resource.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objidl.h>
#include <shlwapi.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <cwctype>
#include <algorithm>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace ui {

namespace {

struct Impl {
    std::vector<UiMsg> queue;
    CRITICAL_SECTION cs;
    LONG pulse = 0;
    LONG bar_w = 300;
    LONG bar_h = 14;
    LONG bar_x = 100;
    LONG bar_y = 200;
    LONG indeterminate = 0;
    uint64_t done_bytes = 0;
    uint64_t total_bytes = 0;
    std::wstring status_text = L"Downloading Game...";
    std::wstring pct_text = L"0%";
    HFONT font = nullptr;
    HBRUSH white = nullptr;
    HBRUSH green = nullptr;
    HBRUSH track = nullptr;
    HBRUSH khaki = nullptr;
    HBRUSH border = nullptr;
    Gdiplus::Bitmap* icon_bmp = nullptr;
    IStream* icon_stream = nullptr;  // GDI+ decodes lazily; must outlive icon_bmp
    HICON h_icon_big = nullptr;
    HICON h_icon_small = nullptr;
    Gdiplus::GdiplusStartupInput gdi_start;
    ULONG_PTR gdi_token = 0;
    int width = 500;
    int height = 330;
    std::function<void()> pipeline;
    LONG closed = 0;
    HWND hMain = nullptr;
};

Impl* g = nullptr;

void PushImpl(const UiMsg& msg) {
    if (!g) return;
    EnterCriticalSection(&g->cs);
    g->queue.push_back(msg);
    LeaveCriticalSection(&g->cs);
    if (g->hMain) PostMessageW(g->hMain, WM_APP + 1, 0, 0);
}

void Paint(HWND hwnd, HDC hdc) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right;
    int h = rc.bottom;

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HBITMAP old_bmp = (HBITMAP)SelectObject(mem, bmp);

    // Fill background
    FillRect(mem, &rc, g->white);

    // Border and squares
    int m = 14, t = 10;
    HPEN border_pen = CreatePen(PS_SOLID, t, RGB(82, 122, 23));
    HBRUSH old_brush = (HBRUSH)SelectObject(mem, GetStockObject(NULL_BRUSH));
    HPEN old_pen = (HPEN)SelectObject(mem, border_pen);
    Rectangle(mem, m, m, w - m, h - m);

    // Khaki squares
    HBRUSH sq_brush = CreateSolidBrush(RGB(178, 180, 92));
    SelectObject(mem, sq_brush);
    SelectObject(mem, GetStockObject(NULL_PEN));
    int s = 30;
    struct Pt { int x, y; };
    Pt pts[] = {{0, 44}, {34, 0}, {0, 258}, {34, 296},
                {472, 48}, {472, 196}, {438, 296}, {472, 296}};
    for (auto& pt : pts) {
        Rectangle(mem, pt.x, pt.y, pt.x + s, pt.y + s);
    }
    DeleteObject(sq_brush);
    SelectObject(mem, old_pen);
    DeleteObject(border_pen);

    // Icon
    if (g->icon_bmp) {
        Gdiplus::Graphics g_gfx(mem);
        g_gfx.DrawImage(g->icon_bmp, 194, 46, 112, 112);
    }

    // Progress bar track
    RECT tr = {g->bar_x - 1, g->bar_y - 1, g->bar_x + g->bar_w + 1, g->bar_y + g->bar_h + 1};
    FillRect(mem, &tr, g->track);

    // Progress bar fill
    RECT fr;
    fr.top = g->bar_y;
    fr.bottom = g->bar_y + g->bar_h;
    if (g->indeterminate) {
        LONG front = max(0L, g->pulse - 60);
        fr.left = g->bar_x + front;
        fr.right = g->bar_x + g->pulse;
    } else {
        fr.left = g->bar_x;
        uint64_t done = g->done_bytes;
        uint64_t total = g->total_bytes;
        LONG w_fill = 0;
        if (total > 0) {
            w_fill = static_cast<LONG>(static_cast<uint64_t>(g->bar_w) * done / total);
            if (w_fill > g->bar_w) w_fill = g->bar_w;
        } else if (done > 0) {
            w_fill = g->bar_w;
        }
        fr.right = g->bar_x + w_fill;
    }
    if (fr.right > fr.left) {
        HBRUSH fill_brush = CreateSolidBrush(RGB(116, 171, 44));
        FillRect(mem, &fr, fill_brush);
        DeleteObject(fill_brush);
    }

    // Text labels
    SelectObject(mem, g->font);
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(74, 74, 74));

    RECT st_rc = {g->bar_x + 60, g->bar_y + 20, g->bar_x + 220, g->bar_y + 50};
    DrawTextW(mem, g->status_text.c_str(), -1, &st_rc, DT_LEFT | DT_SINGLELINE);

    RECT pt_rc = {g->bar_x + 210, g->bar_y + 20, g->bar_x + g->bar_w, g->bar_y + 50};
    DrawTextW(mem, g->pct_text.c_str(), -1, &pt_rc, DT_LEFT | DT_SINGLELINE);

    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old_bmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            Paint(hwnd, hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_TIMER: {
            if (wp == 1) {
                if (g->indeterminate) {
                    g->pulse = (g->pulse + 3) % (g->bar_w + 60);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }
        case (WM_APP + 1): {
            std::vector<UiMsg> local;
            EnterCriticalSection(&g->cs);
            local = std::move(g->queue);
            g->queue.clear();
            LeaveCriticalSection(&g->cs);

            for (auto& m : local) {
                if (m.type == kStatus) {
                    g->status_text = m.text;
                    g->indeterminate = 0;
                } else if (m.type == kProgress) {
                    g->done_bytes = m.a;
                    g->total_bytes = m.b;
                    g->indeterminate = 0;
                    if (g->total_bytes > 0) {
                        int pct = static_cast<int>(g->done_bytes * 100 / g->total_bytes);
                        if (pct > 100) pct = 100;
                        g->pct_text = cfg::ToWString(pct) + L"%";
                    } else {
                        wchar_t buf[64];
                        swprintf(buf, L"%.1f MB", static_cast<double>(g->done_bytes) / 1048576.0);
                        g->pct_text = buf;
                    }
                } else if (m.type == kIndeterminate) {
                    g->indeterminate = 1;
                    g->done_bytes = m.a;
                    wchar_t buf[64];
                    swprintf(buf, L"%.1f MB", static_cast<double>(g->done_bytes) / 1048576.0);
                    g->pct_text = buf;
                } else if (m.type == kClose) {
                    if (!InterlockedExchange(&g->closed, 1)) {
                        SetTimer(hwnd, 2, static_cast<UINT>(m.a), [](HWND, UINT, UINT_PTR, DWORD){
                            PostQuitMessage(0);
                        });
                    }
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

DWORD WINAPI WorkerThread(LPVOID) {
    if (g && g->pipeline) g->pipeline();
    return 0;
}

// Renders the source image into a square HICON of the requested size.
HICON MakeIcon(Gdiplus::Bitmap* src, int size) {
    if (!src || size <= 0) return nullptr;
    Gdiplus::Bitmap scaled(size, size, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics gfx(&scaled);
        gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        gfx.DrawImage(src, 0, 0, size, size);
    }
    HICON icon = nullptr;
    if (scaled.GetHICON(&icon) != Gdiplus::Ok) return nullptr;
    return icon;
}

}  // namespace

void Push(const UiMsg& msg) { PushImpl(msg); }
void PostStatus(const std::wstring& text) { Push({kStatus, 0, 0, text}); }
void PostProgress(uint64_t done, uint64_t total) {
    if (total == 0) Push({kIndeterminate, done, 0, L""});
    else Push({kProgress, done, total, L""});
}
void PostIndeterminate(uint64_t done) { Push({kIndeterminate, done, 0, L""}); }
void PostClose(int delay_ms) { Push({kClose, static_cast<uint64_t>(delay_ms), 0, L""}); }

void Run(const std::wstring& title, int width, int height,
         const std::function<void()>& pipeline) {
    Impl impl;
    g = &impl;
    g->width = width;
    g->height = height;
    g->pipeline = pipeline;
    InitializeCriticalSection(&g->cs);

    HINSTANCE hInst = GetModuleHandleW(nullptr);
    Gdiplus::GdiplusStartup(&g->gdi_token, &g->gdi_start, nullptr);

    HRSRC hrsrc = FindResourceW(hInst, MAKEINTRESOURCEW(IDR_ICON_PNG), (LPWSTR)RT_RCDATA);
    if (hrsrc) {
        HGLOBAL hglob = LoadResource(hInst, hrsrc);
        void* data = LockResource(hglob);
        DWORD size = SizeofResource(hInst, hrsrc);
        if (data && size) {
            IStream* stream = SHCreateMemStream((const BYTE*)data, size);
            if (stream) {
                Gdiplus::Bitmap* bmp = new Gdiplus::Bitmap(stream);
                if (bmp->GetLastStatus() == Gdiplus::Ok) {
                    g->icon_bmp = bmp;
                    g->icon_stream = stream;
                } else {
                    delete bmp;
                    stream->Release();
                }
            }
        }
    }
    if (!g->icon_bmp) {
        std::wstring icon_file = cfg::IconPath();
        if (GetFileAttributesW(icon_file.c_str()) != INVALID_FILE_ATTRIBUTES) {
            g->icon_bmp = new Gdiplus::Bitmap(icon_file.c_str());
        }
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(IDC_ARROW));
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"ReloadedLauncherClass";
    RegisterClassExW(&wc);

    RECT rc = {0, 0, width, height};
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    int ww = rc.right - rc.left;
    int hh = rc.bottom - rc.top;
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int wx = (sw - ww) / 2;
    int wy = (sh - hh) / 2;

    HWND hwnd = CreateWindowExW(0, L"ReloadedLauncherClass", title.c_str(),
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                wx, wy, ww, hh, nullptr, nullptr, hInst, nullptr);
    g->hMain = hwnd;

    // Title bar / taskbar icon from the same embedded PNG.
    if (g->icon_bmp) {
        g->h_icon_big = MakeIcon(g->icon_bmp, GetSystemMetrics(SM_CXICON));
        g->h_icon_small = MakeIcon(g->icon_bmp, GetSystemMetrics(SM_CXSMICON));
        if (g->h_icon_big)
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)g->h_icon_big);
        if (g->h_icon_small)
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)g->h_icon_small);
    }

    g->font = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g->white = CreateSolidBrush(RGB(255, 255, 255));
    g->green = CreateSolidBrush(RGB(116, 171, 44));
    g->track = CreateSolidBrush(RGB(227, 227, 227));

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    SetTimer(hwnd, 1, 50, nullptr);
    CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    KillTimer(hwnd, 1);
    DeleteObject(g->font);
    DeleteObject(g->white);
    DeleteObject(g->green);
    DeleteObject(g->track);
    if (g->h_icon_big) DestroyIcon(g->h_icon_big);
    if (g->h_icon_small) DestroyIcon(g->h_icon_small);
    if (g->icon_bmp) delete g->icon_bmp;
    if (g->icon_stream) g->icon_stream->Release();
    Gdiplus::GdiplusShutdown(g->gdi_token);
    DeleteCriticalSection(&g->cs);
    g = nullptr;
}

}  // namespace ui
