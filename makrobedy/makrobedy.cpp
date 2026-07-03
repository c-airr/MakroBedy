#include <windows.h>
#include <thread>
#include <atomic>
#include <chrono>

#pragma comment(lib, "winmm.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

constexpr int LEFT_CPS = 15;
constexpr int RIGHT_CPS = 22;

constexpr COLORREF COLOR_ON = RGB(34, 197, 94);
constexpr COLORREF COLOR_OFF = RGB(96, 96, 96);
constexpr COLORREF COLOR_RING = RGB(55, 55, 55);

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

void PaintToggle(HDC hdc, const RECT& rc) {
    const COLORREF fill = macroEnabled ? COLOR_ON : COLOR_OFF;
    HBRUSH brush = CreateSolidBrush(fill);
    FillRect(hdc, &rc, brush);
    DeleteObject(brush);

    HPEN ring = CreatePen(PS_SOLID, 2, COLOR_RING);
    HGDIOBJ oldPen = SelectObject(hdc, ring);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));

    const int pad = 6;
    RoundRect(hdc, pad, pad, rc.right - pad, rc.bottom - pad, 14, 14);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(ring);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        PaintToggle(hdc, rc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
        macroEnabled = !macroEnabled;
        if (!macroEnabled) {
            leftClickActive = false;
            rightClickActive = false;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_NCHITTEST:
        return HTCAPTION;

    case WM_ERASEBKGND:
        return 1;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
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
    wc.hCursor = LoadCursor(nullptr, IDC_HAND);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH));
    wc.lpszClassName = L"MakroBedyToggle";
    wc.hIcon = appIcon;
    wc.hIconSm = appIcon;
    RegisterClassExW(&wc);

    int size = GetSystemMetrics(SM_CXSCREEN) / 10;
    if (size < 72) size = 72;
    if (size > 110) size = 110;

    const int x = GetSystemMetrics(SM_CXSCREEN) - size - 24;
    const int y = 24;

    g_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"MakroBedyToggle",
        L"",
        WS_POPUP,
        x, y, size, size,
        nullptr, nullptr, hInst, nullptr
    );

    HRGN rgn = CreateRoundRectRgn(0, 0, size + 1, size + 1, 18, 18);
    SetWindowRgn(g_hwnd, rgn, TRUE);

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
