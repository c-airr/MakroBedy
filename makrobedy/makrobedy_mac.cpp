#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <csignal>
#include <ApplicationServices/ApplicationServices.h>

std::atomic<bool> leftClickActive(false);
std::atomic<bool> rightClickActive(false);
std::atomic<bool> running(true);

static CFMachPortRef g_eventTap = nullptr;

int leftCPS = 15;
int rightCPS = 22;

void signalHandler(int) {
    running = false;
    CFRunLoopStop(CFRunLoopGetCurrent());
}

void clickMouse(int button) {
    CGEventRef locEvent = CGEventCreate(NULL);
    CGPoint point = CGEventGetLocation(locEvent);
    CFRelease(locEvent);

    CGEventType downType = (button == 0) ? kCGEventLeftMouseDown : kCGEventRightMouseDown;
    CGEventType upType = (button == 0) ? kCGEventLeftMouseUp : kCGEventRightMouseUp;
    CGMouseButton mouseButton = (button == 0) ? kCGMouseButtonLeft : kCGMouseButtonRight;

    CGEventRef down = CGEventCreateMouseEvent(NULL, downType, point, mouseButton);
    CGEventRef up = CGEventCreateMouseEvent(NULL, upType, point, mouseButton);
    CGEventPost(kCGHIDEventTap, down);
    CGEventPost(kCGHIDEventTap, up);
    CFRelease(down);
    CFRelease(up);
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

void tapWatchdog(CFRunLoopTimerRef, void*) {
    if (g_eventTap && !CGEventTapIsEnabled(g_eventTap)) {
        CGEventTapEnable(g_eventTap, true);
    }
}

CGEventRef eventCallback(CGEventTapProxy, CGEventType type, CGEventRef event, void*) {
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        if (g_eventTap) {
            CGEventTapEnable(g_eventTap, true);
        }
        return event;
    }

    if (type == kCGEventOtherMouseDown) {
        int button = (int)CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber);
        if (button == 3 && !rightClickActive) {
            rightClickActive = true;
            std::thread(clickRightThread).detach();
        } else if (button == 4 && !leftClickActive) {
            leftClickActive = true;
            std::thread(clickLeftThread).detach();
        }
    } else if (type == kCGEventOtherMouseUp) {
        int button = (int)CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber);
        if (button == 3) {
            rightClickActive = false;
        } else if (button == 4) {
            leftClickActive = false;
        }
    }

    return event;
}

int main() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    std::cout << "================================================\n";
    std::cout << "  KLIKER - HIGH PRECISION TIMING (macOS)\n";
    std::cout << "================================================\n\n";
    std::cout << "[INFO] LEFT CPS: " << leftCPS << ", RIGHT CPS: " << rightCPS << "\n";

    CGEventMask mask = CGEventMaskBit(kCGEventOtherMouseDown) |
                       CGEventMaskBit(kCGEventOtherMouseUp);

    g_eventTap = CGEventTapCreate(
        kCGHIDEventTap,
        kCGHeadInsertEventTap,
        kCGEventTapOptionDefault,
        mask,
        eventCallback,
        NULL
    );

    if (!g_eventTap) {
        std::cerr << "[BLAD] Nie mozna utworzyc event tap!\n";
        std::cerr << "[BLAD] Nadaj uprawnienia: Ustawienia -> Prywatnosc -> Dostepnosc\n";
        return 1;
    }

    CFRunLoopSourceRef runLoopSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, g_eventTap, 0);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), runLoopSource, kCFRunLoopCommonModes);
    CGEventTapEnable(g_eventTap, true);

    CFRunLoopTimerRef watchdog = CFRunLoopTimerCreate(
        kCFAllocatorDefault,
        CFAbsoluteTimeGetCurrent() + 1.0,
        1.0,
        0,
        0,
        tapWatchdog,
        NULL
    );
    CFRunLoopAddTimer(CFRunLoopGetCurrent(), watchdog, kCFRunLoopCommonModes);

    std::cout << "\n========== MAKRO WLACZONE ==========\n";
    std::cout << "Boczny przycisk LEWY (button 4)  -> RIGHT CLICK (" << rightCPS << " CPS)\n";
    std::cout << "Boczny przycisk PRAWY (button 5) -> LEFT CLICK  (" << leftCPS << " CPS)\n";
    std::cout << "Ctrl+C w terminalu -> wyjscie\n";
    std::cout << "===================================\n\n";

    CFRunLoopRun();

    leftClickActive = false;
    rightClickActive = false;

    CGEventTapEnable(g_eventTap, false);
    CFRelease(watchdog);
    CFRelease(runLoopSource);
    CFRelease(g_eventTap);
    g_eventTap = nullptr;

    std::cout << "\n[INFO] Zamknieto makro\n";
    return 0;
}
