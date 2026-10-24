#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include <cstdio>
#include <cwchar>
#include <string>

#include "audio.h"

std::atomic<bool> g_music_playing{false};
std::atomic<bool> g_debug_audio{false};
static std::atomic<bool> g_stop{false};

namespace {

// Spelled out so the build does not depend on __uuidof or uuid.lib.
const CLSID kCLSIDMMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C,
                                        {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const IID kIIDIMMDeviceEnumerator = {0xA95664D2, 0x9614, 0x4F35,
                                     {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
const IID kIIDIAudioSessionManager2 = {0x77AA99A0, 0x1BD6, 0x484F,
                                       {0x8B, 0xC7, 0x2C, 0x65, 0x4C, 0x9A, 0x9B, 0x6F}};
const IID kIIDIAudioSessionControl2 = {0xBFB7FF88, 0x7239, 0x4FC9,
                                       {0x8F, 0xA2, 0x07, 0xC9, 0x50, 0xBE, 0x9C, 0x6D}};

std::string process_name(DWORD pid) {
    if (!pid) return {};
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!handle) return {};

    wchar_t path[1024];
    DWORD size = 1024;
    std::string name;
    if (QueryFullProcessImageNameW(handle, 0, path, &size)) {
        const wchar_t* base = std::wcsrchr(path, L'\\');
        base = base ? base + 1 : path;
        char narrow[1024];
        int len = WideCharToMultiByte(CP_UTF8, 0, base, -1, narrow, sizeof(narrow), nullptr, nullptr);
        if (len > 0) name.assign(narrow, len - 1);
    }
    CloseHandle(handle);
    return name;
}

bool poll_once() {
    bool found = false;
    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(kCLSIDMMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                kIIDIMMDeviceEnumerator, reinterpret_cast<void**>(&enumerator)))) {
        return false;
    }

    IMMDevice* device = nullptr;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device))) {
        IAudioSessionManager2* manager = nullptr;
        if (SUCCEEDED(device->Activate(kIIDIAudioSessionManager2, CLSCTX_ALL, nullptr,
                                       reinterpret_cast<void**>(&manager)))) {
            IAudioSessionEnumerator* sessions = nullptr;
            if (SUCCEEDED(manager->GetSessionEnumerator(&sessions))) {
                int count = 0;
                sessions->GetCount(&count);
                for (int i = 0; i < count && !found; ++i) {
                    IAudioSessionControl* control = nullptr;
                    if (FAILED(sessions->GetSession(i, &control))) continue;

                    IAudioSessionControl2* control2 = nullptr;
                    if (SUCCEEDED(control->QueryInterface(
                            kIIDIAudioSessionControl2, reinterpret_cast<void**>(&control2)))) {
                        AudioSessionState state;
                        if (SUCCEEDED(control2->GetState(&state)) &&
                            state == AudioSessionStateActive) {
                            DWORD pid = 0;
                            if (SUCCEEDED(control2->GetProcessId(&pid))) {
                                std::string name = process_name(pid);
                                if (!name.empty() && is_media_app(name)) found = true;
                            }
                        }
                        control2->Release();
                    }
                    control->Release();
                }
                sessions->Release();
            }
            manager->Release();
        }
        device->Release();
    }
    enumerator->Release();
    return found;
}

DWORD WINAPI monitor_entry(LPVOID) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        std::fprintf(stderr, "[audio] COM init failed: 0x%08lx\n", (unsigned long)hr);
        return 0;
    }

    while (!g_stop) {
        bool playing = poll_once();
        bool was = g_music_playing.exchange(playing);
        if (g_debug_audio && was != playing) {
            std::fprintf(stderr, "[audio] %s\n", playing ? "playing" : "silent");
        }
        Sleep(400);
    }

    if (SUCCEEDED(hr)) CoUninitialize();
    return 0;
}

}  // namespace

void start_audio_monitor() {
    CreateThread(nullptr, 0, monitor_entry, nullptr, 0, nullptr);
}

void stop_audio_monitor() {
    g_stop = true;
}
