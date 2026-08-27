#pragma once
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
#pragma comment(lib, "winhttp.lib")

// Helper baca file PNG
inline std::vector<BYTE> read_png_bytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<BYTE> buffer(size);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    return buffer;
}

// Upload ke Imgur (Paling Aman untuk Discord)
inline std::string upload_to_imgur(const std::vector<BYTE>& bytes) {
    if (bytes.empty()) return "";

    HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";

    HINTERNET hConnect = WinHttpConnect(hSession, L"api.imgur.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/3/image",
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    // Pakai Client-ID Imgur Publik (Ini aman dan umum digunakan)
    std::string boundary = "----WebKitFormBoundaryImgurFuyuRPC";
    std::string headers = "Authorization: Client-ID 5440db00647c493\r\n"
                          "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n";
    
    int whlen = MultiByteToWideChar(CP_UTF8, 0, headers.c_str(), -1, nullptr, 0);
    std::wstring wHeaders(whlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, headers.c_str(), -1, &wHeaders[0], whlen);

    std::string p1 = "--" + boundary + "\r\n"
                     "Content-Disposition: form-data; name=\"image\"; filename=\"icon.png\"\r\n"
                     "Content-Type: image/png\r\n\r\n";
    std::string p2 = "\r\n--" + boundary + "--\r\n";

    std::vector<BYTE> body;
    body.insert(body.end(), p1.begin(), p1.end());
    body.insert(body.end(), bytes.begin(), bytes.end());
    body.insert(body.end(), p2.begin(), p2.end());

    BOOL bResults = WinHttpSendRequest(hRequest,
                                       wHeaders.c_str(), (DWORD)-1,
                                       body.data(),
                                       (DWORD)body.size(),
                                       (DWORD)body.size(), 0);

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
        if (j.contains("data") && j["data"].contains("link")) {
            return j["data"]["link"].get<std::string>();
        }
    } catch (...) {
        printf("[ICON] Gagal parse Imgur: %s\n", response.c_str());
    }

    return "";
}

inline std::string get_or_upload_icon_url(const std::string& exe_name, const std::string& png_path) {
    const std::string cache_file = "icons/cache.json";
    json cacheObj = json::object();

    std::ifstream in(cache_file);
    if (in.is_open()) {
        try { in >> cacheObj; } catch (...) {}
        in.close();
    }

    if (cacheObj.contains(exe_name) && cacheObj[exe_name].is_string()) {
        return cacheObj[exe_name].get<std::string>();
    }

    auto bytes = read_png_bytes(png_path);
    if (bytes.empty()) return "";

    printf("[ICON] Uploading icon to Imgur (Discord-Friendly CDN)...\n");
    std::string url = upload_to_imgur(bytes);

    if (!url.empty()) {
        printf("[ICON] SUCCESS! Direct URL: %s\n", url.c_str());
        cacheObj[exe_name] = url;
        std::ofstream out(cache_file);
        if (out.is_open()) {
            out << cacheObj.dump(4);
        }
        return url;
    } else {
        printf("[ICON] Imgur upload failed.\n");
        return "";
    }
}
