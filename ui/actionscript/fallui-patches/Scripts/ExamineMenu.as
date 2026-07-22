package
{
   import Components.ItemCard;
   import ExamineMenu_fla.InventoryListBase_24;
   import ExamineMenu_fla.ModSlotBase_21;
   import M8r.Controller.ExamineMenuMod;
   import M8r.Debug.Debug;
   import M8r.Framework.ItemCard.ItemCardFwCore;
   import M8r.Helper.TextInputHelper;
   import M8r.Helper.Translation;
   import M8r.Helper.TryHard;
   import M8r.Mods;
   import M8r.Service.F4seService;
   import Shared.AS3.BSButtonHintBar;
   import Shared.AS3.BSButtonHintData;
   import Shared.AS3.BSScrollingList;
   import Shared.AS3.BSScrollingListEntry;
   import Shared.GlobalFunc;
   import Shared.IMenu;
   import flash.display.InteractiveObject;
   import flash.display.MovieClip;
   import flash.events.Event;
   import flash.events.KeyboardEvent;
   import flash.events.MouseEvent;
   import flash.text.TextField;
   import flash.text.TextFieldType;
   import flash.text.TextFormat;
   import flash.ui.Keyboard;
   import flash.utils.clearTimeout;
   import flash.utils.getTimer;
   import flash.utils.setTimeout;
   import scaleform.gfx.Extensions;
   import scaleform.gfx.TextFieldEx;

   public dynamic class ExamineMenu extends IMenu
   {

      public var BGSCodeObj:Object;

      public var InventoryList_mc:MovieClip;

      public var ItemName_tf:TextField;

      public var LegendaryItemDescription_tf:TextField;

      public var ModDescriptionBase_mc:MovieClip;

      public var ItemCardList_mc:ItemCard;

      public var ButtonHintBar_mc:BSButtonHintBar;

      public var InventoryBase_mc:InventoryListBase_24;

      public var ModSlotBase_mc:ModSlotBase_21;

      public var CurrentModsBase_mc:MovieClip;

      public var PerkPanel0_mc:MovieClip;

      public var PerkPanel1_mc:MovieClip;

      public var ItemCatcher:MovieClip;

      public var MouseReleaseCatcher:MovieClip;

      public var VaultBoySafeRectGroup_mc:MovieClip;

      public var ItemSelectBracketBase_mc:MovieClip;

      public var ComponentsBracketBase_mc:MovieClip;

      public var ModBracketBase_mc:MovieClip;

      public var ModSlotBracketBase_mc:MovieClip;

      public var InventoryBracketBase_mc:MovieClip;

      public var InventoryListObject:ListInfoObject;

      public var ModSlotListObject:ListInfoObject;

      public var ComponentsListObject:ListInfoObject;

      public var ModListObject:ListInfoObject;

      public var MiscItemListObject:ListInfoObject;

      public var RequirementsListObject:ListInfoObject;

      public var CurrentModsListObject:ListInfoObject;

      private var InspectModeButtons:Vector.<BSButtonHintData>;

      private var TakeButton:BSButtonHintData;

      private var NextButton:BSButtonHintData;

      private var PrevButton:BSButtonHintData;

      private var ZoomInButton:BSButtonHintData;

      private var ZoomOutButton:BSButtonHintData;

      private var ExitButton:BSButtonHintData;

      private var InventoryButtonHints:Vector.<BSButtonHintData>;

      private var ModSlotButtonHints:Vector.<BSButtonHintData>;

      private var ModButton:BSButtonHintData;

      private var BackButton:BSButtonHintData;

      private var ScrapButton:BSButtonHintData;

      private var RenameButton:BSButtonHintData;

      private var RepairButton:BSButtonHintData;

      private var ModsListHints:Vector.<BSButtonHintData>;

      private var AutoBuild:BSButtonHintData;

      private var ChooseComponents:BSButtonHintData;

      private var TagButton:BSButtonHintData;

      private var AlternateButton:BSButtonHintData;

      private var ComponentsListHints:Vector.<BSButtonHintData>;

      private var Build:BSButtonHintData;

      private var MiscItemListHints:Vector.<BSButtonHintData>;

      private var Add:BSButtonHintData;

      private var RotateButton:BSButtonHintData;

      private var CameraButton:BSButtonHintData;

      private const INVENTORY_MODE:int = 0;

      private const SLOTS_MODE:int = 1;

      private const MOD_MODE:int = 2;

      private const REQUIREMENTS_MODE:int = 3;

      private const ITEM_SELECT_MODE:int = 4;

      private const INSPECT_MODE:int = 5;

      private var _eMode:uint = 0;

      private var bConfirm:Boolean = false;

      private var strStartName:String = "";

      private var strBuildOverrideText:String = "$COOK";

      private var strAlternateButtonText:String = "";

      private var AternateTextEnabled:Boolean = true;

      private var LastFocusedClip:InteractiveObject;

      private var bEnteringText:Boolean = false;

      private var bQueuedBackToMods:Boolean = false;

      public var _inspectMode:Boolean = false;

      private var _featuredItemMode:Boolean = false;

      private var _singleItemInspectMode:Boolean = false;

      private var _itemNameManagedByCode:Boolean = false;

      private var Language:String = "en";

      public var _allowEquip:Boolean = false;

      public var _allowRename:Boolean = true;

      public var _allowRepair:Boolean = false;

      public var _showScrapButton:Boolean = true;

      public var _isCookingMenu:Boolean = false;

      private var _mod:ExamineMenuMod = null;

      private var modAbortTextEditNoSave:Boolean = false;

      private var modUpdate3dItemTimeout:uint = 0;

      private var modUpdateButtonsTimeout:uint = 0;

      private var modUpdatePossModPartArrTimeout:uint = 0;

      private var mod_onlyOnce:Boolean = false;

      // === ConditionSystemFramework repair-mode state ===
      public var isRepairCostMode:Boolean = false;

      public var isRepairModeActive:Boolean = false;

      public var RepairConfirmBtn:BSButtonHintData;

      public var RepairCancelBtn:BSButtonHintData;

      public var RepairTagBtn:BSButtonHintData;

      public var RepairSourceBtn:BSButtonHintData;

      public var ToggleRepairModeBtn:BSButtonHintData;

      public var lastRepairToggleButtonClickTime:int = 0;

      public var lastRepairToggleTime:int = 0;

      public var lastRepairToolKeyTime:int = 0;

      public var cachedRepairCost:Array = null;

      public var cachedRepairMaterials:Array = null;

      public var cachedRepairKits:Array = null;

      public var workbenchRepairSourceMode:int = 0;

      public var repairKitButtonEnabled:Boolean = false;

      public var repairKitButtonCount:int = 0;

      public var repairKitButtonSupported:Boolean = false;

      public var repairMaterialButtonSupported:Boolean = false;

      public var repairTargetSupported:Boolean = false;

      private var repairKitButtonStateRequesting:Boolean = false;

      public var repairKitButtonStateTimeout:uint = 0;

      public var currentRepairLimitNum:Number = 100;

      public var currentRepairLimitText:String = "";

      public var repairCostTimeout:uint = 0;

      public var bJustRepaired:Boolean = false;

      public var bListNeedsUpdate:Boolean = false;

      public var hasMissingMaterials:Boolean = false;

      public var origInventoryX:Number = NaN;

      public var origInventoryY:Number = NaN;

      public var origCardX:Number = NaN;

      public var origCardY:Number = NaN;

      public var origModSlotX:Number = NaN;

      public var origModSlotY:Number = NaN;

      public function ExamineMenu(param1:Boolean = false)
      {
         if(!param1)
         {
            this.AternateTextEnabled = true;
            this._allowRename = true;
            this._mod = new ExamineMenuMod(this);
            this.InventoryListObject = new ListInfoObject();
            this.ModSlotListObject = new ListInfoObject();
            this.ComponentsListObject = new ListInfoObject();
            this.ModListObject = new ListInfoObject();
            this.MiscItemListObject = new ListInfoObject();
            this.RequirementsListObject = new ListInfoObject();
            this.CurrentModsListObject = new ListInfoObject();
            if(!Mods.isVR)
            {
               this.NextButton = new BSButtonHintData("$NEXT","S","PSN_R1","Xenon_R1",1,this.onNextButton);
               this.PrevButton = new BSButtonHintData("$PREV","W","PSN_L1","Xenon_L1",1,this.onPreviousButton);
               this.ZoomInButton = new BSButtonHintData("$ZOOM IN","Wheel up","PSN_R2","Xenon_R2",1,this.onZoomInButton);
               this.ZoomOutButton = new BSButtonHintData("$ZOOM OUT","Wheel down","PSN_L2","Xenon_L2",1,this.onZoomOutButton);
               this.ExitButton = new BSButtonHintData("$EXIT","TAB","PSN_B","Xenon_B",1,this.onBackButton);
               this.BackButton = new BSButtonHintData("$BACK","TAB","PSN_B","Xenon_B",1,this.onBackButton);
               this.RotateButton = new BSButtonHintData("$ROTATE","A/D","PSN_L2R2","Xenon_L2R2",1,null);
               this.CameraButton = new BSButtonHintData("$CAMERA","C","PSN_L1","Xenon_L1",1,null);
               this.TakeButton = new BSButtonHintData("$TAKE","Enter","PSN_A","Xenon_A",1,this.onTakeButton);
               this.ModButton = new BSButtonHintData("$MODIFY","E","PSN_A","Xenon_A",1,this.onModButton);
               this.AutoBuild = new BSButtonHintData("$BUILD","E","PSN_A","Xenon_A",1,this.onScrapBuildAdd);
               this.ChooseComponents = new BSButtonHintData("$CHOOSE COMPONENTS","E","PSN_A","Xenon_A",1,this.onModButton);
               this.ScrapButton = new BSButtonHintData("$SCRAP","R","PSN_X","Xenon_X",1,this.onScrapBuildAdd);
               this.Build = new BSButtonHintData("$BUILD","R","PSN_X","Xenon_X",1,this.onScrapBuildAdd);
               this.AlternateButton = new BSButtonHintData("","R","PSN_X","Xenon_X",1,this.onAlternateButton);
               this.Add = new BSButtonHintData("$ADD","R","PSN_X","Xenon_X",1,this.onScrapBuildAdd);
               this.RenameButton = new BSButtonHintData("$RENAME","T","PSN_Y","Xenon_Y",1,this.onRenameRepairSearch);
               this.RepairButton = new BSButtonHintData("$REPAIR","T","PSN_Y","Xenon_Y",1,this.onRenameRepairSearch);
               this.TagButton = new BSButtonHintData("$TAG FOR SEARCH","T","PSN_Y","Xenon_Y",1,this.onRenameRepairSearch);
               this.ToggleRepairModeBtn = new BSButtonHintData("$CSF_ToggleRepairMaintenance","X","PSN_L1","Xenon_L1",1,null);
               this.RepairSourceBtn = new BSButtonHintData("$CSF_RepairTools","Q","PSN_R1","Xenon_R1",1,this.onRepairSourceClick);
            }
         }
         super();
         if(param1)
         {
            return;
         }
         this.BGSCodeObj = {};
         this.PopulateButtonBar();
         this.InventoryListObject.addEventListener(BSScrollingList.SELECTION_CHANGE,this.InventorySelectionChange,false,0,true);
         this.ModSlotListObject.addEventListener(BSScrollingList.SELECTION_CHANGE,this.FillPossibleModPartArray,false,0,true);
         this.ModListObject.addEventListener(BSScrollingList.SELECTION_CHANGE,this.ModChange,false,0,true);
         this.RequirementsListObject.addEventListener(BSScrollingList.SELECTION_CHANGE,this.UpdateMiscItemList,false,0,true);
         this.MiscItemListObject.CategoryNameList = [];
         this.stage.addEventListener(KeyboardEvent.KEY_DOWN,this.onKeyDown,false,0,true);
         this.addEventListener(KeyboardEvent.KEY_UP,this.onKeyUp,false,0,true);
         this.addEventListener(BSScrollingList.ITEM_PRESS,this.onItemPressed,false,0,true);
         this.addEventListener(MouseEvent.MOUSE_WHEEL,this.onMouseWheel,false,0,true);
         this.ItemCatcher.addEventListener(MouseEvent.MOUSE_DOWN,this.OnStartRotateItem,false,0,true);
         this.stage.addEventListener(MouseEvent.MOUSE_UP,this.OnStopRotateItem,false,0,true);
         this.SetInventoryLabel("$INVENTORY");
         this.MouseReleaseCatcher.visible = false;
         this.stage.stageFocusRect = false;
         this.alpha = 0;
         this.ItemName_tf.selectable = false;
         this.itemName = "";
         this.CurrentModsBase_mc.visible = false;
         this.CurrentModsBase_mc.ModSlotList_mc.disableSelection = true;
         this.UpdatePerks();
         Extensions.enabled = true;
         TextFieldEx.setTextAutoSize(this.ItemName_tf,"shrink");
         TextFieldEx.setTextAutoSize(this.PerkPanel0_mc.PerkName_tf,"shrink");
         TextFieldEx.setTextAutoSize(this.PerkPanel1_mc.PerkName_tf,"shrink");
         TryHard.tryFunction(this._mod.init,"mod.init");
      }

      public function onF4SEObjCreated(param1:*) : void
      {
         Debug.out.info("<ExamineMenu> onF4SEObjCreated() called.");
         F4seService.f4se = param1;
         ItemCardFwCore.instance.onF4seAvailable(param1);
         if(this.modIsRenameAnythingAvailable())
         {
            this.UpdateButtons();
         }
      }

      // === ConditionSystemFramework repair-mode logic ===

      public function ToggleRepairMode() : void
      {
         if(this._allowEquip)
         {
            return;
         }
         if(!this.isRepairModeActive && !this.repairMaterialButtonSupported)
         {
            this.BGSCodeObj.PlaySound("UIMenuCancel");
            return;
         }
         var now:int = getTimer();
         if(this.lastRepairToggleTime > 0 && now - this.lastRepairToggleTime < 250)
         {
            return;
         }
         this.lastRepairToggleTime = now;
         this.isRepairModeActive = !this.isRepairModeActive;
         if(this.isRepairModeActive)
         {
            this.BGSCodeObj.PlaySound("UIMenuOK");
            this.workbenchRepairSourceMode = 0;
            this.ItemName_tf.visible = false;
            this.ModDescriptionBase_mc.visible = false;
            this.cachedRepairCost = null;
            this.cachedRepairMaterials = null;
            this.cachedRepairKits = null;
            this.workbenchRepairSourceMode = 0;
            this.bListNeedsUpdate = true;
            this.RequestRepairCostWithDelay(30);
            this.addEventListener(Event.ENTER_FRAME,this.LockRepairList);
            this.InjectComparisonData();
         }
         else
         {
            this.BGSCodeObj.PlaySound("UIMenuCancel");
            this.workbenchRepairSourceMode = 0;
            this.ResetRepairKitButtonStateRequest();
            this.ItemName_tf.visible = true;
            this.ModDescriptionBase_mc.visible = true;
            this.ClearComparisonData();
            this.removeEventListener(Event.ENTER_FRAME,this.LockRepairList);
            this.HideRepairCost();
            this.RestoreInventoryModeAfterRepair();
            this.RequestRepairKitButtonStateWithDelay(100);
         }
         this.UpdateButtons();
      }

      public function onToggleRepairModeButtonClick() : void
      {
         this.lastRepairToggleButtonClickTime = getTimer();
         this.ToggleRepairMode();
      }

      private function RestoreInventoryModeAfterRepair() : void
      {
         if(this.eMode != this.INVENTORY_MODE)
         {
            return;
         }
         if(this.BGSCodeObj != null)
         {
            this.BGSCodeObj.SwitchBaseItem();
         }
         if(this.ModSlotListObject != null)
         {
            this.ModSlotListObject.SetActive(this.ModSlotBase_mc.ModSlotList_mc,"ModSlotListObject");
            this.ModSlotListObject.RefreshList();
         }
         if(this.ModSlotBase_mc && this.ModSlotBase_mc.ModSlotList_mc)
         {
            this.ModSlotBase_mc.ModSlotList_mc.selectedIndex = -1;
            this.ModSlotBase_mc.ModSlotList_mc.InvalidateData();
         }
         this.SetRightListLabel("$CURRENT MODS:");
         this.UpdateDescription();
      }

      public function InjectComparisonData() : void
      {
         var i:int;
         var child:*;
         var compArr:Array;
         if(!this.isRepairModeActive || !this.ItemCardList_mc)
         {
            return;
         }
         var propList:Object = null;
         if(this.ItemCardList_mc.hasOwnProperty("StatsList_mc"))
         {
            propList = this.ItemCardList_mc["StatsList_mc"];
         }
         else if(this.ItemCardList_mc.hasOwnProperty("List_mc"))
         {
            propList = this.ItemCardList_mc["List_mc"];
         }
         else
         {
            i = 0;
            while(i < this.ItemCardList_mc.numChildren)
            {
               child = this.ItemCardList_mc.getChildAt(i);
               if(child && child.hasOwnProperty("comparisonArray"))
               {
                  propList = child;
                  break;
               }
               i++;
            }
         }
         if(propList && !this.IsWeaponFullHealth())
         {
            compArr = new Array();
            compArr.push({
               "name":"CND",
               "value":100,
               "text":"100",
               "difference":1
            });
            compArr.push({
               "name":"Condition",
               "value":100,
               "text":"100",
               "difference":1
            });
            compArr.push({
               "name":Translation.translate("$CSF_ConditionFullName"),
               "value":100,
               "text":"100",
               "difference":1
            });
            compArr.push({
               "name":Translation.translate("$CSF_ConditionShortName"),
               "value":100,
               "text":"100",
               "difference":1
            });
            propList.comparisonArray = compArr;
            if(propList.hasOwnProperty("InvalidateData"))
            {
               propList.InvalidateData();
            }
         }
      }

      public function ClearComparisonData() : void
      {
         var i:int;
         var child:*;
         if(!this.ItemCardList_mc)
         {
            return;
         }
         var propList:Object = null;
         if(this.ItemCardList_mc.hasOwnProperty("StatsList_mc"))
         {
            propList = this.ItemCardList_mc["StatsList_mc"];
         }
         else
         {
            i = 0;
            while(i < this.ItemCardList_mc.numChildren)
            {
               child = this.ItemCardList_mc.getChildAt(i);
               if(child && child.hasOwnProperty("comparisonArray"))
               {
                  propList = child;
                  break;
               }
               i++;
            }
         }
         if(propList)
         {
            propList.comparisonArray = new Array();
            if(propList.hasOwnProperty("InvalidateData"))
            {
               propList.InvalidateData();
            }
         }
      }

      private function IsWeaponFullHealth() : Boolean
      {
         var selIdx:int = this.InventoryBase_mc.InventoryList_mc.selectedIndex;
         if(selIdx < 0 || !this.InventoryBase_mc.InventoryList_mc.entryList)
         {
            return false;
         }
         var selItem:Object = this.InventoryBase_mc.InventoryList_mc.entryList[selIdx];
         if(selItem == null)
         {
            return false;
         }
         var limit:Number = isNaN(this.currentRepairLimitNum) ? 100 : this.currentRepairLimitNum;
         if(selItem.cndPerc != undefined && selItem.cndPerc >= limit / 100 - 0.01)
         {
            return true;
         }
         if(selItem.cnd != undefined && selItem.cnd >= limit - 1)
         {
            return true;
         }
         if(selItem.health != undefined && selItem.health >= limit - 1)
         {
            return true;
         }
         if(selItem.Condition != undefined && selItem.Condition >= limit - 1)
         {
            return true;
         }
         return false;
      }

      public function RequestRepairCostWithDelay(ms:uint) : void
      {
         clearTimeout(this.repairCostTimeout);
         this.repairCostTimeout = setTimeout(this.RequestCurrentRepairCost,ms);
      }

      public function RequestCurrentRepairCost() : void
      {
         if(this.isRepairModeActive && root.ConditionSystem_Call != null)
         {
            root.ConditionSystem_Call("RequestRepairCost");
         }
      }

      public function onRepairSourceClick() : void
      {
         var selectedEntry:Object;
         var formID:Number;
         var equipState:Number;
         var stackID:Number;
         if(!this.repairKitButtonSupported || !this.repairKitButtonEnabled)
         {
            this.BGSCodeObj.PlaySound("UIMenuCancel");
            return;
         }
         if(root.ConditionSystem_Call != null)
         {
            selectedEntry = this.GetSelectedInventoryEntry();
            formID = this.GetEntryFormID(selectedEntry);
            equipState = this.GetEntryEquipState(selectedEntry);
            stackID = this.GetEntryStackID(selectedEntry);
            this.BGSCodeObj.PlaySound("UIMenuOK");
            root.ConditionSystem_Call("ShowRepairKitListBox",formID,equipState,stackID);
         }
      }

      public function RequestRepairKitButtonState() : void
      {
         var selectedEntry:Object;
         var formID:Number;
         var equipState:Number;
         var stackID:Number;
         if(root.ConditionSystem_Call != null && !this.repairKitButtonStateRequesting)
         {
            selectedEntry = this.GetSelectedInventoryEntry();
            formID = this.GetEntryFormID(selectedEntry);
            equipState = this.GetEntryEquipState(selectedEntry);
            stackID = this.GetEntryStackID(selectedEntry);
            this.repairKitButtonStateRequesting = true;
            root.ConditionSystem_Call("RequestRepairKitButtonState",formID,equipState,stackID);
         }
      }

      public function RequestRepairKitButtonStateWithDelay(ms:uint) : void
      {
         clearTimeout(this.repairKitButtonStateTimeout);
         this.repairKitButtonStateTimeout = setTimeout(this.RequestRepairKitButtonState,ms);
      }

      public function ResetRepairKitButtonStateRequest() : void
      {
         clearTimeout(this.repairKitButtonStateTimeout);
         this.repairKitButtonStateRequesting = false;
      }

      private function GetSelectedInventoryEntry() : Object
      {
         var selIdx:int = this.InventoryBase_mc.InventoryList_mc.selectedIndex;
         if(selIdx < 0 || this.InventoryBase_mc.InventoryList_mc.entryList == null || selIdx >= this.InventoryBase_mc.InventoryList_mc.entryList.length)
         {
            return null;
         }
         return this.InventoryBase_mc.InventoryList_mc.entryList[selIdx];
      }

      private function GetEntryFormID(entry:Object) : Number
      {
         if(entry == null)
         {
            return 0;
         }
         if(entry.hasOwnProperty("formID"))
         {
            return Number(entry.formID);
         }
         if(entry.hasOwnProperty("formId"))
         {
            return Number(entry.formId);
         }
         if(entry.hasOwnProperty("id"))
         {
            return Number(entry.id);
         }
         return 0;
      }

      private function GetEntryEquipState(entry:Object) : Number
      {
         if(entry == null || !entry.hasOwnProperty("equipState"))
         {
            return 0;
         }
         return Number(entry.equipState) > 0 ? 1 : 0;
      }

      private function GetEntryStackID(entry:Object) : Number
      {
         if(entry == null)
         {
            return 0;
         }
         if(entry.hasOwnProperty("stackIndex"))
         {
            if(entry.stackIndex is Array && entry.stackIndex.length > 0)
            {
               return Number(entry.stackIndex[0]);
            }
            return Number(entry.stackIndex);
         }
         if(entry.hasOwnProperty("stackID"))
         {
            if(entry.stackID is Array && entry.stackID.length > 0)
            {
               return Number(entry.stackID[0]);
            }
            return Number(entry.stackID);
         }
         return 0;
      }

      public function SetRepairKitButtonState(enabled:Boolean, count:Number, targetSupported:Boolean = true, materialSupported:Boolean = true, kitSupported:Boolean = true) : void
      {
         var nextCount:int = isNaN(count) ? 0 : int(count);
         var changed:Boolean = this.repairKitButtonEnabled != enabled || this.repairKitButtonCount != nextCount || this.repairKitButtonSupported != kitSupported || this.repairMaterialButtonSupported != materialSupported || this.repairTargetSupported != targetSupported;
         this.repairKitButtonStateRequesting = false;
         this.repairKitButtonEnabled = enabled;
         this.repairKitButtonCount = nextCount;
         this.repairKitButtonSupported = kitSupported;
         this.repairMaterialButtonSupported = materialSupported;
         this.repairTargetSupported = targetSupported;
         if(changed)
         {
            this.UpdateButtons();
         }
      }

      private function GetInventoryRepairButtonHints() : Vector.<BSButtonHintData>
      {
         var result:Vector.<BSButtonHintData> = new Vector.<BSButtonHintData>();
         var i:int = 0;
         var btn:BSButtonHintData;
         while(i < this.InventoryButtonHints.length)
         {
            btn = this.InventoryButtonHints[i];
            if((btn != this.ToggleRepairModeBtn || this.repairMaterialButtonSupported) && (btn != this.RepairSourceBtn || this.repairKitButtonSupported))
            {
               result.push(btn);
            }
            i++;
         }
         return result;
      }

      private function GetRepairKitButtonText() : String
      {
         return Translation.translate("$CSF_RepairTools") + " (" + this.repairKitButtonCount + ")";
      }

      private function ApplyWorkbenchRepairSourceMode() : void
      {
         if(this.workbenchRepairSourceMode == 1)
         {
            this.cachedRepairCost = this.cachedRepairKits != null ? this.cachedRepairKits : [];
            this.hasMissingMaterials = false;
         }
         else
         {
            this.cachedRepairCost = this.cachedRepairMaterials != null ? this.cachedRepairMaterials : [];
            this.RecalculateMissingMaterials();
         }
         this.bListNeedsUpdate = true;
         this.UpdateButtons();
      }

      private function RecalculateMissingMaterials() : void
      {
         var i:int;
         var entry:Object;
         this.hasMissingMaterials = false;
         if(this.cachedRepairMaterials == null)
         {
            return;
         }
         i = 0;
         while(i < this.cachedRepairMaterials.length)
         {
            entry = this.cachedRepairMaterials[i];
            if(entry != null && entry.hasRequired == false)
            {
               this.hasMissingMaterials = true;
               return;
            }
            i++;
         }
      }

      private function LockRepairList(e:Event) : void
      {
         if(!this.isRepairModeActive || this.eMode != this.INVENTORY_MODE)
         {
            return;
         }
         this.ModSlotListObject.SetInactive();
         if(this.cachedRepairCost == null)
         {
            return;
         }
         var targetTitle:String = this.currentRepairLimitText;
         if(this.cachedRepairCost.length == 0)
         {
            targetTitle += this.workbenchRepairSourceMode == 1 ? Translation.translate("$CSF_NoRepairToolsAvailable") : Translation.translate("$CSF_NoRepairNeeded");
         }
         else
         {
            targetTitle += this.workbenchRepairSourceMode == 1 ? Translation.translate("$CSF_RepairTools") : Translation.translate("$REQUIRES:");
         }
         if(this.ModSlotBase_mc.SlotsLabel_tf.text != targetTitle && this.ModSlotBase_mc.SlotsLabel_tf.text != Translation.translate(targetTitle))
         {
            var oldDisable:Boolean = false;
            if(Mods.hasOwnProperty("disableTextColoring"))
            {
               oldDisable = Mods.disableTextColoring;
               Mods.disableTextColoring = true;
            }
            GlobalFunc.SetText(this.ModSlotBase_mc.SlotsLabel_tf,targetTitle,false);
            if(Mods.hasOwnProperty("disableTextColoring"))
            {
               Mods.disableTextColoring = oldDisable;
            }
            if(Mods.configLoaded)
            {
               this.ModSlotBase_mc.SlotsLabel_tf.textColor = Mods.config.stdColor2;
            }
            else
            {
               this.ModSlotBase_mc.SlotsLabel_tf.textColor = 16777215;
            }
         }
         if(this.ModSlotBase_mc.ModSlotList_mc.entryList != this.cachedRepairCost || this.bListNeedsUpdate)
         {
            this.ModSlotListObject.entryList = this.cachedRepairCost;
            this.ModSlotBase_mc.ModSlotList_mc.entryList = this.cachedRepairCost;
            this.ModSlotBase_mc.ModSlotList_mc.InvalidateData();
            this.bListNeedsUpdate = false;
            this.UpdateDescription();
         }
      }

      public function onRepairConfirmClick() : void
      {
         var selectedIndex:int;
         var selectedEntry:Object;
         if(root.ConditionSystem_Call != null)
         {
            if(this.cachedRepairCost == null || this.cachedRepairCost.length == 0 || this.hasMissingMaterials)
            {
               this.BGSCodeObj.PlaySound("UIMenuCancel");
               return;
            }
            this.BGSCodeObj.PlaySound("UIMenuOK");
            root.ConditionSystem_Call("ShowRepairConfirmBox");
         }
      }

      public function onRepairTagClick() : void
      {
         if(root.ConditionSystem_Call != null)
         {
            if(this.cachedRepairCost == null || this.cachedRepairCost.length == 0)
            {
               return;
            }
            this.BGSCodeObj.PlaySound("UIMenuPrevNext");
            root.ConditionSystem_Call("TagRepairComponents");
            this.RequestRepairCostWithDelay(30);
         }
      }

      public function RefreshAfterRepair() : void
      {
         var maxVal:Number = isNaN(this.currentRepairLimitNum) ? 100 : this.currentRepairLimitNum;
         this.RefreshAfterRepairToPercent(maxVal);
      }

      public function RefreshAfterRepairToPercent(percent:Number) : void
      {
         var selIdx:int = this.InventoryBase_mc.InventoryList_mc.selectedIndex;
         if(selIdx !== -1)
         {
            var selItem:Object = this.InventoryBase_mc.InventoryList_mc.entryList[selIdx];
            if(selItem != null)
            {
               var maxVal:Number = isNaN(percent) ? (isNaN(this.currentRepairLimitNum) ? 100 : this.currentRepairLimitNum) : percent;
               selItem.cnd = maxVal;
               selItem.health = maxVal;
               selItem.condition = maxVal;
               selItem.cndPerc = maxVal / 100;
               selItem.Condition = maxVal;
               selItem.isDirty = true;
               selItem._scans = 0;
               if(selItem.hasOwnProperty("_extraData"))
               {
                  selItem._extraData = null;
               }
            }
            if(this._mod != null)
            {
               this._mod.scanOnNextListUpdate = true;
            }
            if(this.InventoryBase_mc && this.InventoryBase_mc.InventoryList_mc)
            {
               this.InventoryBase_mc.InventoryList_mc.InvalidateData();
            }
            if(this.InventoryListObject != null)
            {
               this.InventoryListObject.RefreshList();
            }
            this.InventoryBase_mc.InventoryList_mc.selectedIndex = -1;
            this.InventoryBase_mc.InventoryList_mc.selectedIndex = selIdx;
            this.BGSCodeObj.SwitchBaseItem();
            this.RefreshItemCard();
            this.ClearComparisonData();
            this.BGSCodeObj.PlaySound("UIModsBuild");
            if(this.isRepairModeActive)
            {
               this.cachedRepairCost = null;
               this.cachedRepairMaterials = null;
               this.cachedRepairKits = null;
               this.bListNeedsUpdate = true;
               this.RequestRepairCostWithDelay(80);
            }
         }
      }

      public function onRepairCancelClick() : void
      {
         if(this.isRepairModeActive)
         {
            this.ToggleRepairMode();
         }
      }

      public function ShowRepairCost(costStr:String) : void
      {
         var mainParts:Array;
         var payloadParts:Array;
         var materialPayload:String;
         var kitPayload:String;
         var items:Array;
         var i:int;
         var parts:Array;
         var matName:String;
         var hasCount:int;
         var reqCount:int;
         var isTagged:Boolean;
         var tagPrefix:String;
         var cleanName:String;
         var tagEnd:int;
         var kitID:int;
         var kitName:String;
         var kitCount:int;
         var kitRepairPct:Number;
         var kitLimit:Number;
         if(!this.isRepairModeActive)
         {
            return;
         }
         this.cachedRepairMaterials = [];
         this.cachedRepairKits = [];
         this.cachedRepairCost = [];
         this.currentRepairLimitText = "";
         this.hasMissingMaterials = false;
         if(costStr != null && costStr.length > 0)
         {
            mainParts = costStr.split(";");
            if(mainParts.length >= 1)
            {
               this.currentRepairLimitNum = parseFloat(mainParts[0]) * 100;
               this.currentRepairLimitText = Translation.translate("$CSF_RepairLimitPrefix") + Math.round(this.currentRepairLimitNum) + "% | ";
            }
            materialPayload = "";
            kitPayload = "";
            if(mainParts.length >= 2)
            {
               payloadParts = mainParts[1].split("@@@");
               materialPayload = payloadParts.length >= 1 ? payloadParts[0] : "";
               kitPayload = payloadParts.length >= 2 ? payloadParts[1] : "";
            }
            if(materialPayload.length > 2)
            {
               items = materialPayload.split(",");
               i = 0;
               while(i < items.length)
               {
                  parts = items[i].split("|");
                  if(parts.length >= 3)
                  {
                     matName = parts[0];
                     hasCount = parseInt(parts[1]);
                     reqCount = parseInt(parts[2]);
                     isTagged = false;
                     if(parts.length >= 4)
                     {
                        isTagged = parseInt(parts[3]) > 0;
                     }
                     if(hasCount < reqCount)
                     {
                        this.hasMissingMaterials = true;
                     }
                     tagPrefix = "";
                     cleanName = matName;
                     if(matName.charAt(0) == "[")
                     {
                        tagEnd = matName.indexOf("]");
                        if(tagEnd > 0)
                        {
                           tagPrefix = matName.substring(0,tagEnd + 1) + " ";
                           cleanName = matName.substring(tagEnd + 1);
                           while(cleanName.charAt(0) == " ")
                           {
                              cleanName = cleanName.substring(1);
                           }
                        }
                     }
                     this.cachedRepairMaterials.push({
                        "text":tagPrefix + cleanName,
                        "rawName":cleanName,
                        "name":cleanName,
                        "count":1,
                        "value":hasCount,
                        "accountedFor":hasCount,
                        "requiredCount":reqCount,
                        "available":hasCount,
                        "required":reqCount,
                        "enabled":hasCount >= reqCount,
                        "hasRequired":hasCount >= reqCount,
                        "taggedForSearch":isTagged,
                        "isTagged":isTagged,
                        "ignoreColoring":true
                     });
                  }
                  i++;
               }
            }
            if(kitPayload.length > 2)
            {
               items = kitPayload.split(",");
               i = 0;
               while(i < items.length)
               {
                  parts = items[i].split("|");
                  if(parts.length >= 5)
                  {
                     kitID = parseInt(parts[0]);
                     kitName = parts[1];
                     kitCount = parseInt(parts[2]);
                     kitRepairPct = parseFloat(parts[3]);
                     kitLimit = parseFloat(parts[4]);
                     this.cachedRepairKits.push({
                        "text":kitName,
                        "rawName":kitName,
                        "name":kitName,
                        "count":1,
                        "value":Math.round(kitRepairPct * 100),
                        "accountedFor":kitCount,
                        "requiredCount":1,
                        "available":kitCount,
                        "required":1,
                        "enabled":true,
                        "hasRequired":true,
                        "repairKitID":kitID,
                        "repairPct":kitRepairPct,
                        "repairLimit":kitLimit,
                        "ignoreColoring":true
                     });
                  }
                  i++;
               }
            }
         }
         this.ApplyWorkbenchRepairSourceMode();
      }

      public function HideRepairCost() : void
      {
         this.cachedRepairCost = null;
         this.cachedRepairMaterials = null;
         this.cachedRepairKits = null;
         this.currentRepairLimitText = "";
         this.bListNeedsUpdate = true;
         this.hasMissingMaterials = false;
         this.UpdateDescription();
      }

      // === Frame timeline stops (Flash IDE-style placeholders) ===
      public function frame1() : void
      {
      }

      public function frame5() : void
      {
         this.stop();
      }

      public function frame14() : void
      {
         this.stop();
      }

      public function frame22() : void
      {
         this.stop();
      }

      public function frame30() : void
      {
         this.stop();
      }

      public function frame38() : void
      {
         this.stop();
      }

      public function frame39() : void
      {
         this.stop();
      }

      public function frame47() : void
      {
         this.stop();
      }

      public function frame55() : void
      {
         this.stop();
      }

      public function frame63() : void
      {
         this.stop();
      }

      public function frame71() : void
      {
         this.stop();
      }

      public function frame72() : void
      {
         this.stop();
      }

      // === End repair-mode logic ===

      public function AddRotateButtons() : *
      {
         this.InventoryButtonHints.push(this.RotateButton);
         this.ModsListHints.push(this.RotateButton);
         this.ModSlotButtonHints.push(this.RotateButton);
         this.InventoryButtonHints.push(this.CameraButton);
         this.ModsListHints.push(this.CameraButton);
         this.ModSlotButtonHints.push(this.CameraButton);
         this._mod.isRobotWorkbench = true;
      }

      public function get inspectMode() : *
      {
         return this._inspectMode;
      }

      public function set inspectMode(param1:Boolean) : *
      {
         this._inspectMode = param1;
      }

      public function set singleItemInspectMode(param1:Boolean) : *
      {
         this._singleItemInspectMode = param1;
         this.PrevButton.ButtonVisible = this.NextButton.ButtonVisible = !this._singleItemInspectMode;
      }

      public function set featuredItemMode(param1:Boolean) : *
      {
         this._featuredItemMode = param1;
         this.TakeButton.ButtonVisible = this._featuredItemMode;
         this.ExitButton.ButtonVisible = !this._featuredItemMode;
         if(this._featuredItemMode)
         {
            this.singleItemInspectMode = true;
         }
      }

      public function set alternateTextEnabled(param1:Boolean) : *
      {
         this.AternateTextEnabled = param1;
      }

      public function set alternateButtonText(param1:String) : *
      {
         this.strAlternateButtonText = param1;
      }

      public function set itemNameManagedByCode(param1:Boolean) : *
      {
         this._itemNameManagedByCode = param1;
      }

      public function get shouldHighlight() : Boolean
      {
         return this.eMode != this.INVENTORY_MODE;
      }

      public function get inventoryLevel() : Boolean
      {
         return this.eMode == this.INVENTORY_MODE;
      }

      public function get resetItemRotation() : Boolean
      {
         return this.eMode == this.INVENTORY_MODE || this.eMode == this.INSPECT_MODE;
      }

      public function get shouldReshow() : Boolean
      {
         return this.eMode == this.INVENTORY_MODE || this.eMode == this.MOD_MODE || this.eMode == this.INSPECT_MODE;
      }

      public function set allowEquip(param1:Boolean) : *
      {
         this._mod.isPowerArmorWorkbench = true;
         this._allowEquip = param1;
      }

      public function set BuildOverrideText(param1:String) : *
      {
         this.strBuildOverrideText = param1;
      }

      public function get allowRename() : *
      {
         if(this.eMode === this.INSPECT_MODE)
         {
            if(this.modIsRenameAnythingAvailable())
            {
               return true;
            }
         }
         return this.eMode == this.INVENTORY_MODE && this._allowRename;
      }

      private function modIsRenameAnythingAvailable() : Boolean
      {
         var _loc1_:Object = F4seService.getF4se();
         var _loc2_:Array = null;
         var _loc3_:Object = null;
         if(_loc1_)
         {
            _loc2_ = _loc1_.GetDirectoryListing("Data\\F4SE\\Plugins");
            if(_loc2_)
            {
               for each(_loc3_ in _loc2_)
               {
                  if(String(_loc3_.name).toLowerCase().indexOf("rename_anything.dll") > -1)
                  {
                     return true;
                  }
               }
            }
         }
         return false;
      }

      public function set allowRename(param1:Boolean) : *
      {
         this._allowRename = param1;
      }

      public function set allowRepair(param1:Boolean) : *
      {
         this._allowRepair = param1;
      }

      public function set showScrapButton(param1:Boolean) : *
      {
         this._showScrapButton = param1;
      }

      public function get isCookingMenu() : Boolean
      {
         return this._isCookingMenu;
      }

      public function set language(param1:String) : *
      {
         this.Language = param1.toLowerCase();
      }

      override protected function onSetSafeRect() : void
      {
         GlobalFunc.LockToSafeRect(this.VaultBoySafeRectGroup_mc,"TL",SafeX,SafeY);
      }

      private function OnStartRotateItem(param1:MouseEvent) : void
      {
         this.BGSCodeObj.StartRotate3DItem();
         this.MouseReleaseCatcher.visible = true;
      }

      private function OnStopRotateItem(param1:MouseEvent) : void
      {
         this.BGSCodeObj.EndRotate3DItem();
         this.MouseReleaseCatcher.visible = false;
      }

      public function AllowRotate() : Boolean
      {
         return this.eMode != this.MOD_MODE || this.ModSlotBase_mc.ModSlotList_mc.entryList.length <= 5;
      }

      public function onKeyDown(param1:KeyboardEvent) : void
      {
         if(param1.keyCode == 81 && this.eMode == this.INVENTORY_MODE && !this._allowEquip)
         {
            this.lastRepairToolKeyTime = getTimer();
            this.onRepairSourceClick();
            param1.stopImmediatePropagation();
         }
      }

      public function onKeyUp(param1:KeyboardEvent) : void
      {
         this._mod.anyEvent(param1);
         if(TextInputHelper.instance.active)
         {
            return;
         }
         if(param1.keyCode == 13 && this.bEnteringText)
         {
            this.enteringText = !this.enteringText;
         }
         if((param1.keyCode === Keyboard.TAB || param1.keyCode === Keyboard.ESCAPE) && this.bEnteringText)
         {
            setTimeout(this.AbortTextEditing,50);
         }
         if(param1.keyCode == 81 && this.eMode == this.INVENTORY_MODE && !this._allowEquip)
         {
            return;
         }
         if(this.eMode === this.INSPECT_MODE && param1.keyCode === Keyboard.T && this._allowRename)
         {
            this.onRenameRepairSearch();
         }
         if(param1.keyCode == 88 && this.eMode == this.INVENTORY_MODE && !this._allowEquip)
         {
            if(this.lastRepairToggleButtonClickTime > 0 && getTimer() - this.lastRepairToggleButtonClickTime < 250)
            {
               this.lastRepairToggleButtonClickTime = 0;
               return;
            }
            this.ToggleRepairMode();
            return;
         }
      }

      private function onItemPressed(param1:Event) : *
      {
         if(this.isRepairModeActive)
         {
            param1.stopPropagation();
            return;
         }
         if(!this.bEnteringText)
         {
            this.onModButton();
            param1.stopPropagation();
         }
      }

      public function set itemName(param1:String) : *
      {
         GlobalFunc.SetText(this.ItemName_tf,param1,false,true);
         this._mod.updateItemNameText();
      }

      private function PopulateButtonBar() : void
      {
         var _loc1_:Vector.<BSButtonHintData> = new Vector.<BSButtonHintData>();
         this.InspectModeButtons = _loc1_;
         _loc1_.push(this.TakeButton);
         _loc1_.push(this.RenameButton);
         this.TakeButton.ButtonVisible = false;
         _loc1_.push(this.PrevButton);
         _loc1_.push(this.NextButton);
         if(!Mods.isVR)
         {
            _loc1_.push(this.ZoomInButton);
            _loc1_.push(this.ZoomOutButton);
         }
         _loc1_.push(this.ExitButton);
         _loc1_ = new Vector.<BSButtonHintData>();
         this.InventoryButtonHints = _loc1_;
         _loc1_.push(this.ScrapButton);
         _loc1_.push(this.RepairButton);
         _loc1_.push(this.ModButton);
         _loc1_.push(this.RenameButton);
         _loc1_.push(this.BackButton);
         _loc1_.push(this.AlternateButton);
         _loc1_.push(this.ToggleRepairModeBtn);
         _loc1_.push(this.RepairSourceBtn);
         _loc1_ = new Vector.<BSButtonHintData>();
         this.ModSlotButtonHints = _loc1_;
         _loc1_.push(this.ModButton);
         _loc1_.push(this.BackButton);
         _loc1_.push(this.AlternateButton);
         _loc1_ = new Vector.<BSButtonHintData>();
         this.ModsListHints = _loc1_;
         _loc1_.push(this.AutoBuild);
         _loc1_.push(this.TagButton);
         _loc1_.push(this.BackButton);
         _loc1_.push(this.AlternateButton);
         _loc1_ = new Vector.<BSButtonHintData>();
         this.ComponentsListHints = _loc1_;
         _loc1_.push(this.Build);
         _loc1_.push(this.ChooseComponents);
         _loc1_.push(this.BackButton);
         _loc1_ = new Vector.<BSButtonHintData>();
         this.MiscItemListHints = _loc1_;
         _loc1_.push(this.Add);
         _loc1_.push(this.BackButton);
         this._mod.onPopulateButtonBar(this.ModsListHints,this.Build);
         this._mod.modSetButtonHintData(this.InventoryButtonHints);
      }

      public function StartInspectMode() : *
      {
         this._mod.modSetButtonHintData(this.InspectModeButtons);
         gotoAndStop("InspectMode");
         this.InventoryBase_mc.visible = false;
         Debug.out.log("<ExamineMenu> StartInspectMode",Debug.out.LEVEL_INFO);
         this.CurrentModsBase_mc.ModSlotList_mc.visible = true;
         this.CurrentModsListObject.SetActive(this.CurrentModsBase_mc.ModSlotList_mc,"CurrentModsListObject");
         this.eMode = this.INSPECT_MODE;
         this.InventoryBase_mc.InventoryList_mc.disableInput = true;
         this.UpdateButtons();
      }

      public function RegisterComponents() : *
      {
         this.alpha = 1;
         this.BGSCodeObj.RegisterComponents(this,this.InventoryListObject,this.ItemName_tf,this.ModDescriptionBase_mc.ModDescription_tf,this.ItemCardList_mc,this.ComponentsListObject,this.ModSlotListObject,this.ModListObject,this.MiscItemListObject,this.RequirementsListObject,this.ButtonHintBar_mc,this.CurrentModsListObject);
         this.ItemCardList_mc.scaleX = 0.7;
         this.ItemCardList_mc.scaleY = 0.7;
         this.AdjustBracket(this.InventoryBracketBase_mc);
         this.AdjustBracket(this.ModSlotBracketBase_mc);
         this.AdjustBracket(this.ModBracketBase_mc);
         this.AdjustBracket(this.ComponentsBracketBase_mc);
         this.AdjustBracket(this.ItemSelectBracketBase_mc);
         this.stage.focus = this.InventoryBase_mc.InventoryList_mc;
         if(!this._isCookingMenu)
         {
            this.InventoryListObject.SetActive(this.InventoryBase_mc.InventoryList_mc,"InventoryListObject");
            this.ModSlotListObject.SetActive(this.ModSlotBase_mc.ModSlotList_mc,"ModSlotListObject");
            this.ModSlotBase_mc.ModSlotList_mc.disableSelection = true;
            this.ModSlotBase_mc.ModSlotList_mc.allowWheelScrollNoSelectionChange = true;
         }
         this.ModSlotBase_mc.ModSlotList_mc.selectedIndex = -1;
         this.ModSlotBase_mc.ModSlotList_mc.InvalidateData();
         this.UpdateDescription();
         // === ConditionSystemFramework callbacks: expose to AVM root so native side can invoke them ===
         root.ShowRepairCost_Call = this.ShowRepairCost;
         root.RefreshAfterRepair = this.RefreshAfterRepair;
         root.RefreshAfterRepairToPercent = this.RefreshAfterRepairToPercent;
         root.SetRepairKitButtonState_Call = this.SetRepairKitButtonState;
      }

      public function FillPossibleModPartArray(param1:Event) : *
      {
         if(this.isRepairModeActive)
         {
            return;
         }
         clearTimeout(this.modUpdatePossModPartArrTimeout);
         this.modUpdatePossModPartArrTimeout = setTimeout(this.FillPossibleModPartArrayExec,40);
      }

      private function FillPossibleModPartArrayExec() : *
      {
         if(this.ModSlotListObject.selectedIndex > -1 && this.ModListObject.entryList)
         {
            this.BGSCodeObj.FillModPartArray(this.ModListObject.entryList,this.ModListObject);
         }
         this.UpdateDescription();
      }

      public function ModChange(param1:Event) : *
      {
         if(!this.bEnteringText)
         {
            this.onModChange();
         }
         else
         {
            this.AbortTextEditing();
         }
      }

      public function onModChange() : *
      {
         var _loc1_:Array = null;
         if(!this._isCookingMenu || this.eMode == this.MOD_MODE)
         {
            _loc1_ = [];
            this.BGSCodeObj.SwitchMod(this.ModListObject.selectedIndex,_loc1_);
            this.BGSCodeObj.UpdateRequirements();
            this.AutoBuild.ButtonEnabled = this.GetBuildable(true);
            this.AutoBuild.ButtonVisible = true;
            this.AutoBuild.ButtonText = this.GetLooseModAvailable() ? "$ATTACH MOD" : "$BUILD";
            if(this.eMode == this.MOD_MODE && this.ModListObject.entryList[this.ModListObject.selectedIndex].hasLooseMod)
            {
               this.ChooseComponents.ButtonText = "$ATTACH MOD";
            }
            else
            {
               this.ChooseComponents.ButtonText = "$CHOOSE COMPONENTS";
            }
            this.UpdatePerks();
            this.UpdateDescription();
            this.UpdateButtons();
         }
      }

      public function UpdatePerks() : *
      {
         var _loc6_:int = 0;
         var _loc7_:Array = null;
         var _loc8_:TextField = null;
         var _loc9_:String = null;
         var _loc10_:String = null;
         var _loc1_:int = 0;
         var _loc2_:MovieClip = null;
         var _loc3_:TextFormat = null;
         var _loc4_:int = 0;
         var _loc5_:int = this._mod.modMaxPerkRequirementPanels;
         _loc6_ = 0;
         while(_loc6_ < _loc5_)
         {
            _loc2_ = this["PerkPanelClone" + _loc6_ + "_mc"];
            if(_loc2_)
            {
               _loc2_.visible = false;
            }
            _loc6_++;
         }
         if(this.eMode == this.MOD_MODE)
         {
            if(this.ModListObject.selectedIndex >= 0)
            {
               _loc7_ = this.ModListObject.entryList[this.ModListObject.selectedIndex].perkData;
               if(_loc7_)
               {
                  if(!this["_modPosPerkPanel0OrigX"])
                  {
                     this["_modPosPerkPanel0OrigX"] = this.PerkPanel0_mc.x;
                     this["_modPosPerkPanel0OrigY"] = this.PerkPanel0_mc.y;
                     this["_modPosPerkPanel0OrigWidth"] = this.PerkPanel0_mc.width;
                     this["_modPosPerkPanel0OrigHeight"] = this.PerkPanel0_mc.height;
                  }
                  _loc1_ = 0;
                  while(_loc1_ < _loc5_)
                  {
                     _loc2_ = getChildByName("PerkPanelClone" + _loc1_ + "_mc") as MovieClip;
                     if(!_loc2_)
                     {
                        break;
                     }
                     if(_loc1_ < _loc7_.length)
                     {
                        _loc2_.visible = true;
                        _loc4_++;
                        _loc8_ = _loc2_.Requires_tf;
                        if(this.Language == "ru" || this.Language == "pl")
                        {
                           _loc3_ = _loc2_.PerkName_tf.getTextFormat();
                           _loc3_.font = "$MAIN_Font_Bold";
                           _loc3_.size = 20;
                           _loc3_.align = "left";
                           _loc8_.setTextFormat(_loc3_);
                           TextFieldEx.setTextAutoSize(_loc8_,"shrink");
                           GlobalFunc.SetText(_loc8_,"$Req:",false);
                           GlobalFunc.SetText(_loc8_,_loc7_[_loc1_].perkRank ? _loc8_.text + " " + _loc7_[_loc1_].perkRank.toString() : "",false);
                           _loc8_.setTextFormat(_loc3_);
                           ShrinkFontToFit(_loc8_,1);
                        }
                        else
                        {
                           GlobalFunc.SetText(_loc8_,"$Req:",false);
                           GlobalFunc.SetText(_loc8_,_loc7_[_loc1_].perkRank ? _loc8_.text + " " + _loc7_[_loc1_].perkRank.toString() : "",false);
                           ShrinkFontToFit(_loc8_,1);
                        }
                        _loc2_.PerkLock_mc.visible = _loc7_[_loc1_].perkLocked;
                        _loc9_ = _loc7_[_loc1_].perkName;
                        if(_loc9_ != _loc2_.PerkName_tf.text)
                        {
                           if(this.Language == "ru" || this.Language == "pl")
                           {
                              _loc3_ = _loc2_.PerkName_tf.getTextFormat();
                              _loc3_.font = "$MAIN_Font_Bold";
                              _loc2_.PerkName_tf.setTextFormat(_loc3_);
                              GlobalFunc.SetText(_loc2_.PerkName_tf,_loc9_,false);
                              _loc2_.PerkName_tf.setTextFormat(_loc3_);
                           }
                           else
                           {
                              GlobalFunc.SetText(_loc2_.PerkName_tf,_loc9_,false);
                           }
                           _loc2_.PerkLoaderClip_mc.clipScale = 0.39;
                           _loc10_ = "";
                           _loc10_ = _loc7_[_loc1_].perkID.toString(16);
                           if(_loc7_[_loc1_].perkID > 9999999)
                           {
                              _loc10_ = this._mod.findPerkIdForMod(_loc9_,_loc10_);
                           }
                           if(_loc10_)
                           {
                              _loc2_.PerkLoaderClip_mc.SWFLoad("Components/Vaultboys/Perks/PerkClip_" + _loc10_);
                           }
                        }
                     }
                     else
                     {
                        _loc2_.visible = false;
                     }
                     _loc1_++;
                  }
               }
            }
         }
      }

      public function InventorySelectionChange(param1:Event) : *
      {
         this.AbortTextEditing();
         if(!this._isCookingMenu)
         {
            if(this.InventoryListObject.selectedIndex !== -1)
            {
               this.BGSCodeObj.SwitchBaseItem();
               if(!this._mod.wbConfig.bPerformanceMode)
               {
                  this.FillPossibleModPartArray(new Event(""));
               }
               this.UpdateButtons();
            }
         }
         this.ItemCardList_mc.showItemDesc = this.LegendaryItemDescription_tf.text == " ";
         if(this.isRepairModeActive)
         {
            this.bListNeedsUpdate = true;
            this.RequestRepairCostWithDelay(30);
            this.ClearComparisonData();
            setTimeout(this.InjectComparisonData,50);
         }
      }

      public function SetInventoryLabel(param1:String) : *
      {
         GlobalFunc.SetText(this.InventoryBracketBase_mc.Label_tf,param1,false);
         this.AdjustBracket(this.InventoryBracketBase_mc);
      }

      public function SetModSlotsLabel(param1:String) : *
      {
         GlobalFunc.SetText(this.ModSlotBracketBase_mc.Label_tf,param1,false);
         this.AdjustBracket(this.InventoryBracketBase_mc);
      }

      public function SetModsLabel(param1:String) : *
      {
         GlobalFunc.SetText(this.ModBracketBase_mc.Label_tf,param1,false);
         this.AdjustBracket(this.InventoryBracketBase_mc);
      }

      private function AdjustBracket(param1:MovieClip) : *
      {
         var _loc2_:* = param1.RightBarReference_mc.x;
         param1.ModListBracketTopRight_mc.x = param1.Label_tf.x + param1.Label_tf.textWidth + 1;
         param1.ModListBracketTopRight_mc.width = _loc2_ - param1.ModListBracketTopRight_mc.x - 2;
      }

      private function UpdateMiscItemList() : *
      {
         this.MiscItemListObject.filterer.itemFilter = 1 << this.RequirementsListObject.selectedIndex;
         this.MiscItemListObject.RefreshList();
         this.UpdateButtons();
      }

      private function SetRightListLabel(param1:String) : *
      {
         var oldDisable:Boolean = false;
         if(Mods.hasOwnProperty("disableTextColoring"))
         {
            oldDisable = Mods.disableTextColoring;
            Mods.disableTextColoring = true;
         }
         GlobalFunc.SetText(this.ModSlotBase_mc.SlotsLabel_tf,param1,false);
         if(Mods.hasOwnProperty("disableTextColoring"))
         {
            Mods.disableTextColoring = oldDisable;
         }
         if(Mods.configLoaded)
         {
            this.ModSlotBase_mc.SlotsLabel_tf.textColor = Mods.config.stdColor2;
         }
         else
         {
            this.ModSlotBase_mc.SlotsLabel_tf.textColor = 16777215;
         }
      }

      public function ModModeToSlotsMode() : *
      {
         this.BGSCodeObj.RevertChanges();
         this.bQueuedBackToMods = true;
      }

      public function ExecuteQueuedActions() : *
      {
         if(this.bQueuedBackToMods)
         {
            this.ModListObject.removeEventListener(BSScrollingList.SELECTION_CHANGE,this.ModChange);
            this.bQueuedBackToMods = false;
            this.eMode = this.SLOTS_MODE;
            this.RequirementsListObject.SetInactive();
            this.ModSlotListObject.removeEventListener(BSScrollingList.SELECTION_CHANGE,this.FillPossibleModPartArray);
            this.ModSlotListObject.SetActive(this.InventoryBase_mc.InventoryList_mc,"ModSlotListObject");
            this.ModListObject.SetActive(this.ModSlotBase_mc.ModSlotList_mc,"ModListObject");
            this.SetRightListLabel(this._isCookingMenu ? "$AVAILABLE RECIPES:" : "$AVAILABLE MODS:");
            this.ModSlotListObject.addEventListener(BSScrollingList.SELECTION_CHANGE,this.FillPossibleModPartArray,false,0,true);
            this.UpdatePerks();
            this.UpdateDescription();
            this._mod.onModeChangeModModeToSlotsMode();
         }
      }

      public function InventoryModeToSlotsMode() : *
      {
         if(this._isCookingMenu || this.ModSlotBase_mc.ModSlotList_mc.entryList.length > 0)
         {
            if(!this.mod_onlyOnce)
            {
               gotoAndPlay("ModSlots");
               this.mod_onlyOnce = true;
            }
            if(!this._isCookingMenu)
            {
               this.InventoryListObject.SetInactive();
            }
            this.ModListObject.SetActive(this.ModSlotBase_mc.ModSlotList_mc,"ModListObject");
            this.ModSlotListObject.SetActive(this.InventoryBase_mc.InventoryList_mc,"ModSlotListObject");
            this.InventoryBase_mc.InventoryList_mc.selectedIndex = -1;
            if(this.InventoryBase_mc.InventoryList_mc.mod.modIsMouseDrivenNav() && !this.InventoryBase_mc.InventoryList_mc.mod.SetFocusUnderMouse())
            {
               this.InventoryBase_mc.InventoryList_mc.moveSelectionDown();
            }
            this.SetRightListLabel(this._isCookingMenu ? "$AVAILABLE RECIPES:" : "$AVAILABLE MODS:");
            this.eMode = this.SLOTS_MODE;
            this.BGSCodeObj.PlaySound("UIMenuOK");
            this._mod.onModeChangeInventoryModeToSlotsMode();
         }
         else
         {
            this.BGSCodeObj.PlaySound("UICancel");
         }
      }

      public function CookingMode() : *
      {
         this._isCookingMenu = true;
         this.InventoryBracketBase_mc.visible = false;
         this.ModButton.ButtonText = "$Select";
         this.Build.ButtonText = this.strBuildOverrideText;
         this.AutoBuild.ButtonText = this.strBuildOverrideText;
      }

      private function GetModEquipped() : *
      {
         return this.ModListObject.entryList.length > this.ModListObject.selectedIndex && this.ModListObject.selectedIndex >= 0 && this.ModListObject.entryList[this.ModListObject.selectedIndex].equipState != undefined && this.ModListObject.entryList[this.ModListObject.selectedIndex].equipState > 0;
      }

      private function GetLooseModAvailable() : *
      {
         return this.ModListObject.entryList.length > this.ModListObject.selectedIndex && this.ModListObject.selectedIndex >= 0 && this.ModListObject.entryList[this.ModListObject.selectedIndex].hasLooseMod == true;
      }

      private function GetBuildable(param1:Boolean) : Boolean
      {
         return !this.GetModEquipped() && (this.GetLooseModAvailable() || this.BGSCodeObj.CheckRequirements(param1));
      }

      public function UpdateButtons() : *
      {
         var _loc_repair_:Vector.<BSButtonHintData>;
         var hasValidBill:Boolean;
         if(!this.isRepairModeActive && this.eMode == this.INVENTORY_MODE && !this._allowEquip)
         {
            this.RequestRepairKitButtonStateWithDelay(75);
         }
         this.BackButton.ButtonText = this.eMode == this.SLOTS_MODE && this._isCookingMenu ? "$EXIT" : "$BACK";

         // === Repair-mode button hint override (takes precedence over INVENTORY_MODE) ===
         if(this.isRepairModeActive)
         {
            if(this.RepairConfirmBtn == null)
            {
               this.RepairConfirmBtn = new BSButtonHintData("$REPAIR","E","PSN_A","Xenon_A",1,this.onRepairConfirmClick);
               this.RepairTagBtn = new BSButtonHintData("$TAG FOR SEARCH","T","PSN_Y","Xenon_Y",1,this.onRepairTagClick);
               this.RepairCancelBtn = new BSButtonHintData("$BACK","TAB","PSN_B","Xenon_B",1,this.onBackButton);
            }
            _loc_repair_ = new Vector.<BSButtonHintData>();
            hasValidBill = this.cachedRepairCost != null && this.cachedRepairCost.length > 0;
            this.ScrapButton.ButtonVisible = this._showScrapButton;
            this.ScrapButton.ButtonEnabled = true;
            if(this.ScrapButton.ButtonVisible)
            {
               _loc_repair_.push(this.ScrapButton);
            }
            this.RepairConfirmBtn.ButtonVisible = true;
            this.RepairConfirmBtn.ButtonEnabled = hasValidBill && !this.hasMissingMaterials;
            _loc_repair_.push(this.RepairConfirmBtn);
            this.RepairTagBtn.ButtonVisible = true;
            this.RepairTagBtn.ButtonEnabled = hasValidBill && this.hasMissingMaterials;
            if(this.RepairTagBtn.ButtonVisible)
            {
               _loc_repair_.push(this.RepairTagBtn);
            }
            if(this.RepairSourceBtn != null)
            {
               this.RepairSourceBtn.ButtonText = this.GetRepairKitButtonText();
               this.RepairSourceBtn.ButtonVisible = this.repairKitButtonSupported;
               this.RepairSourceBtn.ButtonEnabled = this.repairKitButtonEnabled;
            }
            this.RepairCancelBtn.ButtonVisible = true;
            _loc_repair_.push(this.RepairCancelBtn);
            if(this.ToggleRepairModeBtn != null)
            {
               this.ToggleRepairModeBtn.ButtonText = "$CSF_SwitchToModify";
               this.ToggleRepairModeBtn.ButtonVisible = !this._allowEquip && this.repairMaterialButtonSupported;
               this.ToggleRepairModeBtn.ButtonEnabled = true;
               if(this.ToggleRepairModeBtn.ButtonVisible)
               {
                  _loc_repair_.push(this.ToggleRepairModeBtn);
               }
            }
            if(this.RepairSourceBtn != null && this.RepairSourceBtn.ButtonVisible)
            {
               _loc_repair_.push(this.RepairSourceBtn);
            }
            this._mod.modSetButtonHintData(_loc_repair_);
            return;
         }

         switch(this.eMode)
         {
            case this.INVENTORY_MODE:
               if(this._allowEquip)
               {
                  this.ScrapButton.ButtonText = this.BGSCodeObj.IsSelectedItemEquipped() ? "$UNEQUIP" : "$EQUIP";
               }
               this.AlternateButton.ButtonVisible = this.strAlternateButtonText.length > 0;
               this.AlternateButton.ButtonText = this.strAlternateButtonText;
               this.AlternateButton.ButtonEnabled = this.AternateTextEnabled;
               if(this._allowRepair)
               {
                  this.RepairButton.ButtonEnabled = this.BGSCodeObj.CanRepairSelectedItem();
               }
               this.RepairButton.ButtonVisible = this._allowRepair;
               this.RenameButton.ButtonVisible = !this._allowRepair && this._allowRename;
               this.ScrapButton.ButtonVisible = this._showScrapButton;
               this.ScrapButton.ButtonEnabled = true;
               this.RenameButton.ButtonEnabled = this.BGSCodeObj.SetName !== undefined;
               if(this.ToggleRepairModeBtn != null)
               {
                  this.ToggleRepairModeBtn.ButtonText = "$CSF_SwitchToRepairMaintenance";
                  this.ToggleRepairModeBtn.ButtonVisible = !this._allowEquip && this.repairMaterialButtonSupported;
               }
               if(this.RepairSourceBtn != null)
               {
                  this.RepairSourceBtn.ButtonText = this.GetRepairKitButtonText();
                  this.RepairSourceBtn.ButtonVisible = !this._allowEquip && this.repairKitButtonSupported;
                  this.RepairSourceBtn.ButtonEnabled = this.repairKitButtonEnabled;
               }
               this._mod.modSetButtonHintData(this.GetInventoryRepairButtonHints());
               break;
            case this.SLOTS_MODE:
               this.AlternateButton.ButtonVisible = this.strAlternateButtonText.length > 0;
               this.AlternateButton.ButtonText = this.strAlternateButtonText;
               this.AlternateButton.ButtonEnabled = this.AternateTextEnabled;
               this._mod.modSetButtonHintData(this.ModSlotButtonHints);
               break;
            case this.MOD_MODE:
               this.AutoBuild.ButtonEnabled = this.GetBuildable(true);
               this.AutoBuild.ButtonVisible = true;
               this.AutoBuild.ButtonText = this.GetLooseModAvailable() ? "$ATTACH MOD" : (this._isCookingMenu ? this.strBuildOverrideText : "$BUILD");
               this.TagButton.ButtonEnabled = this.BGSCodeObj.ShouldShowTagForSearchButton();
               this.AlternateButton.ButtonVisible = this.strAlternateButtonText.length > 0;
               this.AlternateButton.ButtonText = this.strAlternateButtonText;
               this.AlternateButton.ButtonEnabled = this.AternateTextEnabled;
               this._mod.modSetButtonHintData(this.ModsListHints);
               break;
            case this.REQUIREMENTS_MODE:
               this.BGSCodeObj.SetItemSelectValuesForComponents(this.MiscItemListObject.entryList,this.MiscItemListObject.CategoryNameList);
               this.AutoBuild.ButtonEnabled = this.GetBuildable(true);
               this.AutoBuild.ButtonVisible = true;
               this.ChooseComponents.ButtonEnabled = !this.ModSlotBase_mc.ModSlotList_mc.filterer.IsFilterEmpty(this.ModSlotBase_mc.ModSlotList_mc.filterer.itemFilter);
               this._mod.modSetButtonHintData(this.ComponentsListHints);
               break;
            case this.ITEM_SELECT_MODE:
               this._mod.modSetButtonHintData(this.MiscItemListHints);
               break;
            case this.INSPECT_MODE:
               this.RenameButton.ButtonVisible = !this._allowRepair && this.allowRename;
               this.RenameButton.ButtonEnabled = this.BGSCodeObj.SetName !== undefined;
         }
         this.ModSlotBase_mc.ModSlotList_mc.ScrollUp.alpha = this.eMode == this.MOD_MODE ? 1 : 0;
         this.ModSlotBase_mc.ModSlotList_mc.ScrollDown.alpha = this.eMode == this.MOD_MODE ? 1 : 0;
         this._mod.OnUpdateButtons();
      }

      public function set eMode(param1:uint) : *
      {
         this._mod.onSetEMode(param1);
         this._eMode = param1;
         this.UpdateButtons();
      }

      public function get eMode() : *
      {
         return this._eMode;
      }

      public function ProcessUserEvent(param1:String, param2:Boolean) : Boolean
      {
         var isConfirm:Boolean;
         var isCancel:Boolean;
         var isTag:Boolean;
         var isScrap:Boolean;
         var isScroll:Boolean;
         var isRepairToggle:Boolean;
         var isRepairSourceToggle:Boolean;
         var isKitListOpenToggle:Boolean;
         var wasRepairToolKeyHandled:Boolean;
         var _loc3_:Boolean = false;
         this._mod.anyEvent(null);
         wasRepairToolKeyHandled = param1 == "RShoulder" && getTimer() - this.lastRepairToolKeyTime < 250;

         // === Repair-mode event capture: intercept before normal flow ===
         if(this.isRepairModeActive && this.eMode == this.INVENTORY_MODE)
         {
            if(wasRepairToolKeyHandled)
            {
               return true;
            }
            isConfirm = param1 == "Accept" || param1 == "Activate" || param1 == "Right" || param1 == "StrafeRight";
            isCancel = param1 == "Cancel" || param1 == "Tab" || param1 == "Left" || param1 == "StrafeLeft";
            isTag = param1 == "YButton";
            isScrap = param1 == "XButton";
            isRepairToggle = param1 == "Back" || param1 == "R3" || param1 == "Select" || param1 == "LShoulder";
            isRepairSourceToggle = param1 == "RShoulder" || param1 == "Q";
            isScroll = param1 == "Up" || param1 == "Down" || param1 == "Forward" || param1 == "Back";
            if(isRepairSourceToggle && this.RepairSourceBtn != null && this.RepairSourceBtn.ButtonVisible && this.RepairSourceBtn.ButtonEnabled)
            {
               if(!param2)
               {
                  this.onRepairSourceClick();
               }
               return true;
            }
            if(isRepairToggle)
            {
               if(!param2)
               {
                  this.ToggleRepairMode();
               }
               return true;
            }
            if(isConfirm)
            {
               if(!param2)
               {
                  this.onRepairConfirmClick();
               }
               return true;
            }
            if(isTag)
            {
               if(!param2 && this.hasMissingMaterials)
               {
                  this.onRepairTagClick();
               }
               return true;
            }
            if(isScrap)
            {
               if(!param2 && this.ScrapButton.ButtonVisible && this.ScrapButton.ButtonEnabled)
               {
                  this.onScrapBuildAdd();
               }
               return true;
            }
            if(isCancel)
            {
               if(!param2)
               {
                  this.onBackButton();
               }
               return true;
            }
            if(isScroll)
            {
               return false;
            }
            return true;
         }

         if(wasRepairToolKeyHandled && this.eMode == this.INVENTORY_MODE && !this._allowEquip)
         {
            return true;
         }
         isKitListOpenToggle = (param1 == "RShoulder" || param1 == "Q") && this.eMode == this.INVENTORY_MODE && !this._allowEquip;
         if(isKitListOpenToggle)
         {
            if(!param2)
            {
               this.onRepairSourceClick();
            }
            return true;
         }

         if(!this.bEnteringText)
         {
            if(!Mods.disableFocusCapture && !TextInputHelper.instance.active)
            {
               if(this.eMode == this.ITEM_SELECT_MODE)
               {
                  stage.focus = this.ModSlotBase_mc.ModSlotList_mc;
               }
               else if(stage.focus != this.ModSlotBase_mc.ModSlotList_mc)
               {
                  stage.focus = this.InventoryBase_mc.InventoryList_mc;
               }
            }
            if(param2 == false)
            {
               switch(param1)
               {
                  case "Activate":
                  case "Accept":
                     if(this._featuredItemMode)
                     {
                        this.onTakeButton();
                     }
                     break;
                  case "Left":
                  case "StrafeLeft":
                     if(this.eMode == this.INSPECT_MODE || this.eMode == this.INVENTORY_MODE || this.eMode == this.SLOTS_MODE && this._isCookingMenu)
                     {
                        break;
                     }
                  case "Cancel":
                     if(Mods.disableCancelTime + 200 < new Date().getTime())
                     {
                        this.onBackButton();
                     }
                     _loc3_ = true;
                     break;
                  case "StrafeRight":
                  case "Right":
                     this.onModButton();
                     _loc3_ = true;
                     break;
                  case "XButton":
                     if(Mods.isVR || this.ScrapButton.ButtonVisible && this.ScrapButton.ButtonEnabled)
                     {
                        this.onScrapBuildAdd();
                     }
                     _loc3_ = true;
                     break;
                  case "YButton":
                     if(!this.inspectMode)
                     {
                        this.onRenameRepairSearch();
                     }
                     else if(this.eMode === this.INSPECT_MODE && param1 === "YButton" && this._allowRename)
                     {
                        this.onRenameRepairSearch();
                     }
                     _loc3_ = true;
                     break;
                  case "Back":
                     if(this.eMode == this.INSPECT_MODE && !this._singleItemInspectMode)
                     {
                        this.InventoryBase_mc.InventoryList_mc.moveSelectionDown();
                        if(!this._itemNameManagedByCode)
                        {
                           this.itemName = this.InventoryBase_mc.InventoryList_mc.entryList[this.InventoryBase_mc.InventoryList_mc.selectedIndex].text;
                        }
                        _loc3_ = true;
                     }
                     break;
                  case "LShoulder":
                     if(this.eMode == this.INVENTORY_MODE && !this._allowEquip)
                     {
                        this.ToggleRepairMode();
                        _loc3_ = true;
                        break;
                     }
                  case "Forward":
                     if(this.eMode == this.INSPECT_MODE && !this._singleItemInspectMode)
                     {
                        this.InventoryBase_mc.InventoryList_mc.moveSelectionUp();
                        if(!this._itemNameManagedByCode)
                        {
                           this.itemName = this.InventoryBase_mc.InventoryList_mc.entryList[this.InventoryBase_mc.InventoryList_mc.selectedIndex].text;
                        }
                        _loc3_ = true;
                     }
               }
               this.UpdateDescription();
               this.UpdatePerks();
            }
            return _loc3_;
         }
         if(param2 == false && param1 == "Cancel")
         {
            this.AbortTextEditing();
            _loc3_ = true;
         }
         return true;
      }

      private function onMouseWheel(param1:MouseEvent) : *
      {
         if(this.eMode == this.INSPECT_MODE)
         {
            if(param1.delta < 0)
            {
               this.BGSCodeObj.ZoomOut();
            }
            else if(param1.delta > 0)
            {
               this.BGSCodeObj.ZoomIn();
            }
         }

         if(this._mod.onProcessUserEvent(param1,param2))
         {
            return true;
         }
      }

      private function onNextButton() : void
      {
         this.InventoryBase_mc.InventoryList_mc.moveSelectionDown();
         if(!this._itemNameManagedByCode)
         {
            this.itemName = this.InventoryBase_mc.InventoryList_mc.entryList[this.InventoryBase_mc.InventoryList_mc.selectedIndex].text;
         }
      }

      private function onPreviousButton() : void
      {
         this.InventoryBase_mc.InventoryList_mc.moveSelectionUp();
         if(!this._itemNameManagedByCode)
         {
            this.itemName = this.InventoryBase_mc.InventoryList_mc.entryList[this.InventoryBase_mc.InventoryList_mc.selectedIndex].text;
         }
      }

      private function onZoomInButton() : void
      {
         this.BGSCodeObj.ZoomIn();
      }

      private function onZoomOutButton() : void
      {
         this.BGSCodeObj.ZoomOut();
      }

      private function onRenameRepairSearch() : void
      {
         var _loc1_:Boolean = false;
         if(this.eMode == this.MOD_MODE)
         {
            if(this.BGSCodeObj.ToggleFavoriteMod())
            {
               this.BGSCodeObj.PlaySound("UIMenuPrevNext");
               _loc1_ = Boolean(this.ModListObject.entryList[this.ModListObject.selectedIndex].modTaggedForSearch);
               this.ModListObject.entryList[this.ModListObject.selectedIndex].modTaggedForSearch = _loc1_ ? false : true;
               this.ModListObject.RefreshList();
               this.UpdateButtons();
            }
         }
         else if(this._allowRepair)
         {
            if(this.eMode == this.INVENTORY_MODE)
            {
               this._mod.scanOnNextListUpdate = true;
               this.BGSCodeObj.RepairSelectedItem();
            }
         }
         else if(this.allowRename && !this.enteringText)
         {
            this.enteringText = !this.enteringText;
         }
      }

      private function onScrapBuildAdd() : void
      {
         if(this.strAlternateButtonText)
         {
            this.onAlternateButton();
         }
         else
         {
            switch(this.eMode)
            {
               case this.INVENTORY_MODE:
                  if(this._allowEquip)
                  {
                     this._mod.scanOnNextListUpdate = true;
                     this.BGSCodeObj.ToggleItemEquipped();
                     break;
                  }
                  this.InventoryListObject.modIgnoreSelectIndexUpdates = new Date();
                  this.BGSCodeObj.ScrapItem();
                  this._mod.scanOnNextListUpdate = true;
                  break;
               case this.MOD_MODE:
               case this.REQUIREMENTS_MODE:
                  if(this.eMode != this.MOD_MODE && !this.GetModEquipped())
                  {
                     this.BGSCodeObj.OnBuildFailed();
                     break;
                  }
                  this.BGSCodeObj.PlaySound("UIMenuCancel");
                  break;
               case this.ITEM_SELECT_MODE:
                  if(this.MiscItemListObject.selectedIndex >= 0)
                  {
                     this.BGSCodeObj.ItemSelect(this.MiscItemListObject.entryList,int(this.MiscItemListObject.selectedIndex),this.MiscItemListObject.CategoryNameList,int(Math.log(this.MiscItemListObject.filterer.itemFilter) / Math.log(2)),this.MiscItemListObject.filterer.itemFilter);
                     this.RequirementsListObject.RefreshList();
                  }
            }
         }
      }

      private function onAlternateButton() : void
      {
         this.BGSCodeObj.OnAlternateButton();
      }

      private function onModButton() : void
      {
         var _loc2_:Boolean = false;
         var _loc1_:uint = this._eMode;
         switch(this.eMode)
         {
            case this.INVENTORY_MODE:
               if(!this.enteringText)
               {
                  this.InventoryModeToSlotsMode();
                  break;
               }
               this.enteringText = false;
               break;
            case this.SLOTS_MODE:
               this.ModSlotListObject.SetInactive();
               this.eMode = this.MOD_MODE;
               this.BGSCodeObj.SendTutorialEvent(this.eMode);
               this.RequirementsListObject.SetActive(this.ModSlotBase_mc.ModSlotList_mc,"RequirementsListObject");
               this.ModListObject.SetActive(this.InventoryBase_mc.InventoryList_mc,"ModListObject");
               this.SetRightListLabel("$REQUIRES:");
               this.ModListObject.addEventListener(BSScrollingList.SELECTION_CHANGE,this.ModChange,false,0,true);
               this.InventoryBase_mc.InventoryList_mc.selectedIndex = -1;
               if(this.InventoryBase_mc.InventoryList_mc.mod.modIsMouseDrivenNav() && !this.InventoryBase_mc.InventoryList_mc.mod.SetFocusUnderMouse())
               {
                  this.InventoryBase_mc.InventoryList_mc.moveSelectionDown();
                  this.ModListObject.selectedIndex = this.InventoryBase_mc.InventoryList_mc.selectedIndex;
               }
               this.BGSCodeObj.PlaySound("UIMenuOK");
               if(this._isCookingMenu)
               {
                  this.onModChange();
               }
               this._mod.onModeChangeSlotsModeToModMode();
               break;
            case this.MOD_MODE:
               if(!this.GetModEquipped())
               {
                  _loc2_ = false;
                  _loc2_ = this.RequirementsListObject.entryList.length == 1 && this.RequirementsListObject.entryList[0].text === Translation.translate("$None");
                  if(this.ModListObject.entryList[this.ModListObject.selectedIndex].hasLooseMod || _loc2_ && ExamineMenuMod.examineMenuMod.wbConfig.bAutoCraftZeroCostRecipes)
                  {
                     this.BGSCodeObj.ConfirmBuild();
                     this.BGSCodeObj.PlaySound("UIMenuOK");
                     this._mod.onModModeUpdate();
                     break;
                  }
                  this.BGSCodeObj.StartBuildConfirm();
                  this._mod.scanOnNextListUpdate = true;
               }
               break;
            case this.REQUIREMENTS_MODE:
               if(this.ChooseComponents.ButtonEnabled)
               {
                  stage.focus = this.ModSlotBase_mc.ModSlotList_mc;
                  this.ModSlotBase_mc.ModSlotList_mc.selectedIndex = this.ModSlotBase_mc.ModSlotList_mc.filterer.ClampIndex(-1);
                  this.ModSlotBase_mc.ModSlotList_mc.InvalidateData();
                  this.eMode = this.ITEM_SELECT_MODE;
                  this.BGSCodeObj.SetItemSelectValuesForComponents(this.MiscItemListObject.entryList,this.MiscItemListObject.CategoryNameList);
                  this.BGSCodeObj.PlaySound("UIMenuOK");
                  break;
               }
               this.BGSCodeObj.PlaySound("UIMenuCancel");
         }
         this.UpdateDescription();
         this.UpdatePerks();
         this.BGSCodeObj.SendTutorialEvent(this.eMode);
         if(_loc1_ != this._eMode)
         {
            this._mod.scanInventory();
            this.InventoryListObject.RefreshList();
         }
      }

      private function onTakeButton() : void
      {
         this.BGSCodeObj.HideMenu();
      }

      private function onBackButton() : void
      {
         if(this.isRepairModeActive)
         {
            this.isRepairModeActive = false;
            this.ItemName_tf.visible = true;
            this.ModDescriptionBase_mc.visible = true;
            this.ClearComparisonData();
            this.removeEventListener(Event.ENTER_FRAME,this.LockRepairList);
            this.HideRepairCost();
            this.BGSCodeObj.HideMenu();
            this._mod.actionUserAskToClose();
            return;
         }
         switch(this.eMode)
         {
            case this.INVENTORY_MODE:
            case this.INSPECT_MODE:
               if(!this._featuredItemMode)
               {
                  this.BGSCodeObj.HideMenu();
                  this._mod.actionUserAskToClose();
               }
               break;
            case this.SLOTS_MODE:
               if(!this._isCookingMenu)
               {
                  this.ModListObject.SetInactive();
                  this.ModSlotListObject.SetActive(this.ModSlotBase_mc.ModSlotList_mc,"ModSlotListObject");
                  this.InventoryListObject.SetActive(this.InventoryBase_mc.InventoryList_mc,"InventoryListObject");
                  this.InventoryListObject.RefreshList();
                  this.ModSlotListObject.RefreshList();
                  this.ModListObject.removeEventListener(BSScrollingList.SELECTION_CHANGE,this.ModChange);
                  this.SetRightListLabel("$CURRENT MODS:");
                  this.eMode = this.INVENTORY_MODE;
                  this.BGSCodeObj.PlaySound("UIMenuCancel");
                  this.BGSCodeObj.RemoveHighlight();
                  this._mod.onModeChangeSlotsModeToInventoryMode();
                  break;
               }
               this.BGSCodeObj.HideMenu();
               this._mod.actionUserAskToClose();
               break;
            case this.MOD_MODE:
               this.ModModeToSlotsMode();
               this.BGSCodeObj.PlaySound("UIMenuCancel");
               break;
            case this.REQUIREMENTS_MODE:
               this.MiscItemListObject.SetInactive();
               this.ModListObject.SetActive(this.InventoryBase_mc.InventoryList_mc,"ModListObject");
               this.ModListObject.RefreshList();
               this.RequirementsListObject.SetActive(this.ModSlotBase_mc.ModSlotList_mc,"RequirementsListObject");
               this.RequirementsListObject.RefreshList();
               this.SetRightListLabel("$REQUIRES:");
               this.ModListObject.addEventListener(BSScrollingList.SELECTION_CHANGE,this.ModChange,false,0,true);
               this.eMode = this.MOD_MODE;
               this.BGSCodeObj.PlaySound("UIMenuCancel");
               break;
            case this.ITEM_SELECT_MODE:
               stage.focus = this.InventoryBase_mc.InventoryList_mc;
               this.RequirementsListObject.selectedIndex = -1;
               if(this.InventoryBase_mc.InventoryList_mc.mod.modIsMouseDrivenNav() && !this.InventoryBase_mc.InventoryList_mc.mod.SetFocusUnderMouse())
               {
                  this.InventoryBase_mc.InventoryList_mc.moveSelectionDown();
               }
               this.MiscItemListObject.selectedIndex = -1;
               this.SetRightListLabel("$ITEMS:");
               this.eMode = this.REQUIREMENTS_MODE;
               this.BGSCodeObj.PlaySound("UIMenuCancel");
         }
      }

      public function onRightThumbstickInput(param1:uint) : *
      {
         if(param1 == 1)
         {
            --this.ModSlotBase_mc.ModSlotList_mc.scrollPosition;
         }
         else if(param1 == 3)
         {
            ++this.ModSlotBase_mc.ModSlotList_mc.scrollPosition;
         }
      }

      public function get autoBuild() : *
      {
         return this.eMode == this.MOD_MODE || this.eMode == this.INVENTORY_MODE;
      }

      public function AbortTextEditing() : *
      {
         if(this.bEnteringText)
         {
            if(!Mods.isVR)
            {
               this.modAbortTextEditNoSave = true;
            }
            this.enteringText = false;
            this._mod.showTagSelection(false);
         }
      }

      public function onVirtualKeyboardResult(param1:String) : *
      {
         if(param1 != this.ItemName_tf.text)
         {
            this.ItemName_tf.text = param1;
         }
         this.ItemName_tf.type = TextFieldType.DYNAMIC;
         this.ItemName_tf.setSelection(0,0);
         this.ItemName_tf.maxChars = 0;
         this.ItemName_tf.selectable = false;
         setTimeout(this.setNameLate,30,param1);
         stage.focus = null;
      }

      public function CancelVirtualKeyboardNameEdit(param1:String) : *
      {
         var executeDelayed:* = undefined;
         var aOldText:String = param1;
         this.bEnteringText = false;
         executeDelayed = function delayedFunc(param1:Event):void
         {
            stage.focus = LegendaryItemDescription_tf;
            ItemName_tf.text = aOldText;
            ItemName_tf.type = TextFieldType.DYNAMIC;
            ItemName_tf.setSelection(0,0);
            ItemName_tf.selectable = false;
            removeEventListener(Event.ENTER_FRAME,executeDelayed);
            if(eMode == ITEM_SELECT_MODE)
            {
               stage.focus = ModSlotBase_mc.ModSlotList_mc;
            }
            else if(stage.focus != ModSlotBase_mc.ModSlotList_mc)
            {
               stage.focus = InventoryBase_mc.InventoryList_mc;
            }
         };
         this.addEventListener(Event.ENTER_FRAME,executeDelayed);
      }

      public function set enteringText(param1:Boolean) : *
      {
         var _loc4_:Array = null;
         var _loc2_:TextField = this.ItemName_tf;
         var _loc3_:String = "";
         this.bEnteringText = param1;
         if(param1)
         {
            _loc2_.type = TextFieldType.INPUT;
            this.strStartName = _loc2_.text;
            this.BGSCodeObj.SetName();
            _loc2_.selectable = true;
            _loc2_.maxChars = 150;
            stage.focus = _loc2_;
            _loc4_ = null;
            _loc4_ = Mods.config.tagSearchRegExp.exec(_loc2_.text);
            if(_loc4_)
            {
               _loc2_.setSelection(_loc4_[0].length,_loc2_.text.length);
            }
            else
            {
               _loc2_.setSelection(0,_loc2_.text.length);
            }
            this._mod.showTagSelection(true);
         }
         else
         {
            this._mod.showTagSelection(false);
            this._mod.disableItemNameUpdate = true;
            if(this.strStartName != _loc2_.text && !this.modAbortTextEditNoSave)
            {
               _loc3_ = _loc2_.text;
               this.BGSCodeObj.SetName(_loc2_.text);
            }
            else
            {
               this.BGSCodeObj.SetName();
               this.modAbortTextEditNoSave = false;
               _loc3_ = this.strStartName;
            }
            _loc2_.text = _loc3_;
            setTimeout(this.setNameLate,30,_loc3_);
            _loc2_.type = TextFieldType.DYNAMIC;
            _loc2_.setSelection(0,0);
            _loc2_.selectable = false;
            stage.focus = null;
            if(this.eMode == this.ITEM_SELECT_MODE)
            {
               stage.focus = this.ModSlotBase_mc.ModSlotList_mc;
            }
            else if(stage.focus != this.ModSlotBase_mc.ModSlotList_mc)
            {
               stage.focus = this.InventoryBase_mc.InventoryList_mc;
            }
            setTimeout(this._mod.updateItemNameTextForce,50);
         }
      }

      private function setNameLate(param1:String) : void
      {
         this._mod.disableItemNameUpdate = false;
         this.ItemName_tf.text = param1;
         this._mod.updateItemNameTextForce();
      }

      public function get enteringText() : *
      {
         return this.bEnteringText;
      }

      public function GetCurrentModSelected() : Boolean
      {
         return this.ModListObject.entryList.length > 0 ? this.ModListObject.entryList[this.ModListObject.selectedIndex].currentMod == true : false;
      }

      public function GetCurrentModAttachable() : Boolean
      {
         return this.ModListObject.entryList.length > this.ModListObject.selectedIndex && this.ModListObject.selectedIndex >= 0 ? this.ModListObject.entryList[this.ModListObject.selectedIndex].hasLooseMod == true : false;
      }

      public function UpdateDescription() : *
      {
         var _loc1_:String = "";
         var _loc3_:TextField = this.ModSlotBase_mc.SlotsLabel_tf;
         var _loc4_:RightHandList = this.ModSlotBase_mc.ModSlotList_mc;
         if(this.isRepairModeActive)
         {
            _loc1_ = "";
         }
         else if(this.eMode == this.MOD_MODE || this.eMode == this.REQUIREMENTS_MODE)
         {
            if(this._isCookingMenu)
            {
               if(this.ModListObject.selectedIndex < this.ModListObject.entryList.length && this.ModListObject.entryList[this.ModListObject.selectedIndex] && this.ModListObject.entryList[this.ModListObject.selectedIndex].description != undefined)
               {
                  _loc1_ = this.ModListObject.entryList[this.ModListObject.selectedIndex].description;
               }
            }
            else if(this.InventoryBase_mc.InventoryList_mc.selectedIndex < this.InventoryBase_mc.InventoryList_mc.entryList.length && this.InventoryBase_mc.InventoryList_mc.entryList[this.InventoryBase_mc.InventoryList_mc.selectedIndex] && this.InventoryBase_mc.InventoryList_mc.entryList[this.InventoryBase_mc.InventoryList_mc.selectedIndex].description != undefined)
            {
               _loc1_ = this.InventoryBase_mc.InventoryList_mc.entryList[this.InventoryBase_mc.InventoryList_mc.selectedIndex].description;
            }
         }
         GlobalFunc.SetText(this.ModDescriptionBase_mc.ModDescription_tf,_loc1_,false);
         ShrinkFontToFit(this.ModDescriptionBase_mc.ModDescription_tf,1);
         var _loc2_:* = _loc1_ == "" ? 0 : this.ModDescriptionBase_mc.ModDescription_tf.numLines;
         switch(_loc2_)
         {
            case 0:
               _loc3_.y = -5;
               _loc4_.y = 17;
               break;
            case 1:
               _loc3_.y = 16;
               _loc4_.y = 38;
               break;
            default:
               _loc3_.y = 37;
               _loc4_.y = 59;
         }
      }

      public function startItemSelection() : *
      {
         this.BGSCodeObj.StartItemSelection();
      }

      public function RefreshItemCard(param1:Object = null) : *
      {
         if(!this._mod.onRefreshItemCard(param1))
         {
            this.ItemCardList_mc.onDataChange();
         }
      }

      public function set legendaryItemDescription(param1:String) : *
      {
         var _loc2_:String = param1;
         while(_loc2_.indexOf("%%") != -1)
         {
            _loc2_ = _loc2_.replace("%%","%");
         }
         while(_loc2_.indexOf("\r\n") != -1)
         {
            _loc2_ = _loc2_.replace("\r\n","\n");
         }
         GlobalFunc.SetText(this.LegendaryItemDescription_tf,_loc2_,false);
      }

      public function RepositionCurrentModsList() : *
      {
         var _loc1_:Number = 0;
         var _loc2_:int = 0;
         var _loc3_:BSScrollingListEntry = null;
         if(this.CurrentModsListObject.entryList.length)
         {
            this.CurrentModsBase_mc.visible = true;
            this.CurrentModsBase_mc.x = 1280 - this.CurrentModsBase_mc.width - (this.ItemCardList_mc.x - this.ItemCardList_mc.width);
            _loc1_ = 0;
            _loc2_ = 0;
            while(_loc2_ < this.CurrentModsListObject.entryList.length)
            {
               _loc3_ = this.CurrentModsBase_mc.ModSlotList_mc.GetClipByIndex(_loc2_);
               if(_loc3_)
               {
                  if(_loc3_.y + _loc3_.height > _loc1_)
                  {
                     _loc1_ = _loc3_.y + _loc3_.height;
                  }
               }
               _loc2_++;
            }
            this.CurrentModsBase_mc.y = this.CurrentModsBase_mc.y + this.ItemCardList_mc.y - _loc1_ - this.CurrentModsBase_mc.ModSlotList_mc.y - this.CurrentModsBase_mc.y;
         }
         else
         {
            this.CurrentModsBase_mc.visible = false;
         }
      }
   }
}
