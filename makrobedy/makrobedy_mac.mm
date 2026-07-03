#import <Cocoa/Cocoa.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <ApplicationServices/ApplicationServices.h>

static std::atomic<bool> macroEnabled(true);
static std::atomic<bool> leftClickActive(false);
static std::atomic<bool> rightClickActive(false);
static CFMachPortRef g_eventTap = nullptr;

static const int LEFT_CPS = 15;
static const int RIGHT_CPS = 22;

static NSColor* colorBg() {
    return [NSColor colorWithRed:42.0/255 green:42.0/255 blue:42.0/255 alpha:1.0];
}

static NSColor* colorOn() {
    return [NSColor colorWithRed:34.0/255 green:197.0/255 blue:94.0/255 alpha:1.0];
}

static NSColor* colorOff() {
    return [NSColor colorWithRed:96.0/255 green:96.0/255 blue:96.0/255 alpha:1.0];
}

static NSRect buttonRectForBounds(NSRect bounds) {
    CGFloat side = MIN(bounds.size.width, bounds.size.height) * 0.55;
    CGFloat x = (bounds.size.width - side) / 2.0;
    CGFloat y = (bounds.size.height - side) / 2.0;
    return NSMakeRect(x, y, side, side);
}

static void clickMouse(int button) {
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

static void clickLeftThread() {
    const double delayMs = 1000.0 / LEFT_CPS;
    while (leftClickActive) {
        clickMouse(0);
        std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(delayMs));
    }
}

static void clickRightThread() {
    const double delayMs = 1000.0 / RIGHT_CPS;
    while (rightClickActive) {
        clickMouse(1);
        std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(delayMs));
    }
}

static void tapWatchdog(CFRunLoopTimerRef, void*) {
    if (g_eventTap && !CGEventTapIsEnabled(g_eventTap)) {
        CGEventTapEnable(g_eventTap, true);
    }
}

static CGEventRef eventCallback(CGEventTapProxy, CGEventType type, CGEventRef event, void*) {
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        if (g_eventTap) {
            CGEventTapEnable(g_eventTap, true);
        }
        return event;
    }

    if (!macroEnabled) {
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

@interface ToggleView : NSView
@end

@implementation ToggleView

- (void)drawRect:(NSRect)dirtyRect {
    [colorBg() setFill];
    NSRectFill(self.bounds);

    NSRect btn = buttonRectForBounds(self.bounds);
    NSColor* fill = macroEnabled ? colorOn() : colorOff();
    [fill setFill];

    CGFloat radius = btn.size.width / 6.0;
    NSBezierPath* path = [NSBezierPath bezierPathWithRoundedRect:btn xRadius:radius yRadius:radius];
    [path fill];

    [[NSColor colorWithRed:30.0/255 green:30.0/255 blue:30.0/255 alpha:1.0] setStroke];
    path.lineWidth = 3.0;
    [path stroke];
}

- (void)mouseDown:(NSEvent*)event {
    NSPoint pt = [self convertPoint:event.locationInWindow fromView:nil];
    if (NSPointInRect(pt, buttonRectForBounds(self.bounds))) {
        macroEnabled = !macroEnabled;
        if (!macroEnabled) {
            leftClickActive = false;
            rightClickActive = false;
        }
        [self setNeedsDisplay:YES];
    }
}

- (void)resetCursorRects {
    [self addCursorRect:buttonRectForBounds(self.bounds) cursor:[NSCursor pointingHandCursor]];
}

@end

static bool setupEventTap() {
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
        return false;
    }

    CFRunLoopSourceRef runLoopSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, g_eventTap, 0);
    CFRunLoopAddSource(CFRunLoopGetMain(), runLoopSource, kCFRunLoopCommonModes);
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
    CFRunLoopAddTimer(CFRunLoopGetMain(), watchdog, kCFRunLoopCommonModes);
    return true;
}

static CGFloat windowClientSize() {
    CGFloat screenW = [NSScreen mainScreen].frame.size.width;
    CGFloat size = screenW * 3.0 / 10.0;
    if (size < 216) size = 216;
    if (size > 330) size = 330;
    return size;
}

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        if (!setupEventTap()) {
            NSAlert* alert = [[NSAlert alloc] init];
            alert.messageText = @"MakroBedy";
            alert.informativeText = @"Brak uprawnien Dostepnosc. Ustawienia -> Prywatnosc -> Dostepnosc.";
            [alert runModal];
            return 1;
        }

        CGFloat size = windowClientSize();
        NSScreen* screen = [NSScreen mainScreen];
        NSRect screenFrame = screen.visibleFrame;
        CGFloat x = screenFrame.origin.x + (screenFrame.size.width - size) / 2.0;
        CGFloat y = screenFrame.origin.y + (screenFrame.size.height - size) / 2.0;

        NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(x, y, size, size)
                                                       styleMask:NSWindowStyleMaskTitled |
                                                                 NSWindowStyleMaskClosable |
                                                                 NSWindowStyleMaskMiniaturizable
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        [window setTitle:@"MakroBedy"];
        [window setBackgroundColor:colorBg()];
        [window setMovableByWindowBackground:NO];

        ToggleView* view = [[ToggleView alloc] initWithFrame:NSMakeRect(0, 0, size, size)];
        [window setContentView:view];
        [window center];
        [window makeKeyAndOrderFront:nil];

        NSImage* icon = [[NSImage alloc] initWithSize:NSMakeSize(32, 32)];
        [icon lockFocus];
        [[NSColor colorWithRed:70.0/255 green:70.0/255 blue:70.0/255 alpha:1.0] setFill];
        NSRectFill(NSMakeRect(0, 0, 32, 32));
        [colorOn() setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(7, 7, 18, 18)] fill];
        [icon unlockFocus];
        [NSApp setApplicationIconImage:icon];

        [NSApp run];
    }
    return 0;
}
