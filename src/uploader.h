#pragma once
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <string>
#include <vector>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")

// Encode raw bytes ke string Base64
inline std::string base64_encode(const std::vector<BYTE>& data) {
    DWORD outLen = 0;
    CryptBinaryToStringA(data.data(), (DWORD)data.size(), 
                         CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &outLen);
    std::string out(outLen, '\0');
    CryptBinaryToStringA(data.data(), (DWORD)data.size(), 
                         CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &out[0], &outLen);
    if (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

// Baca file PNG lokal ke binary buffer
inline std::vector<BYTE> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<BYTE> buffer(size);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    return buffer;
}

// Request HTTP menggunakan WinHTTP bawaan Windows
inline std::string discord_api_request(const std::string& method,
                                      const std::string& endpoint,
                                      const std::string& bot_token,
                                      const std::string& json_body = "") {
    HINTERNET hSession = WinHttpOpen(L"DiscordPresenceUploader/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";

    HINTERNET hConnect = WinHttpConnect(hSession, L"discord.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }

    int wlen = MultiByteToWideChar(CP_UTF8, 0, endpoint.c_str(), -1, nullptr, 0);
    std::wstring wEndpoint(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, endpoint.c_str(), -1, &wEndpoint[0], wlen);

    int wmlen = MultiByteToWideChar(CP_UTF8, 0, method.c_str(), -1, nullptr, 0);
    std::wstring wMethod(wmlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, method.c_str(), -1, &wMethod[0], wmlen);

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, wMethod.c_str(), wEndpoint.c_str(),
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    std::string authHeader = "Authorization: Bot " + bot_token + "\r\nContent-Type: application/json\r\n";
    int whlen = MultiByteToWideChar(CP_UTF8, 0, authHeader.c_str(), -1, nullptr, 0);
    std::wstring wHeaders(whlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, authHeader.c_str(), -1, &wHeaders[0], whlen);

    BOOL bResults = WinHttpSendRequest(hRequest,
                                       wHeaders.c_str(), (DWORD)-1,
                                       (LPVOID)json_body.c_str(),
                                       (DWORD)json_body.length(),
                                       (DWORD)json_body.length(), 0);

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
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return response;
}

// Bersihkan nama agar valid di Discord (1-32 chars, a-z, 0-9, _)
inline std::string sanitize_asset_name(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (isalnum((unsigned char)c) || c == '_') {
            out += (char)tolower((unsigned char)c);
        } else if (c == '.' || c == '-' || c == ' ') {
            out += '_';
        }
        if (out.size() >= 32) break;
    }
    return out.empty() ? "game_icon" : out;
}

// Fungsi Utama: Check & Auto-Upload Asset
inline std::string sync_and_upload_icon(const std::string& app_id,
                                       const std::string& bot_token,
                                       const std::string& raw_name,
                                       const std::string& png_file_path) {
    std::string asset_name = sanitize_asset_name(raw_name);

    if (bot_token.empty()) {
        printf("[API] Warning: Bot token is empty. Skipping auto-upload.\n");
        return asset_name;
    }

    printf("[API] Checking existing assets on Discord Developer Portal...\n");
    std::string endpoint = "/api/v10/oauth2/applications/" + app_id + "/assets";
    std::string listRes = discord_api_request("GET", endpoint, bot_token);

    if (!listRes.empty()) {
        try {
            auto jList = json::parse(listRes);
            if (jList.is_array()) {
                for (const auto& item : jList) {
                    if (item.value("name", "") == asset_name) {
                        printf("[API] Asset '%s' already exists in portal! Reusing...\n", asset_name.c_str());
                        return asset_name;
                    }
                }
            }
        } catch (...) {}
    }

    // Jika belum ada, lakukan upload
    printf("[API] Uploading '%s' to Discord Developer Portal...\n", asset_name.c_str());

    auto bytes = read_file(png_file_path);
    if (bytes.empty()) {
        printf("[API] Failed to read PNG file: %s\n", png_file_path.c_str());
        return asset_name;
    }

    std::string base64Data = "data:image/png;base64," + base64_encode(bytes);

    json payload;
    payload["name"] = asset_name;
    payload["type"] = "1"; // 1 = LARGE asset
    payload["image"] = base64Data;

    std::string postRes = discord_api_request("POST", endpoint, bot_token, payload.dump());

    try {
        auto resObj = json::parse(postRes);
        if (resObj.contains("id") || resObj.value("name", "") == asset_name) {
            printf("[API] SUCCESS! Asset '%s' uploaded successfully!\n", asset_name.c_str());
        } else {
            printf("[API] Upload Response: %s\n", postRes.c_str());
        }
    } catch (...) {
        printf("[API] Error parsing response: %s\n", postRes.c_str());
    }

    return asset_name;
}