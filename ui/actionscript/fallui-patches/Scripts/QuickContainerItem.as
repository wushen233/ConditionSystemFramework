package
{
   import M8r.Mod.QuickContainerItemMod2;
   import Shared.AS3.BSUIComponent;
   import Shared.GlobalFunc;
   import flash.display.MovieClip;
   import flash.geom.ColorTransform;
   import flash.text.TextField;
   import scaleform.gfx.Extensions;
   import scaleform.gfx.TextFieldEx;
   
   [Embed(source="/_assets/assets.swf", symbol="symbol550")]
   public dynamic class QuickContainerItem extends BSUIComponent
   {
      
      public static var customCNDColor:Number = -1;
      
      public var ItemName_tf:TextField;
      
      public var LegendaryIcon_mc:MovieClip;
      
      public var TaggedForSearchIcon_mc:MovieClip;
      
      public var FavoriteIcon_mc:MovieClip;
      
      public var BetterIcon_mc:MovieClip;
      
      public var SelectionIndicator_mc:MovieClip;
      
      private var BaseTextFieldWidth:int;
      
      private var _data:QuickContainerItemData;
      
      private var _selected:Boolean;
      
      private var _mod:QuickContainerItemMod2 = new QuickContainerItemMod2(this);
      
      public var cndBar:MovieClip;
      
      public var cndBarBG:MovieClip;
      
      public function QuickContainerItem()
      {
         super();
         this.BaseTextFieldWidth = this.ItemName_tf.width;
         Extensions.enabled = true;
         TextFieldEx.setTextAutoSize(this.ItemName_tf,"shrink");
         this.visible = false;
         this._data = null;
         this._mod.init();
         this.cndBarBG = new MovieClip();
         this.cndBarBG.name = "cndBarBG";
         this.addChild(this.cndBarBG);
         this.cndBar = new MovieClip();
         this.cndBar.name = "cndBar";
         this.addChild(this.cndBar);
      }
      
      public function get data() : QuickContainerItemData
      {
         return this._data;
      }
      
      public function set data(param1:QuickContainerItemData) : void
      {
         this._data = param1;
         this.SetIsDirty();
      }
      
      public function get selected() : Boolean
      {
         return this._selected;
      }
      
      public function set selected(param1:Boolean) : void
      {
         this._selected = param1;
         this.SetIsDirty();
      }
      
      override public function redrawUIComponent() : void
      {
         if(this.data)
         {
            this.visible = true;
            this.ItemName_tf.width = this.BaseTextFieldWidth - this.CalcIconWidth();
            if(this.data.count > 1)
            {
               GlobalFunc.SetText(this.ItemName_tf,this.data.text + " (" + this.data.count + ")",false,false,true);
            }
            else
            {
               GlobalFunc.SetText(this.ItemName_tf,this.data.text,false,false,true);
            }
            this.ItemName_tf.textColor = this.selected ? 0 : 16777215;
            this.SelectionIndicator_mc.alpha = this.selected ? 1 : 0;
            this.AddIconsToEntry();
            this._mod.eventRedrawUiComponent();
         }
         else
         {
            this.visible = false;
            this.HideCNDBar();
         }
      }
      
      public function CalcIconWidth() : int
      {
         var _loc1_:int = 0;
         var hasCND:Boolean = Boolean(this.data) && this.data.hasOwnProperty("cnd") && this.data.cnd >= 0;
         if(this.data.taggedForSearch || this.data.isLegendary || this.data.favorite || this.data.isBetterThanEquippedItem || hasCND)
         {
            _loc1_ = 4;
            if(this.data.taggedForSearch)
            {
               _loc1_ += this.TaggedForSearchIcon_mc.width + 2;
            }
            if(this.data.isLegendary)
            {
               _loc1_ += this.LegendaryIcon_mc.width + 2;
            }
            if(this.data.favorite)
            {
               _loc1_ += this.FavoriteIcon_mc.width + 2;
            }
            if(this.data.isBetterThanEquippedItem)
            {
               _loc1_ += this.BetterIcon_mc.width + 2;
            }
            if(hasCND)
            {
               _loc1_ += 60;
            }
         }
         return _loc1_;
      }
      
      public function AddIconsToEntry() : *
      {
         var _loc1_:ColorTransform = this.FavoriteIcon_mc.transform.colorTransform;
         _loc1_.redOffset = this.selected ? -255 : 0;
         _loc1_.greenOffset = this.selected ? -255 : 0;
         _loc1_.blueOffset = this.selected ? -255 : 0;
         this.FavoriteIcon_mc.transform.colorTransform = _loc1_;
         _loc1_ = this.TaggedForSearchIcon_mc.transform.colorTransform;
         _loc1_.redOffset = this.selected ? -255 : 0;
         _loc1_.greenOffset = this.selected ? -255 : 0;
         _loc1_.blueOffset = this.selected ? -255 : 0;
         this.TaggedForSearchIcon_mc.transform.colorTransform = _loc1_;
         _loc1_ = this.LegendaryIcon_mc.transform.colorTransform;
         _loc1_.redOffset = this.selected ? -255 : 0;
         _loc1_.greenOffset = this.selected ? -255 : 0;
         _loc1_.blueOffset = this.selected ? -255 : 0;
         this.LegendaryIcon_mc.transform.colorTransform = _loc1_;
         _loc1_ = this.BetterIcon_mc.transform.colorTransform;
         _loc1_.redOffset = this.selected ? -255 : 0;
         _loc1_.greenOffset = this.selected ? -255 : 0;
         _loc1_.blueOffset = this.selected ? -255 : 0;
         this.BetterIcon_mc.transform.colorTransform = _loc1_;
         var _loc2_:Number = 4 + this.ItemName_tf.getLineMetrics(0).width + this.ItemName_tf.getLineMetrics(0).x + this.ItemName_tf.x;
         this.TaggedForSearchIcon_mc.visible = this.data.taggedForSearch;
         this.TaggedForSearchIcon_mc.x = _loc2_;
         if(this.data.taggedForSearch)
         {
            _loc2_ += this.TaggedForSearchIcon_mc.width + 2;
         }
         this.LegendaryIcon_mc.visible = this.data.isLegendary;
         this.LegendaryIcon_mc.x = _loc2_;
         if(this.data.isLegendary)
         {
            _loc2_ += this.LegendaryIcon_mc.width + 2;
         }
         this.FavoriteIcon_mc.visible = this.data.favorite;
         this.FavoriteIcon_mc.x = _loc2_;
         if(this.data.favorite)
         {
            _loc2_ += this.FavoriteIcon_mc.width + 2;
         }
         this.BetterIcon_mc.visible = this.data.isBetterThanEquippedItem;
         this.BetterIcon_mc.x = _loc2_;
         if(this.data.isBetterThanEquippedItem)
         {
            _loc2_ += this.BetterIcon_mc.width + 2;
         }
         if(this.data && this.data.hasOwnProperty("cnd") && this.data.cnd >= 0)
         {
            this.cndBar.visible = true;
            this.cndBarBG.visible = true;
            var cndBarShield:MovieClip = this.getChildByName("cndBarShield") as MovieClip;
            if(cndBarShield == null)
            {
               cndBarShield = new MovieClip();
               cndBarShield.name = "cndBarShield";
               this.addChild(cndBarShield);
            }
            cndBarShield.visible = true;
            var pct:Number = this.data.cnd / 100;
            if(pct < 0)
            {
               pct = 0;
            }
            var uiColor:uint = 16777215;
            var applySystemColor:Boolean = true;
            if(QuickContainerItem.customCNDColor != -1)
            {
               uiColor = uint(QuickContainerItem.customCNDColor);
               applySystemColor = false;
            }
            var shieldColor:uint = uiColor;
            if(!applySystemColor)
            {
               var origR:uint = uint(uiColor >> 16 & 0xFF);
               var origG:uint = uint(uiColor >> 8 & 0xFF);
               var origB:uint = uint(uiColor & 0xFF);
               if(origR + origG + origB > 650)
               {
                  shieldColor = uint(origR * 0.65 << 16 | origG * 0.65 << 8 | origB * 0.65);
               }
               else
               {
                  var br:uint = origR + (255 - origR) * 0.7;
                  var bg:uint = origG + (255 - origG) * 0.7;
                  var bb:uint = origB + (255 - origB) * 0.7;
                  shieldColor = uint(br << 16 | bg << 8 | bb);
               }
            }
            // 计算深色背景色（用于进度条暗色背景 + 三角形缺口，与 ItemCard 一致）
            var _bgR:uint = uint(Math.max(1, Math.round((uiColor >> 16 & 0xFF) * 0.18)));
            var _bgG:uint = uint(Math.max(1, Math.round((uiColor >> 8 & 0xFF) * 0.18)));
            var _bgB:uint = uint(Math.max(1, Math.round((uiColor & 0xFF) * 0.18)));
            var bgColor:uint = uint(_bgR << 16 | _bgG << 8 | _bgB);
            var posX:Number = this.BaseTextFieldWidth - 40 - -15;
            var posY:Number = this.ItemName_tf.y + this.ItemName_tf.height / 2 - 10 / 2;
            var basePct:Number = Math.min(1,pct);
            var shieldPct:Number = Math.max(0,pct - 1);
            var shieldDrawPct:Number = Math.min(1,shieldPct / 1);
            this.cndBarBG.graphics.clear();
            this.cndBarBG.graphics.lineStyle(2,uiColor,0.5);
            this.cndBarBG.graphics.beginFill(bgColor, 1.0);
            this.cndBarBG.graphics.drawRect(posX,posY,40,10);
            this.cndBarBG.graphics.endFill();
            var maxFillW:Number = 40 - 2 * 2;
            var fillH:Number = 10 - 2 * 2;
            var baseFillAlpha:Number = shieldDrawPct > 0 ? 0.45 : 0.8;
            this.cndBar.graphics.clear();
            if(basePct > 0)
            {
               this.cndBar.graphics.lineStyle();
               this.cndBar.graphics.beginFill(uiColor,baseFillAlpha);
               this.cndBar.graphics.drawRect(posX + 2,posY + 2,maxFillW * basePct,fillH);
               this.cndBar.graphics.endFill();
            }
            cndBarShield.graphics.clear();
            var shieldFillWidth:Number = maxFillW * shieldDrawPct;
            cndBarShield.visible = shieldFillWidth > 0;
            if(shieldFillWidth > 0)
            {
               var shieldH:Number = fillH * 0.33;
               var shieldY:Number = posY + 2 + (fillH - shieldH) / 2;
               var shieldX:Number = posX + 2 + (maxFillW - shieldFillWidth);
               cndBarShield.graphics.lineStyle();
               cndBarShield.graphics.beginFill(shieldColor,1);
               cndBarShield.graphics.drawRect(shieldX,shieldY,shieldFillWidth,shieldH);
               cndBarShield.graphics.endFill();
            }
            // 👑 三角形门槛缺口（与 ItemCard_Entry 同步）
            var cndBarThreshold:MovieClip = this.getChildByName("cndBarThreshold") as MovieClip;
            if(cndBarThreshold == null)
            {
               cndBarThreshold = new MovieClip();
               cndBarThreshold.name = "cndBarThreshold";
               this.addChild(cndBarThreshold);
            }
            cndBarThreshold.visible = false;
            cndBarThreshold.graphics.clear();
            // 只在非过量状态（shieldDrawPct <= 0）才绘制门槛缺口
            // 只有 CSF 系统管理的物品（isPA = false）才绘制门槛缺口
            // 动力甲甲片(vanilla 耐久) 和融合核心(充能) 不应显示
            var drawNotch:Boolean = shieldDrawPct <= 0 && !this.data.isPA;
            var notchPct1:Number = this.data.hasOwnProperty("thresholdPct") ? Number(this.data.thresholdPct) : -1;
            var notchPct2:Number = this.data.hasOwnProperty("thresholdPct2") ? Number(this.data.thresholdPct2) : -1;
            if(drawNotch && (notchPct1 >= 0 || notchPct2 >= 0))
            {
               var markList:Array = [];
               if(notchPct1 >= 0 && notchPct1 <= 1)
               {
                  var mp1:Number = maxFillW * notchPct1;
                  if(mp1 > 1 && mp1 < maxFillW - 1) markList.push(mp1);
               }
               if(notchPct2 >= 0 && notchPct2 <= 1)
               {
                  var mp2:Number = maxFillW * notchPct2;
                  if(mp2 > 1 && mp2 < maxFillW - 1)
                  {
                     var isDup2:Boolean = false;
                     for(var di2:int = 0; di2 < markList.length; di2++)
                     {
                        if(Math.abs(markList[di2] - mp2) < 5) { isDup2 = true; break; }
                     }
                     if(!isDup2) markList.push(mp2);
                  }
               }
               // 手动排序
               if(markList.length > 1 && markList[0] > markList[1])
               {
                  var mtmp:Number = markList[0];
                  markList[0] = markList[1];
                  markList[1] = mtmp;
               }
               var triH:Number = Math.max(2, Math.round(fillH / 2));
               var triBw:Number = Math.max(1, Math.round(fillH * 0.3));
               cndBarThreshold.visible = markList.length > 0;
               cndBarThreshold.graphics.beginFill(bgColor, 1.0);
               for(var mi:int = 0; mi < markList.length; mi++)
               {
                  var mx:Number = posX + 2 + markList[mi];
                  // 上三角形（倒三角）
                  cndBarThreshold.graphics.moveTo(mx - triBw, posY + 2);
                  cndBarThreshold.graphics.lineTo(mx, posY + 2 + triH);
                  cndBarThreshold.graphics.lineTo(mx + triBw, posY + 2);
                  cndBarThreshold.graphics.lineTo(mx - triBw, posY + 2);
                  // 下三角形（正三角）
                  cndBarThreshold.graphics.moveTo(mx - triBw, posY + 2 + fillH);
                  cndBarThreshold.graphics.lineTo(mx, posY + 2 + fillH - triH);
                  cndBarThreshold.graphics.lineTo(mx + triBw, posY + 2 + fillH);
                  cndBarThreshold.graphics.lineTo(mx - triBw, posY + 2 + fillH);
               }
               cndBarThreshold.graphics.endFill();
            }
            // 进度条不跟随选中态变色（选中态已由文字变色 + 图标 + SelectionIndicator 体现）
            this.cndBarBG.transform.colorTransform = new ColorTransform();
            this.cndBar.transform.colorTransform = new ColorTransform();
            cndBarShield.transform.colorTransform = new ColorTransform();
            cndBarThreshold.transform.colorTransform = new ColorTransform();
         }
         else
         {
            this.HideCNDBar();
         }
         this._mod.eventAddIconsToEntry();
      }
      
      private function HideCNDBar() : void
      {
         if(this.cndBar)
         {
            this.cndBar.graphics.clear();
            this.cndBar.visible = false;
         }
         if(this.cndBarBG)
         {
            this.cndBarBG.graphics.clear();
            this.cndBarBG.visible = false;
         }
         var cndBarShieldHide:MovieClip = this.getChildByName("cndBarShield") as MovieClip;
         if(cndBarShieldHide)
         {
            cndBarShieldHide.graphics.clear();
            cndBarShieldHide.visible = false;
         }
         var cndBarThresholdHide:MovieClip = this.getChildByName("cndBarThreshold") as MovieClip;
         if(cndBarThresholdHide)
         {
            cndBarThresholdHide.graphics.clear();
            cndBarThresholdHide.visible = false;
         }
      }
   }
}

