package
{
   import HUDMenu_fla.ListHeaderAndBracket_50;
   import M8r.Components.BackgroundSimple;
   import M8r.Components.ButtonHintBarSynth;
   import M8r.Mod.QuickContainerWidgetMod;
   import M8r.Model.FallUIHUDConfig;
   import M8r.Mods;
   import M8r.Service.TagConfiguration;
   import Shared.AS3.BSButtonHintBar;
   import Shared.AS3.BSButtonHintData;
   import Shared.AS3.BSUIComponent;
   import Shared.GlobalFunc;
   import flash.display.MovieClip;
   import flash.geom.ColorTransform;
   import flash.text.TextField;
   import flash.utils.setTimeout;
   import scaleform.gfx.Extensions;
   
   [Embed(source="/_assets/assets.swf", symbol="symbol560")]
   public dynamic class QuickContainerWidget extends BSUIComponent
   {
      
      private static var cuiNumClips:int = 5;
      
      public var ListHeaderAndBracket_mc:ListHeaderAndBracket_50;
      
      public var ListItems_mc:MovieClip;
      
      public var ButtonHintBar_mc:BSButtonHintBar;
      
      public var ItemDataA:Vector.<QuickContainerItemData>;
      
      public var cachedCndData:Array;
      
      private var _selectedIndex:int = 0;
      
      private var _bracketsVisible:Boolean;
      
      public var AButton:BSButtonHintData = new BSButtonHintData("$TAKE","E","PSN_A","Xenon_A",1,null);
      
      public var XButton:BSButtonHintData = new BSButtonHintData("$QuickContainerTransfer","R","PSN_X","Xenon_X",1,null);
      
      public var YButton:BSButtonHintData = new BSButtonHintData("Special Action","$SPACEBAR","PSN_Y","Xenon_Y",1,null);
      
      private var ItemClipsA:Vector.<QuickContainerItem>;
      
      private var PositionForListSize:Vector.<int>;
      
      private var _mod:QuickContainerWidgetMod = new QuickContainerWidgetMod(this);
      
      private var _origHeight:Number = 0;
      
      public var _modIsStealMode:Boolean = false;
      
      private var _cndRefreshPending:Boolean = false;
      
      public function QuickContainerWidget()
      {
         super();
         this.ButtonHintBar_mc = new ButtonHintBarSynth(this.ButtonHintBar_mc);
         Extensions.enabled = true;
         this._selectedIndex = -1;
         this.PopulateButtonBar();
         this.ItemDataA = new Vector.<QuickContainerItemData>();
         this.ItemClipsA = new Vector.<QuickContainerItem>(QuickContainerWidget.cuiNumClips,true);
         this.PositionForListSize = new Vector.<int>(QuickContainerWidget.cuiNumClips + 1,true);
         var _loc1_:TextField = this.ListHeaderAndBracket_mc.ContainerName_mc.textField_tf as TextField;
         _loc1_.multiline = false;
         _loc1_.wordWrap = false;
         this.scanItems();
         this.PositionForListSize[0] = this.PositionForListSize[1];
         this.visible = false;
         this.alpha = 0;
         this.ButtonHintBar_mc.BackgroundAlpha = 1;
         this.ButtonHintBar_mc.BackgroundColor = 0;
         this.ButtonHintBar_mc.bracketCornerLength = 6;
         this.ButtonHintBar_mc.bracketLineWidth = 1.5;
         this.ButtonHintBar_mc.BracketStyle = "horizontal";
         this.ButtonHintBar_mc.bRedirectToButtonBarMenu = false;
         this.ButtonHintBar_mc.bShowBrackets = false;
         this.ButtonHintBar_mc.bUseShadedBackground = false;
         this.ButtonHintBar_mc.ShadedBackgroundMethod = "Shader";
         this.ButtonHintBar_mc.ShadedBackgroundType = "normal";
         this._mod.init(this.cuiNumClips);
         this._origHeight = this.height;
      }
      
      private function scanItems() : void
      {
         var _loc1_:QuickContainerItem = null;
         var _loc2_:int = 0;
         var _loc3_:int = int((this.ListItems_mc.getChildByName("ItemText0") as QuickContainerItem).y);
         var _loc4_:int = (this.ListItems_mc.getChildByName("ItemText1") as QuickContainerItem).y - (this.ListItems_mc.getChildByName("ItemText0") as QuickContainerItem).y;
         while(_loc2_ < QuickContainerWidget.cuiNumClips)
         {
            _loc1_ = this.ListItems_mc.getChildByName("ItemText" + _loc2_) as QuickContainerItem;
            this.ItemClipsA[_loc2_] = _loc1_;
            this.PositionForListSize[QuickContainerWidget.cuiNumClips - _loc2_] = _loc1_.y;
            _loc2_++;
         }
      }
      
      public function get numClips() : uint
      {
         return QuickContainerWidget.cuiNumClips;
      }
      
      protected function PopulateButtonBar() : void
      {
         var _loc1_:Vector.<BSButtonHintData> = new Vector.<BSButtonHintData>();
         _loc1_.push(this.AButton);
         _loc1_.push(this.XButton);
         _loc1_.push(this.YButton);
         this.XButton.ButtonVisible = false;
         this.AButton.ButtonVisible = false;
         this.YButton.ButtonVisible = false;
         this.ButtonHintBar_mc.SetButtonHintData(_loc1_);
      }
      
      public function UpdateList(param1:int) : void
      {
         this.ListItems_mc.transform.colorTransform = null;
         this.ListHeaderAndBracket_mc.transform.colorTransform = new ColorTransform(1,1,0.999999,1);
         this._modIsStealMode = this.AButton.ButtonText === "$STEAL";
         this._mod.updateColors();
         var _loc2_:QuickContainerItem = null;
         if(param1 !== -2)
         {
            this._selectedIndex = param1;
         }
         this.applyAutoTags();
         this.applyCachedCND();
         var _loc3_:int = 0;
         while(_loc3_ < QuickContainerWidget.cuiNumClips)
         {
            _loc2_ = this.ItemClipsA[_loc3_];
            if(_loc3_ < this.ItemDataA.length)
            {
               _loc2_.data = this.ItemDataA[_loc3_];
               _loc2_.selected = this._selectedIndex == _loc3_;
            }
            else
            {
               _loc2_.data = null;
            }
            _loc3_++;
         }
      }
      
      private function applyAutoTags() : void
      {
         if(Mods.configLoaded)
         {
            if(Mods.config.Settings.bEnableAutoTagger)
            {
               if(FallUIHUDConfig.instance.bAllowAutoItemTagsQuickloot)
               {
                  TagConfiguration.instance.autoTagItems(this.ItemDataA);
               }
            }
         }
      }
      
      private function applyCachedCND() : Boolean
      {
         var changed:Boolean = false;
         if(!this.ItemDataA)
         {
            return false;
         }
         if(!this.cachedCndData || this.cachedCndData.length == 0)
         {
            var clearIndex:int = 0;
            while(clearIndex < this.ItemDataA.length)
            {
               if(this.clearCNDFields(this.ItemDataA[clearIndex]))
               {
                  changed = true;
               }
               clearIndex++;
            }
            return changed;
         }
         var visiblePAByCount:Object = this.buildVisiblePAByCount();
         var matchedIndices:Object = {};
         var i:int = 0;
         while(i < this.ItemDataA.length)
         {
            var item:Object = this.ItemDataA[i];
            var matchIndex:int = this.findCNDMatch(item,matchedIndices,visiblePAByCount);
            if(matchIndex >= 0)
            {
               matchedIndices[matchIndex] = true;
               if(this.applyCNDFields(item,this.cachedCndData[matchIndex]))
               {
                  changed = true;
               }
            }
            else if(this.clearCNDFields(item))
            {
               changed = true;
            }
            i++;
         }
         return changed;
      }
      
      private function findCNDMatch(item:Object, matchedIndices:Object, visiblePAByCount:Object) : int
      {
         var matchIndex:int = -1;
         var formID:Number = this.getFormID(item);
         if(formID > 0)
         {
            matchIndex = this.findUniqueCandidate(item,matchedIndices,"form",formID,visiblePAByCount);
            if(matchIndex >= 0)
            {
               return matchIndex;
            }
         }
         matchIndex = this.findUniqueCandidate(item,matchedIndices,"exactName",-1,visiblePAByCount);
         if(matchIndex >= 0)
         {
            return matchIndex;
         }
         matchIndex = this.findUniqueCandidate(item,matchedIndices,"containsName",-1,visiblePAByCount);
         if(matchIndex >= 0)
         {
            return matchIndex;
         }
         return this.findUniqueCandidate(item,matchedIndices,"singlePA",-1,visiblePAByCount);
      }
      
      private function findUniqueCandidate(item:Object, matchedIndices:Object, mode:String, formID:Number, visiblePAByCount:Object) : int
      {
         var foundIndex:int = -1;
         var foundCount:int = 0;
         var itemName:String = this.normalizeCNDName(item.text);
         var itemCount:Number = Number(item.count);
         var j:int = 0;
         while(j < this.cachedCndData.length)
         {
            if(!matchedIndices[j])
            {
               var cndData:Object = this.cachedCndData[j];
               if(this.candidateMatches(itemName,itemCount,mode,formID,cndData,visiblePAByCount))
               {
                  foundIndex = j;
                  foundCount++;
                  if(foundCount > 1)
                  {
                     return -1;
                  }
               }
            }
            j++;
         }
         return foundCount == 1 ? foundIndex : -1;
      }
      
      private function candidateMatches(itemName:String, itemCount:Number, mode:String, formID:Number, cndData:Object, visiblePAByCount:Object) : Boolean
      {
         if(!cndData || Number(cndData.count) != itemCount)
         {
            return false;
         }
         if(mode == "form")
         {
            return cndData.hasOwnProperty("formID") && Number(cndData.formID) == formID;
         }
         var cndName:String = this.normalizeCNDName(cndData.name);
         if(mode == "exactName")
         {
            return itemName.length > 0 && cndName.length > 0 && itemName == cndName;
         }
         if(mode == "containsName")
         {
            return itemName.length >= 3 && cndName.length >= 3 && (itemName.indexOf(cndName) != -1 || cndName.indexOf(itemName) != -1);
         }
         if(mode == "singlePA")
         {
            if(!cndData.isPA || !this.isPowerArmorText(itemName))
            {
               return false;
            }
            return visiblePAByCount[String(itemCount)] == 1;
         }
         return false;
      }
      
      private function applyCNDFields(item:Object, cndData:Object) : Boolean
      {
         var changed:Boolean = false;
         var foundCnd:Number = Number(cndData.cnd);
         if(isNaN(foundCnd))
         {
            return this.clearCNDFields(item);
         }
         var isPA:Boolean = cndData.hasOwnProperty("isPA") ? Boolean(cndData.isPA) : false;
         var thresholdPct:Number = cndData.hasOwnProperty("thresholdPct") ? Number(cndData.thresholdPct) : -1;
         var thresholdPct2:Number = cndData.hasOwnProperty("thresholdPct2") ? Number(cndData.thresholdPct2) : -1;
         if(!item.hasOwnProperty("cnd") || item.cnd != foundCnd)
         {
            item.cnd = foundCnd;
            changed = true;
         }
         if(!item.hasOwnProperty("isPA") || item.isPA != isPA)
         {
            item.isPA = isPA;
            changed = true;
         }
         if(!item.hasOwnProperty("thresholdPct") || item.thresholdPct != thresholdPct)
         {
            item.thresholdPct = thresholdPct;
            changed = true;
         }
         if(!item.hasOwnProperty("thresholdPct2") || item.thresholdPct2 != thresholdPct2)
         {
            item.thresholdPct2 = thresholdPct2;
            changed = true;
         }
         return changed;
      }
      
      private function clearCNDFields(item:Object) : Boolean
      {
         var changed:Boolean = false;
         if(!item.hasOwnProperty("cnd") || item.cnd != -1)
         {
            item.cnd = -1;
            changed = true;
         }
         if(!item.hasOwnProperty("isPA") || item.isPA != false)
         {
            item.isPA = false;
            changed = true;
         }
         if(!item.hasOwnProperty("thresholdPct") || item.thresholdPct != -1)
         {
            item.thresholdPct = -1;
            changed = true;
         }
         if(!item.hasOwnProperty("thresholdPct2") || item.thresholdPct2 != -1)
         {
            item.thresholdPct2 = -1;
            changed = true;
         }
         return changed;
      }
      
      private function buildVisiblePAByCount() : Object
      {
         var result:Object = {};
         var i:int = 0;
         while(i < this.ItemDataA.length)
         {
            var item:Object = this.ItemDataA[i];
            var itemName:String = this.normalizeCNDName(item.text);
            if(this.isPowerArmorText(itemName))
            {
               var countKey:String = String(Number(item.count));
               if(result[countKey] == undefined)
               {
                  result[countKey] = 0;
               }
               result[countKey]++;
            }
            i++;
         }
         return result;
      }
      
      private function getFormID(item:Object) : Number
      {
         if(item.hasOwnProperty("formId"))
         {
            return Number(item.formId);
         }
         if(item.hasOwnProperty("formID"))
         {
            return Number(item.formID);
         }
         return -1;
      }
      
      private function normalizeCNDName(value:Object) : String
      {
         if(value == null || value == undefined)
         {
            return "";
         }
         var text:String = String(value);
         text = text.replace(/<[^>]*>/g,"");
         text = text.replace(/^\s*(\[[^\]]+\]\s*)+/,"");
         text = text.replace(/\s+/g," ");
         text = text.replace(/^\s+|\s+$/g,"");
         return text.toLowerCase();
      }
      
      private function isPowerArmorText(text:String) : Boolean
      {
         return text.indexOf("t-45") != -1 || text.indexOf("t-51") != -1 || text.indexOf("t-60") != -1 || text.indexOf("x-01") != -1 || text.indexOf("x-02") != -1 || text.indexOf("raider") != -1 || text.indexOf("power armor") != -1 || text.indexOf("pa ") != -1 || text.indexOf("掠夺者") != -1 || text.indexOf("动力装甲") != -1;
      }
      
      public function ReceiveCNDData(cndArray:Array) : void
      {
         this.cachedCndData = cndArray != null ? cndArray : [];
         this.UpdateList(-2);
         this.scheduleCNDRefresh();
      }
      
      private function scheduleCNDRefresh() : void
      {
         if(this._cndRefreshPending)
         {
            return;
         }
         this._cndRefreshPending = true;
         setTimeout(this.runDelayedCNDRefresh,40);
      }
      
      private function runDelayedCNDRefresh() : void
      {
         this._cndRefreshPending = false;
         this.UpdateList(-2);
      }
      
      public function SetCNDColor(colorHex:Number) : void
      {
         QuickContainerItem.customCNDColor = colorHex;
         this.UpdateList(-2);
      }
      
      public function UpdateComponentPositions() : void
      {
         if(FallUIHUDConfig._flag_inLayoutManager)
         {
            this.scanItems();
         }
         var _loc1_:Number = this.PositionForListSize[this.ItemDataA.length];
         this.ListItems_mc.y = _loc1_;
         this.ListHeaderAndBracket_mc.BracketPairHolder_mc.UpperBracket_mc.y = _loc1_;
         this.ListHeaderAndBracket_mc.BracketPairHolder_mc.visible = this._bracketsVisible;
         this.ListHeaderAndBracket_mc.ContainerName_mc.y = _loc1_ - (!this._bracketsVisible ? this.ListHeaderAndBracket_mc.y : 0);
         var _loc2_:BackgroundSimple = this.ListHeaderAndBracket_mc.BracketPairHolder_mc._fallUIShadow as BackgroundSimple;
         if(_loc2_)
         {
            _loc2_.setActive(this._bracketsVisible);
            _loc2_.update();
         }
         var _loc3_:Object = this["fallUiHud_layoutOptions"];
         if(_loc3_)
         {
            if(this.ItemDataA.length > 0 || !this._bracketsVisible)
            {
               this.ListHeaderAndBracket_mc.alpha = 1;
               this.ButtonHintBar_mc.alpha = _loc3_["butHntsAlpha"] / 100;
            }
            else
            {
               this.ListHeaderAndBracket_mc.alpha = _loc3_["onEmptyAlpha"] / 100;
               this.ButtonHintBar_mc.alpha = _loc3_["onEmptyAlpha"] / 100;
            }
            if(_loc3_.lstDir === 1)
            {
               this.y = this["fallUiHud_widgetProps"].y - _loc1_ * this.scaleY / 2;
               if(!this._bracketsVisible)
               {
                  this.ListHeaderAndBracket_mc.ContainerName_mc.y += this.ListHeaderAndBracket_mc.y * this.scaleY / 2;
               }
            }
            else if(_loc3_.lstDir === 2)
            {
               this.y = this["fallUiHud_widgetProps"].y - _loc1_ * this.scaleY;
               if(!this._bracketsVisible)
               {
                  this.y += this.ListHeaderAndBracket_mc.y * this.scaleY;
               }
            }
         }
      }
      
      public function set containerName(param1:String) : *
      {
         GlobalFunc.SetText(this.ListHeaderAndBracket_mc.ContainerName_mc.textField_tf,param1,false,true);
      }
      
      public function set bracketsVisible(param1:Boolean) : void
      {
         this._bracketsVisible = param1;
      }
      
      public function applyFallUIHudLayoutOptions() : void
      {
         this._mod.applyModSettings();
      }
   }
}

