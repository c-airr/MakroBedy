#include <windows.h>
#include <windowsx.h>
#include <thread>
#include <atomic>
#include <chrono>

#pragma comment(lib, "winmm.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

constexpr int LEFT_CPS = 15;
constexpr int RIGHT_CPS = 22;

constexpr COLORREF COLOR_BG = RGB(42, 42, 42);
constexpr COLORREF COLOR_ON = RGB(34, 197, 94);
constexpr COLORREF COLOR_OFF = RGB(96, 96, 96);
constexpr COLORREF COLOR_RING = RGB(30, 30, 30);

std::atomic<bool> macroEnabled(true);
std::atomic<bool> leftClickActive(false);
std::atomic<bool> rightClickActive(false);

HWND g_hwnd = nullptr;
HHOOK g_mouseHook = nullptr;

void clickMouse(int button) {
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = (button == 0) ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_RIGHTDOWN;
    SendInput(1, &input, sizeof(INPUT));

    input.mi.dwFlags = (button == 0) ? MOUSEEVENTF_LEFTUP : MOUSEEVENTF_RIGHTUP;
    SendInput(1, &input, sizeof(INPUT));
}

void clickLeftThread() {
    const double delayMs = 1000.0 / LEFT_CPS;
    while (leftClickActive) {
        clickMouse(0);
        std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(delayMs));
    }
}

void clickRightThread() {
    const double delayMs = 1000.0 / RIGHT_CPS;
    while (rightClickActive) {
        clickMouse(1);
        std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(delayMs));
    }
}

LRESULT CALLBACK MouseHookCallback(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && macroEnabled) {
        const MSLLHOOKSTRUCT* pMouseStruct = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);

        if (wParam == WM_XBUTTONDOWN) {
            if (GET_XBUTTON_WPARAM(pMouseStruct->mouseData) == XBUTTON1) {
                if (!rightClickActive) {
                    rightClickActive = true;
                    std::thread(clickRightThread).detach();
                }
            } else if (GET_XBUTTON_WPARAM(pMouseStruct->mouseData) == XBUTTON2) {
                if (!leftClickActive) {
                    leftClickActive = true;
                    std::thread(clickLeftThread).detach();
                }
            }
        } else if (wParam == WM_XBUTTONUP) {
            if (GET_XBUTTON_WPARAM(pMouseStruct->mouseData) == XBUTTON1) {
                rightClickActive = false;
            } else if (GET_XBUTTON_WPARAM(pMouseStruct->mouseData) == XBUTTON2) {
                leftClickActive = false;
            }
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

HICON CreateAppIcon() {
    constexpr int sz = 32;
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbmColor = CreateCompatibleBitmap(hdcScreen, sz, sz);
    HBITMAP hbmMask = CreateBitmap(sz, sz, 1, 1, nullptr);
    SelectObject(hdcMem, hbmColor);

    HBRUSH bg = CreateSolidBrush(RGB(70, 70, 70));
    RECT r = { 0, 0, sz, sz };
    FillRect(hdcMem, &r, bg);
    DeleteObject(bg);

    HPEN nullPen = static_cast<HPEN>(GetStockObject(NULL_PEN));
    SelectObject(hdcMem, nullPen);
    HBRUSH dot = CreateSolidBrush(COLOR_ON);
    SelectObject(hdcMem, dot);
    Ellipse(hdcMem, 7, 7, 25, 25);
    DeleteObject(dot);

    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmColor = hbmColor;
    ii.hbmMask = hbmMask;
    HICON icon = CreateIconIndirect(&ii);

    DeleteObject(hbmColor);
    DeleteObject(hbmMask);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
    return icon;
}

RECT GetButtonRect(const RECT& client) {
    const int w = client.right - client.left;
    const int h = client.bottom - client.top;
    const int btn = (w < h ? w : h) * 55 / 100;
    const int cx = (w - btn) / 2;
    const int cy = (h - btn) / 2;
    RECT btnRc = { cx, cy, cx + btn, cy + btn };
    return btnRc;
}

void PaintWindow(HDC hdc, const RECT& client) {
    HBRUSH bg = CreateSolidBrush(COLOR_BG);
    FillRect(hdc, &client, bg);
    DeleteObject(bg);

    const RECT btnRc = GetButtonRect(client);
    const COLORREF fill = macroEnabled ? COLOR_ON : COLOR_OFF;

    HBRUSH brush = CreateSolidBrush(fill);
    HPEN ring = CreatePen(PS_SOLID, 3, COLOR_RING);
    HGDIOBJ oldPen = SelectObject(hdc, ring);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);

    const int radius = (btnRc.right - btnRc.left) / 6;
    RoundRect(hdc, btnRc.left, btnRc.top, btnRc.right, btnRc.bottom, radius, radius);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(ring);
}

bool HitButton(HWND hwnd, int x, int y) {
    RECT client;
    GetClientRect(hwnd, &client);
    const RECT btnRc = GetButtonRect(client);
    POINT pt = { x, y };
    return PtInRect(&btnRc, pt) != 0;
}

void ToggleMacro(HWND hwnd) {
    macroEnabled = !macroEnabled;
    if (!macroEnabled) {
        leftClickActive = false;
        rightClickActive = false;
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT client;
        GetClientRect(hwnd, &client);
        PaintWindow(hdc, client);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
        if (HitButton(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam))) {
            ToggleMacro(hwnd);
        }
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            SetCursor(LoadCursor(nullptr, HitButton(hwnd, pt.x, pt.y) ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int GetWindowClientSize() {
    int size = GetSystemMetrics(SM_CXSCREEN) * 3 / 10;
    if (size < 216) size = 216;
    if (size > 330) size = 330;
    return size;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nShow) {
    timeBeginPeriod(1);

    g_mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookCallback, nullptr, 0);
    if (!g_mouseHook) {
        MessageBoxW(nullptr, L"Nie mozna ustawic hooka myszy.", L"MakroBedy", MB_ICONERROR);
        timeEndPeriod(1);
        return 1;
    }

    HICON appIcon = CreateAppIcon();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(COLOR_BG);
    wc.lpszClassName = L"MakroBedyWindow";
    wc.hIcon = appIcon;
    wc.hIconSm = appIcon;
    RegisterClassExW(&wc);

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    const int clientSize = GetWindowClientSize();

    RECT wr = { 0, 0, clientSize, clientSize };
    AdjustWindowRect(&wr, style, FALSE);

    const int winW = wr.right - wr.left;
    const int winH = wr.bottom - wr.top;
    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    const int screenH = GetSystemMetrics(SM_CYSCREEN);
    const int x = (screenW - winW) / 2;
    const int y = (screenH - winH) / 2;

    g_hwnd = CreateWindowExW(
        0,
        L"MakroBedyWindow",
        L"MakroBedy",
        style,
        x, y, winW, winH,
        nullptr, nullptr, hInst, nullptr
    );

    ShowWindow(g_hwnd, nShow);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWindowsHookEx(g_mouseHook);
    timeEndPeriod(1);
    DestroyIcon(appIcon);
    return static_cast<int>(msg.wParam);
}
