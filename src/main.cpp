#include <cstdio>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>
#include <fstream>
#include <string>
#include <algorithm>
#include <nlohmann/json.hpp>

#include "picker.h"
#include "icon.h"
#include "uploader.h"
#include "rpc.h"

using json = nlohmann::json;
static std::atomic<bool> g_running{true};
static void sig(int) { g_running = false; }

// Hapus ekstensi .exe / .EXE dari nama process
inline std::string strip_exe(std::string name) {
    if (name.size() >= 4) {
        std::string ext = name.substr(name.size() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".exe") {
            name = name.substr(0, name.size() - 4);
        }
    }
    return name;
}

// Format durasi: "Playing for 5 minutes" / "Playing for 2 hours" dll.
inline std::string format_playtime(long long total_seconds) {
    long long mins  = total_seconds / 60;
    long long hours = mins / 60;
    long long days  = hours / 24;

    char buf[128];

    if (days > 0) {
        long long rem_hours = hours % 24;
        if (rem_hours > 0)
            snprintf(buf, sizeof(buf),
                     "Playing for %lld day%s %lld hour%s",
                     days,  days  == 1 ? "" : "s",
                     rem_hours, rem_hours == 1 ? "" : "s");
        else
            snprintf(buf, sizeof(buf),
                     "Playing for %lld day%s",
                     days, days == 1 ? "" : "s");
    }
    else if (hours > 0) {
        long long rem_mins = mins % 60;
        if (rem_mins > 0)
            snprintf(buf, sizeof(buf),
                     "Playing for %lld hour%s %lld minute%s",
                     hours, hours == 1 ? "" : "s",
                     rem_mins, rem_mins == 1 ? "" : "s");
        else
            snprintf(buf, sizeof(buf),
                     "Playing for %lld hour%s",
                     hours, hours == 1 ? "" : "s");
    }
    else if (mins > 0) {
        snprintf(buf, sizeof(buf),
                 "Playing for %lld minute%s",
                 mins, mins == 1 ? "" : "s");
    }
    else {
        snprintf(buf, sizeof(buf), "Just started");
    }

    return std::string(buf);
}

int main() {
    std::signal(SIGINT, sig);
    std::signal(SIGTERM, sig);

    printf("╔════════════════════════════════════════════════╗\n");
    printf("║ Discord Rich Presence — Litterbox Auto Mode    ║\n");
    printf("╚════════════════════════════════════════════════╝\n\n");

    json cfg;
    {
        std::ifstream f("config.json");
        if (f.is_open()) cfg = json::parse(f, nullptr, false);
        if (cfg.is_discarded() || cfg.empty()) {
            cfg["app_id"] = "";
            cfg["update_ms"] = 5000;
            std::ofstream out("config.json");
            out << cfg.dump(4);
        }
    }

    std::string app_id = cfg.value("app_id", "");
    int update_ms      = cfg.value("update_ms", 5000);

    if (app_id.empty()) {
        printf("[ERR] Set 'app_id' in config.json first!\n");
        printf("Press Enter to exit...");
        getchar();
        return 1;
    }

    gdi_init();
    rpc_init(app_id);

    // === PICK PROCESS ===
    ProcessInfo selected{};
    while (g_running) {
        auto procs = scan_processes();
        int idx = pick_process(procs);
        if (idx == -1) break;
        if (idx == -2 || idx == -3) continue;

        selected = procs[idx];
        printf("\n[OK] Selected: %s (PID %lu)\n", selected.exe_name.c_str(), selected.pid);
        break;
    }

    if (!g_running || selected.exe_name.empty()) {
        rpc_shutdown();
        gdi_shutdown();
        return 0;
    }

    // Nama tampilan: chrome.exe → chrome
    std::string display_name = strip_exe(selected.exe_name);

    // Window title lebih bagus? pakai kalau ada
    if (!selected.window_title.empty()) {
        // optional: pakai window title sebagai nama
        // display_name = selected.window_title;
    }

    // === AUTO EXTRACT & UPLOAD ICON ===
    std::string icon_url = "";
    if (!selected.full_path.empty()) {
        std::string png_path = extract_exe_icon(selected.full_path, selected.exe_name);
        if (!png_path.empty()) {
            icon_url = get_or_upload_icon_url(selected.exe_name, png_path);
        }
    }

    // === PRESENCE LOOP ===
    auto start_time = std::chrono::system_clock::now();

    // startTimestamp = 0 → sembunyikan timer bawaan Discord (0:27)
    // kita pakai teks custom "Playing for X minutes" saja
    int64_t start_ts = 0;

    printf("\n[PRESENCE] Tracking: %s\n", display_name.c_str());
    printf("[PRESENCE] Press Ctrl+C to exit.\n\n");
    printf("[TIP] Baris 'Playing FuyuRPC' = nama App di Developer Portal.\n");
    printf("      Rename app di https://discord.com/developers/applications\n");
    printf("      agar tidak menampilkan nama App ID.\n\n");

    while (g_running) {
        Discord_RunCallbacks();

        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, selected.pid);
        bool alive = (h != nullptr);
        if (h) CloseHandle(h);

        if (alive) {
            auto now = std::chrono::system_clock::now();
            auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(
                                   now - start_time).count();

            // details = nama exe tanpa .exe
            // state   = "Playing for X minutes"
            std::string details = display_name;
            std::string state   = format_playtime(elapsed_sec);
            std::string large_text = display_name; // tooltip saat hover icon

            rpc_set(details, state, icon_url, large_text, start_ts);

            // Log ke console tiap update (opsional)
            // printf("\r[RPC] %s | %s          ", details.c_str(), state.c_str());
            // fflush(stdout);
        } else {
            printf("[PRESENCE] Process ended.\n");
            rpc_clear();
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(update_ms));
    }

    rpc_shutdown();
    gdi_shutdown();
    return 0;
}
