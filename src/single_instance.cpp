#include "single_instance.h"

#include <atomic>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {
HANDLE g_mutex = nullptr;
HANDLE g_event = nullptr;
std::atomic<bool> g_wake{false};
}  // namespace

bool try_claim_instance() {
    g_mutex = CreateMutexW(nullptr, TRUE, L"Local\\GuyDancesSingle");
    if (g_mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\GuyDancesWake");
        if (event) {
            SetEvent(event);
            CloseHandle(event);
        }
        CloseHandle(g_mutex);
        g_mutex = nullptr;
        return false;
    }

    g_event = CreateEventW(nullptr, FALSE, FALSE, L"Local\\GuyDancesWake");
    static auto waiter = [](LPVOID) -> DWORD {
        for (;;) {
            WaitForSingleObject(g_event, INFINITE);
            g_wake = true;
        }
        return 0;
    };
    CreateThread(nullptr, 0, waiter, nullptr, 0, nullptr);
    return true;
}

bool take_wake_request() {
    return g_wake.exchange(false);
}

#else

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <thread>

namespace {
int g_listen = -1;
std::atomic<bool> g_wake{false};

std::string socket_path() {
    return "/tmp/guydances-" + std::to_string((unsigned)getuid()) + ".sock";
}
}  // namespace

bool try_claim_instance() {
    std::string path = socket_path();

    int probe = socket(AF_UNIX, SOCK_STREAM, 0);
    if (probe >= 0) {
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
        if (connect(probe, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
            char byte = 1;
            (void)!write(probe, &byte, 1);
            close(probe);
            return false;
        }
        close(probe);
    }

    unlink(path.c_str());
    int listener = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener < 0) return true;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(listener);
        return true;
    }
    chmod(path.c_str(), 0600);
    listen(listener, 4);
    g_listen = listener;

    std::thread([] {
        for (;;) {
            int client = accept(g_listen, nullptr, nullptr);
            if (client < 0) break;
            char byte = 0;
            (void)!read(client, &byte, 1);
            close(client);
            g_wake = true;
        }
    }).detach();
    return true;
}

bool take_wake_request() {
    return g_wake.exchange(false);
}

#endif
