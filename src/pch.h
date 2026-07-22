#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <RE/Fallout.h>
#include <F4SE/F4SE.h>
#include <REX/REX.h>
#include <Scaleform/Scaleform.h>

#include <winsock2.h>
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#ifdef ERROR
#undef ERROR
#endif
#ifdef GetObject
#undef GetObject
#endif
#ifdef GetMessage
#undef GetMessage
#endif

#include <algorithm>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <format>
#include <filesystem>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <thread>
#include <chrono>

#define MAKE_EXE_VERSION_EX(major, minor, build, sub) ((((major) & 0xFF) << 24) | (((minor) & 0xFF) << 16) | (((build) & 0xFFF) << 4) | ((sub) & 0xF))
#define MAKE_EXE_VERSION(major, minor, build)         MAKE_EXE_VERSION_EX(major, minor, build, 0)

// 🚀 编码转换：GBK (系统编码) → UTF-8 (Scaleform/SWF 需要)
inline std::string GBKToUTF8(const std::string& a_gbkStr) {
    if (a_gbkStr.empty()) return a_gbkStr;
    int wideLen = MultiByteToWideChar(CP_ACP, 0, a_gbkStr.c_str(), -1, nullptr, 0);
    if (wideLen <= 0) return a_gbkStr;
    std::wstring wideStr(wideLen, L'\0');
    MultiByteToWideChar(CP_ACP, 0, a_gbkStr.c_str(), -1, wideStr.data(), wideLen);
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) return a_gbkStr;
    std::string utf8Str(utf8Len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), -1, utf8Str.data(), utf8Len, nullptr, nullptr);
    while (!utf8Str.empty() && utf8Str.back() == '\0') utf8Str.pop_back();
    return utf8Str;
}
inline std::string GBKToUTF8(const char* a_gbkStr) {
    return a_gbkStr ? GBKToUTF8(std::string(a_gbkStr)) : std::string();
}

using namespace std::literals;
using namespace REL;
using namespace REX;
using namespace F4SE;