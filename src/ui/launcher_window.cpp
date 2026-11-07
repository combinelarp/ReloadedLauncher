#include "launcher_window.hpp"
#include "config.hpp"
#include "../resource.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objidl.h>
#include <shlwapi.h>
#include <gdiplus.h>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <cwctype>
#include <algorithm>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace ui {

namespace {

// ---- default client size (same as the original file) ----
constexpr int kWinW = 500;
constexpr int kWinH = 330;

// The layout below is measured from the reference screenshot. Coordinates
// are in "design" pixels (the screenshot's pixels) and are scaled to the real
// client size at paint time, so it looks the same at any window size.
constexpr double kDesignW = 399.0;
constexpr double kDesignH = 274.0;   // screenshot rows 4..277
constexpr double kDesignY0 = 4.0;    // screenshot row where the window starts

// ---- colors (sampled from the screenshot) ----
constexpr COLORREF kFrameOlive = RGB(107, 152, 49);   // frame bars
constexpr COLORREF kTeal       = RGB(0, 155, 76);     // corner squares
constexpr COLORREF kLime       = RGB(184, 206, 46);   // inner squares
constexpr COLORREF kTrack      = RGB(231, 231, 231);
constexpr COLORREF kTrackEdge  = RGB(205, 205, 205);
constexpr COLORREF kFill       = RGB(3, 152, 38);
constexpr COLORREF kText       = RGB(74, 74, 74);

// The screenshot's top-right corner has no teal square; set false to match
// the screenshot literally.
constexpr bool kDrawTopRightSquare = true;

// Your own icon, placed exactly where the reference icon is (design pixels:
// a 101x101 square centered at 210.5, 113.5), drawn square.
constexpr double kIconDesign = 101.0;
constexpr double kIconCx = 210.5;
constexpr double kIconCy = 113.5;

// The reference window has no title bar: the green frame is the window edge.
// true  = borderless window (drag it anywhere to move; Alt+F4 / taskbar to close)
// false = normal title bar like before
constexpr bool kBorderless = true;

struct Impl {
    std::vector<UiMsg> queue;
    CRITICAL_SECTION cs;
    LONG pulse = 0;
    LONG bar_w = 299;           // track width at 500px; only used for the pulse wrap
    LONG indeterminate = 0;
    uint64_t done_bytes = 0;
    uint64_t total_bytes = 0;
    std::wstring status_text = L"Downloading Game...";
    std::wstring pct_text = L"0%";
    HFONT font = nullptr;
    Gdiplus::Bitmap* icon_bmp = nullptr;
    IStream* icon_stream = nullptr;  // GDI+ decodes lazily; must outlive icon_bmp
    HICON h_icon_big = nullptr;
    HICON h_icon_small = nullptr;
    Gdiplus::GdiplusStartupInput gdi_start;
    ULONG_PTR gdi_token = 0;
    int width = kWinW;
    int height = kWinH;
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

    // design -> client pixel mapping
    auto X = [&](double x) { return (int)std::lround(x * w / kDesignW); };
    auto Y = [&](double y) { return (int)std::lround((y - kDesignY0) * h / kDesignH); };

    HBRUSH dc_brush = (HBRUSH)GetStockObject(DC_BRUSH);
    auto fillPx = [&](int l, int t, int r, int b, COLORREF c) {
        if (r <= l || b <= t) return;
        SetDCBrushColor(mem, c);
        RECT r2 = {l, t, r, b};
        FillRect(mem, &r2, dc_brush);
    };
    // rectangle given in design coordinates (right/bottom exclusive)
    auto box = [&](double x0, double y0, double x1, double y1, COLORREF c) {
        fillPx(X(x0), Y(y0), X(x1), Y(y1), c);
    };

    // ---- background: near-white, fading to light gray toward the bottom ----
    for (int y = 0; y < h; ++y) {
        double dy = y * kDesignH / h + kDesignY0;   // screenshot row
        double v = 251.0;
        if (dy > 172.0) {
            double t = (std::min)((dy - 172.0) / (262.0 - 172.0), 1.0);
            v = 251.0 - 13.0 * t;                   // 251 -> 238
        }
        int c = (int)std::lround(v);
        fillPx(0, y, w, y + 1, RGB(c, c, c));
    }

    // ---- frame bars (olive) ----
    box(21, 4, 378, 10, kFrameOlive);       // top bar
    box(21, 272, 378, 278, kFrameOlive);    // bottom bar
    box(0, 25, 5, 258, kFrameOlive);        // left bar
    box(393, 25, 399, 258, kFrameOlive);    // right bar
    // top-right step pieces
    box(372, 10, 378, 25, kFrameOlive);     // vertical piece under the top bar end
    box(380, 25, 399, 31, kFrameOlive);     // short horizontal piece before the right bar

    // ---- teal corner squares (20x20) ----
    box(0, 4, 20, 24, kTeal);               // top-left
    box(0, 258, 20, 278, kTeal);            // bottom-left
    box(379, 258, 399, 278, kTeal);         // bottom-right
    if (kDrawTopRightSquare)
        box(379, 4, 399, 24, kTeal);        // top-right

    // ---- lime inner squares (20x20) ----
    box(20, 24, 40, 44, kLime);             // top-left
    box(359, 24, 379, 44, kLime);           // top-right
    box(20, 238, 40, 258, kLime);           // bottom-left
    box(359, 238, 379, 258, kLime);         // bottom-right

    // ---- icon (your own image) ----
    if (g->icon_bmp) {
        Gdiplus::Graphics g_gfx(mem);
        g_gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        double sc = (std::min)(w / kDesignW, h / kDesignH);
        int isz = (int)std::lround(kIconDesign * sc);
        g_gfx.DrawImage(g->icon_bmp, X(kIconCx) - isz / 2, Y(kIconCy) - isz / 2, isz, isz);
    }

    // ---- progress bar: outer rect (80,177)-(321,190) in screenshot pixels ----
    int ox0 = X(80),  oy0 = Y(177), ox1 = X(321), oy1 = Y(190);
    int tx0 = ox0 + 1, ty0 = oy0 + 1, tx1 = ox1 - 1, ty1 = oy1 - 1;   // inner track
    fillPx(ox0, oy0, ox1, oy1, kTrackEdge);
    fillPx(tx0, ty0, tx1, ty1, kTrack);

    LONG trackW = tx1 - tx0;
    LONG left, right;
    if (g->indeterminate) {
        LONG front = (std::max)(0L, g->pulse - 60L);
        left = tx0 + (std::min)(front, trackW);
        right = tx0 + (std::min)((LONG)g->pulse, trackW);
    } else {
        left = tx0;
        uint64_t done = g->done_bytes;
        uint64_t total = g->total_bytes;
        LONG w_fill = 0;
        if (total > 0) {
            w_fill = static_cast<LONG>(static_cast<uint64_t>(trackW) * done / total);
            if (w_fill > trackW) w_fill = trackW;
        } else if (done > 0) {
            w_fill = trackW;
        }
        right = tx0 + w_fill;
    }
    if (right > left)
        fillPx(left, ty0, right, ty1, kFill);

    // ---- text: stage on the left, percent/size on the right, under the bar ----
    HFONT old_font = (HFONT)SelectObject(mem, g->font);
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, kText);

    int text_top = oy1 + 8;
    int text_bot = text_top + 26;
    RECT st_rc = {tx0, text_top, tx1 - 70, text_bot};
    DrawTextW(mem, g->status_text.c_str(), -1, &st_rc,
              DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    RECT pt_rc = {tx1 - 70, text_top, tx1, text_bot};
    DrawTextW(mem, g->pct_text.c_str(), -1, &pt_rc,
              DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(mem, old_font);

    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old_bmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

static void __stdcall CloseTimerProc(HWND, UINT, UINT_PTR, DWORD) {
    PostQuitMessage(0);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;  // everything is painted in Paint(); avoids flicker
        case WM_NCHITTEST: {
            LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);
            if (kBorderless && hit == HTCLIENT) return HTCAPTION;  // drag anywhere
            return hit;
        }
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
                        SetTimer(hwnd, 2, static_cast<UINT>(m.a), &CloseTimerProc);
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
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"ReloadedLauncherClass";
    RegisterClassExW(&wc);

    const DWORD style = kBorderless
        ? (WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX)
        : (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX);
    const DWORD exStyle = kBorderless ? WS_EX_APPWINDOW : 0;

    RECT rc = {0, 0, width, height};
    AdjustWindowRectEx(&rc, style, FALSE, exStyle);
    int ww = rc.right - rc.left;
    int hh = rc.bottom - rc.top;
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int wx = (sw - ww) / 2;
    int wy = (sh - hh) / 2;

    HWND hwnd = CreateWindowExW(exStyle, L"ReloadedLauncherClass", title.c_str(),
                                style,
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
    if (g->h_icon_big) DestroyIcon(g->h_icon_big);
    if (g->h_icon_small) DestroyIcon(g->h_icon_small);
    if (g->icon_bmp) delete g->icon_bmp;
    if (g->icon_stream) g->icon_stream->Release();
    Gdiplus::GdiplusShutdown(g->gdi_token);
    DeleteCriticalSection(&g->cs);
    g = nullptr;
}

}  // namespace ui