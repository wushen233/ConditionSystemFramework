/*
 * For modders: Copy this file into your own project if you wish to use this API.
 *
 * Prisma UI F4 2.0 API Header — IVPrismaUI1 through IVPrismaUI9
 *
 * This header is backward-compatible with Prisma UI F4 1.0.
 * Recompiling a 1.0 plugin against this header requires no code changes.
 */
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
    #define NOMINMAX
#endif

#include <Windows.h>
#include <stdint.h>

typedef uint64_t PrismaView;

namespace PRISMA_UI_API {
    constexpr const auto PrismaUIPluginName = "PrismaUI_F4";

    // Available PrismaUI interface versions (V1 through V9 in Prisma 2.0)
    enum class InterfaceVersion : uint8_t {
        V1, V2, V3, V4, V5, V6, V7, V8, V9
    };

    typedef void (*OnDomReadyCallback)(PrismaView view);
    typedef void (*JSCallback)(const char* result);
    typedef void (*JSListenerCallback)(const char* argument);

    // JavaScript console message severity level for use with RegisterConsoleCallback().
    enum class ConsoleMessageLevel : uint8_t { Log = 0, Warning, Error, Debug, Info };

    // Console message callback.
    typedef void (*ConsoleMessageCallback)(PrismaView view, ConsoleMessageLevel level, const char* message);

    // Callback type for EnumerateViews.
    typedef void (*ViewEnumCallback)(PrismaView id, const char* htmlPath, void* userdata);

    // Extended callback type for EnumerateViewsEx (V8) — includes owner plugin name.
    typedef void (*ViewEnumExCallback)(PrismaView id, const char* htmlPath, const char* ownerPlugin, void* userdata);

    // PrismaUI modder interface v1 — Core view operations
    class IVPrismaUI1 {
    protected:
        ~IVPrismaUI1() = default;

    public:
        // Create view.
        virtual PrismaView CreateView(const char* htmlPath,
                                      OnDomReadyCallback onDomReadyCallback = nullptr) noexcept = 0;

        // Send JS code to UI.
        virtual void Invoke(PrismaView view, const char* script, JSCallback callback = nullptr) noexcept = 0;

        // Call JS function through JS Interop API (best performance).
        virtual void InteropCall(PrismaView view, const char* functionName, const char* argument) noexcept = 0;

        // Register JS listener.
        virtual void RegisterJSListener(PrismaView view, const char* functionName,
                                        JSListenerCallback callback) noexcept = 0;

        // Returns true if view has focus.
        virtual bool HasFocus(PrismaView view) noexcept = 0;

        // Set focus on view.
        virtual bool Focus(PrismaView view, bool pauseGame = false, bool disableFocusMenu = false) noexcept = 0;

        // Remove focus from view.
        virtual void Unfocus(PrismaView view) noexcept = 0;

        // Show a hidden view.
        virtual void Show(PrismaView view) noexcept = 0;

        // Hide a visible view.
        virtual void Hide(PrismaView view) noexcept = 0;

        // Returns true if view is hidden.
        virtual bool IsHidden(PrismaView view) noexcept = 0;

        // Get scroll size in pixels.
        virtual int GetScrollingPixelSize(PrismaView view) noexcept = 0;

        // Set scroll size in pixels.
        virtual void SetScrollingPixelSize(PrismaView view, int pixelSize) noexcept = 0;

        // Returns true if view exists.
        virtual bool IsValid(PrismaView view) noexcept = 0;

        // Completely destroy view.
        virtual void Destroy(PrismaView view) noexcept = 0;

        // Set view order.
        virtual void SetOrder(PrismaView view, int order) noexcept = 0;

        // Get view order.
        virtual int GetOrder(PrismaView view) noexcept = 0;

        // Create inspector view for debugging.
        virtual void CreateInspectorView(PrismaView view) noexcept = 0;

        // Show or hide the inspector overlay.
        virtual void SetInspectorVisibility(PrismaView view, bool visible) noexcept = 0;

        // Returns true if inspector is visible.
        virtual bool IsInspectorVisible(PrismaView view) noexcept = 0;

        // Set inspector window position and size.
        virtual void SetInspectorBounds(PrismaView view, float topLeftX, float topLeftY, unsigned int width,
                                        unsigned int height) noexcept = 0;

        // Returns true if any view has active focus.
        virtual bool HasAnyActiveFocus() noexcept = 0;
    };

    // PrismaUI modder interface v2 (extends v1) — Console logging
    class IVPrismaUI2 : public IVPrismaUI1 {
    protected:
        ~IVPrismaUI2() = default;

    public:
        // Register a callback to receive JavaScript console messages from a view.
        // Pass nullptr to unregister.
        virtual void RegisterConsoleCallback(PrismaView view, ConsoleMessageCallback callback) noexcept = 0;
    };

    // PrismaUI modder interface v3 (extends v2) — Translations
    class IVPrismaUI3 : public IVPrismaUI2 {
    protected:
        ~IVPrismaUI3() = default;

    public:
        // Register translations for a view from a Fallout 4 translation file.
        virtual void RegisterTranslations(PrismaView view, const char* pluginName) noexcept = 0;
    };

    // PrismaUI modder interface v4 (extends v3) — Game-thread listeners & view enumeration
    class IVPrismaUI4 : public IVPrismaUI3 {
    protected:
        ~IVPrismaUI4() = default;

    public:
        // Game-thread-safe JS listener.
        virtual void BindUIEvent(PrismaView view, const char* functionName,
                                 JSListenerCallback callback) noexcept = 0;

        // Enumerate all currently-registered views across all plugins.
        virtual void EnumerateViews(ViewEnumCallback callback, void* userdata) noexcept = 0;
    };

    // PrismaUI modder interface v5 (extends v4) — On-mesh rendering
    class IVPrismaUI5 : public IVPrismaUI4 {
    protected:
        ~IVPrismaUI5() = default;

    public:
        // Get the ID3D11ShaderResourceView for a view's current offscreen texture.
        // Returns nullptr if the view is not offscreen-rendered.
        virtual void* GetViewSRV(PrismaView view) noexcept = 0;

        // Enable or disable offscreen rendering for a view.
        // When enabled, the view renders to an offscreen texture instead of compositing directly.
        virtual void SetViewOffscreen(PrismaView view, bool offscreen) noexcept = 0;

        // Bind a view's offscreen texture to an in-world 3D geometry node.
        // targetRef is a pointer to the RE::TESObjectREFR — passed as void* to keep
        // this header standalone without requiring CommonLibF4's RE declarations.
        virtual bool BindViewToGeometry(PrismaView view, const char* nodeName,
                                        void* targetRef) noexcept = 0;

        // Bind a view's offscreen texture to a screen-space texture handle.
        virtual bool BindViewToScreenTexture(PrismaView view,
                                              const char* textureHandle) noexcept = 0;

        // Unbind a view from its previously bound geometry.
        virtual void UnbindViewFromGeometry(PrismaView view) noexcept = 0;
    };

    // PrismaUI modder interface v6 (extends v5) — Vanilla UI suppression
    class IVPrismaUI6 : public IVPrismaUI5 {
    protected:
        ~IVPrismaUI6() = default;

    public:
        // Suppress or restore a vanilla HUD widget by its Scaleform path.
        // Known limitation: only works on Old-Gen (1.10.163) currently.
        virtual void SuppressHUDWidget(PrismaView view, const char* widgetPath,
                                        bool suppress) noexcept = 0;

        // Suppress or restore a vanilla menu by name.
        virtual void SuppressVanillaMenu(const char* menuName, bool suppress) noexcept = 0;

        // Close a vanilla menu by name.
        virtual void CloseVanillaMenu(const char* menuName) noexcept = 0;
    };

    // PrismaUI modder interface v7 (extends v6) — Conditional suppression & activate choice
    class IVPrismaUI7 : public IVPrismaUI6 {
    protected:
        ~IVPrismaUI7() = default;

    public:
        // Suppress a vanilla menu only if a condition function returns true.
        virtual void SuppressVanillaMenuIf(const char* menuName,
                                            bool (*condition)(const char* menuName)) noexcept = 0;

        // Enable or disable filtering of the vanilla multi-activate choice menu.
        // Known limitation: only works on Old-Gen (1.10.163) currently.
        virtual void EnableActivateChoiceFilter(PrismaView view,
                                                 bool (*filter)(const char* activateLabel,
                                                                const char* refHandle),
                                                 bool enable) noexcept = 0;

        // Suppress a specific perk entry from the activate choice menu.
        virtual void SuppressActivateChoicePerk(const char* perkName) noexcept = 0;
    };

    // PrismaUI modder interface v8 (extends v7) — Extended enumeration & activate choice control
    class IVPrismaUI8 : public IVPrismaUI7 {
    protected:
        ~IVPrismaUI8() = default;

    public:
        // Enumerate all views with owner-plugin tracking.
        virtual void EnumerateViewsEx(ViewEnumExCallback callback, void* userdata) noexcept = 0;

        // Get the label text of an activate choice entry.
        virtual const char* GetActivateChoiceLabel(const char* refHandle) noexcept = 0;

        // Programmatically trigger an activate choice by its label.
        virtual void TriggerActivateChoice(const char* label) noexcept = 0;

        // Get the health/status of a view. Returns 0 if the view is healthy,
        // non-zero if the view has crashed or is unresponsive.
        virtual uint32_t GetViewHealth(PrismaView view) noexcept = 0;

        // Override the effective size used for offscreen rendering.
        virtual void SetViewOffscreenSize(PrismaView view,
                                           unsigned int width,
                                           unsigned int height) noexcept = 0;
    };

    // Controller style enum for GetControllerStyle / SetControllerStyle (V9)
    enum class ControllerStyle : uint8_t {
        Xbox = 0,
        PlayStation = 1
    };

    // PrismaUI modder interface v9 (extends v8) — Controller/input & escape ownership
    class IVPrismaUI9 : public IVPrismaUI8 {
    protected:
        ~IVPrismaUI9() = default;

    public:
        // Returns true if the player is currently using a gamepad.
        virtual bool IsUsingGamepad() noexcept = 0;

        // Get the current controller button style (Xbox or PlayStation).
        virtual ControllerStyle GetControllerStyle() noexcept = 0;

        // Set the controller button style (Xbox or PlayStation).
        virtual void SetControllerStyle(ControllerStyle style) noexcept = 0;

        // Notify the framework of the current input device.
        virtual void NoteInputDevice(bool isGamepad) noexcept = 0;

        // Get a button prompt token for a given action (e.g., "Activate").
        // Returns a string suitable for rendering in the shell's button prompt system.
        virtual const char* GetButtonPrompt(const char* actionName) noexcept = 0;

        // Get the localized display name for a gamepad button.
        virtual const char* GetGamepadButtonName(uint32_t buttonCode) noexcept = 0;

        // When true, the view consumes Escape key events without unfocusing.
        virtual void SetViewOwnsEscape(PrismaView view, bool ownsEscape) noexcept = 0;

        // Set the background alpha/color for an offscreen-rendered view.
        // Pass nullptr for a_rgba to make the background transparent.
        virtual void SetViewOffscreenBackground(PrismaView view,
                                                 const float* rgba) noexcept = 0;
    };

    // Maps interface types to InterfaceVersion enum values.
    // compile-time constraint -- only request interface versions that actually exist.
    template <typename T>
    struct InterfaceVersionMap;

    template <>
    struct InterfaceVersionMap<IVPrismaUI1> {
        static constexpr InterfaceVersion version = InterfaceVersion::V1;
    };

    template <>
    struct InterfaceVersionMap<IVPrismaUI2> {
        static constexpr InterfaceVersion version = InterfaceVersion::V2;
    };

    template <>
    struct InterfaceVersionMap<IVPrismaUI3> {
        static constexpr InterfaceVersion version = InterfaceVersion::V3;
    };

    template <>
    struct InterfaceVersionMap<IVPrismaUI4> {
        static constexpr InterfaceVersion version = InterfaceVersion::V4;
    };

    template <>
    struct InterfaceVersionMap<IVPrismaUI5> {
        static constexpr InterfaceVersion version = InterfaceVersion::V5;
    };

    template <>
    struct InterfaceVersionMap<IVPrismaUI6> {
        static constexpr InterfaceVersion version = InterfaceVersion::V6;
    };

    template <>
    struct InterfaceVersionMap<IVPrismaUI7> {
        static constexpr InterfaceVersion version = InterfaceVersion::V7;
    };

    template <>
    struct InterfaceVersionMap<IVPrismaUI8> {
        static constexpr InterfaceVersion version = InterfaceVersion::V8;
    };

    template <>
    struct InterfaceVersionMap<IVPrismaUI9> {
        static constexpr InterfaceVersion version = InterfaceVersion::V9;
    };

    typedef void* (*RequestPluginAPIFunc)(InterfaceVersion interfaceVersion);

    /// Request the PrismaUI API interface.
    /// Recommended: Send your request during or after F4SE::MessagingInterface::kGameDataReady to make sure the dll
    /// has already been loaded
    [[nodiscard]] inline void* RequestPluginAPI(InterfaceVersion a_interfaceVersion = InterfaceVersion::V1) {
        auto pluginHandle = GetModuleHandleW(L"PrismaUI_F4.dll");
        if (!pluginHandle) {
            return nullptr;
        }

        auto requestAPIFunction =
            reinterpret_cast<RequestPluginAPIFunc>(GetProcAddress(pluginHandle, "RequestPluginAPI"));

        if (requestAPIFunction) {
            return requestAPIFunction(a_interfaceVersion);
        }

        return nullptr;
    }

    /// Request a specific PrismaUI API interface version.
    /// Returns nullptr if the loaded PrismaUI DLL does not support the requested version.
    ///
    /// Usage:
    ///   auto* api = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI9>(); // recommended for Prisma 2.0
    ///   auto* api = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI4>(); // legacy fallback
    template <typename T>
    [[nodiscard]] inline T* RequestPluginAPI() {
        return static_cast<T*>(RequestPluginAPI(InterfaceVersionMap<T>::version));
    }
}
