#pragma once
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
#pragma comment(lib, "winhttp.lib")

// Baca file PNG lokal ke binary
inline std::vector<BYTE> read_png_binary(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<BYTE> buffer(size);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    return buffer;
}

// WinHTTP POST Multipart helper
inline std::string winhttp_post_multipart(const std::wstring& host,
                                         const std::wstring& path,
                                         const std::string& field_name,
                                         const std::string& filename,
                                         const std::vector<BYTE>& file_bytes) {
    HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) FuyuRPC/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(),
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    std::string boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
    std::string contentType = "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n";
    
    int whlen = MultiByteToWideChar(CP_UTF8, 0, contentType.c_str(), -1, nullptr, 0);
    std::wstring wContentType(whlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, contentType.c_str(), -1, &wContentType[0], whlen);

    std::string bodyHead = "--" + boundary + "\r\n"
                           "Content-Disposition: form-data; name=\"" + field_name + "\"; filename=\"" + filename + "\"\r\n"
                           "Content-Type: image/png\r\n\r\n";
    std::string bodyTail = "\r\n--" + boundary + "--\r\n";

    std::vector<BYTE> fullBody;
    fullBody.insert(fullBody.end(), bodyHead.begin(), bodyHead.end());
    fullBody.insert(fullBody.end(), file_bytes.begin(), file_bytes.end());
    fullBody.insert(fullBody.end(), bodyTail.begin(), bodyTail.end());

    BOOL bResults = WinHttpSendRequest(hRequest,
                                       wContentType.c_str(), (DWORD)-1,
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

    // Trim whitespace/newline
    while (!response.empty() && (response.back() == '\n' || response.back() == '\r' || response.back() == ' '))
        response.pop_back();

    return response;
}

// Upload ke 0x0.st (Super Cepat & Langsung mengembalikan direct URL)
inline std::string upload_to_0x0(const std::vector<BYTE>& bytes) {
    return winhttp_post_multipart(L"0x0.st", L"/", "file", "icon.png", bytes);
}

// Upload ke tmpfiles.org (Sebagai Fallback)
inline std::string upload_to_tmpfiles(const std::vector<BYTE>& bytes) {
    std::string res = winhttp_post_multipart(L"tmpfiles.org", L"/api/v1/upload", "file", "icon.png", bytes);
    try {
        auto j = json::parse(res);
        if (j.contains("data") && j["data"].contains("url")) {
            std::string url = j["data"]["url"].get<std::string>();
            // Ganti tmpfiles.org/123/icon.png -> tmpfiles.org/dl/123/icon.png (Direct Image)
            size_t pos = url.find("tmpfiles.org/");
            if (pos != std::string::npos) {
                url.insert(pos + 13, "dl/");
            }
            return url;
        }
    } catch (...) {}
    return "";
}

// Upload & Cache Handler
inline std::string get_or_upload_icon_url(const std::string& exe_name, const std::string& png_path) {
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
        printf("[ICON] Using cached URL: %s\n", cached_url.c_str());
        return cached_url;
    }

    auto bytes = read_png_binary(png_path);
    if (bytes.empty()) {
        printf("[ICON] Failed to open PNG file: %s\n", png_path.c_str());
        return "";
    }

    // 2. Coba Upload ke Server Utama (0x0.st)
    printf("[ICON] Uploading icon to CDN (0x0.st)...\n");
    std::string url = upload_to_0x0(bytes);

    // 3. Jika gagal, coba ke Fallback Server (tmpfiles.org)
    if (url.empty() || url.rfind("http", 0) != 0) {
        printf("[ICON] Primary CDN failed, trying fallback CDN...\n");
        url = upload_to_tmpfiles(bytes);
    }

    // 4. Sukses
    if (!url.empty() && url.rfind("http", 0) == 0) {
        printf("[ICON] SUCCESS! Uploaded to: %s\n", url.c_str());
        cacheObj[exe_name] = url;
        std::ofstream out(cache_file);
        if (out.is_open()) {
            out << cacheObj.dump(4);
        }
        return url;
    } else {
        printf("[ICON] All upload providers failed. Running without custom icon.\n");
        return "";
    }
}
