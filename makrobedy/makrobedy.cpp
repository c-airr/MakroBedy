#include <windows.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>

#pragma comment(lib, "winmm.lib")

std::atomic<bool> leftClickActive(false);
std::atomic<bool> rightClickActive(false);

// Ustawione zgodnie z Twoim ¿yczeniem
int leftCPS = 15;
int rightCPS = 20;

void clickMouse(int button) {
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = (button == 0) ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_RIGHTDOWN;
    SendInput(1, &input, sizeof(INPUT));

    input.mi.dwFlags = (button == 0) ? MOUSEEVENTF_LEFTUP : MOUSEEVENTF_RIGHTUP;
    SendInput(1, &input, sizeof(INPUT));
}

void clickLeftThread() {
    double delayMs = 1000.0 / leftCPS;
    std::cout << "\n[KLIK] Lewy przycisk - START (LEFT CLICK, " << leftCPS << " CPS)\n";

    while (leftClickActive) {
        clickMouse(0);
        std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(delayMs));
    }

    std::cout << "[KLIK] Lewy przycisk - STOP\n";
}

void clickRightThread() {
    double delayMs = 1000.0 / rightCPS;
    std::cout << "\n[KLIK] Prawy przycisk - START (RIGHT CLICK, " << rightCPS << " CPS)\n";

    while (rightClickActive) {
        clickMouse(1);
        std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(delayMs));
    }

    std::cout << "[KLIK] Prawy przycisk - STOP\n";
}

LRESULT CALLBACK MouseHookCallback(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        MSLLHOOKSTRUCT* pMouseStruct = (MSLLHOOKSTRUCT*)lParam;

        if (wParam == WM_XBUTTONDOWN) {
            if (GET_XBUTTON_WPARAM(pMouseStruct->mouseData) == XBUTTON1) {
                if (!rightClickActive) {
                    rightClickActive = true;
                    std::thread(clickRightThread).detach();
                }
            }
            else if (GET_XBUTTON_WPARAM(pMouseStruct->mouseData) == XBUTTON2) {
                if (!leftClickActive) {
                    leftClickActive = true;
                    std::thread(clickLeftThread).detach();
                }
            }
        }
        else if (wParam == WM_XBUTTONUP) {
            if (GET_XBUTTON_WPARAM(pMouseStruct->mouseData) == XBUTTON1) {
                rightClickActive = false;
            }
            else if (GET_XBUTTON_WPARAM(pMouseStruct->mouseData) == XBUTTON2) {
                leftClickActive = false;
            }
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

int main() {
    timeBeginPeriod(1);

    std::cout << "================================================\n";
    std::cout << "  KLIKER - HIGH PRECISION TIMING\n";
    std::cout << "================================================\n\n";
    std::cout << "[INFO] LEFT CPS: " << leftCPS << ", RIGHT CPS: " << rightCPS << "\n";

    std::cout << "\n[INFO] Ustawianie mouse hooka...\n";
    HHOOK hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookCallback, NULL, 0);

    if (!hMouseHook) {
        std::cerr << "[BLAD] Nie mozna ustawic hooka!\n";
        timeEndPeriod(1);
        return 1;
    }

    std::cout << "\n========== MAKRO WLACZONE ==========\n";
    std::cout << "Boczny przycisk LEWY (XBUTTON1)  -> RIGHT CLICK (" << rightCPS << " CPS)\n";
    std::cout << "Boczny przycisk PRAWY (XBUTTON2) -> LEFT CLICK  (" << leftCPS << " CPS)\n";
    std::cout << "===================================\n\n";

    MSG msg;
    while (true) {
        if (GetMessage(&msg, NULL, 0, 0)) {
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
                break;
            }
        }
        else {
            break;
        }
    }

    UnhookWindowsHookEx(hMouseHook);
    timeEndPeriod(1);

    std::cout << "\n[INFO] Zamknieto makro\n";
    return 0;
}