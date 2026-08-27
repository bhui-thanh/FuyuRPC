#pragma once
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
#pragma comment(lib, "winhttp.lib")

// Baca binary PNG dengan Log Debug
inline std::vector<BYTE> read_png_bytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        printf("[DEBUG] Gagal membuka file lokal: %s\n", path.c_str());
        return {};
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<BYTE> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        printf("[DEBUG] Gagal membaca buffer file PNG!\n");
        return {};
    }
    printf("[DEBUG] File PNG berhasil dibaca: %zu bytes\n", buffer.size());
    return buffer;
}

// Upload ke Litterbox dengan Verbose Debugging
inline std::string upload_to_litterbox(const std::vector<BYTE>& file_data) {
    if (file_data.empty()) {
        printf("[DEBUG] File PNG kosong (0 bytes), upload dibatalkan.\n");
        return "";
    }

    HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        printf("[DEBUG] WinHttpOpen Gagal! Error: %lu\n", GetLastError());
        return "";
    }

    HINTERNET hConnect = WinHttpConnect(hSession, L"litterbox.catbox.moe", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        printf("[DEBUG] WinHttpConnect Gagal! Error: %lu\n", GetLastError());
        WinHttpCloseHandle(hSession);
        return "";
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/resources/internals/api.php",
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        printf("[DEBUG] WinHttpOpenRequest Gagal! Error: %lu\n", GetLastError());
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    // Boundary unik standar Chrome
    std::string boundary = "----WebKitFormBoundaryFuyuRPC9876543210";
    std::wstring headers = L"Content-Type: multipart/form-data; boundary=----WebKitFormBoundaryFuyuRPC9876543210\r\n";

    // 1. Head part (reqtype & time)
    std::string partHead = "";
    partHead += "--" + boundary + "\r\n";
    partHead += "Content-Disposition: form-data; name=\"reqtype\"\r\n\r\n";
    partHead += "fileupload\r\n";
    
    partHead += "--" + boundary + "\r\n";
    partHead += "Content-Disposition: form-data; name=\"time\"\r\n\r\n";
    partHead += "24h\r\n";

    partHead += "--" + boundary + "\r\n";
    partHead += "Content-Disposition: form-data; name=\"fileToUpload\"; filename=\"icon.png\"\r\n";
    partHead += "Content-Type: image/png\r\n\r\n";

    // 2. Tail part
    std::string partTail = "\r\n--" + boundary + "--\r\n";

    // 3. Rakit Payload
    std::vector<BYTE> fullBody;
    fullBody.insert(fullBody.end(), partHead.begin(), partHead.end());
    fullBody.insert(fullBody.end(), file_data.begin(), file_data.end());
    fullBody.insert(fullBody.end(), partTail.begin(), partTail.end());

    printf("[DEBUG] Sending HTTP POST to Litterbox... Total Body: %zu bytes\n", fullBody.size());

    BOOL bResults = WinHttpSendRequest(hRequest,
                                       headers.c_str(), (DWORD)-1,
                                       fullBody.data(),
                                       (DWORD)fullBody.size(),
                                       (DWORD)fullBody.size(), 0);

    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, nullptr);
    } else {
        printf("[DEBUG] WinHttpSendRequest Gagal! Error: %lu\n", GetLastError());
    }

    // Ambil HTTP Status Code
    DWORD dwStatusCode = 0;
    DWORD dwSize = sizeof(dwStatusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);
    printf("[DEBUG] HTTP Status Code: %lu\n", dwStatusCode);

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

    // Trim whitespace
    while (!response.empty() && (response.back() == '\n' || response.back() == '\r' || response.back() == ' '))
        response.pop_back();

    printf("[DEBUG] Raw Response Server: '%s'\n", response.c_str());

    return response;
}

// Handler Cache & URL
inline std::string get_or_upload_icon_url(const std::string& exe_name, const std::string& png_path) {
    const std::string cache_file = "icons/cache.json";
    json cacheObj = json::object();

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

    printf("[DEBUG] Membaca file ikon dari: %s\n", png_path.c_str());
    auto bytes = read_png_bytes(png_path);
    if (bytes.empty()) {
        printf("[ICON] Error: Gagal membaca file ikon PNG!\n");
        return "";
    }

    printf("[ICON] Uploading to Litterbox CDN...\n");
    std::string url = upload_to_litterbox(bytes);

    if (!url.empty() && url.rfind("http", 0) == 0) {
        printf("[ICON] SUCCESS! Direct URL: %s\n", url.c_str());
        cacheObj[exe_name] = url;
        std::ofstream out(cache_file);
        if (out.is_open()) out << cacheObj.dump(4);
        return url;
    } else {
        printf("[ICON] Litterbox Fail: %s\n", url.c_str());
        return "";
    }
}
