#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <filesystem>

#pragma comment(lib, "gdiplus.lib")

// Inisialisasi GDI+ (panggil sekali di awal main)
inline ULONG_PTR g_gdiplusToken = 0;

inline void gdi_init() {
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr);
}

inline void gdi_shutdown() {
    Gdiplus::GdiplusShutdown(g_gdiplusToken);
}

// Convert HICON → simpan sebagai PNG
inline bool save_icon_as_png(HICON hIcon, const std::wstring& outPath) {
    if (!hIcon) return false;

    ICONINFO info{};
    if (!GetIconInfo(hIcon, &info)) return false;

    // Pakai bitmap warna (hbmColor). Jika nullptr, fallback ke hbmMask
    HBITMAP hbm = info.hbmColor ? info.hbmColor : info.hbmMask;
    if (!hbm) {
        if (info.hbmColor) DeleteObject(info.hbmColor);
        if (info.hbmMask)  DeleteObject(info.hbmMask);
        return false;
    }

    Gdiplus::Bitmap bmp(hbm, nullptr);

    // Cari PNG encoder
    UINT num = 0, sz = 0;
    Gdiplus::GetImageEncodersSize(&num, &sz);
    std::vector<BYTE> buf(sz);
    auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    Gdiplus::GetImageEncoders(num, sz, codecs);

    CLSID clsid{};
    bool found = false;
    for (UINT i = 0; i < num; i++) {
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0) {
            clsid = codecs[i].Clsid;
            found = true;
            break;
        }
    }

    Gdiplus::Status st = Gdiplus::Ok;
    if (found) {
        st = bmp.Save(outPath.c_str(), &clsid, nullptr);
    }

    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask)  DeleteObject(info.hbmMask);

    return (st == Gdiplus::Ok);
}

// Extract icon besar dari .exe → simpan ke folder icons/
inline std::string extract_exe_icon(const std::string& exePathUtf8,
                                    const std::string& exeName) {
    // Convert UTF-8 → wstring
    int wlen = MultiByteToWideChar(CP_UTF8, 0, exePathUtf8.c_str(), -1, nullptr, 0);
    std::wstring wPath(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, exePathUtf8.c_str(), -1, &wPath[0], wlen);
    wPath.pop_back();

    // Extract icon (index 0 = icon utama)
    HICON hIconLarge = nullptr;
    HICON hIconSmall = nullptr;
    UINT count = ExtractIconExW(wPath.c_str(), 0, &hIconLarge, &hIconSmall, 1);

    HICON hIcon = hIconLarge ? hIconLarge : hIconSmall;
    if (!hIcon || count == 0) {
        printf("[ICON] No icon found in %s\n", exeName.c_str());
        return "";
    }

    // Buat folder icons/
    std::filesystem::create_directories("icons");

    // Nama output: icons/nama_exe.png
    std::string baseName = exeName;
    auto dot = baseName.rfind('.');
    if (dot != std::string::npos) baseName = baseName.substr(0, dot);

    std::string outUtf8 = "icons/" + baseName + ".png";
    int olen = MultiByteToWideChar(CP_UTF8, 0, outUtf8.c_str(), -1, nullptr, 0);
    std::wstring wOut(olen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, outUtf8.c_str(), -1, &wOut[0], olen);
    wOut.pop_back();

    if (save_icon_as_png(hIcon, wOut)) {
        printf("[ICON] Saved: %s\n", outUtf8.c_str());
    } else {
        printf("[ICON] Failed to save PNG\n");
        outUtf8 = "";
    }

    if (hIconLarge) DestroyIcon(hIconLarge);
    if (hIconSmall) DestroyIcon(hIconSmall);

    return outUtf8;
}