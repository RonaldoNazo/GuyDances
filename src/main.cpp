#include <GLFW/glfw3.h>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <windows.h>
#else
#define GLFW_EXPOSE_NATIVE_X11
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#endif
#include <GLFW/glfw3native.h>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "audio.h"
#include "font8x8.h"
#include "single_instance.h"
#include "sprite.h"

namespace {

constexpr int MENU_SCALE = 2;
constexpr int GLYPH = 8 * MENU_SCALE;
constexpr int ROW_H = 24;
constexpr int PAD = 8;

enum Action {
    A_PAUSE = 1,
    A_STOP,
    A_QUIT,
    A_H1,
    A_H2,
    A_H4,
    A_H8,
};

struct Item {
    const char* label;
    int action;
};

struct App {
    GLFWwindow* win = nullptr;
    Sprite sprite;
    GLuint sprite_tex = 0;
    GLuint font_tex = 0;
    std::vector<uint8_t> pixels;

    int frame = 0;
    double frame_acc = 0;
    double last_time = 0;
    bool frame_dirty = true;

    bool visible = false;
    bool paused = false;
    bool dragging = false;
    double grab_x = 0;
    double grab_y = 0;

    bool menu_open = false;
    int menu_page = 0;
    int menu_w = 0;
    int menu_h = 0;
    int menu_x = 0;
    int menu_y = 0;
    int hover = -1;

    double snooze_until = 0;
    bool demo = false;

    int pos_x = 0;
    int pos_y = 0;
    bool pos_loaded = false;

    bool music_stable = false;
    double music_since = 0;
    double silence_since = 0;
};

std::vector<Item> items_for(const App& a) {
    if (a.menu_page == 0) {
        return {
            {a.paused ? "Resume" : "Pause", A_PAUSE},
            {"Stop", A_STOP},
            {"Quit", A_QUIT},
        };
    }
    return {
        {"1 hour", A_H1},
        {"2 hours", A_H2},
        {"4 hours", A_H4},
        {"8 hours", A_H8},
    };
}

void quad(float x, float y, float w, float h, float r, float g, float b, float a) {
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

void draw_text(const App& a, const char* text, float x, float y, float r, float g, float b,
               float alpha) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, a.font_tex);
    glColor4f(r, g, b, alpha);
    glBegin(GL_QUADS);
    for (const char* p = text; *p; ++p) {
        unsigned char c = static_cast<unsigned char>(*p);
        if (c < 32 || c > 127) c = '?';
        if (c != ' ') {
            float u0 = static_cast<float>(c & 15) * 8.0f / 128.0f;
            float v0 = static_cast<float>(c >> 4) * 8.0f / 64.0f;
            float u1 = u0 + 8.0f / 128.0f;
            float v1 = v0 + 8.0f / 64.0f;
            float x1 = x + GLYPH;
            float y1 = y + GLYPH;
            glTexCoord2f(u0, v0);
            glVertex2f(x, y);
            glTexCoord2f(u1, v0);
            glVertex2f(x1, y);
            glTexCoord2f(u1, v1);
            glVertex2f(x1, y1);
            glTexCoord2f(u0, v1);
            glVertex2f(x, y1);
        }
        x += GLYPH;
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

GLuint make_font_texture() {
    constexpr int W = 128;
    constexpr int H = 64;
    std::vector<uint8_t> pix(W * H * 4, 0);
    for (int c = 0; c < 128; ++c) {
        int cx = (c & 15) * 8;
        int cy = (c >> 4) * 8;
        for (int y = 0; y < 8; ++y) {
            unsigned char bits = font8x8_basic[c][y];
            for (int x = 0; x < 8; ++x) {
                if (bits & (1u << x)) {
                    size_t o = (static_cast<size_t>(cy + y) * W + cx + x) * 4;
                    pix[o + 0] = 255;
                    pix[o + 1] = 255;
                    pix[o + 2] = 255;
                    pix[o + 3] = 255;
                }
            }
        }
    }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, pix.data());
    return tex;
}

void hide_from_taskbar(GLFWwindow* window) {
#ifdef _WIN32
    HWND handle = glfwGetWin32Window(window);
    LONG_PTR style = GetWindowLongPtrW(handle, GWL_EXSTYLE);
    style = (style | WS_EX_TOOLWINDOW) & ~WS_EX_APPWINDOW;
    SetWindowLongPtrW(handle, GWL_EXSTYLE, style);
#else
    Display* display = glfwGetX11Display();
    Window handle = glfwGetX11Window(window);
    Atom state = XInternAtom(display, "_NET_WM_STATE", False);
    Atom skip = XInternAtom(display, "_NET_WM_STATE_SKIP_TASKBAR", False);
    Atom pager = XInternAtom(display, "_NET_WM_STATE_SKIP_PAGER", False);
    XChangeProperty(display, handle, state, XA_ATOM, 32, PropModeAppend,
                    reinterpret_cast<unsigned char*>(&skip), 1);
    XChangeProperty(display, handle, state, XA_ATOM, 32, PropModeAppend,
                    reinterpret_cast<unsigned char*>(&pager), 1);

    Atom type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom utility = XInternAtom(display, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    XChangeProperty(display, handle, type, XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&utility), 1);
    XFlush(display);
#endif
}

void clamp_to_screens(int& x, int& y, int w, int h) {
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    if (count <= 0) return;

    int min_x = INT_MAX, min_y = INT_MAX, max_x = INT_MIN, max_y = INT_MIN;
    for (int i = 0; i < count; ++i) {
        int mx, my, mw, mh;
        glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);
        min_x = std::min(min_x, mx);
        min_y = std::min(min_y, my);
        max_x = std::max(max_x, mx + mw);
        max_y = std::max(max_y, my + mh);
    }
    x = std::clamp(x, min_x, std::max(min_x, max_x - w));
    y = std::clamp(y, min_y, std::max(min_y, max_y - h));
}

std::string config_dir() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    return appdata ? std::string(appdata) + "\\GuyDances" : std::string(".");
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return std::string(xdg) + "/guydances";
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/.config/guydances" : std::string(".");
#endif
}

void save_state(const App& a) {
    std::error_code ec;
    std::filesystem::create_directories(config_dir(), ec);
    std::ofstream out(config_dir() + "/state");
    if (out) out << a.pos_x << " " << a.pos_y << "\n";
}

void load_state(App& a) {
    std::ifstream in(config_dir() + "/state");
    int x, y;
    if (in >> x >> y) {
        a.pos_x = x;
        a.pos_y = y;
        a.pos_loaded = true;
        clamp_to_screens(a.pos_x, a.pos_y, a.sprite.width, a.sprite.height);
    }
}

void default_position(App& a, int& x, int& y) {
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    int wx, wy, ww, wh;
    glfwGetMonitorWorkarea(monitor, &wx, &wy, &ww, &wh);
    x = wx + ww - a.sprite.width - 32;
    y = wy + wh - a.sprite.height - 32;
}

void layout_menu(App& a) {
    std::vector<Item> items = items_for(a);
    int max_chars = 0;
    for (const Item& item : items) {
        max_chars = std::max(max_chars, static_cast<int>(std::strlen(item.label)));
    }
    a.menu_w = std::max(120, max_chars * GLYPH + PAD * 2);
    a.menu_h = PAD * 2 + static_cast<int>(items.size()) * ROW_H;
}

void place_menu(App& a, double cursor_x, double cursor_y) {
    int x = static_cast<int>(std::lround(cursor_x)) - 12;
    int y = static_cast<int>(std::lround(cursor_y)) - 12;
    a.menu_x = std::clamp(x, 0, std::max(0, a.sprite.width - a.menu_w));
    a.menu_y = std::clamp(y, 0, std::max(0, a.sprite.height - a.menu_h));
}

void close_menu(App& a) {
    a.menu_open = false;
    a.hover = -1;
}

void open_menu(App& a) {
    a.menu_page = 0;
    a.hover = -1;
    layout_menu(a);
    double cx = 0;
    double cy = 0;
    glfwGetCursorPos(a.win, &cx, &cy);
    place_menu(a, cx, cy);
    a.menu_open = true;
}

void hide_sprite(App& a) {
    close_menu(a);
    if (a.visible) glfwHideWindow(a.win);
    a.visible = false;
    a.dragging = false;
    a.paused = false;
    a.frame_acc = 0;
}

void show_sprite(App& a) {
    if (!a.pos_loaded) {
        default_position(a, a.pos_x, a.pos_y);
        a.pos_loaded = true;
    }
    clamp_to_screens(a.pos_x, a.pos_y, a.sprite.width, a.sprite.height);
    glfwShowWindow(a.win);
    glfwSetWindowPos(a.win, a.pos_x, a.pos_y);
    glfwSetWindowAttrib(a.win, GLFW_FLOATING, GLFW_TRUE);
    a.visible = true;
    a.frame = 0;
    a.frame_acc = 0;
    a.frame_dirty = true;
    a.last_time = glfwGetTime();
}

void perform_action(App& a, int action) {
    switch (action) {
        case A_PAUSE:
            a.paused = !a.paused;
            close_menu(a);
            break;

        case A_STOP:
            a.menu_page = 1;
            layout_menu(a);
            {
                double cx = 0;
                double cy = 0;
                glfwGetCursorPos(a.win, &cx, &cy);
                place_menu(a, cx, cy);
            }
            a.hover = -1;
            break;

        case A_QUIT:
            close_menu(a);
            glfwSetWindowShouldClose(a.win, GLFW_TRUE);
            break;

        case A_H1:
        case A_H2:
        case A_H4:
        case A_H8: {
            int hours = action == A_H1 ? 1 : action == A_H2 ? 2 : action == A_H4 ? 4 : 8;
            a.snooze_until = glfwGetTime() + hours * 3600.0;
            a.paused = false;
            close_menu(a);
            hide_sprite(a);
            break;
        }

        default:
            break;
    }
}

void draw_menu(App& a) {
    if (!a.menu_open) return;

    glPushMatrix();
    glTranslatef(static_cast<float>(a.menu_x), static_cast<float>(a.menu_y), 0);

    const float w = static_cast<float>(a.menu_w);
    const float h = static_cast<float>(a.menu_h);

    quad(0, 0, w, h, 0.06f, 0.06f, 0.09f, 0.96f);
    quad(0, 0, w, 2, 0.83f, 0.29f, 0.42f, 1.0f);
    quad(0, h - 2, w, 2, 0.83f, 0.29f, 0.42f, 1.0f);
    quad(0, 0, 2, h, 0.83f, 0.29f, 0.42f, 1.0f);
    quad(w - 2, 0, 2, h, 0.83f, 0.29f, 0.42f, 1.0f);

    std::vector<Item> items = items_for(a);
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        float top = static_cast<float>(PAD + i * ROW_H);
        if (i == a.hover) {
            quad(3, top, w - 6, static_cast<float>(ROW_H), 0.83f, 0.29f, 0.42f, 0.30f);
        }
        draw_text(a, items[i].label, static_cast<float>(PAD),
                  top + (ROW_H - GLYPH) / 2.0f, 0.97f, 0.97f, 0.99f, 1.0f);
    }

    glPopMatrix();
}

void render(App& a) {
    int fbw = 0;
    int fbh = 0;
    glfwGetFramebufferSize(a.win, &fbw, &fbh);
    glViewport(0, 0, fbw, fbh);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, a.sprite.width, a.sprite.height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    if (a.frame_dirty) {
        a.sprite.decode(a.frame, a.pixels);
        glBindTexture(GL_TEXTURE_2D, a.sprite_tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, a.sprite.width, a.sprite.height, GL_RGBA,
                        GL_UNSIGNED_BYTE, a.pixels.data());
        a.frame_dirty = false;
    }

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, a.sprite_tex);
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2f(0, 0);
    glTexCoord2f(1, 0);
    glVertex2f(static_cast<float>(a.sprite.width), 0);
    glTexCoord2f(1, 1);
    glVertex2f(static_cast<float>(a.sprite.width), static_cast<float>(a.sprite.height));
    glTexCoord2f(0, 1);
    glVertex2f(0, static_cast<float>(a.sprite.height));
    glEnd();
    glDisable(GL_TEXTURE_2D);

    draw_menu(a);
    glfwSwapBuffers(a.win);
}

int hovered_item(App& a, double x, double y) {
    std::vector<Item> items = items_for(a);
    double lx = x - a.menu_x;
    double ly = y - a.menu_y;
    if (lx < 0 || lx >= a.menu_w || ly < PAD || ly >= a.menu_h) return -1;
    int row = static_cast<int>((ly - PAD) / ROW_H);
    return row >= 0 && row < static_cast<int>(items.size()) ? row : -1;
}

void on_mouse(GLFWwindow* window, int button, int action, int) {
    App& a = *static_cast<App*>(glfwGetWindowUserPointer(window));

    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action != GLFW_PRESS) return;
        if (a.menu_open) {
            close_menu(a);
        } else {
            open_menu(a);
        }
        return;
    }

    if (button != GLFW_MOUSE_BUTTON_LEFT) return;

    if (action == GLFW_PRESS) {
        if (a.menu_open) {
            double x = 0;
            double y = 0;
            glfwGetCursorPos(window, &x, &y);
            int row = hovered_item(a, x, y);
            if (row >= 0) {
                std::vector<Item> items = items_for(a);
                perform_action(a, items[row].action);
            } else {
                close_menu(a);
            }
            return;
        }
        double x = 0;
        double y = 0;
        glfwGetCursorPos(window, &x, &y);
        a.grab_x = x;
        a.grab_y = y;
        a.dragging = true;
    } else if (action == GLFW_RELEASE && a.dragging) {
        a.dragging = false;
        glfwGetWindowPos(window, &a.pos_x, &a.pos_y);
        save_state(a);
    }
}

void on_cursor(GLFWwindow* window, double cx, double cy) {
    App& a = *static_cast<App*>(glfwGetWindowUserPointer(window));

    if (a.menu_open) {
        int row = hovered_item(a, cx, cy);
        if (row != a.hover) a.hover = row;
        return;
    }

    if (!a.dragging) return;

    int wx = 0;
    int wy = 0;
    glfwGetWindowPos(window, &wx, &wy);
    int gx = wx + static_cast<int>(std::lround(cx));
    int gy = wy + static_cast<int>(std::lround(cy));
    int nx = gx - static_cast<int>(std::lround(a.grab_x));
    int ny = gy - static_cast<int>(std::lround(a.grab_y));
    clamp_to_screens(nx, ny, a.sprite.width, a.sprite.height);
    glfwSetWindowPos(window, nx, ny);
}

void on_key(GLFWwindow* window, int key, int, int action, int) {
    App& a = *static_cast<App*>(glfwGetWindowUserPointer(window));
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) close_menu(a);
}

void on_focus(GLFWwindow* window, int focused) {
    App& a = *static_cast<App*>(glfwGetWindowUserPointer(window));
    if (!focused) close_menu(a);
}

void error_callback(int code, const char* description) {
    std::fprintf(stderr, "glfw error %d: %s\n", code, description);
}

}  // namespace

int main(int argc, char** argv) {
    App app;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--demo") == 0) app.demo = true;
        if (std::strcmp(argv[i], "--debug") == 0) g_debug_audio = true;
    }

    if (!try_claim_instance()) return 0;

    glfwSetErrorCallback(error_callback);
    if (!glfwInit()) return 1;

    if (!load_embedded_sprite(app.sprite)) {
        std::fprintf(stderr, "embedded sprite is broken\n");
        glfwTerminate();
        return 1;
    }

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);

    app.win = glfwCreateWindow(app.sprite.width, app.sprite.height, "GuyDances", nullptr, nullptr);
    if (!app.win) {
        glfwTerminate();
        return 1;
    }
    glfwSetWindowUserPointer(app.win, &app);
    glfwSetMouseButtonCallback(app.win, on_mouse);
    glfwSetCursorPosCallback(app.win, on_cursor);
    glfwSetKeyCallback(app.win, on_key);
    glfwSetWindowFocusCallback(app.win, on_focus);
    hide_from_taskbar(app.win);

    glfwMakeContextCurrent(app.win);
    glfwSwapInterval(1);

    glGenTextures(1, &app.sprite_tex);
    glBindTexture(GL_TEXTURE_2D, app.sprite_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, app.sprite.width, app.sprite.height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);

    app.font_tex = make_font_texture();

    load_state(app);
    start_audio_monitor();

    app.last_time = glfwGetTime();

    while (!glfwWindowShouldClose(app.win)) {
        glfwPollEvents();

        if (take_wake_request()) app.snooze_until = 0;

        double now = glfwGetTime();
        bool raw_music = app.demo || g_music_playing.load();
        if (raw_music) {
            app.silence_since = 0;
            if (app.music_since == 0) app.music_since = now;
            if (now - app.music_since >= 1.5) app.music_stable = true;
        } else {
            app.music_since = 0;
            if (app.silence_since == 0) app.silence_since = now;
            if (now - app.silence_since >= 2.0) app.music_stable = false;
        }

        bool snoozed = now < app.snooze_until;
        bool want_visible = app.paused || (app.music_stable && !snoozed);

        if (want_visible && !app.visible) {
            show_sprite(app);
        } else if (!want_visible && app.visible) {
            hide_sprite(app);
        }

        double dt = now - app.last_time;
        app.last_time = now;
        if (dt > 0.25) dt = 0.25;

        if (app.visible) {
            if (!app.paused) {
                app.frame_acc += dt;
                double step = app.sprite.frame_delay_us / 1000000.0;
                while (app.frame_acc >= step) {
                    app.frame_acc -= step;
                    app.frame = (app.frame + 1) % app.sprite.frame_count;
                    app.frame_dirty = true;
                }
            }
            render(app);
        } else {
            glfwWaitEventsTimeout(0.1);
        }
    }

    save_state(app);
    stop_audio_monitor();
    glfwDestroyWindow(app.win);
    glfwTerminate();
    return 0;
}
