#pragma once
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
#pragma comment(lib, "winhttp.lib")

// Baca file PNG binary
inline std::vector<BYTE> read_binary_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<BYTE> buffer(size);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    return buffer;
}

// Upload multipart/form-data ke Catbox
inline std::string upload_to_catbox(const std::string& png_path) {
    auto fileBytes = read_binary_file(png_path);
    if (fileBytes.empty()) {
        printf("[UPLOADER] Gagal membaca file: %s\n", png_path.c_str());
        return "";
    }

    HINTERNET hSession = WinHttpOpen(L"DiscordPresence/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";

    HINTERNET hConnect = WinHttpConnect(hSession, L"catbox.moe", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/user/api.php",
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    std::string boundary = "----DiscordPresenceBoundary123456";
    std::string header = "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n";
    int whlen = MultiByteToWideChar(CP_UTF8, 0, header.c_str(), -1, nullptr, 0);
    std::wstring wHeader(whlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, header.c_str(), -1, &wHeader[0], whlen);

    // Build Payload Body
    std::string bodyHead = "--" + boundary + "\r\n"
                           "Content-Disposition: form-data; name=\"reqtype\"\r\n\r\n"
                           "fileupload\r\n"
                           "--" + boundary + "\r\n"
                           "Content-Disposition: form-data; name=\"fileToUpload\"; filename=\"icon.png\"\r\n"
                           "Content-Type: image/png\r\n\r\n";

    std::string bodyTail = "\r\n--" + boundary + "--\r\n";

    std::vector<BYTE> fullBody;
    fullBody.insert(fullBody.end(), bodyHead.begin(), bodyHead.end());
    fullBody.insert(fullBody.end(), fileBytes.begin(), fileBytes.end());
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

            std::vector<char> pszOutBuffer(dwSize + 1, 0);
            if (WinHttpReadData(hRequest, pszOutBuffer.data(), dwSize, &dwDownloaded)) {
                response.append(pszOutBuffer.data(), dwDownloaded);
            }
        } while (dwSize > 0);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpConnect(hSession, L"catbox.moe", INTERNET_DEFAULT_HTTPS_PORT, 0);
    WinHttpCloseHandle(hSession);

    // Trim whitespace
    while (!response.empty() && (response.back() == '\n' || response.back() == '\r' || response.back() == ' '))
        response.pop_back();

    return response;
}

// Check Local Cache & Auto-Upload
inline std::string get_or_upload_icon_url(const std::string& exe_name, const std::string& png_path) {
    const std::string cache_file = "icons/cache.json";
    json cacheObj = json::object();

    // 1. Baca cache lokal
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

    // 2. Upload jika belum ada di cache
    printf("[ICON] Uploading extracted icon to CDN...\n");
    std::string uploaded_url = upload_to_catbox(png_path);

    if (!uploaded_url.empty() && uploaded_url.rfind("http", 0) == 0) {
        printf("[ICON] SUCCESS! Uploaded to: %s\n", uploaded_url.c_str());
        // Simpan ke cache
        cacheObj[exe_name] = uploaded_url;
        std::ofstream out(cache_file);
        if (out.is_open()) {
            out << cacheObj.dump(4);
        }
        return uploaded_url;
    } else {
        printf("[ICON] Upload failed! Response: %s\n", uploaded_url.c_str());
        return "";
    }
}
