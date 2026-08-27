#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <set>
#include <cstdio>

struct ProcessInfo {
    DWORD pid;
    std::string exe_name;   // "paripari.exe"
    std::string full_path;  // "C:\Games\paripari\paripari.exe"
    std::string window_title;
};

// Ambil full path dari PID
inline std::string get_process_path(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return "";
    wchar_t buf[MAX_PATH]{};
    DWORD sz = MAX_PATH;
    std::string result;
    if (QueryFullProcessImageNameW(h, 0, buf, &sz)) {
        int len = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
        result.resize(len);
        WideCharToMultiByte(CP_UTF8, 0, buf, -1, &result[0], len, nullptr, nullptr);
        result.pop_back();
    }
    CloseHandle(h);
    return result;
}

// Ambil window title dari PID (jika ada window visible)
inline std::string get_window_title(DWORD pid) {
    struct Data { DWORD pid; std::wstring title; };
    Data data{pid, L""};

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        auto* d = reinterpret_cast<Data*>(lParam);
        DWORD wpid = 0;
        GetWindowThreadProcessId(hwnd, &wpid);
        if (wpid == d->pid && IsWindowVisible(hwnd)) {
            wchar_t buf[256]{};
            GetWindowTextW(hwnd, buf, 256);
            if (wcslen(buf) > 0) {
                d->title = buf;
                return FALSE; // stop
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&data));

    if (data.title.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, data.title.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, data.title.c_str(), -1, &s[0], len, nullptr, nullptr);
    s.pop_back();
    return s;
}

// Scan semua process, return list unik (berdasarkan nama exe)
inline std::vector<ProcessInfo> scan_processes() {
    std::vector<ProcessInfo> list;
    std::set<std::string> seen;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return list;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            int len = WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, nullptr, 0, nullptr, nullptr);
            std::string name(len, '\0');
            WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, &name[0], len, nullptr, nullptr);
            name.pop_back();

            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

            // Skip system / noise
            if (lower == "system" || lower == "idle" ||
                lower == "svchost.exe" || lower == "csrss.exe" ||
                lower == "lsass.exe" || lower == "smss.exe" ||
                lower == "wininit.exe" || lower == "services.exe" ||
                lower == "dwm.exe" || lower == "fontdrvhost.exe" ||
                lower == "runtimebroker.exe" || lower == "ctfmon.exe")
                continue;

            // Deduplicate by exe name
            if (seen.count(lower)) continue;
            seen.insert(lower);

            ProcessInfo pi;
            pi.pid = pe.th32ProcessID;
            pi.exe_name = name;
            pi.full_path = get_process_path(pi.pid);
            pi.window_title = get_window_title(pi.pid);
            list.push_back(pi);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    // Sort alphabetically
    std::sort(list.begin(), list.end(), [](const ProcessInfo& a, const ProcessInfo& b) {
        return a.exe_name < b.exe_name;
    });

    return list;
}

// Tampilkan menu interaktif, return index yang dipilih (-1 = cancel)
inline int pick_process(const std::vector<ProcessInfo>& list) {
    printf("\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║          SELECT A RUNNING PROCESS                       ║\n");
    printf("╠══════════════════════════════════════════════════════════╣\n");

    for (size_t i = 0; i < list.size(); i++) {
        const auto& p = list[i];
        std::string title = p.window_title.empty() ? "" : (" [" + p.window_title + "]");
        printf("║  %3zu │ %-20s%s\n", i, p.exe_name.c_str(), title.c_str());
    }

    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║  Type number to select, 'r' to refresh, 'q' to quit     ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\n> ");

    char input[64]{};
    if (!fgets(input, sizeof(input), stdin)) return -1;

    if (input[0] == 'q' || input[0] == 'Q') return -1;
    if (input[0] == 'r' || input[0] == 'R') return -2; // refresh

    int idx = atoi(input);
    if (idx >= 0 && idx < (int)list.size()) return idx;

    printf("[!] Invalid selection.\n");
    return -3;
}