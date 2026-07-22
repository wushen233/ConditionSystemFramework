package
{
   import M8r.Helper.ColorsRec;
   import M8r.Helper.FontLoader;
   import M8r.Helper.Tools;
   import M8r.Helper.Translation;
   import M8r.Mods;
   import PipboyMenu_fla.BottomBar_Info_3;
   import Shared.AS3.BSButtonHintData;
   import Shared.AS3.BSUIComponent;
   import Shared.BGSExternalInterface;
   import Shared.GlobalFunc;
   import flash.display.DisplayObject;
   import flash.display.DisplayObjectContainer;
   import flash.display.Loader;
   import flash.display.MovieClip;
   import flash.events.Event;
   import flash.events.KeyboardEvent;
   import flash.geom.ColorTransform;
   import flash.net.URLRequest;
   import flash.text.TextField;
   import scaleform.gfx.Extensions;
   import scaleform.gfx.TextFieldEx;

   public class Pipboy_BottomBar extends BSUIComponent
   {

      private const MAX_DISPLAY_CAPS:int = 1000000;

      private const MAX_DISPLAY_CAPS_TEXT:String = "1000000+";

      public var Info_mc:BottomBar_Info_3;

      protected var _DataObj:Pipboy_DataObj;

      public var curListWeight:int = -1;

      private var modLastWeightStr:String = "";

      // === ConditionSystemFramework repair-mode state ===
      public var modIsRepairMode:Boolean = false;

      public var RepairButton:BSButtonHintData;

      public var juryLoader:Loader = null;

      public var hasRepairInjected:Boolean = false;

      public var btnExecute:BSButtonHintData;

      public var btnToggleKits:BSButtonHintData;

      public var btnReturn:BSButtonHintData;

      public var repairBtnsVec:Vector.<BSButtonHintData> = null;

      private var openedOnTab:int = -1;

      public var pendingModalHideFrames:int = 0;

      public var savedNativeIndex:int = -1;

      public var savedFormID:Number = 0;

      public var savedItemText:String = "";

      public var savedItemEquipState:int = 0;

      public var savedStackID:Number = 0;

      public var savedStackIsIndex:Boolean = false;

      public var savedIsRepairable:Boolean = false;

      public var savedRepairSupported:Boolean = false;

      public var repairCheckSerial:int = 0;

      public var pendingRepairCheckSerial:int = 0;

      public var needsRefreshMacro:Boolean = false;

      public var equippedMacroText:String = "";

      public var pendingReselectFrames:int = 0;

      private var _lastSeenFormID:Number = 0;

      private var _lastSeenStackID:Number = -1;

      private var _lastSeenStackIsIndex:Boolean = false;

      private var _stableFrames:int = 0;

      public function Pipboy_BottomBar()
      {
         super();
         Extensions.enabled = true;
         this.curListWeight = -1;
      }

      private function SetNativeItemCardVisible(page:MovieClip, isVisible:Boolean) : void
      {
         var card:Object = null;
         if(!page)
         {
            return;
         }
         if(page.hasOwnProperty("ItemCard_mc"))
         {
            card = page["ItemCard_mc"];
         }
         else if(page.hasOwnProperty("ItemCardList_mc"))
         {
            card = page["ItemCardList_mc"];
         }
         if(card)
         {
            card.visible = isVisible;
            if(card.hasOwnProperty("mouseEnabled"))
            {
               card.mouseEnabled = isVisible;
            }
            if(card.hasOwnProperty("mouseChildren"))
            {
               card.mouseChildren = isVisible;
            }
         }
      }

      private function SetRepairModePaperDollVisible(page:MovieClip, isVisible:Boolean) : void
      {
         var paperDoll:Object = null;
         if(!page || this.openedOnTab != 1)
         {
            return;
         }
         if(page.hasOwnProperty("PaperDoll_mc"))
         {
            paperDoll = page["PaperDoll_mc"];
         }
         if(paperDoll)
         {
            paperDoll.visible = isVisible;
            if(paperDoll.hasOwnProperty("mouseEnabled"))
            {
               paperDoll.mouseEnabled = isVisible;
            }
            if(paperDoll.hasOwnProperty("mouseChildren"))
            {
               paperDoll.mouseChildren = isVisible;
            }
         }
      }

      override public function onAddedToStage() : void
      {
         var menuRoot:MovieClip;
         var self:Pipboy_BottomBar;
         super.onAddedToStage();
         if(this.stage)
         {
            PipboyChangeEvent.Register(this.stage,this.onPipboyChangeEvent);
            this.stage.addEventListener(KeyboardEvent.KEY_DOWN,this.onSystemKeyDown,true,9999);
            this.stage.addEventListener(KeyboardEvent.KEY_UP,this.onSystemKeyUp,true,9999);
         }
         menuRoot = this.parent as MovieClip;
         if(menuRoot)
         {
            Object(menuRoot.root).ToggleRepairLayout_Call = this.ToggleRepairLayout;
            self = this;
            Object(menuRoot.root).SetRepairButtonEnabled_Call = function(isSupported:Boolean, needsRepair:Boolean = false, requestId:Number = -1):void
            {
               if(requestId >= 0 && int(requestId) != self.pendingRepairCheckSerial)
               {
                  return;
               }
               self.savedRepairSupported = isSupported;
               self.savedIsRepairable = needsRepair;
               if(self.RepairButton)
               {
                  self.RepairButton.ButtonVisible = isSupported;
                  self.RepairButton.ButtonDisabled = !needsRepair;
                  if(menuRoot.updateHintButtons != null)
                  {
                     menuRoot.updateHintButtons();
                  }
               }
            };
         }
         this.addEventListener(Event.ENTER_FRAME,this.RepairLogicLoop);
      }

      override public function onRemovedFromStage() : void
      {
         if(this.stage)
         {
            PipboyChangeEvent.Unregister(this.stage,this.onPipboyChangeEvent);
            this.stage.removeEventListener(KeyboardEvent.KEY_DOWN,this.onSystemKeyDown,true);
            this.stage.removeEventListener(KeyboardEvent.KEY_UP,this.onSystemKeyUp,true);
         }
         super.onRemovedFromStage();
      }

      private function SafeSetRepairButtons(menuRoot:MovieClip) : void
      {
         if(this.modIsRepairMode && menuRoot && menuRoot.ButtonHintBar_mc && this.repairBtnsVec)
         {
            menuRoot.ButtonHintBar_mc.SetButtonHintData(this.repairBtnsVec.slice());
         }
      }

      public function UpdateRepairKitsState(isKitMode:Boolean, totalKits:int) : void
      {
         var newText:String;
         var bDisabled:Boolean;
         var showKitButton:Boolean;
         var self:Pipboy_BottomBar;
         var menuRoot:MovieClip;
         if(this.repairBtnsVec)
         {
            newText = "";
            bDisabled = false;
            showKitButton = isKitMode || totalKits > 0;
            if(isKitMode)
            {
               newText = "$CSF_EmergencyRepair";
               bDisabled = false;
            }
            else if(totalKits <= 0)
            {
               newText = "$CSF_RepairTools";
               bDisabled = true;
            }
            else
            {
               newText = Translation.translate("$CSF_RepairTools") + " (" + totalKits + ")";
               bDisabled = false;
            }
            self = this;
            this.btnToggleKits = new BSButtonHintData(newText,"R","PSN_X","Xenon_X",1,function():void
            {
               if(self.juryLoader && self.juryLoader.content)
               {
                  self.juryLoader.content.ProcessUserEvent("XButton",false);
               }
            });
            this.btnToggleKits.ButtonVisible = showKitButton;
            this.btnToggleKits.ButtonDisabled = bDisabled;
            this.repairBtnsVec = new Vector.<BSButtonHintData>();
            this.repairBtnsVec.push(this.btnExecute);
            if(showKitButton)
            {
               this.repairBtnsVec.push(this.btnToggleKits);
            }
            this.repairBtnsVec.push(this.btnReturn);
            menuRoot = this.parent as MovieClip;
            this.SafeSetRepairButtons(menuRoot);
         }
      }

      private function onSystemKeyDown(e:KeyboardEvent) : void
      {
         if(this.modIsRepairMode)
         {
            e.stopImmediatePropagation();
            if(this.juryLoader && this.juryLoader.content)
            {
               if(e.keyCode == 38 || e.keyCode == 87)
               {
                  this.juryLoader.content.ProcessUserEvent("Up",false);
               }
               else if(e.keyCode == 40 || e.keyCode == 83)
               {
                  this.juryLoader.content.ProcessUserEvent("Down",false);
               }
               else if(e.keyCode == 13)
               {
                  this.juryLoader.content.ProcessUserEvent("Accept",false);
               }
               else if(e.keyCode == 82 && this.btnToggleKits && this.btnToggleKits.ButtonVisible && !this.btnToggleKits.ButtonDisabled)
               {
                  this.juryLoader.content.ProcessUserEvent("XButton",false);
               }
            }
         }
      }

      private function onSystemKeyUp(e:KeyboardEvent) : void
      {
         var mr:MovieClip = null;
         var pg:MovieClip = null;
         var instantFormID:Number = NaN;
         var instantText:String = null;
         var instantEq:int = 0;
         var realStackID:Number = NaN;
         var realStackIsIndex:Boolean = false;
         var curEntry:Object = null;
         if(this.modIsRepairMode)
         {
            e.stopImmediatePropagation();
            if(e.keyCode == 9 || e.keyCode == 27)
            {
               this.CloseMenuSafely();
            }
         }
         else if(e.keyCode == 90 && this.RepairButton && this.RepairButton.ButtonVisible)
         {
            e.stopImmediatePropagation();
            mr = this.parent as MovieClip;
            pg = mr ? mr.CurrentPage as MovieClip : null;
            if(this.RepairButton.ButtonDisabled)
            {
               if(pg && pg.hasOwnProperty("codeObj") && pg["codeObj"])
               {
                  BGSExternalInterface.call(pg["codeObj"],"PlaySound","UIMenuCancel");
               }
               else if(mr && mr.BGSCodeObj)
               {
                  BGSExternalInterface.call(mr.BGSCodeObj,"PlaySound","UIMenuCancel");
               }
               return;
            }
            instantFormID = this.savedFormID;
            instantText = this.savedItemText;
            instantEq = this.savedItemEquipState;
            realStackID = this.savedStackID;
            realStackIsIndex = this.savedStackIsIndex;
            if(pg && pg.List_mc && pg.List_mc.selectedEntry != null)
            {
               curEntry = pg.List_mc.selectedEntry;
               if(curEntry.hasOwnProperty("formID"))
               {
                  instantFormID = Number(curEntry.formID);
               }
               else if(curEntry.hasOwnProperty("id"))
               {
                  instantFormID = Number(curEntry.id);
               }
               if(curEntry.text != null)
               {
                  instantText = curEntry.text;
               }
               instantEq = curEntry.equipState > 0 ? 1 : 0;
               if(curEntry.hasOwnProperty("stackID"))
               {
                  realStackIsIndex = false;
                  if(curEntry.stackID is Array && curEntry.stackID.length > 0)
                  {
                     realStackID = Number(curEntry.stackID[0]);
                  }
                  else
                  {
                     realStackID = Number(curEntry.stackID);
                  }
               }
               else if(curEntry.hasOwnProperty("stackIndex"))
               {
                  realStackIsIndex = true;
                  if(curEntry.stackIndex is Array && curEntry.stackIndex.length > 0)
                  {
                     realStackID = Number(curEntry.stackIndex[0]);
                  }
                  else
                  {
                     realStackID = Number(curEntry.stackIndex);
                  }
               }
            }
            if(pg && pg.hasOwnProperty("codeObj") && pg["codeObj"])
            {
               BGSExternalInterface.call(pg["codeObj"],"PlaySound","UIPipBoyOKPress");
            }
            else if(mr && mr.BGSCodeObj)
            {
               BGSExternalInterface.call(mr.BGSCodeObj,"PlaySound","UIPipBoyOKPress");
            }
            if(mr && mr.root && Object(mr.root).ConditionSystem_Call != null)
            {
               Object(mr.root).ConditionSystem_Call("OpenRepairMenuFromMouse",instantFormID,instantText,instantEq,realStackID,realStackIsIndex ? 1 : 0);
            }
         }
      }

      public function ToggleRepairLayout(bEnable:Boolean) : void
      {
         var menuRoot:MovieClip;
         var page:MovieClip;
         var self:Pipboy_BottomBar;
         var isPopup:Boolean;
         var existSwf:Object;
         if(this.modIsRepairMode == bEnable)
         {
            return;
         }
         menuRoot = this.parent as MovieClip;
         page = menuRoot.CurrentPage as MovieClip;
         self = this;
         if(!page)
         {
            return;
         }
         if(bEnable)
         {
            if(!this._DataObj || this._DataObj.CurrentPage != 1 || this._DataObj.CurrentTab != 0 && this._DataObj.CurrentTab != 1)
            {
               return;
            }
            isPopup = false;
            if(page.hasOwnProperty("QuickkeyPopup_mc") && page["QuickkeyPopup_mc"] && page["QuickkeyPopup_mc"].visible)
            {
               isPopup = true;
            }
            if(page.hasOwnProperty("ModalFadeRect_mc") && page.ModalFadeRect_mc.visible && page.ModalFadeRect_mc.alpha > 0.1)
            {
               isPopup = true;
            }
            if(page.hasOwnProperty("_ReadOnlyMode") && page["_ReadOnlyMode"])
            {
               isPopup = true;
            }
            if(isPopup)
            {
               return;
            }
            this.modIsRepairMode = true;
            this.openedOnTab = this._DataObj.CurrentTab;
            this.pendingModalHideFrames = 0;
            if(menuRoot && menuRoot.BGSCodeObj)
            {
               BGSExternalInterface.call(menuRoot.BGSCodeObj,"PlaySound","UIMenuOK");
            }
            if(!this.repairBtnsVec)
            {
               this.btnExecute = new BSButtonHintData("$Repair","Enter","PSN_A","Xenon_A",1,function():void
               {
                  if(self.juryLoader && self.juryLoader.content)
                  {
                     self.juryLoader.content.ProcessUserEvent("Accept",false);
                  }
                  if(menuRoot.BGSCodeObj)
                  {
                     BGSExternalInterface.call(menuRoot.BGSCodeObj,"PlaySound","UIMenuOK");
                  }
               });
               this.btnReturn = new BSButtonHintData("$Back","Tab","PSN_B","Xenon_B",1,function():void
               {
                  self.CloseMenuSafely();
               });
               this.btnToggleKits = new BSButtonHintData("$CSF_RepairTools","R","PSN_X","Xenon_X",1,null);
               this.btnToggleKits.ButtonVisible = true;
               this.btnToggleKits.ButtonDisabled = true;
               this.repairBtnsVec = new Vector.<BSButtonHintData>();
               this.repairBtnsVec.push(this.btnExecute);
               this.repairBtnsVec.push(this.btnReturn);
            }
            if(menuRoot.BGSCodeObj)
            {
               BGSExternalInterface.call(menuRoot.BGSCodeObj,"onModalOpen",true);
               BGSExternalInterface.call(menuRoot.BGSCodeObj,"toggleMovementToDirectional",true);
            }
            Object(menuRoot.root).PlaySound_Call = function(snd:String):void
            {
               if(page && page.hasOwnProperty("codeObj") && page["codeObj"])
               {
                  BGSExternalInterface.call(page["codeObj"],"PlaySound",snd);
               }
               else if(menuRoot.BGSCodeObj)
               {
                  BGSExternalInterface.call(menuRoot.BGSCodeObj,"PlaySound",snd);
               }
            };
            Object(menuRoot.root).ExecuteJuryRigging_Call = function(tUID:Number, mUID:Number):void
            {
               if(Object(menuRoot.root).ConditionSystem_Call != null)
               {
                  Object(menuRoot.root).ConditionSystem_Call("ExecuteJuryRigging",tUID,mUID);
               }
            };
            if(page.List_mc)
            {
               page.List_mc.alpha = 0;
               page.List_mc.mouseEnabled = false;
               page.List_mc.mouseChildren = false;
            }
            this.SetNativeItemCardVisible(page,false);
            this.SetRepairModePaperDollVisible(page,false);
            this.Info_mc.visible = true;
            if(menuRoot.Header_mc)
            {
               menuRoot.Header_mc.alpha = 0;
               menuRoot.Header_mc.mouseChildren = false;
            }
            if(page.ModalFadeRect_mc)
            {
               page.ModalFadeRect_mc.visible = true;
               page.ModalFadeRect_mc.alpha = 0.01;
            }
            this.SafeSetRepairButtons(menuRoot);
            if(!this.juryLoader)
            {
               this.juryLoader = new Loader();
               page.addChild(this.juryLoader);
               this.juryLoader.contentLoaderInfo.addEventListener(Event.COMPLETE,function(e:Event):void
               {
                  var swf:Object = e.target.content;
                  swf["JuryRigging_Callback"] = function(cmd:String, target:Number = 0, mat:Number = 0):void
                  {
                     if(cmd == "ExecuteJuryRigging" && Object(menuRoot.root).ConditionSystem_Call != null)
                     {
                        Object(menuRoot.root).ConditionSystem_Call("ExecuteJuryRigging",target,mat);
                     }
                  };
                  swf["UpdateRepairKitsState_Callback"] = function(isKitMode:Boolean, totalKits:int):void
                  {
                     self.UpdateRepairKitsState(isKitMode,totalKits);
                  };
                  Object(menuRoot.root).ReceiveJuryRiggingData = function(str:String, isR:Boolean, col:Number):void
                  {
                     if(swf.ReceiveJuryRiggingData != null)
                     {
                        swf.ReceiveJuryRiggingData(str,isR,col);
                     }
                     else if(swf.HandleReceiveData != null)
                     {
                        swf.HandleReceiveData(str,isR,col);
                     }
                  };
                  if(Object(menuRoot.root).ConditionSystem_Call != null)
                  {
                     Object(menuRoot.root).ConditionSystem_Call("RequestData",0);
                  }
               });
               this.juryLoader.load(new URLRequest("JuryRiggingMenu.swf"));
            }
            else
            {
               if(this.juryLoader.parent != page)
               {
                  page.addChild(this.juryLoader);
               }
               page.setChildIndex(this.juryLoader,page.numChildren - 1);
               this.juryLoader.visible = true;
               existSwf = this.juryLoader.content;
               if(existSwf)
               {
                  existSwf["UpdateRepairKitsState_Callback"] = function(isKitMode:Boolean, totalKits:int):void
                  {
                     self.UpdateRepairKitsState(isKitMode,totalKits);
                  };
               }
               if(Object(menuRoot.root).ConditionSystem_Call != null)
               {
                  Object(menuRoot.root).ConditionSystem_Call("RequestData",0);
               }
            }
            if(this.juryLoader && this.juryLoader.parent == page)
            {
               page.setChildIndex(this.juryLoader,page.numChildren - 1);
            }
         }
         else
         {
            this.CloseMenuSafely();
         }
      }

      public function OnL3Press() : void
      {
         if(this.RepairButton && this.RepairButton.ButtonVisible && !this.RepairButton.ButtonDisabled)
         {
            var mr:MovieClip = this.parent as MovieClip;
            if(mr && mr.root && Object(mr.root).ConditionSystem_Call != null)
            {
               Object(mr.root).ConditionSystem_Call("OpenRepairMenuFromMouse",this.savedFormID,this.savedItemText,this.savedItemEquipState,this.savedStackID,this.savedStackIsIndex ? 1 : 0);
            }
         }
      }

      public function CloseMenuSafely() : void
      {
         if(!this.modIsRepairMode)
         {
            return;
         }
         this.modIsRepairMode = false;
         var menuRoot:MovieClip = this.parent as MovieClip;
         var page:MovieClip = menuRoot.CurrentPage as MovieClip;
         if(menuRoot && menuRoot.BGSCodeObj)
         {
            BGSExternalInterface.call(menuRoot.BGSCodeObj,"PlaySound","UIMenuCancel");
         }
         if(this.juryLoader)
         {
            this.juryLoader.visible = false;
         }
         if(page && page.List_mc)
         {
            page.List_mc.alpha = 1;
            page.List_mc.mouseEnabled = true;
            page.List_mc.mouseChildren = true;
         }
         this.SetNativeItemCardVisible(page,true);
         this.SetRepairModePaperDollVisible(page,true);
         this.openedOnTab = -1;
         if(menuRoot.Header_mc)
         {
            menuRoot.Header_mc.alpha = 1;
            menuRoot.Header_mc.mouseChildren = true;
         }
         this.Info_mc.visible = true;
         if(menuRoot.BGSCodeObj)
         {
            BGSExternalInterface.call(menuRoot.BGSCodeObj,"onModalOpen",false);
            BGSExternalInterface.call(menuRoot.BGSCodeObj,"toggleMovementToDirectional",false);
         }
         if(menuRoot.updateHintButtons != null)
         {
            menuRoot.updateHintButtons();
         }
         this.pendingModalHideFrames = 2;
         if(menuRoot && menuRoot.root && Object(menuRoot.root).ConditionSystem_Call != null)
         {
            Object(menuRoot.root).ConditionSystem_Call("CloseRepairMenu");
         }
         if(this.needsRefreshMacro)
         {
            this.pendingReselectFrames = 16;
            this.needsRefreshMacro = false;
         }
         else
         {
            this.pendingReselectFrames = 0;
         }
      }

      private function CheckIfHasCND(container:DisplayObjectContainer) : Boolean
      {
         var cndBg:DisplayObject;
         var i:int;
         var child:DisplayObjectContainer;
         if(!container)
         {
            return false;
         }
         try
         {
            cndBg = container.getChildByName("cndBarBG");
            if(cndBg != null && cndBg.visible == true)
            {
               return true;
            }
            i = 0;
            while(i < container.numChildren)
            {
               child = container.getChildAt(i) as DisplayObjectContainer;
               if(child != null)
               {
                  if(this.CheckIfHasCND(child))
                  {
                     return true;
                  }
               }
               i++;
            }
         }
         catch(e:Error)
         {
         }
         return false;
      }

      private function RepairLogicLoop(e:Event) : void
      {
         var menuRoot:MovieClip;
         var page:MovieClip;
         var self:Pipboy_BottomBar;
         var dObj:*;
         var shouldShow:Boolean;
         var isPopup:Boolean;
         var listHasValidTarget:Boolean;
         var entry:Object;
         var tempFormID:Number;
         var tempStackID:Number;
         var tempEquipState:Number;
         var tempStackIsIndex:Boolean;
         var canQueryRepair:Boolean;
         try
         {
            menuRoot = this.parent as MovieClip;
            if(!menuRoot)
            {
               return;
            }
            page = menuRoot.CurrentPage as MovieClip;
            dObj = this["_DataObj"];
            if(!dObj)
            {
               dObj = this.DataObj;
            }
            listHasValidTarget = false;
            if(!this.modIsRepairMode && page && page.List_mc)
            {
               entry = page.List_mc.selectedEntry;
               if(entry != null && entry.text != null && entry.text != "")
               {
                  tempFormID = 0;
                  if(entry.hasOwnProperty("formID"))
                  {
                     tempFormID = Number(entry.formID);
                  }
                  else if(entry.hasOwnProperty("id"))
                  {
                     tempFormID = Number(entry.id);
                  }
                  tempStackID = 0;
                  tempEquipState = entry.equipState > 0 ? 1 : 0;
                  tempStackIsIndex = false;
                  if(entry.hasOwnProperty("stackID"))
                  {
                     tempStackIsIndex = false;
                     if(entry.stackID is Array && entry.stackID.length > 0)
                     {
                        tempStackID = Number(entry.stackID[0]);
                     }
                     else
                     {
                        tempStackID = Number(entry.stackID);
                     }
                  }
                  else if(entry.hasOwnProperty("stackIndex"))
                  {
                     tempStackIsIndex = true;
                     if(entry.stackIndex is Array && entry.stackIndex.length > 0)
                     {
                        tempStackID = Number(entry.stackIndex[0]);
                     }
                     else
                     {
                        tempStackID = Number(entry.stackIndex);
                     }
                  }
                  if(tempFormID != this._lastSeenFormID || tempStackID != this._lastSeenStackID || tempStackIsIndex != this._lastSeenStackIsIndex)
                  {
                     this._stableFrames = 0;
                     this._lastSeenFormID = tempFormID;
                     this._lastSeenStackID = tempStackID;
                     this._lastSeenStackIsIndex = tempStackIsIndex;
                  }
                  else
                  {
                     ++this._stableFrames;
                  }
                  if(this._stableFrames >= 3)
                  {
                     this.savedItemText = entry.text;
                     this.savedItemEquipState = entry.equipState > 0 ? 1 : 0;
                     this.savedFormID = tempFormID;
                     if(entry.hasOwnProperty("stackID"))
                     {
                        this.savedStackIsIndex = false;
                        if(entry.stackID is Array && entry.stackID.length > 0)
                        {
                           this.savedStackID = entry.stackID[0];
                        }
                        else
                        {
                           this.savedStackID = entry.stackID;
                        }
                     }
                     else if(entry.hasOwnProperty("stackIndex"))
                     {
                        this.savedStackIsIndex = true;
                        if(entry.stackIndex is Array && entry.stackIndex.length > 0)
                        {
                           this.savedStackID = entry.stackIndex[0];
                        }
                        else
                        {
                           this.savedStackID = entry.stackIndex;
                        }
                     }
                     else
                     {
                        this.savedStackID = 0;
                        this.savedStackIsIndex = false;
                     }
                  }
                  listHasValidTarget = true;
               }
               else if(this.savedFormID != 0)
               {
                  listHasValidTarget = true;
               }
            }
            if(this.pendingModalHideFrames > 0)
            {
               --this.pendingModalHideFrames;
               if(this.pendingModalHideFrames <= 0 && page && page.ModalFadeRect_mc)
               {
                  page.ModalFadeRect_mc.visible = false;
                  page.ModalFadeRect_mc.alpha = 1;
               }
            }
            if(this.pendingReselectFrames > 0 && !this.modIsRepairMode)
            {
               --this.pendingReselectFrames;
            }
            if(this.modIsRepairMode)
            {
               if(page && this.juryLoader && this.juryLoader.parent == page)
               {
                  page.setChildIndex(this.juryLoader,page.numChildren - 1);
               }
               if(page && page.ModalFadeRect_mc)
               {
                  page.ModalFadeRect_mc.visible = true;
                  page.ModalFadeRect_mc.alpha = 0.01;
               }
               if(page && page.List_mc)
               {
                  page.List_mc.alpha = 0;
                  page.List_mc.mouseEnabled = false;
                  page.List_mc.mouseChildren = false;
               }
               this.SetNativeItemCardVisible(page,false);
               this.SetRepairModePaperDollVisible(page,false);
               if(menuRoot.Header_mc)
               {
                  menuRoot.Header_mc.alpha = 0;
                  menuRoot.Header_mc.mouseChildren = false;
               }
              this.Info_mc.visible = true;
               this.SafeSetRepairButtons(menuRoot);
               return;
            }
            if(!this.hasRepairInjected)
            {
               this.hasRepairInjected = true;
               self = this;
               menuRoot.root.OnL3Press_Call = this.OnL3Press;
               menuRoot.root.OnControllerXButton_Call = function():void
               {
                  if(self.modIsRepairMode && self.juryLoader && self.juryLoader.content)
                  {
                     self.juryLoader.content.ProcessUserEvent("XButton", false);
                  }
               };
               this.RepairButton = new BSButtonHintData("$Repair","Z","PSN_L3","Xenon_L3",1,function():void
               {
                  if(self.RepairButton.ButtonDisabled)
                  {
                     return;
                  }
                  var mr:MovieClip = self.parent as MovieClip;
                  var pg:MovieClip = mr ? mr.CurrentPage as MovieClip : null;
                  if(pg && pg.hasOwnProperty("codeObj") && pg["codeObj"])
                  {
                     BGSExternalInterface.call(pg["codeObj"],"PlaySound","UIPipBoyOKPress");
                  }
                  else if(mr && mr.BGSCodeObj)
                  {
                     BGSExternalInterface.call(mr.BGSCodeObj,"PlaySound","UIPipBoyOKPress");
                  }
                  if(mr && mr.root && Object(mr.root).ConditionSystem_Call != null)
                  {
                     Object(mr.root).ConditionSystem_Call("OpenRepairMenuFromMouse",self.savedFormID,self.savedItemText,self.savedItemEquipState,self.savedStackID,self.savedStackIsIndex ? 1 : 0);
                  }
               });
               this.RepairButton.ButtonVisible = false;
               if(menuRoot.ButtonHintBar_mc && !menuRoot.ButtonHintBar_mc.hasOwnProperty("modOrigSetButtonData"))
               {
                  menuRoot.ButtonHintBar_mc["modOrigSetButtonData"] = menuRoot.ButtonHintBar_mc.SetButtonHintData;
                  menuRoot.ButtonHintBar_mc.SetButtonHintData = function(vec:Vector.<BSButtonHintData>):void
                  {
                     if(self.modIsRepairMode)
                     {
                        this.modOrigSetButtonData(self.repairBtnsVec.slice());
                        return;
                     }
                     if(self.RepairButton && self.RepairButton.ButtonVisible)
                     {
                        var newVec:Vector.<BSButtonHintData> = vec.slice();
                        if(newVec.indexOf(self.RepairButton) == -1)
                        {
                           newVec.push(self.RepairButton);
                        }
                        this.modOrigSetButtonData(newVec);
                     }
                     else
                     {
                        this.modOrigSetButtonData(vec);
                     }
                  };
               }
            }
            shouldShow = false;
            if(dObj && dObj.CurrentPage == 1 && (dObj.CurrentTab == 0 || dObj.CurrentTab == 1))
            {
               if(listHasValidTarget)
               {
                  isPopup = false;
                  if(page.hasOwnProperty("QuickkeyPopup_mc") && page["QuickkeyPopup_mc"] && page["QuickkeyPopup_mc"].visible)
                  {
                     isPopup = true;
                  }
                  if(page.hasOwnProperty("ModalFadeRect_mc") && page.ModalFadeRect_mc.visible && page.ModalFadeRect_mc.alpha > 0.1)
                  {
                     isPopup = true;
                  }
                  if(page.hasOwnProperty("_ReadOnlyMode") && page["_ReadOnlyMode"])
                  {
                     isPopup = true;
                  }
                  if(page.hasOwnProperty("_ShowingFavorites") && page["_ShowingFavorites"])
                  {
                     isPopup = true;
                  }
                  if(!isPopup)
                  {
                     shouldShow = true;
                  }
               }
            }
            shouldShow = shouldShow && this.savedRepairSupported;
            if(this.RepairButton.ButtonVisible != shouldShow || this.RepairButton.ButtonDisabled != !this.savedIsRepairable)
            {
               this.RepairButton.ButtonVisible = shouldShow;
               this.RepairButton.ButtonDisabled = !this.savedIsRepairable;
               if(menuRoot.updateHintButtons != null)
               {
                  menuRoot.updateHintButtons();
               }
            }
         }
         catch(err:Error)
         {
         }
      }

      private function onPipboyChangeEvent(param1:PipboyChangeEvent) : void
      {
         this._DataObj = param1.DataObj;
         if(this.modIsRepairMode)
         {
            if(this._DataObj && (this._DataObj.CurrentPage != 1 || this._DataObj.CurrentTab != this.openedOnTab))
            {
               this.CloseMenuSafely();
            }
         }
         this.SetIsDirty();
      }

      override public function redrawUIComponent() : void
      {
         var _loc2_:DisplayObject = null;
         var _loc6_:TextField = null;
         var _loc7_:Boolean = false;
         var _loc8_:int = 0;
         var _loc9_:int = 0;
         var _loc10_:Number = NaN;
         var _loc11_:Number = NaN;
         var _loc12_:ColorTransform = null;
         var _loc13_:int = 0;
         var _loc1_:MovieClip = this.Info_mc;
         var _loc3_:Object = Mods.config;
         var _loc4_:Object = _loc3_.Settings;
         super.redrawUIComponent();
         var _loc5_:Pipboy_DataObj = this._DataObj;
         if(_loc5_)
         {
            _loc7_ = Mods.disableTextColoring;
            Mods.disableTextColoring = false;
            _loc8_ = int(_loc5_.TimeHour);
            _loc9_ = int((_loc5_.TimeHour - _loc8_) * 60);
            if(_loc3_.Pipboy.bShowTimeGame)
            {
               if(_loc3_.Pipboy.bShowTimeLocal)
               {
                  GlobalFunc.SetText(_loc1_.t1,Tools.formatTime(_loc4_.bTimeSet12Format,_loc8_,_loc9_),false);
               }
               else
               {
                  GlobalFunc.SetText(_loc1_.t1,Tools.formatDate(_loc4_.iDateFormat,_loc5_.DateDay,_loc5_.DateMonth,_loc5_.DateYear + 2000),false);
                  GlobalFunc.SetText(_loc1_.t2,Tools.formatTime(_loc4_.bTimeSet12Format,_loc8_,_loc9_),false);
               }
            }
            switch(_loc5_.CurrentPage)
            {
               case 0:
                  _loc1_.gotoAndStop("StatsPage");
                  _loc6_ = _loc1_.HP_tf;
                  TextFieldEx.setTextAutoSize(_loc6_,TextFieldEx.TEXTAUTOSZ_SHRINK);
                  GlobalFunc.SetText(_loc6_,"$HP",false);
                  GlobalFunc.SetText(_loc6_,_loc6_.text + "  " + Math.max(0,int(_loc5_.CurrHP)) + "/" + int(_loc5_.MaxHP),false);
                  _loc6_ = _loc1_.LVL_tf;
                  TextFieldEx.setTextAutoSize(_loc6_,TextFieldEx.TEXTAUTOSZ_SHRINK);
                  GlobalFunc.SetText(_loc6_,"$LEVEL",false);
                  GlobalFunc.SetText(_loc6_,_loc6_.text + " " + _loc5_.XPLevel,false);
                  _loc1_.XPMeter_mc.SetMeter(_loc5_.XPProgressPct * 100,0,100);
                  _loc6_ = _loc1_.AP_tf;
                  TextFieldEx.setTextAutoSize(_loc6_,TextFieldEx.TEXTAUTOSZ_SHRINK);
                  GlobalFunc.SetText(_loc6_,"$AP",false);
                  if(_loc5_.MaxAP <= 0)
                  {
                     GlobalFunc.SetText(_loc6_,_loc6_.text + "  --/--",false);
                     break;
                  }
                  GlobalFunc.SetText(_loc6_,_loc6_.text + "  " + int(_loc5_.CurrAP) + "/" + int(_loc5_.MaxAP),false);
                  break;
               case 1:
                  switch(_loc5_.CurrentTab)
                  {
                     case 0:
                        _loc1_.gotoAndStop("InvPage_Weapons");
                        _loc1_.DMGDRWidget_mc.redraw(true,_loc5_.TotalDamages);
                        break;
                     case 1:
                        _loc1_.gotoAndStop("InvPage_Apparel");
                        _loc1_.DMGDRWidget_mc.redraw(false,_loc5_.TotalResists);
                        break;
                     case 2:
                        _loc1_.gotoAndStop("InvPage_Aid");
                        _loc10_ = _loc5_.CurrHP;
                        _loc11_ = _loc5_.MaxHP;
                        if(_loc5_.CurrentHPGain > 0)
                        {
                           _loc10_ += _loc11_ * _loc5_.CurrentHPGain;
                        }
                        if(_loc5_.SelectedItemHPGain > 0)
                        {
                           _loc10_ += _loc11_ * _loc5_.SelectedItemHPGain;
                        }
                        if(_loc5_.CurrentHPGain == 0 && _loc5_.SelectedItemHPGain == 0)
                        {
                           _loc10_ = 0;
                        }
                        _loc1_.HPMeter.SetMeter(_loc5_.CurrHP,_loc10_ < _loc11_ ? _loc10_ : _loc11_,_loc11_);
                        break;
                     default:
                        _loc1_.gotoAndStop("InvPage_Misc");
                  }
                  TextFieldEx.setTextAutoSize(_loc1_.Weight_tf,TextFieldEx.TEXTAUTOSZ_SHRINK);
                  TextFieldEx.setTextAutoSize(_loc1_.Caps_tf,TextFieldEx.TEXTAUTOSZ_SHRINK);
                  this.modLastWeightStr = int(_loc5_.CurrWeight) + "/" + int(_loc5_.MaxWeight);
                  GlobalFunc.SetText(_loc1_.Weight_tf,this.modLastWeightStr,false);
                  this.modUpdateWeightInfo();
                  TextFieldEx.setTextAutoSize(_loc1_.Weight_tf,"shrink");
                  if(_loc5_.Caps >= this.MAX_DISPLAY_CAPS)
                  {
                     GlobalFunc.SetText(_loc1_.Caps_tf,this.MAX_DISPLAY_CAPS_TEXT,false);
                     break;
                  }
                  GlobalFunc.SetText(_loc1_.Caps_tf,_loc5_.Caps.toString(),false);
                  break;
               case 2:
               case 3:
                  _loc1_.gotoAndStop("DataPage");
                  GlobalFunc.SetText(_loc1_.Date_tf,Tools.formatDate(_loc4_.iDateFormat,_loc5_.DateDay,_loc5_.DateMonth,_loc5_.DateYear + 2000),false);
                  GlobalFunc.SetText(_loc1_.Time_tf,Tools.formatTime(_loc4_.bTimeSet12Format,_loc8_,_loc9_),false);
                  if(_loc5_.CurrentPage == 3)
                  {
                     GlobalFunc.SetText(_loc1_.Location_tf,_loc5_.CurrLocationName,false);
                     break;
                  }
                  GlobalFunc.SetText(_loc1_.Location_tf," ",false);
                  break;
               default:
                  _loc1_.gotoAndStop("None");
            }
            if(Mods.configLoaded)
            {
               if(_loc3_.stdColor !== 16777215)
               {
                  if(_loc1_ != null)
                  {
                     _loc12_ = Mods.stdColorTransformFromWhite;
                     _loc13_ = _loc1_.numChildren;
                     while(_loc13_--)
                     {
                        _loc2_ = _loc1_.getChildAt(_loc13_);
                        if(Math.abs(_loc2_.width / _loc2_.height - 1) < 0.2)
                        {
                           _loc2_.transform.colorTransform = _loc12_;
                        }
                     }
                     _loc2_ = _loc1_.getChildByName("XPMeter_mc");
                     if(_loc2_)
                     {
                        _loc2_.transform.colorTransform = _loc12_;
                     }
                     _loc2_ = _loc1_.getChildByName("HPMeter");
                     if(_loc2_)
                     {
                        _loc2_.transform.colorTransform = _loc12_;
                        _loc13_ = _loc1_.numChildren;
                        while(_loc13_--)
                        {
                           _loc6_ = _loc1_.getChildAt(_loc13_) as TextField;
                           if(_loc6_)
                           {
                              if(_loc6_.text === Translation.translate("$HP"))
                              {
                                 _loc6_.textColor = _loc3_.stdColor;
                              }
                           }
                        }
                     }
                     _loc2_ = _loc1_.getChildByName("DMGDRWidget_mc");
                     if(_loc2_)
                     {
                        ColorsRec.decolorizeTextFields(_loc2_ as DisplayObjectContainer);
                        _loc2_.transform.colorTransform = _loc12_;
                     }
                  }
               }
            }
            Mods.disableTextColoring = _loc7_;
         }
      }

      public function modUpdateWeightInfo() : void
      {
         var _loc2_:Number = NaN;
         var _loc1_:TextField = this.Info_mc.Weight_tf;
         if(!_loc1_)
         {
            return;
         }
         if(this.curListWeight > -1)
         {
            _loc1_.text = "";
            _loc2_ = FontLoader.instance.textsize;
            TextFieldEx.appendHtml(_loc1_,"<font size=\"" + int(_loc2_ * 0.8 + 0.5) + "\">" + this.curListWeight + "/</font>" + "<font size=\"" + int(_loc2_ + 0.5) + "\">" + this.modLastWeightStr + "</font>");
            TextFieldEx.setTextAutoSize(_loc1_,"shrink");
         }
      }
   }
}
