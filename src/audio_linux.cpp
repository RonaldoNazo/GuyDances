#include "audio.h"

#include <pulse/pulseaudio.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

std::atomic<bool> g_music_playing{false};
std::atomic<bool> g_debug_audio{false};
static std::atomic<bool> g_stop{false};

namespace {

struct Query {
    bool found = false;
    std::string name;
};

void sink_cb(pa_context*, const pa_sink_input_info* info, int eol, void* userdata) {
    if (eol || !info) return;
    auto* q = static_cast<Query*>(userdata);
    if (info->corked || info->mute) return;
    if (pa_cvolume_avg(&info->volume) == 0) return;

    const char* role = pa_proplist_gets(info->proplist, "media.role");
    if (role && std::strcmp(role, "communication") == 0) return;

    static const char* keys[] = {
        "application.name", "application.process.binary", "application.id", "media.name",
    };
    for (const char* key : keys) {
        const char* value = pa_proplist_gets(info->proplist, key);
        if (value && is_media_app(value)) {
            q->found = true;
            q->name = value;
            return;
        }
    }
}

void monitor_thread() {
    pa_mainloop* ml = nullptr;
    pa_context* ctx = nullptr;

    while (!g_stop) {
        if (!ml) {
            ml = pa_mainloop_new();
            if (!ml) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            ctx = pa_context_new(pa_mainloop_get_api(ml), "guydances");
            if (!ctx) {
                pa_mainloop_free(ml);
                ml = nullptr;
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            pa_context_connect(ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr);
        }

        pa_context_state_t state = pa_context_get_state(ctx);
        if (state == PA_CONTEXT_READY) {
            Query q;
            pa_operation* op = pa_context_get_sink_input_info_list(ctx, sink_cb, &q);
            if (op) {
                while (pa_operation_get_state(op) == PA_OPERATION_RUNNING && !g_stop) {
                    pa_mainloop_iterate(ml, 1, nullptr);
                }
                pa_operation_unref(op);
            }
            bool was = g_music_playing.exchange(q.found);
            if (g_debug_audio && was != q.found) {
                if (q.found)
                    std::fprintf(stderr, "[audio] playing: %s\n", q.name.c_str());
                else
                    std::fprintf(stderr, "[audio] silent\n");
            }
            for (int i = 0; i < 8 && !g_stop; ++i) {
                pa_mainloop_iterate(ml, 0, nullptr);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        } else if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
            if (g_debug_audio) std::fprintf(stderr, "[audio] pulse unavailable, retrying\n");
            g_music_playing = false;
            pa_context_disconnect(ctx);
            pa_context_unref(ctx);
            pa_mainloop_free(ml);
            ctx = nullptr;
            ml = nullptr;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        } else {
            pa_mainloop_iterate(ml, 0, nullptr);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    if (ctx) {
        pa_context_disconnect(ctx);
        pa_context_unref(ctx);
    }
    if (ml) pa_mainloop_free(ml);
}

}  // namespace

void start_audio_monitor() {
    std::thread(monitor_thread).detach();
}

void stop_audio_monitor() {
    g_stop = true;
}
