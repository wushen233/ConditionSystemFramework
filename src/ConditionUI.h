#pragma once

#include <string>
#include <filesystem>
#include <SimpleIni.h>

namespace ConditionSystem
{
    // =========================================================================
    // 模块 1：独立 HUD 菜单类 (Scaleform/QuickLoot 轮询 + PrismaUI HUD 渲染)
    // =========================================================================
    class ConditionUI : public RE::GameMenuBase 
    {
    public:
        // 游戏内注册的菜单名称 (保留给 QuickLoot 耐久数据轮询)
        static constexpr auto MENU_NAME = "ConditionWidgetMenu";

        ConditionUI();
        virtual ~ConditionUI() = default;

        // ----------------------------------------------------
        // 菜单生命周期管理
        // ----------------------------------------------------
        static RE::IMenu* CreateMenu(const RE::UIMessage&);
        static void RegisterMenu();
        static void InitializePrisma();
        static void CreatePrismaView();
        static void OpenMenu();
        static void CloseMenu();
        
        // ----------------------------------------------------
        // UI 核心可见性与刷新控制
        // ----------------------------------------------------
        static void EvaluateVisibility(bool a_animateWidget = false, int a_animationMs = 0);
        static void SetWidgetVisible(bool a_visible, bool a_animate = false, int a_animationMs = 0);
        static void ForceColorRefresh(); 

        // ----------------------------------------------------
        // HUD 耐久度小部件交互 (右下角主件)
        // ----------------------------------------------------
        static void UpdateDurability(float a_percent, float a_points);
        static void UpdateTextVisibility(bool a_show); 
        static void UpdateVisuals(float a_x, float a_y, float a_scale);

        // ----------------------------------------------------
        // 快速掠夺容器 (QuickLoot) 交互
        // ----------------------------------------------------
        static void UpdateQuickLootColor();
        
        // ----------------------------------------------------
        // 卡壳与排障 (Unjam) 机制交互
        // ----------------------------------------------------
        static void UpdatePenaltyThreshold(float a_percent);
        static void ShowUnjammingUI(bool a_show);
        static void UpdateUnjammingProgress(float a_percent);
        static void UpdateUnjamVisuals(float a_offsetX, float a_offsetY, float a_scale);
        
        // ----------------------------------------------------
        // 底层重写
        // ----------------------------------------------------
        // Scaleform 每帧更新回调 (拦截此函数以向 UI 注入 QuickLoot 动态数据)
        virtual void AdvanceMovie(float a_interval, std::uint64_t a_currentTime) override;
        
        // 自动推导并使用和原版一样的智能指针，管理排障部件的着色器滤镜
        decltype(filterHolder) unjamFilterHolder;
    };

    // =========================================================================
    // 模块 2：MCM 配置管理器与菜单事件监听
    // =========================================================================
    class SettingsManager : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
    {
    public:
        static SettingsManager* GetSingleton();
        
        // 加载配置（从 ini 文件及 JSON Profile）
        void Load();
        
        // 注册菜单开关事件，实现 MCM 配置的热更新 (关闭暂停菜单时触发)
        void InstallHook(); 

        virtual RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;
    };
}
