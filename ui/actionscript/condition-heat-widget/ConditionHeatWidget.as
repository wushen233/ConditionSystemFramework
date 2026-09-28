package
{
   import flash.display.MovieClip;
   import flash.events.Event;
   import flash.filters.DropShadowFilter;
   import flash.text.TextField;
    import flash.text.TextFormat;
    import flash.text.TextFormatAlign;
    import flash.geom.ColorTransform;
    import flash.utils.getTimer;

   [SWF(width="1280", height="720", backgroundColor="#333333", frameRate="24")]
   public class ConditionHeatWidget extends MovieClip
   {
      private var heatBar:MovieClip;
      private var heatText:TextField;
      private var overheatText:TextField;
      private var widget:MovieClip;
      private var heatValue:Number = 0.55;
      private var heatOverheated:Boolean = false;
      private var overheatFlashYellow:Boolean = false;
      private var nextFlashAt:int = 0;
      private var liveStateInitialized:Boolean = false;
      private var pendingHeatHide:Boolean = false;
      private var heatHideAt:int = 0;
      private var pendingHostHide:Boolean = false;
      private var hostHideAt:int = 0;
      private var heatStateVisible:Boolean = true;
      private var hostVisible:Boolean = true;
      private var inheritPreviewColor:Boolean = true;
      private var hudColor:uint = 0x18FF00;
      private var heatOffsetX:Number = 0.0;
      private var heatOffsetY:Number = 0.0;
      private var heatScale:Number = 1.0;

      public function ConditionHeatWidget()
      {
         super();
         addEventListener(Event.ADDED_TO_STAGE, this.onAddedToStage);
         addEventListener(Event.ENTER_FRAME, this.onEnterFrame);
      }

      private function onAddedToStage(param1:Event):void
      {
         removeEventListener(Event.ADDED_TO_STAGE, this.onAddedToStage);
         stage.addEventListener(Event.RESIZE, this.onStageResize);
         while (numChildren > 0) {
            removeChildAt(0);
          }
          this.createWidget();
          this.applyInheritedPreviewColor();
          this.updateWidgetTransform();
          this.render();
       }

       private function applyInheritedPreviewColor():void
       {
          if (!this.inheritPreviewColor) {
             return;
          }
          if (parent == null) {
             return;
          }
          var inherited:ColorTransform = parent.transform.colorTransform;
          if (inherited == null) {
             return;
          }
          var isIdentity:Boolean = inherited.redMultiplier == 1.0 &&
             inherited.greenMultiplier == 1.0 &&
             inherited.blueMultiplier == 1.0 &&
             inherited.alphaMultiplier == 1.0 &&
             inherited.redOffset == 0.0 &&
             inherited.greenOffset == 0.0 &&
             inherited.blueOffset == 0.0;
          if (isIdentity) {
             return;
          }
          var red:int = Math.max(0, Math.min(255, Math.round(inherited.redMultiplier * 255.0 + inherited.redOffset)));
          var green:int = Math.max(0, Math.min(255, Math.round(inherited.greenMultiplier * 255.0 + inherited.greenOffset)));
          var blue:int = Math.max(0, Math.min(255, Math.round(inherited.blueMultiplier * 255.0 + inherited.blueOffset)));
          var inheritedColor:uint = uint((red << 16) | (green << 8) | blue);
          if (this.hudColor != inheritedColor) {
             this.hudColor = inheritedColor;
             this.render();
          }
       }

      private function createWidget():void
      {
         this.widget = new MovieClip();
         addChild(this.widget);

         this.heatBar = new MovieClip();
         this.heatBar.filters = [new DropShadowFilter(1.5, 45, 0, 0.9, 3, 3, 1, 1)];
         this.widget.addChild(this.heatBar);

         this.heatText = new TextField();
         var heatFormat:TextFormat = new TextFormat("$MAIN_Font_Bold", 14, 0xF7F7EE, true);
         heatFormat.align = TextFormatAlign.CENTER;
         this.heatText.defaultTextFormat = heatFormat;
         this.heatText.embedFonts = false;
         this.heatText.text = "0.0%";
         this.heatText.width = 168;
         this.heatText.height = 19;
         this.heatText.selectable = false;
         this.heatText.mouseEnabled = false;
         this.heatText.x = 0;
         this.heatText.y = 16;
         this.widget.addChild(this.heatText);

         this.overheatText = new TextField();
         var overheatFormat:TextFormat = new TextFormat("$MAIN_Font_Bold", 14, 0xF2DC00, true);
         overheatFormat.align = TextFormatAlign.CENTER;
         this.overheatText.defaultTextFormat = overheatFormat;
         this.overheatText.embedFonts = false;
         this.overheatText.text = "OVERHEAT!";
         this.overheatText.width = 168;
         this.overheatText.height = 19;
         this.overheatText.selectable = false;
         this.overheatText.mouseEnabled = false;
         this.overheatText.x = 0;
         this.overheatText.y = 16;
         this.overheatText.visible = false;
         this.widget.addChild(this.overheatText);
      }

      private function onStageResize(param1:Event):void
      {
         this.updateWidgetTransform();
      }

      private function render():void
      {
         var tickX:Number;
         if (!this.heatBar) {
            return;
         }

         var barX:Number = 6;
         var barY:Number = 3;
         var barH:Number = 9;
         var fillWidth:Number = 156 * this.heatValue;
         var isOverheated:Boolean = this.heatOverheated && this.heatValue >= 1;
         var borderColor:uint = isOverheated
            ? (this.overheatFlashYellow ? 0xF2DC00 : 0xE21E2A)
            : this.hudColor;

         this.heatBar.graphics.clear();
         if (fillWidth > 0) {
            if (isOverheated) {
               this.heatBar.graphics.beginFill(borderColor, 1);
            } else {
               // The normal bar must use the selected HUD color. The previous
               // split SWF used a fixed green/yellow/red gradient here, so
               // SetHeatColor only affected the border and text.
               this.heatBar.graphics.beginFill(this.hudColor, 1);
            }
            this.heatBar.graphics.drawRect(barX, barY, fillWidth, barH);
            this.heatBar.graphics.endFill();
         }

         this.heatBar.graphics.lineStyle(2, borderColor, 1);
         this.heatBar.graphics.drawRect(5, 3, 158, 9);
         this.heatBar.graphics.lineStyle(1.5, borderColor, 1);
         var tick:int = 1;
         while (tick <= 4) {
            tickX = 4 + 160 * tick / 5;
            this.heatBar.graphics.moveTo(tickX, 11);
            this.heatBar.graphics.lineTo(tickX, 6);
            tick++;
         }

         this.heatText.text = (this.heatValue * 100).toFixed(1) + "%";
         this.heatText.textColor = this.hudColor;
         this.heatText.visible = !isOverheated;
         this.overheatText.textColor = borderColor;
         this.overheatText.visible = isOverheated;
         visible = this.hostVisible && this.heatStateVisible;
      }

      public function SetHeat(param1:Number, param2:Boolean, param3:Boolean):void
      {
         this.liveStateInitialized = true;
         var wasOverheated:Boolean = this.heatOverheated && this.heatValue >= 1;
         this.heatValue = Math.max(0, Math.min(1, param1));
         this.heatOverheated = param2;
         if (param3) {
            this.pendingHeatHide = false;
            this.heatStateVisible = true;
         } else {
            this.pendingHeatHide = true;
            this.heatHideAt = getTimer() + 150;
         }

         var isOverheated:Boolean = this.heatOverheated && this.heatValue >= 1;
         if (!isOverheated) {
            this.overheatFlashYellow = false;
            this.nextFlashAt = 0;
         } else if (!wasOverheated) {
            this.overheatFlashYellow = false;
            this.nextFlashAt = getTimer();
         }
         this.render();
      }

      private function onEnterFrame(param1:Event):void
      {
         var now:int = getTimer();
         var needsRender:Boolean = false;
         if (this.pendingHeatHide && now >= this.heatHideAt) {
            this.pendingHeatHide = false;
            this.heatStateVisible = false;
            needsRender = true;
         }
         if (this.pendingHostHide && now >= this.hostHideAt) {
            this.pendingHostHide = false;
            this.hostVisible = false;
            needsRender = true;
         }
         if (this.heatOverheated && this.heatValue >= 1 && now >= this.nextFlashAt) {
            this.overheatFlashYellow = !this.overheatFlashYellow;
            this.nextFlashAt = now + 325;
            needsRender = true;
         }
         if (needsRender) {
            this.render();
         }
      }

      public function SetHeatColor(param1:Number):void
      {
         if (param1 >= 0) {
            this.inheritPreviewColor = false;
            this.hudColor = uint(param1) & 0xFFFFFF;
         }
         this.render();
      }

      // Fallout's HUD/MCM positioner calls this callback for clips that
      // follow the interface color. Keep the standalone movie consistent
      // with the combined ConditionWidget preview.
      public function onApplyColorChange(param1:uint):void
      {
         // This is a HUD-following callback, not an explicit MCM custom-color
         // override. Keep inheritance enabled so the positioner can update
         // the preview again when its parent ColorTransform changes.
         this.hudColor = param1 & 0xFFFFFF;
         this.render();
      }

      public function SetHeatTransform(param1:Number, param2:Number, param3:Number):void
      {
         this.heatOffsetX = param1;
         this.heatOffsetY = param2;
         this.heatScale = param3;
         this.updateWidgetTransform();
      }

      private function updateWidgetTransform():void
      {
         if (this.widget == null || stage == null) {
            return;
         }
         this.widget.x = stage.stageWidth / 2 - 84 + this.heatOffsetX;
         this.widget.y = stage.stageHeight / 2 - 19 + this.heatOffsetY;
         this.widget.scaleX = this.heatScale;
         this.widget.scaleY = this.heatScale;
      }

      public function SetWidgetVisible(param1:Boolean):void
      {
         if (param1) {
            this.pendingHostHide = false;
            this.hostVisible = true;
         } else if (!this.liveStateInitialized) {
            this.pendingHostHide = false;
            this.hostVisible = false;
         } else {
            this.pendingHostHide = true;
            this.hostHideAt = getTimer() + 150;
         }
         this.render();
      }
   }
}
