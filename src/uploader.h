#pragma once
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
#pragma comment(lib, "winhttp.lib")

// Baca binary PNG
inline std::vector<BYTE> read_png_bytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<BYTE> buffer(size);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    return buffer;
}

// Upload file langsung ke Discord Webhook (cdn.discordapp.com)
inline std::string upload_to_discord_webhook(const std::string& webhook_url, const std::vector<BYTE>& bytes) {
    if (webhook_url.empty()) return "";

    // Parse Webhook URL: https://discord.com/api/webhooks/ID/TOKEN
    std::string prefix = "https://discord.com";
    std::string path = webhook_url;
    if (path.find(prefix) == 0) {
        path = path.substr(prefix.length());
    } else {
        size_t pos = webhook_url.find("/api/webhooks/");
        if (pos != std::string::npos) {
            path = webhook_url.substr(pos);
        } else {
            printf("[UPLOADER] Invalid Webhook URL format!\n");
            return "";
        }
    }

    HINTERNET hSession = WinHttpOpen(L"DiscordPresence/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";

    HINTERNET hConnect = WinHttpConnect(hSession, L"discord.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }

    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wPath(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wPath[0], wlen);

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", wPath.c_str(),
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    std::string boundary = "----DiscordWebhookBoundary8934275";
    std::string header = "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n";
    
    int whlen = MultiByteToWideChar(CP_UTF8, 0, header.c_str(), -1, nullptr, 0);
    std::wstring wHeader(whlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, header.c_str(), -1, &wHeader[0], whlen);

    std::string bodyHead = "--" + boundary + "\r\n"
                           "Content-Disposition: form-data; name=\"files[0]\"; filename=\"icon.png\"\r\n"
                           "Content-Type: image/png\r\n\r\n";
    std::string bodyTail = "\r\n--" + boundary + "--\r\n";

    std::vector<BYTE> fullBody;
    fullBody.insert(fullBody.end(), bodyHead.begin(), bodyHead.end());
    fullBody.insert(fullBody.end(), bytes.begin(), bytes.end());
    fullBody.insert(fullBody.end(), bodyTail.begin(), bodyTail.end());

    BOOL bResults = WinHttpSendRequest(hRequest,
                                       wHeader.c_str(), (DWORD)-1,
                                       fullBody.data(),
                                       (DWORD)fullBody.size(),
                                       (DWORD)fullBody.size(), 0);

    if (bResults) bResults = WinHttpReceiveResponse(hRequest, nullptr);

    std::string response;
    if (bResults) {
        DWORD dwSize = 0;
        do {
            DWORD dwDownloaded = 0;
            WinHttpQueryDataAvailable(hRequest, &dwSize);
            if (dwSize == 0) break;

            std::vector<char> buf(dwSize + 1, 0);
            if (WinHttpReadData(hRequest, buf.data(), dwSize, &dwDownloaded)) {
                response.append(buf.data(), dwDownloaded);
            }
        } while (dwSize > 0);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    try {
        auto j = json::parse(response);
        if (j.contains("attachments") && j["attachments"].is_array() && !j["attachments"].empty()) {
            return j["attachments"][0].value("url", "");
        }
    } catch (...) {}

    return "";
}

// Handler utama: Cek cache & Upload
inline std::string get_or_upload_icon_url(const std::string& exe_name, 
                                         const std::string& png_path, 
                                         const std::string& webhook_url) {
    const std::string cache_file = "icons/cache.json";
    json cacheObj = json::object();

    // 1. Cek Cache Lokal
    std::ifstream in(cache_file);
    if (in.is_open()) {
        try { in >> cacheObj; } catch (...) {}
        in.close();
    }

    if (cacheObj.contains(exe_name) && cacheObj[exe_name].is_string()) {
        std::string cached_url = cacheObj[exe_name].get<std::string>();
        printf("[ICON] Using cached Discord CDN URL: %s\n", cached_url.c_str());
        return cached_url;
    }

    auto bytes = read_png_bytes(png_path);
    if (bytes.empty()) {
        printf("[ICON] Failed to open PNG file: %s\n", png_path.c_str());
        return "";
    }

    std::string url = "";
    if (!webhook_url.empty()) {
        printf("[ICON] Uploading icon directly to Discord CDN via Webhook...\n");
        url = upload_to_discord_webhook(webhook_url, bytes);
    } else {
        printf("[ICON] Warning: 'webhook_url' is empty in config.json! Discord might reject public CDN links.\n");
    }

    // Sukses
    if (!url.empty()) {
        printf("[ICON] SUCCESS! Discord CDN URL: %s\n", url.c_str());
        cacheObj[exe_name] = url;
        std::ofstream out(cache_file);
        if (out.is_open()) {
            out << cacheObj.dump(4);
        }
        return url;
    } else {
        printf("[ICON] Upload failed. Please check your 'webhook_url' in config.json.\n");
        return "";
    }
}
