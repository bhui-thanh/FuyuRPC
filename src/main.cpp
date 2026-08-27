#include <cstdio>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>
#include <fstream>
#include <nlohmann/json.hpp>

#include "picker.h"
#include "icon.h"
#include "uploader.h"
#include "rpc.h"

using json = nlohmann::json;
static std::atomic<bool> g_running{true};
static void sig(int) { g_running = false; }

int main() {
    std::signal(SIGINT, sig);
    std::signal(SIGTERM, sig);

    printf("╔════════════════════════════════════════════════╗\n");
    printf("║ Discord Rich Presence — Full Auto Icon Sync   ║\n");
    printf("╚════════════════════════════════════════════════╝\n\n");

    json cfg;
    {
        std::ifstream f("config.json");
        if (f.is_open()) cfg = json::parse(f, nullptr, false);
        if (cfg.is_discarded()) {
            printf("[ERR] config.json invalid or missing!\n");
            return 1;
        }
    }

    std::string app_id    = cfg.value("app_id", "");
    std::string bot_token = cfg.value("bot_token", "");
    int update_ms         = cfg.value("update_ms", 5000);

    if (app_id.empty()) {
        printf("[ERR] Set app_id in config.json first!\n");
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

    // === AUTO EXTRACT & UPLOAD ICON ===
    std::string asset_key = "default";
    if (!selected.full_path.empty()) {
        std::string png_path = extract_exe_icon(selected.full_path, selected.exe_name);
        if (!png_path.empty()) {
            // Upload otomatis ke Discord via API
            asset_key = sync_and_upload_icon(app_id, bot_token, selected.exe_name, png_path);
        }
    }

    // === PRESENCE LOOP ===
    auto start_time = std::chrono::system_clock::now();
    int64_t start_ts = std::chrono::duration_cast<std::chrono::seconds>(
                           start_time.time_since_epoch()).count();

    printf("\n[PRESENCE] Running Rich Presence for: %s\n", selected.exe_name.c_str());
    printf("[PRESENCE] Press Ctrl+C to exit.\n\n");

    while (g_running) {
        Discord_RunCallbacks();

        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, selected.pid);
        bool alive = (h != nullptr);
        if (h) CloseHandle(h);

        if (alive) {
            auto now = std::chrono::system_clock::now();
            auto mins = std::chrono::duration_cast<std::chrono::minutes>(now - start_time).count();
            auto hrs  = mins / 60;
            mins %= 60;

            char timebuf[64];
            snprintf(timebuf, sizeof(timebuf), "%02lld:%02lld", hrs, mins);

            std::string details = "Playing " + selected.exe_name;
            std::string state   = std::string("Time: ") + timebuf;
            std::string title   = selected.window_title.empty()
                                      ? selected.exe_name
                                      : selected.window_title;

            rpc_set(details, state, asset_key, title, start_ts);
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