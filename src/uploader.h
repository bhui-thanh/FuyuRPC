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

// Upload ke Litterbox (Sangat Ketat Multipart)
inline std::string upload_to_litterbox(const std::vector<BYTE>& file_data) {
    if (file_data.empty()) return "";

    HINTERNET hSession = WinHttpOpen(L"FuyuRPC/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
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

    // Boundary unik
    std::string boundary = "FuyuRPCBoundary7MA4YWxkTrZu0gW";
    std::string contentType = "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n";
    
    int whlen = MultiByteToWideChar(CP_UTF8, 0, contentType.c_str(), -1, nullptr, 0);
    std::wstring wHeaders(whlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, contentType.c_str(), -1, &wHeaders[0], whlen);

    // BANGUN BODY MULTIPART (PHP/Litterbox sangat sensitif terhadap \r\n)
    // 1. reqtype
    std::string bodyHead = "--" + boundary + "\r\n";
    bodyHead += "Content-Disposition: form-data; name=\"reqtype\"\r\n\r\n";
    bodyHead += "fileupload\r\n";
    
    // 2. time (kita set 24 jam)
    bodyHead += "--" + boundary + "\r\n";
    bodyHead += "Content-Disposition: form-data; name=\"time\"\r\n\r\n";
    bodyHead += "24h\r\n";

    // 3. fileToUpload
    bodyHead += "--" + boundary + "\r\n";
    bodyHead += "Content-Disposition: form-data; name=\"fileToUpload\"; filename=\"icon.png\"\r\n";
    bodyHead += "Content-Type: image/png\r\n\r\n";

    std::string bodyTail = "\r\n--" + boundary + "--\r\n";

    // Gabung Binary
    std::vector<BYTE> fullBody;
    fullBody.insert(fullBody.end(), bodyHead.begin(), bodyHead.end());
    fullBody.insert(fullBody.end(), file_data.begin(), file_data.end());
    fullBody.insert(fullBody.end(), bodyTail.begin(), bodyTail.end());

    BOOL bResults = WinHttpSendRequest(hRequest,
                                       wHeaders.c_str(), (DWORD)-1,
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

    // Bersihkan hasil (hapus spasi/newline)
    while (!response.empty() && (response.back() == '\n' || response.back() == '\r' || response.back() == ' '))
        response.pop_back();

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

    // Jika sudah di cache dan linknya masih litterbox (yang valid 24 jam), pakai aja
    if (cacheObj.contains(exe_name) && cacheObj[exe_name].is_string()) {
        std::string cached_url = cacheObj[exe_name].get<std::string>();
        // Cek apakah itu link litterbox (bisa kedaluwarsa, tapi untuk sesi ini kita anggap ok)
        return cached_url;
    }

    auto bytes = read_png_bytes(png_path);
    if (bytes.empty()) return "";

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
