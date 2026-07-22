#pragma once
#include <cstdint>
#include <vector>
#include <F4SE/Interfaces.h>

namespace RE { class Actor; class TESObjectWEAP; }

namespace IIF_API
{
    constexpr std::uint32_t kMessage_UpdateItemCard = 0x49494600; // 'IIF'
    constexpr std::uint32_t kMessage_ExchangeInterface = 0x49494601;

    using DamageModifierCallback = void (*)(RE::Actor* attacker, RE::TESObjectWEAP* weapon, float* damagePtr);
    using ArmorModifierCallback = void (*)(RE::Actor* wearer, float* ratingPtr);

    struct CPPCardRegistration {
        const char* id{ nullptr };
        int defaultPriority{ 800 };
        const char* defaultAnchorTarget{ nullptr };
        const char* defaultAnchorMode{ nullptr };
        const char* fallbackAnchorTarget{ nullptr };
        const char* fallbackAnchorMode{ nullptr };
    };

    struct IIF_Interface {
        void (*RegisterCPPCard)(const CPPCardRegistration* reg);
        void (*RegisterDamageModifier)(DamageModifierCallback cb);
        void (*RegisterArmorModifier)(ArmorModifierCallback cb);
    };

    struct CardRequest {
        const char* text{ nullptr }; const char* label{ nullptr }; bool highlightLabel{ false };
        const char* value{ nullptr }; float difference{ 0.0f }; bool hideDifference{ false };
        bool invertDiffColor{ false }; int displayType{ 2 }; bool hasBackground{ false };
        std::uint32_t backgroundColor{ 0 }; const char* sortValueFrom{ nullptr };
        float fillPct{ -1.0f }; float shieldPct{ 0.0f }; std::uint32_t fillColor{ 0 };
        float thresholdPct{ -1.0f }; float thresholdPct2{ -1.0f }; std::uint32_t thresholdColor{ 0 };
        bool showBar{ true }; bool showValue{ false }; const char* valueText{ nullptr };
        const char* valueAlign{ nullptr }; std::uint32_t valueColor{ 0 };
        bool valueStandard{ false }; bool valueBad{ false }; bool valueGood{ false };
        const char* icon1{ nullptr }; bool icon1IsText{ false }; const char* val1{ nullptr };
        bool val1Bad{ false }; bool val1Good{ false }; bool val1Standard{ false }; const char* align1{ nullptr };
        const char* icon2{ nullptr }; bool icon2IsText{ false }; const char* val2{ nullptr };
        bool val2Bad{ false }; bool val2Good{ false }; bool val2Standard{ false }; const char* align2{ nullptr };
    };

    using AddCardCallback = void (*)(void* context, const CardRequest* card);
    using ModifyCardCallback = void (*)(void* context, const char* targetID, double valueMultiplier);

    struct UpdateMessage {
        void* itemForm; void* inventoryItem; void* instanceData; void* gfxArray; void* movie;
        std::uint32_t stackIndex; std::uint32_t itemHandleID; void* context;
        AddCardCallback addCard;
        ModifyCardCallback modifyCard;
    };

    using ClientCallback = void (*)(UpdateMessage* msg);

    // 新增：打包注册结构体
    struct ProviderConfig {
        std::vector<CPPCardRegistration> cards;
        DamageModifierCallback damageModifier{ nullptr };
        ArmorModifierCallback armorModifier{ nullptr };
    };

    inline ProviderConfig& _GetIIFProviderConfig() {
        static ProviderConfig config; return config;
    }
    inline ClientCallback& _GetIIFCallback() {
        static ClientCallback s_cb = nullptr; return s_cb;
    }

    // 智能监听器：自动过滤 IIF 专线频道并完成挂载！
    inline void OnIIFMessage_Internal(F4SE::MessagingInterface::Message* a_msg) {
        if (!a_msg) return;
        if (a_msg->type == kMessage_UpdateItemCard && _GetIIFCallback()) {
            _GetIIFCallback()(static_cast<UpdateMessage*>(a_msg->data));
        }
        else if (a_msg->type == kMessage_ExchangeInterface) {
            auto api = static_cast<IIF_Interface*>(a_msg->data);
            if (api) {
                if (api->RegisterCPPCard) {
                    for (const auto& reg : _GetIIFProviderConfig().cards) api->RegisterCPPCard(&reg);
                }
                if (api->RegisterDamageModifier && _GetIIFProviderConfig().damageModifier) {
                    api->RegisterDamageModifier(_GetIIFProviderConfig().damageModifier);
                }
                if (api->RegisterArmorModifier && _GetIIFProviderConfig().armorModifier) {
                    api->RegisterArmorModifier(_GetIIFProviderConfig().armorModifier);
                }
            }
        }
    }

    inline void RegisterCardProvider(const F4SE::MessagingInterface* messaging, ClientCallback a_callback, ProviderConfig a_config) {
        _GetIIFCallback() = a_callback;
        _GetIIFProviderConfig() = a_config;
        if (messaging) {
            // Prefer the ItemIntegrationFramework sender channel, but fall back to the global
            // listener path because plugin load order can make sender-scoped
            // registration unavailable during early PostLoad.
            if (!messaging->RegisterListener(OnIIFMessage_Internal, "ItemIntegrationFramework")) {
                messaging->RegisterListener(OnIIFMessage_Internal);
            }
        }
    }
}
