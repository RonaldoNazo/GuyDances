#pragma once

#include <algorithm>
#include <atomic>
#include <cctype>
#include <string>

extern std::atomic<bool> g_music_playing;
extern std::atomic<bool> g_debug_audio;

void start_audio_monitor();
void stop_audio_monitor();

inline bool is_media_app(const std::string& raw) {
    std::string s;
    s.reserve(raw.size());
    for (unsigned char c : raw) s.push_back(static_cast<char>(std::tolower(c)));

    static const char* keys[] = {
        "chrome", "chromium", "msedge", "microsoft-edge", "firefox", "brave",
        "opera", "vivaldi", "spotify", "vlc", "mpv", "ffplay", "rhythmbox",
        "gnome-music", "music.ui", "wmplayer", "mediaplayer", "mplayer",
        "deezer", "tidal",
    };
    for (const char* k : keys) {
        if (s.find(k) != std::string::npos) return true;
    }
    return false;
}
