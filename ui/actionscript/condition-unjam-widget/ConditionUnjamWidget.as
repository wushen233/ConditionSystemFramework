// Reconstructed standalone unjam widget document class.
// The released SWF is retained as behavior reference only; this AS3 source
// and its paired FLA are the authoritative publish inputs.
package
{
   import flash.display.Graphics;
   import flash.display.MovieClip;
    import flash.events.Event;
    import flash.filters.DropShadowFilter;
    import flash.geom.ColorTransform;

   [SWF(width="1280", height="720", backgroundColor="#333333", frameRate="24")]
   public class ConditionUnjamWidget extends MovieClip
   {
      private var widget:MovieClip;
      private var visibleState:Boolean = true;
      private var progress:Number = 0;
      private var inheritPreviewColor:Boolean = true;
      private var hudColor:uint = 0x18FF00;

      public function ConditionUnjamWidget()
      {
         super();
         addEventListener(Event.ADDED_TO_STAGE, onAddedToStage);
         addEventListener(Event.ENTER_FRAME, onEnterFrame);
      }

      private function onAddedToStage(event:Event):void
      {
         removeEventListener(Event.ADDED_TO_STAGE, onAddedToStage);
         while (numChildren > 0) {
            removeChildAt(0);
         }

         widget = new MovieClip();
         widget.filters = [new DropShadowFilter(1.5, 45, 0, 0.9, 2.5, 2.5, 3, 1)];
         addChild(widget);
          widget.x = stage.stageWidth / 2;
          widget.y = stage.stageHeight / 2;
          applyInheritedPreviewColor();
          render();
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

      private function onEnterFrame(event:Event):void
      {
         this.applyInheritedPreviewColor();
      }

      public function SetWidgetTransform(offsetX:Number, offsetY:Number, scale:Number):void
      {
         if (widget == null || stage == null) {
            return;
         }
         widget.x = stage.stageWidth / 2 + offsetX;
         widget.y = stage.stageHeight / 2 + offsetY;
         this.scaleX = scale;
         this.scaleY = scale;
      }

      public function SetWidgetVisible(isVisible:Boolean):void
      {
         visibleState = isVisible;
         visible = isVisible;
         render();
      }

      public function SetUnjamProgress(value:Number):void
      {
         progress = Math.max(0, Math.min(1, value));
         render();
      }

      public function SetUnjamColor(value:Number):void
      {
         if (value >= 0) {
            inheritPreviewColor = false;
            hudColor = uint(value) & 0xFFFFFF;
         }
         render();
      }

      // Fallout's HUD/MCM positioner calls this callback for clips that
      // follow the interface color. Keep the standalone movie consistent
      // with the combined ConditionWidget preview.
      public function onApplyColorChange(value:uint):void
      {
         // This callback carries the current HUD color; it must not turn off
         // parent-color inheritance used by the MCM positioner preview.
         hudColor = value & 0xFFFFFF;
         render();
      }

      private function render():void
      {
         if (widget == null) {
            return;
         }

         var graphics:Graphics = widget.graphics;
         graphics.clear();

         var start:Number = -progress * Math.PI;
         var step:Number = Math.PI * 2 / 8;
         graphics.lineStyle(0, 0, 0);
         graphics.beginFill(hudColor, 0.85);

         for (var index:int = 0; index < 8; index++) {
            var center:Number = start + index * step;
            var outerStart:Number = center - 0.27;
            var innerStart:Number = center - 0.19;
            var innerEnd:Number = center + 0.19;
            var outerEnd:Number = center + 0.27;

            if (index == 0) {
               graphics.moveTo(Math.cos(outerStart) * 42, Math.sin(outerStart) * 42);
            } else {
               graphics.lineTo(Math.cos(outerStart) * 42, Math.sin(outerStart) * 42);
            }
            graphics.lineTo(Math.cos(innerStart) * 50, Math.sin(innerStart) * 50);
            graphics.lineTo(Math.cos(innerEnd) * 50, Math.sin(innerEnd) * 50);
            graphics.lineTo(Math.cos(outerEnd) * 42, Math.sin(outerEnd) * 42);
         }

         graphics.lineTo(Math.cos(start - 0.27) * 42, Math.sin(start - 0.27) * 42);
         graphics.moveTo(28, 0);
         for (var circleIndex:int = 1; circleIndex <= 60; circleIndex++) {
            var angle:Number = circleIndex * (-(Math.PI * 2) / 60);
            graphics.lineTo(Math.cos(angle) * 28, Math.sin(angle) * 28);
         }
         graphics.endFill();

         drawArc(graphics, 25, 0, Math.PI * 2, 48, hudColor, 0.25, 4);
         if (progress > 0) {
            drawArc(graphics, 25, -Math.PI / 2, -Math.PI / 2 + progress * Math.PI * 2, 48, hudColor, 1, 4);
         }
         widget.visible = visibleState;
      }

      private function drawArc(
         graphics:Graphics,
         radius:Number,
         startAngle:Number,
         endAngle:Number,
         segments:int,
         color:uint,
         alpha:Number,
         thickness:Number
      ):void
      {
         graphics.lineStyle(thickness, color, alpha);
         graphics.moveTo(Math.cos(startAngle) * radius, Math.sin(startAngle) * radius);
         for (var index:int = 1; index <= segments; index++) {
            var angle:Number = startAngle + (endAngle - startAngle) * index / segments;
            graphics.lineTo(Math.cos(angle) * radius, Math.sin(angle) * radius);
         }
      }
   }
}
