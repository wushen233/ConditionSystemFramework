#pragma once
#include "pch.h"

namespace ConditionSystem::L10n
{
    // 使用游戏引擎的 BSScaleformTranslator 解析 $ 键
    // 游戏在启动时已自动加载 Data/Interface/Translations/*.txt，
    // 因此无需手动读取任何文件。
    inline std::string T(const char* a_key)
    {
        if (!a_key || !a_key[0]) return a_key ? std::string(a_key) : "";

        auto* mgr = RE::BSScaleformManager::GetSingleton();
        if (!mgr) return a_key;

        auto* sfTranslator = mgr->GetTranslator();
        if (!sfTranslator) return a_key;

        // BSTranslator 成员位于 BSScaleformTranslator 偏移 0x20
        auto& map = sfTranslator->translator.translationMap;

        // 将 UTF-8 key 转为宽字符串用于查找（.txt 解析为 UTF-16 LE）
        int wlen = MultiByteToWideChar(CP_UTF8, 0, a_key, -1, nullptr, 0);
        if (wlen <= 0) return a_key;

        std::wstring wkey(static_cast<std::size_t>(wlen) - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, a_key, -1, wkey.data(), wlen);

        RE::BSFixedStringWCS fixedKey(wkey);
        auto it = map.find(fixedKey);
        if (it != map.end()) {
            // 将宽字符串结果转回 UTF-8
            const wchar_t* wstr = it->second.c_str();
            if (wstr && wstr[0]) {
                int utf8len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
                if (utf8len > 0) {
                    std::string result(static_cast<std::size_t>(utf8len) - 1, '\0');
                    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result.data(), utf8len, nullptr, nullptr);
                    return result;
                }
            }
        }

        return a_key;  // fallback: 返回键名本身
    }

    // 重载：支持 std::string
    inline std::string T(const std::string& a_key)
    {
        return T(a_key.c_str());
    }
}

// 便捷宏 — 使用游戏翻译器解析 $ 键
#define LOC(key) ConditionSystem::L10n::T(key)
