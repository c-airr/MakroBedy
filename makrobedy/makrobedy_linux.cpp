#include <gtk/gtk.h>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>

static constexpr int LEFT_CPS = 15;
static constexpr int RIGHT_CPS = 22;

static constexpr double COLOR_BG[] = { 24.0 / 255, 24.0 / 255, 27.0 / 255 };
static constexpr double COLOR_ON[] = { 21.0 / 255, 128.0 / 255, 61.0 / 255 };
static constexpr double COLOR_OFF[] = { 39.0 / 255, 39.0 / 255, 42.0 / 255 };
static constexpr double COLOR_BORDER_ON[] = { 34.0 / 255, 150.0 / 255, 72.0 / 255 };
static constexpr double COLOR_BORDER_OFF[] = { 63.0 / 255, 63.0 / 255, 70.0 / 255 };
static constexpr double COLOR_SHADOW[] = { 9.0 / 255, 9.0 / 255, 11.0 / 255 };
static constexpr double COLOR_HIGHLIGHT_ON[] = { 40.0 / 255, 170.0 / 255, 88.0 / 255 };
static constexpr double COLOR_HIGHLIGHT_OFF[] = { 72.0 / 255, 72.0 / 255, 78.0 / 255 };

static std::atomic<bool> macroEnabled(true);
static std::atomic<bool> leftClickActive(false);
static std::atomic<bool> rightClickActive(false);
static std::atomic<bool> x11Running(false);
static std::atomic<bool> waylandSession(false);

static Display* g_dpy = nullptr;
static std::mutex g_xMutex;
static GtkWidget* g_toggleArea = nullptr;
static GtkWidget* g_infoLabel = nullptr;

static bool isWaylandSession() {
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    const char* session = std::getenv("XDG_SESSION_TYPE");
    if (wayland && wayland[0] != '\0') {
        return true;
    }
    if (session && std::strcmp(session, "wayland") == 0) {
        return true;
    }
    return false;
}

static void clickMouse(int button) {
    std::lock_guard<std::mutex> lock(g_xMutex);
    if (!g_dpy) {
        return;
    }

    const unsigned int xButton = (button == 0) ? 1u : 3u;
    XTestFakeButtonEvent(g_dpy, xButton, True, CurrentTime);
    XTestFakeButtonEvent(g_dpy, xButton, False, CurrentTime);
    XFlush(g_dpy);
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

static gboolean showInfoMessage(gpointer data) {
    gtk_label_set_text(GTK_LABEL(g_infoLabel), static_cast<const char*>(data));
    return G_SOURCE_REMOVE;
}

static void x11InputThread() {
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        g_idle_add(showInfoMessage, const_cast<char*>("Brak polaczenia z X11. Makro niedostepne."));
        return;
    }

    int xtestEvent = 0;
    int xtestError = 0;
    int xtestMajor = 0;
    int xtestMinor = 0;
    if (!XTestQueryExtension(dpy, &xtestEvent, &xtestError, &xtestMajor, &xtestMinor)) {
        g_idle_add(showInfoMessage, const_cast<char*>("Brak rozszerzenia XTest. Makro niedostepne."));
        XCloseDisplay(dpy);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_xMutex);
        g_dpy = dpy;
    }

    Window root = DefaultRootWindow(dpy);
    XGrabButton(dpy, 8, AnyModifier, root, True,
        ButtonPressMask | ButtonReleaseMask,
        GrabModeAsync, GrabModeAsync, None, None);
    XGrabButton(dpy, 9, AnyModifier, root, True,
        ButtonPressMask | ButtonReleaseMask,
        GrabModeAsync, GrabModeAsync, None, None);
    XSync(dpy, False);

    x11Running = true;

    while (x11Running) {
        while (XPending(dpy) > 0) {
            XEvent ev{};
            XNextEvent(dpy, &ev);

            if (!macroEnabled || waylandSession) {
                continue;
            }

            if (ev.type == ButtonPress) {
                if (ev.xbutton.button == 8 && !rightClickActive) {
                    rightClickActive = true;
                    std::thread(clickRightThread).detach();
                } else if (ev.xbutton.button == 9 && !leftClickActive) {
                    leftClickActive = true;
                    std::thread(clickLeftThread).detach();
                }
            } else if (ev.type == ButtonRelease) {
                if (ev.xbutton.button == 8) {
                    rightClickActive = false;
                } else if (ev.xbutton.button == 9) {
                    leftClickActive = false;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    XUngrabButton(dpy, 8, AnyModifier, root);
    XUngrabButton(dpy, 9, AnyModifier, root);

    {
        std::lock_guard<std::mutex> lock(g_xMutex);
        g_dpy = nullptr;
    }
    XCloseDisplay(dpy);
}

static void cairoSetColor(cairo_t* cr, const double rgb[3], double alpha = 1.0) {
    cairo_set_source_rgba(cr, rgb[0], rgb[1], rgb[2], alpha);
}

static void drawRoundedRect(cairo_t* cr, double x, double y, double w, double h, double r) {
    const double degrees = G_PI / 180.0;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -90 * degrees, 0 * degrees);
    cairo_arc(cr, x + w - r, y + h - r, r, 0 * degrees, 90 * degrees);
    cairo_arc(cr, x + r, y + h - r, r, 90 * degrees, 180 * degrees);
    cairo_arc(cr, x + r, y + r, r, 180 * degrees, 270 * degrees);
    cairo_close_path(cr);
}

static GdkRectangle buttonRect(int width, int height) {
    const int side = static_cast<int>((width < height ? width : height) * 0.55);
    const int x = (width - side) / 2;
    const int y = (height - side) / 2;
    GdkRectangle rect{};
    rect.x = x;
    rect.y = y;
    rect.width = side;
    rect.height = side;
    return rect;
}

static gboolean onToggleDraw(GtkWidget* widget, cairo_t* cr, gpointer) {
    const int width = gtk_widget_get_allocated_width(widget);
    const int height = gtk_widget_get_allocated_height(widget);

    cairoSetColor(cr, COLOR_BG);
    cairo_paint(cr);

    GdkRectangle btn = buttonRect(width, height);
    const double radius = btn.width / 5.0;

    drawRoundedRect(cr, btn.x, btn.y + 5, btn.width, btn.height, radius);
    cairoSetColor(cr, COLOR_SHADOW);
    cairo_fill(cr);

    const double* fill = macroEnabled ? COLOR_ON : COLOR_OFF;
    const double* border = macroEnabled ? COLOR_BORDER_ON : COLOR_BORDER_OFF;
    const double* highlight = macroEnabled ? COLOR_HIGHLIGHT_ON : COLOR_HIGHLIGHT_OFF;

    drawRoundedRect(cr, btn.x, btn.y, btn.width, btn.height, radius);
    cairoSetColor(cr, fill);
    cairo_fill_preserve(cr);
    cairoSetColor(cr, border);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    cairo_rectangle(cr, btn.x + radius * 0.5, btn.y + 6, btn.width - radius, 1.0);
    cairoSetColor(cr, highlight, macroEnabled ? 0.55 : 0.35);
    cairo_fill(cr);

    return FALSE;
}

static gboolean onToggleClick(GtkWidget* widget, GdkEventButton* event, gpointer) {
    if (event->button != 1) {
        return FALSE;
    }

    const int width = gtk_widget_get_allocated_width(widget);
    const int height = gtk_widget_get_allocated_height(widget);
    GdkRectangle btn = buttonRect(width, height);

    if (event->x >= btn.x && event->x < btn.x + btn.width &&
        event->y >= btn.y && event->y < btn.y + btn.height) {
        if (waylandSession) {
            return TRUE;
        }

        macroEnabled = !macroEnabled;
        if (!macroEnabled) {
            leftClickActive = false;
            rightClickActive = false;
        }
        gtk_widget_queue_draw(widget);
    }

    return TRUE;
}

static void onWindowDestroy(GtkWidget*, gpointer) {
    x11Running = false;
    gtk_main_quit();
}

static int windowClientSize() {
    GdkDisplay* display = gdk_display_get_default();
    GdkMonitor* monitor = gdk_display_get_primary_monitor(display);
    GdkRectangle geom{};
    gdk_monitor_get_geometry(monitor, &geom);

    int size = geom.width * 3 / 10;
    if (size < 216) size = 216;
    if (size > 330) size = 330;
    return size;
}

int main(int argc, char* argv[]) {
    gtk_init(&argc, &argv);

    waylandSession = isWaylandSession();

    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "MakroBedy");
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(window), windowClientSize(), windowClientSize());

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), box);

    g_toggleArea = gtk_drawing_area_new();
    gtk_widget_set_size_request(g_toggleArea, windowClientSize(), windowClientSize());
    gtk_widget_add_events(g_toggleArea, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(g_toggleArea, "draw", G_CALLBACK(onToggleDraw), nullptr);
    g_signal_connect(g_toggleArea, "button-press-event", G_CALLBACK(onToggleClick), nullptr);
    gtk_box_pack_start(GTK_BOX(box), g_toggleArea, TRUE, TRUE, 0);

    g_infoLabel = gtk_label_new(nullptr);
    gtk_label_set_line_wrap(GTK_LABEL(g_infoLabel), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(g_infoLabel), 36);
    gtk_widget_set_margin_start(g_infoLabel, 12);
    gtk_widget_set_margin_end(g_infoLabel, 12);
    gtk_widget_set_margin_bottom(g_infoLabel, 10);
    gtk_widget_set_halign(g_infoLabel, GTK_ALIGN_CENTER);

    if (waylandSession) {
        macroEnabled = false;
        gtk_label_set_text(GTK_LABEL(g_infoLabel),
            "Wayland: makro nie dziala. Przelacz sesje na X11 (np. \"Ubuntu on Xorg\" przy logowaniu).");
        gtk_widget_show(g_infoLabel);
        gtk_box_pack_start(GTK_BOX(box), g_infoLabel, FALSE, FALSE, 0);
    } else {
        gtk_label_set_text(GTK_LABEL(g_infoLabel), "");
        gtk_widget_hide(g_infoLabel);
        std::thread(x11InputThread).detach();
    }

    g_signal_connect(window, "destroy", G_CALLBACK(onWindowDestroy), nullptr);

    gtk_widget_show_all(window);
    gtk_main();

    x11Running = false;
    return 0;
}
