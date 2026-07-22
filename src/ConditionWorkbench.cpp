#include "pch.h"
#include "ConditionWorkbench.h"
#include "ConditionCore.h"
#include "Repair/RepairSystem.h"
#include "Translation.h" 
#include "RE/E/ExamineMenu.h"
#include <mutex>

namespace ConditionSystem::Workbench
{
    std::atomic<RE::TESBoundObject*> g_hoveredObject{ nullptr };
    std::atomic<void*> g_hoveredStack{ nullptr };
    
    static std::vector<RepairMaterial> g_pendingCosts; 
    static std::mutex s_costMutex;  // 🐛 保护 g_pendingCosts 并发访问

    struct WorkbenchRepairKitOption
    {
        std::uint16_t id{ 0 };
        RE::TESBoundObject* kitForm{ nullptr };
        std::uint32_t count{ 0 };
        float repairPct{ 0.0f };
        float maxLimit{ 1.0f };
        std::string displayName;
    };

    static std::vector<WorkbenchRepairKitOption> g_pendingKits;
    static std::uint16_t g_pendingConfirmKitID = 0;
    static constexpr std::size_t kRepairKitButtonsPerPage = 2;

    static std::unordered_map<std::uint32_t, RE::BGSConstructibleObject*> g_recipeCache;
    static std::mutex s_cacheMutex;  // 🐛 保护 g_recipeCache 与 g_cacheInitialized
    static bool g_cacheInitialized = false;

    static std::uint32_t GetInventoryStackCount(const RE::BGSInventoryItem& item);

    void InitializeRecipeCache() {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        if (g_cacheInitialized) return;
        auto dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return;
        auto& cobjs = dataHandler->GetFormArray<RE::BGSConstructibleObject>();
        for (auto* cobj : cobjs) {
            if (cobj && cobj->createdItem) g_recipeCache[cobj->createdItem->GetFormID()] = cobj;
        }
        g_cacheInitialized = true;
    }

    void ResetRecipeCache() {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        g_recipeCache.clear();
        g_cacheInitialized = false;
    }

    void ClearRuntimeSelection()
    {
        g_hoveredObject.store(nullptr);
        g_hoveredStack.store(nullptr);
        std::lock_guard<std::mutex> lock(s_costMutex);
        g_pendingCosts.clear();
        g_pendingKits.clear();
    }

    static RE::BGSConstructibleObject* FindRecipeForCreatedItem(std::uint32_t a_formID)
    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        auto it = g_recipeCache.find(a_formID);
        return it != g_recipeCache.end() ? it->second : nullptr;
    }

    static bool IsWorkbenchRepairTargetSupported(RE::TESBoundObject* a_targetObj)
    {
        if (!a_targetObj) return false;
        if (!a_targetObj->Is(RE::ENUM_FORM_ID::kWEAP) && !a_targetObj->Is(RE::ENUM_FORM_ID::kARMO)) return false;

        auto flags = ConditionSystem::GetItemFlags(a_targetObj);
        return flags.enableDurabilitySystem && flags.showDurabilityUI;
    }

    static float GetModScrapScalar(RE::TESBoundObject* a_componentForm)
    {
        auto component = a_componentForm ? a_componentForm->As<RE::BGSComponent>() : nullptr;
        if (component && component->modScrapScalar) {
            return std::max(0.0f, component->modScrapScalar->GetValue());
        }
        return 0.5f;
    }

    static void AddComponentAmount(std::unordered_map<RE::TESBoundObject*, float>& a_materials, RE::TESForm* a_form, float a_count, bool a_applyModScrapScalar)
    {
        if (!a_form || a_count <= 0.0f) return;

        auto bound = a_form->As<RE::TESBoundObject>();
        if (!bound) return;

        float amount = a_count;
        if (a_applyModScrapScalar && bound->Is(RE::ENUM_FORM_ID::kCMPO)) {
            amount *= GetModScrapScalar(bound);
        }

        if (amount > 0.0f) {
            a_materials[bound] += amount;
        }
    }

    static bool AddMiscScrapComponents(std::unordered_map<RE::TESBoundObject*, float>& a_materials, RE::TESObjectMISC* a_misc, float a_count, bool a_applyModScrapScalar)
    {
        if (!a_misc || !a_misc->componentData || a_misc->componentData->empty() || a_count <= 0.0f) return false;

        bool added = false;
        for (auto& comp : *a_misc->componentData) {
            float count = static_cast<float>(comp.second.i) * a_count;
            auto component = comp.first ? comp.first->As<RE::TESBoundObject>() : nullptr;
            if (!component || count <= 0.0f) continue;

            AddComponentAmount(a_materials, component, count, a_applyModScrapScalar);
            added = true;
        }
        return added;
    }

    static bool AddRecipeScrapComponents(std::unordered_map<RE::TESBoundObject*, float>& a_materials, RE::BGSConstructibleObject* a_recipe)
    {
        if (!a_recipe || !a_recipe->requiredItems) return false;

        bool added = false;
        for (auto& req : *a_recipe->requiredItems) {
            auto reqItem = req.first ? req.first->As<RE::TESBoundObject>() : nullptr;
            float count = static_cast<float>(req.second.i);
            if (!reqItem || count <= 0.0f) continue;

            if (reqItem->Is(RE::ENUM_FORM_ID::kMISC)) {
                if (AddMiscScrapComponents(a_materials, reqItem->As<RE::TESObjectMISC>(), count, true)) {
                    added = true;
                    continue;
                }
            }

            AddComponentAmount(a_materials, reqItem, count, true);
            added = true;
        }
        return added;
    }

    static bool AddRecipeCraftingComponents(std::unordered_map<RE::TESBoundObject*, float>& a_materials, RE::BGSConstructibleObject* a_recipe)
    {
        if (!a_recipe || !a_recipe->requiredItems) return false;

        bool added = false;
        for (auto& req : *a_recipe->requiredItems) {
            auto reqItem = req.first ? req.first->As<RE::TESBoundObject>() : nullptr;
            float count = static_cast<float>(req.second.i);
            if (!reqItem || count <= 0.0f) continue;

            if (reqItem->Is(RE::ENUM_FORM_ID::kMISC)) {
                if (AddMiscScrapComponents(a_materials, reqItem->As<RE::TESObjectMISC>(), count, false)) {
                    added = true;
                    continue;
                }
            }

            AddComponentAmount(a_materials, reqItem, count, false);
            added = true;
        }
        return added;
    }

    static bool AddCurrentExamineScrapComponents(std::unordered_map<RE::TESBoundObject*, float>& a_materials, RE::TESBoundObject* a_object)
    {
        auto ui = RE::UI::GetSingleton();
        auto examineMenu = ui ? ui->GetMenu<RE::ExamineMenu>() : nullptr;
        if (!examineMenu || !a_object || examineMenu->GetCurrentObj() != a_object) return false;

        examineMenu->BuildWeaponScrappingArray();

        bool added = false;
        for (auto& scrap : examineMenu->scrappingArray) {
            if (scrap.first && scrap.second > 0) {
                a_materials[scrap.first] += static_cast<float>(scrap.second);
                added = true;
            }
        }
        return added;
    }

    static bool AddInstalledModScrapComponents(std::unordered_map<RE::TESBoundObject*, float>& a_materials, RE::BGSInventoryItem::Stack* a_stack)
    {
        if (!a_stack || !a_stack->extra) return false;

        auto instExtra = a_stack->extra->GetByType<RE::BGSObjectInstanceExtra>();
        if (!instExtra || !instExtra->values || instExtra->itemIndex == static_cast<std::uint16_t>(-1)) return false;

        bool added = false;
        auto indexData = instExtra->GetIndexData();
        for (auto& idxData : indexData) {
            if (idxData.objectID == 0) continue;

            auto form = RE::TESForm::GetFormByID(idxData.objectID);
            auto looseMod = form ? form->As<RE::TESObjectMISC>() : nullptr;
            if (looseMod && AddMiscScrapComponents(a_materials, looseMod, 1.0f, false)) {
                added = true;
                continue;
            }

            auto omod = form ? form->As<RE::BGSMod::Attachment::Mod>() : nullptr;
            if (omod) {
                looseMod = omod->GetLooseMod();
                if (looseMod && AddMiscScrapComponents(a_materials, looseMod, 1.0f, false)) {
                    added = true;
                    continue;
                }
            }

            RE::BGSConstructibleObject* recipe = FindRecipeForCreatedItem(idxData.objectID);
            if (!recipe && looseMod) {
                recipe = FindRecipeForCreatedItem(looseMod->GetFormID());
            }

            if (recipe && AddRecipeScrapComponents(a_materials, recipe)) {
                added = true;
            }
        }
        return added;
    }

    static bool AddInstalledModCraftingComponents(std::unordered_map<RE::TESBoundObject*, float>& a_materials, RE::BGSInventoryItem::Stack* a_stack)
    {
        if (!a_stack || !a_stack->extra) return false;

        auto instExtra = a_stack->extra->GetByType<RE::BGSObjectInstanceExtra>();
        if (!instExtra || !instExtra->values || instExtra->itemIndex == static_cast<std::uint16_t>(-1)) return false;

        bool added = false;
        auto indexData = instExtra->GetIndexData();
        for (auto& idxData : indexData) {
            if (idxData.objectID == 0) continue;

            auto form = RE::TESForm::GetFormByID(idxData.objectID);
            auto looseMod = form ? form->As<RE::TESObjectMISC>() : nullptr;
            if (!looseMod) {
                auto omod = form ? form->As<RE::BGSMod::Attachment::Mod>() : nullptr;
                looseMod = omod ? omod->GetLooseMod() : nullptr;
            }

            auto recipe = FindRecipeForCreatedItem(idxData.objectID);
            if (!recipe && looseMod) {
                recipe = FindRecipeForCreatedItem(looseMod->GetFormID());
            }

            if (recipe && AddRecipeCraftingComponents(a_materials, recipe)) {
                added = true;
                continue;
            }

            if (looseMod && AddMiscScrapComponents(a_materials, looseMod, 1.0f, false)) {
                added = true;
            }
        }
        return added;
    }

    std::vector<RepairMaterial> CalculateRepairCost(RE::TESBoundObject* a_weapon, RE::BGSInventoryItem::Stack* a_stack, float a_damagePercent) {
        std::vector<RepairMaterial> finalCost;
        std::unordered_map<RE::TESBoundObject*, float> rawMaterials;
        if (!a_weapon) return finalCost;

        InitializeRecipeCache();
        if (g_mcmSettings.repairMaterialMode.load() == 1) {
            AddInstalledModCraftingComponents(rawMaterials, a_stack);
        } else if (!AddCurrentExamineScrapComponents(rawMaterials, a_weapon)) {
            AddInstalledModScrapComponents(rawMaterials, a_stack);
        }

        if (rawMaterials.empty()) {
            auto GetCMPOFromMisc = [](std::uint32_t miscID) -> RE::TESBoundObject* {
                auto miscForm = RE::TESForm::GetFormByID(miscID);
                if (miscForm) {
                    auto misc = miscForm->As<RE::TESObjectMISC>();
                    if (misc && misc->componentData && !misc->componentData->empty()) {
                        auto compForm = (*misc->componentData)[0].first;
                        if (compForm) return static_cast<RE::TESBoundObject*>(compForm);
                    }
                }
                return nullptr;
            };

            auto c_Steel    = GetCMPOFromMisc(0x000731A4); 
            auto c_Wood     = GetCMPOFromMisc(0x000731A3); 
            auto c_Plastic  = GetCMPOFromMisc(0x0006907F); 
            auto c_Leather  = GetCMPOFromMisc(0x000AEC64); 
            auto c_Cloth    = GetCMPOFromMisc(0x000AEC5F); 
            auto c_Aluminum = GetCMPOFromMisc(0x0006907A); 
            
            float weight = 0.0f;
            if (auto valForm = a_weapon->As<RE::TESWeightForm>()) weight = valForm->weight;

            bool isPowerArmor = false, isCombat = false, isMetal = false, isLeather = false, isSynth = false;
            bool isLaser = false, isPlasma = false, isPipe = false, isMelee = false;

            if (auto kwForm = a_weapon->As<RE::BGSKeywordForm>()) {
                if (kwForm->keywords) {
                    for (std::uint32_t i = 0; i < kwForm->numKeywords; ++i) {
                        if (kwForm->keywords[i]) {
                            std::string_view kwStr(kwForm->keywords[i]->formEditorID.c_str());
                            if (kwStr.find("ArmorTypePower") != std::string_view::npos) isPowerArmor = true;
                            else if (kwStr.find("ArmorTypeCombat") != std::string_view::npos) isCombat = true;
                            else if (kwStr.find("ArmorTypeMetal") != std::string_view::npos) isMetal = true;
                            else if (kwStr.find("ArmorTypeLeather") != std::string_view::npos) isLeather = true;
                            else if (kwStr.find("ArmorTypeSynth") != std::string_view::npos) isSynth = true;
                            else if (kwStr.find("Laser") != std::string_view::npos) isLaser = true;
                            else if (kwStr.find("Plasma") != std::string_view::npos) isPlasma = true;
                            else if (kwStr.find("Pipe") != std::string_view::npos) isPipe = true;
                            else if (kwStr.find("Melee") != std::string_view::npos || kwStr.find("Unarmed") != std::string_view::npos) isMelee = true;
                        }
                    }
                }
            }

            if (a_weapon->Is(RE::ENUM_FORM_ID::kWEAP)) {
                if (isLaser && c_Plastic) rawMaterials[c_Plastic] += std::max(2.0f, weight * 0.5f);
                else if (isPlasma && c_Plastic) { rawMaterials[c_Plastic] += 2.0f; if(c_Steel) rawMaterials[c_Steel] += 1.0f; }
                else if (isPipe && c_Wood && c_Steel) { rawMaterials[c_Wood] += std::max(1.0f, weight * 0.4f); rawMaterials[c_Steel] += std::max(1.0f, weight * 0.4f); }
                else if (isMelee && c_Wood && c_Steel) { rawMaterials[c_Wood] += 1.0f; rawMaterials[c_Steel] += std::max(1.0f, weight * 0.5f); }
                else if (c_Steel) rawMaterials[c_Steel] += std::max(2.0f, weight * 0.6f);
            } 
            else if (a_weapon->Is(RE::ENUM_FORM_ID::kARMO)) {
                if (isPowerArmor) {
                    if (c_Aluminum) rawMaterials[c_Aluminum] += std::max(3.0f, weight * 0.3f);
                    if (c_Steel) rawMaterials[c_Steel] += std::max(3.0f, weight * 0.3f);
                } 
                else if (isCombat) {
                    if (c_Plastic) rawMaterials[c_Plastic] += std::max(2.0f, weight * 0.4f);
                    if (c_Steel) rawMaterials[c_Steel] += std::max(1.0f, weight * 0.2f);
                }
                else if (isMetal) {
                    if (c_Steel) rawMaterials[c_Steel] += std::max(3.0f, weight * 0.5f);
                }
                else if (isLeather) {
                    if (c_Leather) rawMaterials[c_Leather] += std::max(2.0f, weight * 0.5f);
                }
                else if (isSynth) {
                    if (c_Plastic) rawMaterials[c_Plastic] += std::max(2.0f, weight * 0.5f);
                }
                else {
                    if (c_Cloth) rawMaterials[c_Cloth] += std::max(2.0f, weight * 0.8f);
                }
            }
        }

        // 按 baseCost 降序排序，主材（需求量大的）排前面
        std::vector<std::pair<RE::TESBoundObject*, float>> sortedMats(rawMaterials.begin(), rawMaterials.end());
        std::sort(sortedMats.begin(), sortedMats.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });

        // 按损坏档位决定保留几种材料：轻伤只要主材，重伤要全部
        bool dynamicCost = g_mcmSettings.dynamicRepairMaterialCost.load();
        float costDamagePercent = dynamicCost ? a_damagePercent : 1.0f;
        size_t totalTypes = sortedMats.size();
        size_t typesToKeep;
        if (!dynamicCost)                   typesToKeep = totalTypes;
        else if (a_damagePercent <= 0.25f)  typesToKeep = 1;
        else if (a_damagePercent <= 0.50f)  typesToKeep = 2;
        else if (a_damagePercent <= 0.75f)  typesToKeep = 3;
        else                                typesToKeep = totalTypes;
        if (typesToKeep > totalTypes) typesToKeep = totalTypes;

        float repairPenaltyMult = 1.2f;
        for (size_t i = 0; i < typesToKeep; ++i) {
            auto componentForm = sortedMats[i].first;
            float baseCost = sortedMats[i].second;
            std::uint32_t finalCount = static_cast<std::uint32_t>(std::ceil(baseCost * costDamagePercent * repairPenaltyMult));

            if (finalCount > 0 && componentForm) {
                std::string compName = LOC("$CSF_UnknownComponent");
                if (auto nameForm = componentForm->As<RE::TESFullName>()) {
                    if (nameForm->GetFullName()) compName = nameForm->GetFullName();
                }
                finalCost.push_back({componentForm, finalCount, compName});
            }
        }
        return finalCost;
    }

    RE::TESObjectREFR* GetWorkbenchContainer() {
        auto ui = RE::UI::GetSingleton();
        if (!ui) return nullptr;
        auto examineMenu = ui->GetMenu<RE::ExamineMenu>();
        if (examineMenu && examineMenu->workbenchContainerRef) {
            return examineMenu->workbenchContainerRef.get();
        }
        return nullptr;
    }

    std::uint32_t GetPlayerMaterialCount(RE::TESBoundObject* materialForm) {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return 0;
        
        std::uint32_t totalCount = 0;
        std::uint32_t count = 0;
        
        player->GetItemCount(count, materialForm, true);
        totalCount += count;

        // 检查工作台自带的容器（玩家存放在工作台里的废料）
        auto workbenchContainer = GetWorkbenchContainer();
        if (workbenchContainer) {
            std::uint32_t wbCount = 0;
            workbenchContainer->GetItemCount(wbCount, materialForm, true);
            totalCount += wbCount;
        }

        auto workshop = RE::Workshop::FindNearestValidWorkshop(*player);
        if (workshop) {
            std::uint32_t wsCount = 0;
            workshop->GetItemCount(wsCount, materialForm, true);
            totalCount += wsCount;
        }
        
        return totalCount;
    }

    RE::BGSInventoryItem::Stack* GetRealStack(RE::TESBoundObject* a_obj, RE::BGSInventoryItem::Stack* a_ghostStack) {
        if (!a_obj || !a_ghostStack) return nullptr;
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->inventoryList) return nullptr;

        RE::BGSInventoryItem::Stack* fallbackStack = nullptr;
        for (auto& item : player->inventoryList->data) {
            if (item.object == a_obj) {
                for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                    if (stack == a_ghostStack) return stack;
                    auto ghostInst = a_ghostStack->extra ? a_ghostStack->extra->GetByType<RE::ExtraInstanceData>() : nullptr;
                    auto realInst = stack->extra ? stack->extra->GetByType<RE::ExtraInstanceData>() : nullptr;
                    
                    bool isInstanceMatch = false;
                    if (ghostInst && realInst) {
                        if (ghostInst->data == realInst->data) isInstanceMatch = true;
                    } else if (!ghostInst && !realInst) {
                        isInstanceMatch = true; 
                    }

                    if (isInstanceMatch) {
                        float ghostHealth = ConditionSystem::GetVisualDurabilityPercent(a_obj, a_ghostStack);
                        float realHealth = ConditionSystem::GetVisualDurabilityPercent(a_obj, stack);
                        if (std::abs(ghostHealth - realHealth) < 0.001f) return stack; 
                    }
                    if (!fallbackStack) fallbackStack = stack;
                }
            }
        }
        return fallbackStack;
    }

    static RE::BGSInventoryItem::Stack* GetStackByIndexSafe(const RE::BGSInventoryItem& a_item, std::uint32_t a_stackIndex)
    {
        auto stack = a_item.stackData.get();
        while (stack && a_stackIndex-- > 0) {
            stack = stack->nextStack.get();
        }
        return stack;
    }

    static bool StackBelongsToItem(const RE::BGSInventoryItem& a_item, RE::BGSInventoryItem::Stack* a_stack)
    {
        if (!a_stack) return false;
        for (auto stack = a_item.stackData.get(); stack; stack = stack->nextStack.get()) {
            if (stack == a_stack) return true;
        }
        return false;
    }

    struct WorkbenchTarget
    {
        RE::TESBoundObject* object{ nullptr };
        RE::BGSInventoryItem::Stack* stack{ nullptr };
    };

    static WorkbenchTarget ResolveHoveredWorkbenchTarget(std::uint32_t a_expectedFormID = 0)
    {
        WorkbenchTarget result;
        auto obj = g_hoveredObject.load();
        auto ghostStack = static_cast<RE::BGSInventoryItem::Stack*>(g_hoveredStack.load());
        if (!obj || !ghostStack) return result;
        if (a_expectedFormID != 0 && obj->GetFormID() != a_expectedFormID) return result;

        auto realStack = GetRealStack(obj, ghostStack);
        if (!realStack) return result;

        result.object = obj;
        result.stack = realStack;
        return result;
    }

    static WorkbenchTarget ResolveWorkbenchTarget(std::uint32_t a_formID, bool a_isEquipped, std::uint32_t a_exactStackIndex, bool a_storeResolvedHover = true)
    {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->inventoryList) return {};

        RE::TESBoundObject* targetObj = nullptr;
        if (a_formID != 0) {
            auto form = RE::TESForm::GetFormByID(a_formID);
            targetObj = form ? form->As<RE::TESBoundObject>() : nullptr;
        }
        if (!targetObj) {
            targetObj = g_hoveredObject.load();
        }
        if (!targetObj) {
            targetObj = ConditionSystem::g_hoveredObject.load();
        }
        if (!targetObj) return {};

        WorkbenchTarget result;
        result.object = targetObj;

        for (auto& item : player->inventoryList->data) {
            if (item.object != targetObj) continue;

            auto hoveredStack = static_cast<RE::BGSInventoryItem::Stack*>(g_hoveredStack.load());
            auto hoveredObj = g_hoveredObject.load();
            auto globalHoveredObj = ConditionSystem::g_hoveredObject.load();
            if (hoveredStack && (hoveredObj == targetObj || globalHoveredObj == targetObj) && StackBelongsToItem(item, hoveredStack)) {
                result.stack = hoveredStack;
            }
            if (!result.stack && a_formID != 0) {
                result.stack = GetStackByIndexSafe(item, a_exactStackIndex);
            }
            if (!result.stack) {
                auto globalStackIndex = ConditionSystem::g_hoveredStackIndex.load();
                if (globalHoveredObj == targetObj) {
                    result.stack = GetStackByIndexSafe(item, globalStackIndex);
                }
            }
            if (!result.stack) {
                for (auto s = item.stackData.get(); s; s = s->nextStack.get()) {
                    if (s->IsEquipped() == a_isEquipped) {
                        result.stack = s;
                        break;
                    }
                }
            }
            if (!result.stack) {
                result.stack = item.stackData.get();
            }
            break;
        }

        if (!result.object || !result.stack) return {};
        if (!ConditionSystem::ShouldShowDurability(result.object)) return {};
        auto flags = ConditionSystem::GetItemFlags(result.object);
        if (!flags.enableDurabilitySystem) return {};

        if (a_storeResolvedHover) {
            g_hoveredObject.store(result.object);
            g_hoveredStack.store(static_cast<void*>(result.stack));
        }
        return result;
    }

    static std::string SanitizeUiToken(std::string a_text)
    {
        for (char& ch : a_text) {
            if (ch == '|' || ch == ',' || ch == ';') {
                ch = ' ';
            }
        }
        return a_text;
    }

    static std::string GetWorkbenchDisplayName(RE::TESBoundObject* a_obj)
    {
        if (!a_obj) return LOC("$CSF_UnnamedItem");
        auto textData = a_obj->As<RE::TESFullName>();
        if (textData && textData->GetFullName()) return textData->GetFullName();
        return LOC("$CSF_UnnamedItem");
    }

    static float GetTargetMaxDurability(RE::TESBoundObject* a_obj)
    {
        if (!a_obj) return 0.0f;
        if (auto weapon = a_obj->As<RE::TESObjectWEAP>()) {
            return ConditionSystem::GetProfileForWeapon(weapon).maxDurability;
        }
        if (auto armor = a_obj->As<RE::TESObjectARMO>()) {
            return ConditionSystem::GetProfileForArmor(armor).maxDurability;
        }
        return 0.0f;
    }

    static std::vector<WorkbenchRepairKitOption> BuildWorkbenchRepairKits(
        RE::TESBoundObject* a_targetObj,
        RE::BGSInventoryItem::Stack* a_targetStack,
        float a_currentPct,
        float a_playerLimit)
    {
        std::vector<WorkbenchRepairKitOption> result;
        if (!ConditionSystem::g_mcmSettings.enableRepairKits.load()) return result;

        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->inventoryList || !a_targetObj || !a_targetStack) return result;
        if (!IsWorkbenchRepairTargetSupported(a_targetObj)) return result;

        const bool isWeapon = a_targetObj->Is(RE::ENUM_FORM_ID::kWEAP);
        const bool isArmor = a_targetObj->Is(RE::ENUM_FORM_ID::kARMO);
        if (!isWeapon && !isArmor) return result;

        const float targetMax = GetTargetMaxDurability(a_targetObj);
        if (targetMax <= 0.0f) return result;

        std::uint16_t nextID = 1;
        for (auto& item : player->inventoryList->data) {
            auto kitObj = item.object;
            if (!kitObj) continue;

            auto kitProfile = ConditionSystem::Repair::GetRepairKitProfile(kitObj);
            if (!kitProfile.isValid) continue;
            if ((isWeapon && !kitProfile.canRepairWeapon) || (isArmor && !kitProfile.canRepairArmor)) continue;

            std::uint32_t count = GetInventoryStackCount(item);
            if (count == 0) continue;

            auto amountData = ConditionSystem::Repair::CalculateRepairAmount(kitProfile);
            float repairPct = amountData.value;
            if (amountData.isFlatPoints) {
                repairPct = amountData.value / targetMax;
            }
            if (repairPct <= 0.0f) continue;

            const float finalLimit = std::min(a_playerLimit, kitProfile.maxConditionLimit);
            if (a_currentPct >= finalLimit - 0.001f) continue;

            WorkbenchRepairKitOption option;
            option.id = nextID++;
            option.kitForm = kitObj;
            option.count = count;
            option.repairPct = repairPct;
            option.maxLimit = finalLimit;
            option.displayName = SanitizeUiToken(GetWorkbenchDisplayName(kitObj));
            result.push_back(option);
        }
        return result;
    }

    static std::uint32_t CountApplicableRepairKits(RE::TESBoundObject* a_targetObj)
    {
        if (!ConditionSystem::g_mcmSettings.enableRepairKits.load()) return 0;

        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->inventoryList || !a_targetObj) return 0;
        if (!IsWorkbenchRepairTargetSupported(a_targetObj)) return 0;

        const bool isWeapon = a_targetObj->Is(RE::ENUM_FORM_ID::kWEAP);
        const bool isArmor = a_targetObj->Is(RE::ENUM_FORM_ID::kARMO);
        if (!isWeapon && !isArmor) return 0;

        std::uint32_t total = 0;
        for (auto& item : player->inventoryList->data) {
            auto kitObj = item.object;
            if (!kitObj) continue;

            auto kitProfile = ConditionSystem::Repair::GetRepairKitProfile(kitObj);
            if (!kitProfile.isValid) continue;
            if ((isWeapon && !kitProfile.canRepairWeapon) || (isArmor && !kitProfile.canRepairArmor)) continue;

            total += GetInventoryStackCount(item);
        }
        return total;
    }

    static void SendRepairKitButtonStateValues(bool a_enabled, std::uint32_t a_totalCount, bool a_supported = true)
    {
        auto ui = RE::UI::GetSingleton();
        auto menu = ui ? ui->GetMenu("ExamineMenu") : nullptr;
        if (menu && menu->uiMovie) {
            const bool materialSupported = a_supported && ConditionSystem::g_mcmSettings.enableWorkbenchMaterialRepair.load();
            const bool kitSupported = a_supported && ConditionSystem::g_mcmSettings.enableRepairKits.load();

            Scaleform::GFx::Value args[5];
            args[0] = a_enabled && kitSupported;
            args[1] = static_cast<double>(a_totalCount);
            args[2] = a_supported;
            args[3] = materialSupported;
            args[4] = kitSupported;
            menu->uiMovie->Invoke("root.SetRepairKitButtonState_Call", nullptr, args, 5);
        }
    }

    static bool RefreshPendingWorkbenchRepairKits(std::uint32_t a_formID = 0, bool a_isEquipped = false, std::uint32_t a_exactStackIndex = 0)
    {
        auto target = ResolveHoveredWorkbenchTarget(a_formID);
        if (!target.object || !target.stack) {
            target = ResolveWorkbenchTarget(a_formID, a_isEquipped, a_exactStackIndex, false);
        }
        RE::TESBoundObject* pObj = target.object;
        RE::BGSInventoryItem::Stack* pRealStack = target.stack;
        if (!pObj || !pRealStack) return false;
        if (!IsWorkbenchRepairTargetSupported(pObj)) {
            std::lock_guard<std::mutex> lock(s_costMutex);
            g_pendingKits.clear();
            return false;
        }

        float currentHealthPct = ConditionSystem::GetVisualDurabilityPercent(pObj, pRealStack);
        float maxRepairLimit = ConditionSystem::GetPlayerOverRepairLimit(pObj, pRealStack->extra.get());
        if (currentHealthPct >= maxRepairLimit - 0.001f) {
            std::lock_guard<std::mutex> lock(s_costMutex);
            g_pendingKits.clear();
            return false;
        }

        auto kits = BuildWorkbenchRepairKits(pObj, pRealStack, currentHealthPct, maxRepairLimit);
        {
            std::lock_guard<std::mutex> lock(s_costMutex);
            g_pendingKits = std::move(kits);
        }
        return true;
    }

    void SendRepairKitButtonStateToUI(std::uint32_t a_formID, bool a_isEquipped, std::uint32_t a_exactStackIndex)
    {
        (void)a_isEquipped;
        (void)a_exactStackIndex;

        bool enabled = false;
        std::uint32_t totalCount = 0;

        auto target = ResolveHoveredWorkbenchTarget(a_formID);
        RE::TESBoundObject* pObj = target.object;
        RE::BGSInventoryItem::Stack* pRealStack = target.stack;
        const bool supported = pObj && pRealStack && IsWorkbenchRepairTargetSupported(pObj);
        if (supported) {
            totalCount = CountApplicableRepairKits(pObj);

            if (pRealStack && totalCount > 0) {
                float currentHealthPct = ConditionSystem::GetVisualDurabilityPercent(pObj, pRealStack);
                float maxRepairLimit = ConditionSystem::GetPlayerOverRepairLimit(pObj, pRealStack->extra.get());
                if (currentHealthPct < maxRepairLimit - 0.001f) {
                    auto kits = BuildWorkbenchRepairKits(pObj, pRealStack, currentHealthPct, maxRepairLimit);
                    enabled = !kits.empty();
                }
            }
        }

        SendRepairKitButtonStateValues(enabled, totalCount, supported);
    }

    void SendRepairCostToUI() {
        auto target = ResolveHoveredWorkbenchTarget();
        RE::TESBoundObject* pObj = target.object;
        RE::BGSInventoryItem::Stack* pRealStack = target.stack;
        if (!pObj || !pRealStack) {
            SendRepairKitButtonStateValues(false, 0, false);
            return;
        }
        if (!IsWorkbenchRepairTargetSupported(pObj)) {
            {
                std::lock_guard<std::mutex> lock(s_costMutex);
                g_pendingCosts.clear();
                g_pendingKits.clear();
            }
            auto ui = RE::UI::GetSingleton();
            auto menu = ui ? ui->GetMenu("ExamineMenu") : nullptr;
            if (menu && menu->uiMovie) {
                SendRepairKitButtonStateValues(false, 0, false);
                menu->uiMovie->Invoke("root.ShowRepairCost_Call", nullptr, "%s", "1.0;@@@");
            }
            return;
        }

        float currentHealthPct = ConditionSystem::GetVisualDurabilityPercent(pObj, pRealStack);
        float maxRepairLimit = ConditionSystem::GetPlayerOverRepairLimit(pObj, pRealStack->extra.get());
        
        auto ui = RE::UI::GetSingleton();
        auto menu = ui ? ui->GetMenu("ExamineMenu") : nullptr;
        if (!menu || !menu->uiMovie) return;

        std::string materialData;
        std::string kitData;
        bool repairKitButtonEnabled = false;
        std::uint32_t repairKitButtonCount = CountApplicableRepairKits(pObj);

        if (currentHealthPct < maxRepairLimit - 0.001f) {
            float damagePct = maxRepairLimit - currentHealthPct;
            std::vector<RepairMaterial> pendingCopy;
            {
                std::lock_guard<std::mutex> lock(s_costMutex);
                g_pendingCosts = ConditionSystem::g_mcmSettings.enableWorkbenchMaterialRepair.load() ? CalculateRepairCost(pObj, pRealStack, damagePct) : std::vector<RepairMaterial>();
                g_pendingKits = BuildWorkbenchRepairKits(pObj, pRealStack, currentHealthPct, maxRepairLimit);
                pendingCopy = g_pendingCosts;  // 🐛 拷贝到局部变量，缩短锁持有时间
            }
            
            auto favMgr = RE::FavoritesManager::GetSingleton();

            for (size_t i = 0; i < pendingCopy.size(); i++) {
                auto& mat = pendingCopy[i];
                std::uint32_t playerHas = GetPlayerMaterialCount(mat.componentForm);
                std::uint32_t req = mat.requiredCount;
                
                bool isTagged = false;
                if (favMgr && mat.componentForm) {
                    auto it = favMgr->favoritedComponents.find(mat.componentForm);
                    isTagged = (it != favMgr->favoritedComponents.end());
                }

                materialData += mat.componentName + "|" + std::to_string(playerHas) + "|" + std::to_string(req) + "|" + (isTagged ? "1" : "0");
                if (i < pendingCopy.size() - 1) materialData += ",";
            }

            std::vector<WorkbenchRepairKitOption> kitCopy;
            {
                std::lock_guard<std::mutex> lock(s_costMutex);
                kitCopy = g_pendingKits;
            }
            repairKitButtonEnabled = !kitCopy.empty();
            for (size_t i = 0; i < kitCopy.size(); i++) {
                const auto& kit = kitCopy[i];
                kitData += std::to_string(kit.id) + "|" + kit.displayName + "|" + std::to_string(kit.count) + "|" +
                    std::to_string(kit.repairPct) + "|" + std::to_string(kit.maxLimit);
                if (i < kitCopy.size() - 1) kitData += ",";
            }
        } else {
            std::lock_guard<std::mutex> lock(s_costMutex);
            g_pendingCosts.clear();
            g_pendingKits.clear();
        }

        std::string costData = std::to_string(maxRepairLimit) + ";" + materialData + "@@@" + kitData;
        
        SendRepairKitButtonStateValues(repairKitButtonEnabled, repairKitButtonCount);
        menu->uiMovie->Invoke("root.ShowRepairCost_Call", nullptr, "%s", costData.c_str());
    }

    static std::uint32_t GetInventoryStackCount(const RE::BGSInventoryItem& item) {
        std::uint32_t stackCount = 0;
        for (auto s = item.stackData.get(); s; s = s->nextStack.get()) {
            stackCount += s->count;
        }
        return stackCount;
    }

    static std::uint32_t PreviewComponentRemainder(RE::TESBoundObject* targetCMPO, std::uint32_t countNeeded, RE::TESObjectREFR* container) {
        if (!container || !container->inventoryList || countNeeded == 0) return countNeeded;
        std::uint32_t remaining = countNeeded;

        for (auto& item : container->inventoryList->data) {
            if (remaining == 0) break;
            if (item.object != targetCMPO) continue;

            std::uint32_t stackCount = GetInventoryStackCount(item);
            if (stackCount >= remaining) remaining = 0;
            else remaining -= stackCount;
        }

        auto consumeMatchingJunk = [&](bool pureOnly) {
            for (auto& item : container->inventoryList->data) {
                if (remaining == 0) break;
                auto obj = item.object;
                if (!obj || !obj->Is(RE::ENUM_FORM_ID::kMISC)) continue;

                auto misc = obj->As<RE::TESObjectMISC>();
                if (!misc || !misc->componentData) continue;

                bool isPure = misc->componentData->size() == 1;
                if (pureOnly != isPure) continue;

                for (auto& comp : *misc->componentData) {
                    if (comp.first != targetCMPO || comp.second.i == 0) continue;

                    std::uint32_t stackCount = GetInventoryStackCount(item);
                    std::uint32_t yielded = stackCount * comp.second.i;
                    if (yielded >= remaining) {
                        remaining = 0;
                    } else {
                        remaining -= yielded;
                    }
                    break;
                }
            }
        };

        consumeMatchingJunk(true);
        if (remaining > 0) consumeMatchingJunk(false);
        return remaining;
    }

    // 扣除组件来源：先扣真正的 loose component，再拆包含该组件的垃圾。
    // 返回仍然需要的剩余数量（如果全扣完了返回0）
    std::uint32_t DeductComponent(RE::TESBoundObject* targetCMPO, std::uint32_t countNeeded, RE::TESObjectREFR* container) {
        if (!container || !container->inventoryList || countNeeded == 0) return countNeeded;
        std::uint32_t remaining = countNeeded;

        for (auto& item : container->inventoryList->data) {
            if (remaining == 0) break;
            if (item.object != targetCMPO) continue;

            std::uint32_t stackCount = GetInventoryStackCount(item);
            std::uint32_t itemsToTake = std::min(stackCount, remaining);
            if (itemsToTake > 0) {
                RE::TESObjectREFR::RemoveItemData rmData(targetCMPO, itemsToTake);
                container->RemoveItem(rmData);
                remaining -= itemsToTake;
            }
        }

        // 第一轮：优先吃纯正材料（产出只有1种的废料）
        for (auto& item : container->inventoryList->data) {
            if (remaining == 0) break;
            auto obj = item.object;
            if (obj && obj->Is(RE::ENUM_FORM_ID::kMISC)) {
                auto misc = obj->As<RE::TESObjectMISC>();
                if (misc && misc->componentData && misc->componentData->size() == 1) {
                    if ((*misc->componentData)[0].first == targetCMPO) {
                        std::uint32_t yield = (*misc->componentData)[0].second.i;
                        if (yield == 0) continue;
                        std::uint32_t stackCount = GetInventoryStackCount(item);

                        std::uint32_t itemsToTake = std::min(stackCount, static_cast<std::uint32_t>(std::ceil(static_cast<float>(remaining) / yield)));
                        if (itemsToTake > 0) {
                            RE::TESObjectREFR::RemoveItemData rmData(obj, itemsToTake);
                            container->RemoveItem(rmData);
                            std::uint32_t yielded = itemsToTake * yield;
                            if (yielded >= remaining) remaining = 0;
                            else remaining -= yielded;
                        }
                    }
                }
            }
        }

        // 第二轮：如果纯正材料不够，连复杂的垃圾也一起拆了抵债
        if (remaining > 0) {
            for (auto& item : container->inventoryList->data) {
                if (remaining == 0) break;
                auto obj = item.object;
                if (obj && obj->Is(RE::ENUM_FORM_ID::kMISC)) {
                    auto misc = obj->As<RE::TESObjectMISC>();
                    if (misc && misc->componentData && misc->componentData->size() > 1) {
                        for (auto& comp : *misc->componentData) {
                            if (comp.first == targetCMPO) {
                                std::uint32_t yield = comp.second.i;
                                if (yield == 0) continue;
                                std::uint32_t stackCount = GetInventoryStackCount(item);

                                std::uint32_t itemsToTake = std::min(stackCount, static_cast<std::uint32_t>(std::ceil(static_cast<float>(remaining) / yield)));
                                if (itemsToTake > 0) {
                                    RE::TESObjectREFR::RemoveItemData rmData(obj, itemsToTake);
                                    container->RemoveItem(rmData);
                                    std::uint32_t yielded = itemsToTake * yield;
                                    if (yielded >= remaining) remaining = 0;
                                    else remaining -= yielded;
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
        return remaining;
    }

    void ExecuteWorkbenchRepairFromUI() {
        if (!ConditionSystem::g_mcmSettings.enableWorkbenchMaterialRepair.load()) return;

        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->inventoryList) return;

        RE::TESBoundObject* pObj = g_hoveredObject.load();
        RE::BGSInventoryItem::Stack* pGhostStack = static_cast<RE::BGSInventoryItem::Stack*>(g_hoveredStack.load());
        if (!pObj || !pGhostStack) return;
        RE::BGSInventoryItem::Stack* pRealStack = GetRealStack(pObj, pGhostStack);
        if (!pRealStack) return;

        float currentHealthPct = ConditionSystem::GetVisualDurabilityPercent(pObj, pRealStack);
        float maxRepairLimit = ConditionSystem::GetPlayerOverRepairLimit(pObj, pRealStack->extra.get());

        if (currentHealthPct >= maxRepairLimit - 0.001f) {
            RE::SendHUDMessage::ShowHUDMessage("$CSF_RepairMaxLimit", "UIActionDeny", true, true);
            return;
        }

        float damagePct = maxRepairLimit - currentHealthPct;
        auto exactCosts = CalculateRepairCost(pObj, pRealStack, damagePct);

        for (const auto& mat : exactCosts) {
            std::uint32_t needed = PreviewComponentRemainder(mat.componentForm, mat.requiredCount, player);
            if (needed > 0) {
                needed = PreviewComponentRemainder(mat.componentForm, needed, GetWorkbenchContainer());
            }
            if (needed > 0) {
                auto workshop = RE::Workshop::FindNearestValidWorkshop(*player);
                needed = PreviewComponentRemainder(mat.componentForm, needed, workshop);
            }

            if (needed > 0) {
                RE::SendHUDMessage::ShowHUDMessage("$CSF_RepairNoMaterials", "UIActionDeny", true, true);
                return;
            }
        }

        // Deduct materials in order from inventory, workbench container, and workshop.
        for (const auto& mat : exactCosts) {
            std::uint32_t needed = mat.requiredCount;

            // 第1层：从玩家背包扣除（包括随身携带的 junk components）
            needed = DeductComponent(mat.componentForm, needed, player);

            // 第2层：从工作台容器扣除（玩家存放在武器工作台里的废料）
            auto workbenchContainer = GetWorkbenchContainer();
            if (workbenchContainer && needed > 0) {
                needed = DeductComponent(mat.componentForm, needed, workbenchContainer);
            }

            // 第3层：从左近定居点工坊扣除
            auto workshop = RE::Workshop::FindNearestValidWorkshop(*player);
            if (workshop && needed > 0) {
                needed = DeductComponent(mat.componentForm, needed, workshop);
            }

            if (needed > 0) {
                RE::SendHUDMessage::ShowHUDMessage("$CSF_RepairNoMaterials", "UIActionDeny", true, true);
                return;
            }
        }

        if (!pRealStack->extra) pRealStack->extra = RE::BSTSmartPointer<RE::ExtraDataList>(new RE::ExtraDataList());
        pRealStack->extra->SetHealthPerc(ConditionSystem::GetCompressedPct(pObj, maxRepairLimit));

        if (pGhostStack != pRealStack && pGhostStack->extra) {
            pGhostStack->extra->SetHealthPerc(ConditionSystem::GetCompressedPct(pObj, maxRepairLimit));
        }

        std::lock_guard<std::mutex> lock(s_costMutex);
        g_pendingCosts.clear();
        g_pendingKits.clear();

        auto inv = RE::BGSInventoryInterface::GetSingleton();
        if (inv) {
            for (auto& item : player->inventoryList->data) {
                if (item.object == pObj) {
                    RE::InventoryInterface::FavoriteChangedEvent ev; ev.itemAffected = &item;
                    // BGSInventoryInterface privately inherits BSTEventSource<FavoriteChangedEvent> at offset 0x60.
                    // Verified by: static_assert(sizeof(BGSInventoryInterface) == 0xD0).
                    auto evSource = reinterpret_cast<RE::BSTEventSource<RE::InventoryInterface::FavoriteChangedEvent>*>(reinterpret_cast<uintptr_t>(inv) + 0x60);
                    if (evSource) evSource->Notify(ev);
                    break;
                }
            }
        }
        
        // 发送纯净的文字提示；AS3 会负责播放维修完成 UI 音效。
        RE::SendHUDMessage::ShowHUDMessage("$CSF_RepairSuccess", nullptr, true, true);

        auto ui = RE::UI::GetSingleton();
        auto menu = ui ? ui->GetMenu("ExamineMenu") : nullptr;
        if (menu && menu->uiMovie) {
            // 调用 AS3 的刷新函数，那个函数现在自带发声功能！
            menu->uiMovie->Invoke("root.RefreshAfterRepair", nullptr, nullptr, 0);
        }
    }

    void ExecuteWorkbenchRepairKitFromUI(std::uint16_t a_kitID)
    {
        if (!ConditionSystem::g_mcmSettings.enableRepairKits.load()) return;

        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->inventoryList || a_kitID == 0) return;

        RE::TESBoundObject* pObj = g_hoveredObject.load();
        RE::BGSInventoryItem::Stack* pGhostStack = static_cast<RE::BGSInventoryItem::Stack*>(g_hoveredStack.load());
        if (!pObj || !pGhostStack) return;

        RE::BGSInventoryItem::Stack* pRealStack = GetRealStack(pObj, pGhostStack);
        if (!pRealStack) return;

        WorkbenchRepairKitOption kitOption;
        {
            std::lock_guard<std::mutex> lock(s_costMutex);
            auto it = std::find_if(g_pendingKits.begin(), g_pendingKits.end(), [a_kitID](const auto& option) {
                return option.id == a_kitID;
            });
            if (it == g_pendingKits.end()) return;
            kitOption = *it;
        }

        if (!kitOption.kitForm) return;

        bool hasKit = false;
        for (auto& item : player->inventoryList->data) {
            if (item.object == kitOption.kitForm && GetInventoryStackCount(item) > 0) {
                hasKit = true;
                break;
            }
        }
        if (!hasKit) {
            RE::SendHUDMessage::ShowHUDMessage("$CSF_NoRepairToolsAvailable", "UIActionDeny", true, true);
            return;
        }

        float currentHealthPct = ConditionSystem::GetVisualDurabilityPercent(pObj, pRealStack);
        float playerLimit = ConditionSystem::GetPlayerOverRepairLimit(pObj, pRealStack->extra.get());
        float finalLimit = std::min(playerLimit, kitOption.maxLimit);
        if (currentHealthPct >= finalLimit - 0.001f) {
            RE::SendHUDMessage::ShowHUDMessage("$CSF_RepairMaxLimit", "UIActionDeny", true, true);
            return;
        }

        float targetMax = GetTargetMaxDurability(pObj);
        float newTotal = std::min(finalLimit, currentHealthPct + kitOption.repairPct);
        float alignedPercent = newTotal;
        if (targetMax > 0.0f) {
            alignedPercent = std::round(newTotal * targetMax) / targetMax;
            if (alignedPercent > finalLimit) alignedPercent = finalLimit;
        }

        if (!pRealStack->extra) pRealStack->extra = RE::BSTSmartPointer<RE::ExtraDataList>(new RE::ExtraDataList());
        pRealStack->extra->SetHealthPerc(ConditionSystem::GetCompressedPct(pObj, alignedPercent));

        if (pGhostStack != pRealStack && pGhostStack->extra) {
            pGhostStack->extra->SetHealthPerc(ConditionSystem::GetCompressedPct(pObj, alignedPercent));
        }

        RE::TESObjectREFR::RemoveItemData rmData(kitOption.kitForm, 1);
        player->RemoveItem(rmData);

        {
            std::lock_guard<std::mutex> lock(s_costMutex);
            g_pendingCosts.clear();
            g_pendingKits.clear();
        }

        auto inv = RE::BGSInventoryInterface::GetSingleton();
        if (inv) {
            for (auto& item : player->inventoryList->data) {
                if (item.object == pObj) {
                    RE::InventoryInterface::FavoriteChangedEvent ev; ev.itemAffected = &item;
                    auto evSource = reinterpret_cast<RE::BSTEventSource<RE::InventoryInterface::FavoriteChangedEvent>*>(reinterpret_cast<uintptr_t>(inv) + 0x60);
                    if (evSource) evSource->Notify(ev);
                    break;
                }
            }
        }

        RE::SendHUDMessage::ShowHUDMessage("$CSF_ToolRepairDone", nullptr, true, true);

        auto ui = RE::UI::GetSingleton();
        auto menu = ui ? ui->GetMenu("ExamineMenu") : nullptr;
        if (menu && menu->uiMovie) {
            Scaleform::GFx::Value args[1];
            args[0] = static_cast<double>(alignedPercent * 100.0f);
            menu->uiMovie->Invoke("root.RefreshAfterRepairToPercent", nullptr, args, 1);
        }
    }
    
    class RepairConfirmCallback : public RE::IMessageBoxCallback {
    public:
        virtual void operator()(std::uint8_t a_buttonIdx) override {
            if (a_buttonIdx == 0) {
                ConditionSystem::Workbench::ExecuteWorkbenchRepairFromUI();
            }
        }
    };

    void ShowRepairConfirmBox() {
        if (!ConditionSystem::g_mcmSettings.enableWorkbenchMaterialRepair.load()) return;

        std::string bodyText = LOC("$REPAIR WITH") + "\n\n";
        {
            std::lock_guard<std::mutex> lock(s_costMutex);
            if (g_pendingCosts.empty()) return;

            for (const auto& mat : g_pendingCosts) {
                std::uint32_t playerHas = GetPlayerMaterialCount(mat.componentForm);
                bodyText += mat.componentName + " (" + std::to_string(playerHas) + "/" + std::to_string(mat.requiredCount) + ")\n";
            }
        }

        auto msgMgr = RE::MessageMenuManager::GetSingleton();
        if (msgMgr) {
            auto callback = new RepairConfirmCallback();
            auto titleText = LOC("$Repair");
            auto btn1Text = LOC("$Confirm");
            auto btn2Text = LOC("$Cancel");
            msgMgr->Create(titleText.c_str(), bodyText.c_str(), callback, static_cast<RE::WARNING_TYPES>(0), btn1Text.c_str(), btn2Text.c_str());
        }
    }

    class RepairKitConfirmCallback : public RE::IMessageBoxCallback {
    public:
        virtual void operator()(std::uint8_t a_buttonIdx) override {
            if (a_buttonIdx == 0) {
                ConditionSystem::Workbench::ExecuteWorkbenchRepairKitFromUI(g_pendingConfirmKitID);
            }
            g_pendingConfirmKitID = 0;
        }
    };

    void ShowRepairKitConfirmBox(std::uint16_t a_kitID)
    {
        if (!ConditionSystem::g_mcmSettings.enableRepairKits.load()) return;

        WorkbenchRepairKitOption kitOption;
        {
            std::lock_guard<std::mutex> lock(s_costMutex);
            auto it = std::find_if(g_pendingKits.begin(), g_pendingKits.end(), [a_kitID](const auto& option) {
                return option.id == a_kitID;
            });
            if (it == g_pendingKits.end()) return;
            kitOption = *it;
        }

        g_pendingConfirmKitID = a_kitID;

        std::string bodyText = LOC("$REPAIR WITH") + "\n\n";
        bodyText += kitOption.displayName + " x" + std::to_string(kitOption.count);

        auto msgMgr = RE::MessageMenuManager::GetSingleton();
        if (msgMgr) {
            auto callback = new RepairKitConfirmCallback();
            auto titleText = LOC("$Repair");
            auto btn1Text = LOC("$Confirm");
            auto btn2Text = LOC("$Cancel");
            msgMgr->Create(titleText.c_str(), bodyText.c_str(), callback, static_cast<RE::WARNING_TYPES>(0), btn1Text.c_str(), btn2Text.c_str());
        }
    }

    class RepairKitListCallback : public RE::IMessageBoxCallback {
    public:
        explicit RepairKitListCallback(std::uint16_t a_page) :
            page(a_page)
        {}

        virtual void operator()(std::uint8_t a_buttonIdx) override {
            std::vector<WorkbenchRepairKitOption> kits;
            {
                std::lock_guard<std::mutex> lock(s_costMutex);
                kits = g_pendingKits;
            }

            const std::size_t start = static_cast<std::size_t>(page) * kRepairKitButtonsPerPage;
            if (a_buttonIdx < kRepairKitButtonsPerPage) {
                const std::size_t kitIndex = start + a_buttonIdx;
                if (kitIndex < kits.size()) {
                    ConditionSystem::Workbench::ExecuteWorkbenchRepairKitFromUI(kits[kitIndex].id);
                }
                return;
            }

            if (a_buttonIdx == 2 && start + kRepairKitButtonsPerPage < kits.size()) {
                ConditionSystem::Workbench::ShowRepairKitListBox(page + 1);
            }
        }

    private:
        std::uint16_t page{ 0 };
    };

    void ShowRepairKitListBox(std::uint16_t a_page, std::uint32_t a_formID, bool a_isEquipped, std::uint32_t a_exactStackIndex)
    {
        if (!ConditionSystem::g_mcmSettings.enableRepairKits.load()) {
            RE::SendHUDMessage::ShowHUDMessage("$CSF_NoRepairToolsAvailable", "UIActionDeny", true, true);
            return;
        }

        if (!RefreshPendingWorkbenchRepairKits(a_formID, a_isEquipped, a_exactStackIndex)) {
            RE::SendHUDMessage::ShowHUDMessage("$CSF_NoRepairToolsAvailable", "UIActionDeny", true, true);
            return;
        }

        std::vector<WorkbenchRepairKitOption> kits;
        {
            std::lock_guard<std::mutex> lock(s_costMutex);
            kits = g_pendingKits;
        }

        if (kits.empty()) {
            RE::SendHUDMessage::ShowHUDMessage("$CSF_NoRepairToolsAvailable", "UIActionDeny", true, true);
            return;
        }

        const std::uint16_t maxPage = static_cast<std::uint16_t>((kits.size() - 1) / kRepairKitButtonsPerPage);
        if (a_page > maxPage) a_page = 0;

        const std::size_t start = static_cast<std::size_t>(a_page) * kRepairKitButtonsPerPage;
        const bool hasFirst = start < kits.size();
        const bool hasSecond = start + 1 < kits.size();
        const bool hasNext = start + kRepairKitButtonsPerPage < kits.size();

        std::string bodyText = LOC("$CSF_RepairTools");
        bodyText += "\n";
        bodyText += std::to_string(a_page + 1) + "/" + std::to_string(maxPage + 1);

        std::string btn1 = hasFirst ? kits[start].displayName + " x" + std::to_string(kits[start].count) : LOC("$Cancel");
        std::string btn2 = hasSecond ? kits[start + 1].displayName + " x" + std::to_string(kits[start + 1].count) : LOC("$Cancel");
        std::string btn3 = hasNext ? LOC("$NEXT") : LOC("$Cancel");
        std::string btn4 = LOC("$Cancel");

        auto msgMgr = RE::MessageMenuManager::GetSingleton();
        if (msgMgr) {
            auto callback = new RepairKitListCallback(a_page);
            auto titleText = LOC("$Repair");
            msgMgr->Create(
                titleText.c_str(),
                bodyText.c_str(),
                callback,
                static_cast<RE::WARNING_TYPES>(0),
                btn1.c_str(),
                btn2.c_str(),
                btn3.c_str(),
                btn4.c_str());
        }
    }

    void ToggleRepairComponentsTag() {
        if (!ConditionSystem::g_mcmSettings.enableWorkbenchMaterialRepair.load()) return;

        auto favMgr = RE::FavoritesManager::GetSingleton();
        if (!favMgr) return;

        bool allMissingTagged = true;
        bool hasMissing = false;

        {
            std::lock_guard<std::mutex> lock(s_costMutex);
            if (g_pendingCosts.empty()) return;

            for (auto& mat : g_pendingCosts) {
            std::uint32_t playerHas = GetPlayerMaterialCount(mat.componentForm);
            if (playerHas < mat.requiredCount) {
                hasMissing = true;
                if (favMgr->favoritedComponents.find(mat.componentForm) == favMgr->favoritedComponents.end()) {
                    allMissingTagged = false; 
                    break;
                }
            }
        }

        if (!hasMissing) {
            allMissingTagged = true;
        }

        bool targetState = !allMissingTagged; 

        for (auto& mat : g_pendingCosts) {
            if (!mat.componentForm) continue;

            if (targetState) {
                std::uint32_t playerHas = GetPlayerMaterialCount(mat.componentForm);
                if (playerHas < mat.requiredCount) {
                    RE::BSTTuple<const RE::TESBoundObject* const, std::uint32_t> favItem{ mat.componentForm, 1u };
                    favMgr->favoritedComponents.insert(favItem);
                }
            } else {
                auto it = favMgr->favoritedComponents.find(mat.componentForm);
                if (it != favMgr->favoritedComponents.end()) {
                    favMgr->favoritedComponents.erase(it);
                }
            }
        }
    }
}
}
