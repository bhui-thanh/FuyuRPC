#pragma once
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <fstream>
#include <cstdio>
#include <memory>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
#pragma comment(lib, "winhttp.lib")

// 1. Upload via Windows Native curl.exe (Sesuai Dokumentasi Resmi Litterbox)
inline std::string upload_via_curl(const std::string& png_path) {
    // Jalankan cURL bawaan Windows 10/11
    std::string cmd = "curl.exe -s -F \"reqtype=fileupload\" -F \"time=24h\" -F \"fileToUpload=@" + png_path + "\" https://litterbox.catbox.moe/resources/internals/api.php";
    
    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) return "";

    char buffer[128];
    std::string result = "";
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        result += buffer;
    }
    _pclose(pipe);

    // Trim whitespace/newline
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' '))
        result.pop_back();

    return result;
}

// 2. Baca file PNG lokal (untuk fallback)
inline std::vector<BYTE> read_png_bytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<BYTE> buffer(size);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    return buffer;
}

// 3. Fallback WinHttp (Dengan WinHttpAddRequestHeaders yang sudah diperbaiki)
inline std::string upload_via_winhttp(const std::vector<BYTE>& file_data) {
    if (file_data.empty()) return "";

    HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64)",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";

    HINTERNET hConnect = WinHttpConnect(hSession, L"litterbox.catbox.moe", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/resources/internals/api.php",
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    std::wstring contentType = L"Content-Type: multipart/form-data; boundary=FuyuRPCBoundary123456";
    WinHttpAddRequestHeaders(hRequest, contentType.c_str(), -1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

    std::string boundary = "FuyuRPCBoundary123456";
    std::string bodyHead = "--" + boundary + "\r\n"
                           "Content-Disposition: form-data; name=\"reqtype\"\r\n\r\nfileupload\r\n"
                           "--" + boundary + "\r\n"
                           "Content-Disposition: form-data; name=\"time\"\r\n\r\n24h\r\n"
                           "--" + boundary + "\r\n"
                           "Content-Disposition: form-data; name=\"fileToUpload\"; filename=\"icon.png\"\r\n"
                           "Content-Type: image/png\r\n\r\n";
    std::string bodyTail = "\r\n--" + boundary + "--\r\n";

    std::vector<BYTE> body;
    body.insert(body.end(), bodyHead.begin(), bodyHead.end());
    body.insert(body.end(), file_data.begin(), file_data.end());
    body.insert(body.end(), bodyTail.begin(), bodyTail.end());

    BOOL bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                       body.data(), (DWORD)body.size(), (DWORD)body.size(), 0);

    if (bResults) bResults = WinHttpReceiveResponse(hRequest, nullptr);

    std::string response;
    if (bResults) {
        DWORD dwAvailable = 0;
        do {
            DWORD dwDownloaded = 0;
            WinHttpQueryDataAvailable(hRequest, &dwAvailable);
            if (dwAvailable == 0) break;
            std::vector<char> buf(dwAvailable + 1, 0);
            if (WinHttpReadData(hRequest, buf.data(), dwAvailable, &dwDownloaded)) {
                response.append(buf.data(), dwDownloaded);
            }
        } while (dwAvailable > 0);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    while (!response.empty() && (response.back() == '\n' || response.back() == '\r' || response.back() == ' '))
        response.pop_back();

    return response;
}

// Handler Cache & Upload Utama
inline std::string get_or_upload_icon_url(const std::string& exe_name, const std::string& png_path) {
    const std::string cache_file = "icons/cache.json";
    json cacheObj = json::object();

    // 1. Cek Cache
    std::ifstream in(cache_file);
    if (in.is_open()) {
        try { in >> cacheObj; } catch (...) {}
        in.close();
    }

    if (cacheObj.contains(exe_name) && cacheObj[exe_name].is_string()) {
        std::string cached_url = cacheObj[exe_name].get<std::string>();
        printf("[ICON] Using cached CDN URL: %s\n", cached_url.c_str());
        return cached_url;
    }

    // 2. Metode 1: Pakai Native Windows cURL
    printf("[ICON] Uploading to Litterbox via Windows cURL...\n");
    std::string url = upload_via_curl(png_path);

    // 3. Metode 2: Fallback WinHttp jika cURL tidak merespon
    if (url.empty() || url.rfind("http", 0) != 0) {
        printf("[ICON] cURL fallback to WinHttp API...\n");
        auto bytes = read_png_bytes(png_path);
        if (!bytes.empty()) {
            url = upload_via_winhttp(bytes);
        }
    }

    if (!url.empty() && url.rfind("http", 0) == 0) {
        printf("[ICON] SUCCESS! Direct URL: %s\n", url.c_str());
        cacheObj[exe_name] = url;
        std::ofstream out(cache_file);
        if (out.is_open()) out << cacheObj.dump(4);
        return url;
    } else {
        printf("[ICON] Litterbox Fail Response: %s\n", url.c_str());
        return "";
    }
}
