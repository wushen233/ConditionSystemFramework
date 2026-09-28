package {
    import flash.display.MovieClip;
    import flash.events.Event;
    import flash.filters.DropShadowFilter; 
    import flash.geom.ColorTransform;
    import flash.geom.Point;
    import flash.text.TextField;
    import flash.text.TextFormat;

    public class ConditionWidget extends MovieClip {
        
        public var Widget_mc:DurabilityWidget; 
        public var UnjamWidget_mc:MovieClip;
        
        // 用来装载抽离出的红条的安全屋
        public var redBarContainer:MovieClip;
        
        // 专门用来隔离颜色、纯粹挂载黑色投影的外壳
        public var shadowContainer:MovieClip;

        // MCM positioners load this movie without calling SetWidgetVisible.
        // Start the CND sample visible; the native menu applies its live
        // visibility state immediately after the movie is created.
        private var targetAlpha:Number = 1.0;
        private var mcmOffsetX:Number = 0;
        private var mcmOffsetY:Number = 0;
        // The MCM positioner loads the shared movie without calling
        // SetCndStyle. Use the recovered CND-bar preview as the design-time
        // default; the native menu overrides this immediately for the live
        // Fallout 4 style when that mode is selected.
        private var fnvStyle:Boolean = false;
        private var fnvWidget:MovieClip;
        private var fnvPanel:MovieClip;
        private var fnvBar:MovieClip;
        private var fnvText:TextField;
        private var fnvMultiplier:TextField;
        private var fnvPercent:Number = 1.0;
        private var fnvThreshold:Number = 0.75;
        private var fnvColor:uint = 0xFFFFFF;
        private var fnvTextVisible:Boolean = true;
        private var heatWidget:MovieClip;
        private var heatBar:MovieClip;
        private var heatText:TextField;
        private var heatPercent:Number = 0.0;
        private var heatOverheated:Boolean = false;
        private var heatStateVisible:Boolean = false;
        private var heatScreenX:Number = 0.0;
        private var heatScreenY:Number = 0.0;
        private var heatScreenScale:Number = 1.0;

        public function ConditionWidget() {
            super();
            
            if (Widget_mc != null) {
                // MCM positioners place the loaded SWF root directly at the
                // configured coordinates. The FLA keeps a design-time
                // preview transform on Widget_mc, so normalize that instance
                // before the same coordinates are applied by the game.
                Widget_mc.x = 0;
                Widget_mc.y = 0;
                Widget_mc.scaleX = 1.0;
                Widget_mc.scaleY = 1.0;
                Widget_mc.alpha = 0.0;
                Widget_mc.visible = false;
                
                // ==========================================
                // 创建阴影隔离容器
                // ==========================================
                shadowContainer = new MovieClip();
                // 放入原本 Widget_mc 所在的层级
                this.addChildAt(shadowContainer, this.getChildIndex(Widget_mc));
                // 将 Widget_mc 移入隔离容器
                shadowContainer.addChild(Widget_mc);
                
                // 金蝉脱壳！把红条从即将被绿光洗礼的 Widget_mc 里面偷出来！
                if (Widget_mc.jamThresholdBar != null) {
                    redBarContainer = new MovieClip();
                    // 把它垫在 Widget_mc 的正下方 (此时都在 shadowContainer 里面)
                    shadowContainer.addChildAt(redBarContainer, shadowContainer.getChildIndex(Widget_mc));
                    
                    // actionscript 的 addChild 会自动把它从原父级(Widget_mc)剥离
                    redBarContainer.addChild(Widget_mc.jamThresholdBar);
                    
                    // 给它涂上安全的暗红色
                    var ct:ColorTransform = new ColorTransform();
                    ct.color = 0x8B0000; 
                    Widget_mc.jamThresholdBar.transform.colorTransform = ct;
                }
                
                // 【微调纯黑投影】：距离 1.0, 角度 45, 纯黑, 透明度 1.0, 模糊 2.0x2.0, 强度 1.2
                var ds:DropShadowFilter = new DropShadowFilter(1.0, 45, 0x000000, 1.0, 2.0, 2.0, 1.2, 1);
                shadowContainer.filters = [ds];
            }
            
            if (UnjamWidget_mc != null) {
                UnjamWidget_mc.x = 0;
                UnjamWidget_mc.y = 0;
                UnjamWidget_mc.scaleX = 1.0;
                UnjamWidget_mc.scaleY = 1.0;
                UnjamWidget_mc.alpha = 0.0;
                UnjamWidget_mc.visible = false;
                // 原封不动保留你原本排障齿轮的投影参数
                var uiShadow:DropShadowFilter = new DropShadowFilter(1.5, 45, 0x000000, 0.9, 2.5, 2.5, 3.0, 1);
                UnjamWidget_mc.filters = [uiShadow];
            }
            
            if (Widget_mc != null) {
                Widget_mc.SetCndStyle(false);
                Widget_mc.SetTextVisible(true);
            }
            EnsureFNVWidget();
            ApplyFNVVisibility();
            RenderFNVWidget();
            addEventListener(Event.ENTER_FRAME, onEnterFrame);
            addEventListener(Event.ADDED_TO_STAGE, onAddedToStage);
        }

        private function onAddedToStage(e:Event):void {
            removeEventListener(Event.ADDED_TO_STAGE, onAddedToStage);
            stage.addEventListener(Event.RESIZE, onStageResize);
            ApplyInheritedPreviewColor();
            UpdateUnjamPosition();
        }

        private function ApplyInheritedPreviewColor():void {
            // MCM applies its HUD ColorTransform to the loaded SWF parent.
            // Reuse that transform for dynamically rendered preview content.
            if (parent == null) return;
            var inherited:ColorTransform = parent.transform.colorTransform;
            if (inherited == null) return;
            var isIdentity:Boolean = inherited.redMultiplier == 1.0 &&
                inherited.greenMultiplier == 1.0 &&
                inherited.blueMultiplier == 1.0 &&
                inherited.alphaMultiplier == 1.0 &&
                inherited.redOffset == 0.0 &&
                inherited.greenOffset == 0.0 &&
                inherited.blueOffset == 0.0;
            if (isIdentity) return;
            var red:int = Math.max(0, Math.min(255, Math.round(inherited.redMultiplier * 255.0 + inherited.redOffset)));
            var green:int = Math.max(0, Math.min(255, Math.round(inherited.greenMultiplier * 255.0 + inherited.greenOffset)));
            var blue:int = Math.max(0, Math.min(255, Math.round(inherited.blueMultiplier * 255.0 + inherited.blueOffset)));
            onApplyColorChange(uint((red << 16) | (green << 8) | blue));
        }

        private function EnsureFNVWidget():void {
            if (fnvWidget != null) return;
            fnvWidget = new MovieClip();
            fnvPanel = new MovieClip();
            fnvBar = new MovieClip();
            fnvText = new TextField();
            fnvMultiplier = new TextField();
            fnvWidget.mouseEnabled = false;
            fnvWidget.mouseChildren = false;
            // Use the Fallout HUD font alias used by JuryRiggingMenu. System
            // fonts such as Impact/_sans are not consistently available in HUD SWFs.
            fnvText.defaultTextFormat = new TextFormat("$MAIN_Font_Bold", 20, fnvColor, true);
            fnvText.embedFonts = false;
            fnvText.selectable = false;
            fnvText.text = "CND";
            fnvText.x = 2;
            fnvText.y = 2;
            fnvText.width = 42;
            fnvText.height = 25;
            fnvMultiplier.defaultTextFormat = new TextFormat("$MAIN_Font_Bold", 14, fnvColor, true);
            fnvMultiplier.embedFonts = false;
            fnvMultiplier.selectable = false;
            fnvMultiplier.text = "";
            fnvMultiplier.x = 114;
            fnvMultiplier.y = 3;
            fnvMultiplier.width = 26;
            fnvMultiplier.height = 22;
            fnvWidget.filters = [new DropShadowFilter(1.5, 45, 0x000000, 0.78, 4.0, 4.0, 1.0, 1)];
            fnvWidget.addChild(fnvPanel);
            fnvWidget.addChild(fnvBar);
            fnvWidget.addChild(fnvText);
            fnvWidget.addChild(fnvMultiplier);
            addChild(fnvWidget);
            fnvWidget.visible = false;
            RenderFNVWidget();
        }

        private function EnsureHeatWidget():void {
            if (heatWidget != null) return;
            heatWidget = new MovieClip();
            heatBar = new MovieClip();
            heatText = new TextField();
            heatText.defaultTextFormat = new TextFormat("$MAIN_Font_Bold", 12, 0xFFB000, true);
            heatText.embedFonts = false;
            heatText.selectable = false;
            heatText.text = "HEAT";
            heatText.x = 0;
            heatText.y = 1;
            heatText.width = 38;
            heatText.height = 18;
            heatWidget.mouseEnabled = false;
            heatWidget.mouseChildren = false;
            heatWidget.addChild(heatBar);
            heatWidget.addChild(heatText);
            addChild(heatWidget);
            heatWidget.visible = false;
            heatWidget.alpha = 0.0;
            RenderHeatWidget();
        }

        private function RenderHeatWidget():void {
            if (heatWidget == null) return;
            var barX:Number = 39;
            var barY:Number = 3;
            var barW:Number = 82;
            var barH:Number = 10;
            var p:Number = Math.max(0.0, Math.min(1.0, heatPercent));
            var color:uint = heatOverheated ? 0xFF3030 : (p >= 0.70 ? 0xFFB000 : 0xFFD54A);
            heatText.textColor = color;
            heatBar.graphics.clear();
            heatBar.graphics.beginFill(0x100C08, 0.78);
            heatBar.graphics.drawRect(barX, barY, barW, barH);
            heatBar.graphics.endFill();
            heatBar.graphics.lineStyle(1, 0x4A3B22, 0.9);
            heatBar.graphics.drawRect(barX, barY, barW, barH);
            heatBar.graphics.beginFill(color, 1.0);
            heatBar.graphics.drawRect(barX + 1, barY + 1, (barW - 2) * p, barH - 2);
            heatBar.graphics.endFill();
            if (heatOverheated) {
                heatBar.graphics.lineStyle(2, 0xFF3030, 0.9);
                heatBar.graphics.drawRect(barX - 1, barY - 1, barW + 2, barH + 2);
            }
        }

        public function SetHeat(heat:Number, overheated:Boolean, isVisible:Boolean):void {
            EnsureHeatWidget();
            heatPercent = Math.max(0.0, Math.min(1.0, heat));
            heatOverheated = overheated;
            heatStateVisible = isVisible;
            UpdateHeatPosition();
            RenderHeatWidget();
            heatWidget.visible = heatStateVisible && targetAlpha > 0.0;
            if (!heatWidget.visible) heatWidget.alpha = 0.0;
        }

        public function SetHeatTransform(posX:Number, posY:Number, scale:Number):void {
            heatScreenX = posX;
            heatScreenY = posY;
            heatScreenScale = scale;
            EnsureHeatWidget();
            UpdateHeatPosition();
        }

        private function UpdateHeatPosition():void {
            if (heatWidget == null || stage == null) return;
            var localPoint:Point = globalToLocal(new Point(
                stage.stageWidth / 2 + heatScreenX,
                stage.stageHeight / 2 + heatScreenY));
            heatWidget.x = localPoint.x;
            heatWidget.y = localPoint.y;
            heatWidget.scaleX = heatScreenScale;
            heatWidget.scaleY = heatScreenScale;
        }

        private function RenderFNVWidget():void {
            if (fnvWidget == null) return;
            fnvPanel.graphics.clear();
            fnvPanel.visible = false;

            var barX:Number = 45;
            var barY:Number = 8;
            var barW:Number = 66;
            var barH:Number = 14;
            var visiblePct:Number = Math.max(0, Math.min(1, fnvPercent > 1 ? fnvPercent - 1 : fnvPercent));
            var markerX:Number = barX + barW * fnvThreshold;
            var notchHalfWidth:Number = 2.5;
            var notchDepth:Number = 6;
            var fillWidth:Number = barW * visiblePct;
            fnvBar.graphics.clear();
            fnvBar.graphics.beginFill(0x1C160F, 0.48);
            fnvBar.graphics.drawRect(barX, barY, barW, barH);
            fnvBar.graphics.beginFill(fnvColor, 1);
            fnvBar.graphics.drawRect(barX, barY, fillWidth, barH);
            fnvBar.graphics.endFill();

            if (visiblePct > fnvThreshold) {
                var notchLeft:Number = markerX - notchHalfWidth;
                var notchRight:Number = markerX + notchHalfWidth;
                var topTipY:Number = barY + notchDepth;
                var bottomTipY:Number = barY + barH - notchDepth;
                fnvBar.graphics.beginFill(0x1C160F, 0.48);
                fnvBar.graphics.moveTo(notchLeft, barY);
                fnvBar.graphics.curveTo(notchLeft + 1, barY + 3, markerX - 1, topTipY - 1);
                fnvBar.graphics.curveTo(markerX, topTipY, markerX + 1, topTipY - 1);
                fnvBar.graphics.curveTo(notchRight - 1, barY + 3, notchRight, barY);
                fnvBar.graphics.lineTo(notchLeft, barY);
                fnvBar.graphics.endFill();
                fnvBar.graphics.beginFill(0x1C160F, 0.48);
                fnvBar.graphics.moveTo(notchLeft, barY + barH);
                fnvBar.graphics.curveTo(notchLeft + 1, barY + barH - 3, markerX - 1, bottomTipY + 1);
                fnvBar.graphics.curveTo(markerX, bottomTipY, markerX + 1, bottomTipY + 1);
                fnvBar.graphics.curveTo(notchRight - 1, barY + barH - 3, notchRight, barY + barH);
                fnvBar.graphics.lineTo(notchLeft, barY + barH);
                fnvBar.graphics.endFill();
            }
            fnvText.textColor = fnvColor;
            fnvText.visible = fnvTextVisible;
            fnvMultiplier.textColor = fnvColor;
            fnvMultiplier.text = fnvPercent > 1.0 ? "X2" : "";
            fnvMultiplier.visible = fnvTextVisible && fnvPercent > 1.0;
        }

        private function ApplyFNVVisibility():void {
            if (fnvWidget != null) {
                fnvWidget.visible = fnvStyle && targetAlpha > 0;
                fnvWidget.alpha = targetAlpha;
            }
            if (Widget_mc != null) {
                Widget_mc.visible = !fnvStyle && targetAlpha > 0;
                Widget_mc.alpha = targetAlpha;
            }
        }

        // ==========================================
        // 【全新安全颜色管理模块】
        // ==========================================
        private var isCustomColor:Boolean = false;
        private var customColorValue:uint = 0xFFFFFF;
        private var lastHudColor:uint = 0xFFFFFF;

        // C++ 用来强制覆盖为 MCM 自定义颜色的接口
        public function SetCustomColor(colorVal:Number):void {
            if (colorVal < 0) {
                isCustomColor = false;
                ApplyColor(lastHudColor);
            } else {
                isCustomColor = true;
                customColorValue = uint(colorVal);
                ApplyColor(customColorValue);
            }
        }

        // 引擎原生的 HUD 颜色更新回调！(读档、切图、改设置时，引擎会自动呼叫它)
        public function onApplyColorChange(hudColor:uint):void {
            lastHudColor = hudColor;
            if (!isCustomColor) {
                ApplyColor(hudColor);
            }
        }

        // 执行染色
        private function ApplyColor(color:uint):void {
            fnvColor = color;
            if (fnvText != null) RenderFNVWidget();
            if (Widget_mc != null) Widget_mc.SetHudColor(color);
            var ct:ColorTransform = new ColorTransform();
            ct.color = color;
            
            // 将整个小部件和排障图标染成主题色 (因为只染内部的 Widget_mc，外部的阴影绝对不发绿！)
            if (Widget_mc != null) Widget_mc.transform.colorTransform = ct;
            if (UnjamWidget_mc != null) UnjamWidget_mc.transform.colorTransform = ct;
            
            // 确保红条不被染成绿的，把它强制拉回暗红色！
            if (redBarContainer != null) {
                var redCt:ColorTransform = new ColorTransform();
                redCt.color = 0x8B0000; // 统一改成暗红色，解决颜色跳变瑕疵
                redBarContainer.transform.colorTransform = redCt;
            }
        }    
    
        private function onStageResize(e:Event):void {
            UpdateUnjamPosition();
            UpdateHeatPosition();
        }

        private function UpdateUnjamPosition():void {
            if (UnjamWidget_mc != null && stage != null) {
                UnjamWidget_mc.x = (stage.stageWidth / 2) + mcmOffsetX;
                UnjamWidget_mc.y = (stage.stageHeight / 2) + mcmOffsetY;
            }
        }

        public function SetUnjamTransform(offsetX:Number, offsetY:Number, scale:Number):void {
            mcmOffsetX = offsetX;
            mcmOffsetY = offsetY;
            if (UnjamWidget_mc != null) {
                UnjamWidget_mc.scaleX = scale;
                UnjamWidget_mc.scaleY = scale;
            }
            UpdateUnjamPosition();
        }

        public function SetWidgetTransform(posX:Number, posY:Number, scale:Number):void {
            // MCM's positioner moves the loaded SWF root (plch). Native C++
            // invokes this same method, so the root is the shared coordinate
            // contract for the preview and the live CND widget.
            this.x = posX;
            this.y = posY;
            this.scaleX = scale;
            this.scaleY = scale;
        }

        public function SetCndStyle(useFNVStyle:Boolean):void {
            fnvStyle = useFNVStyle;
            if (Widget_mc != null) {
                Widget_mc.SetCndStyle(useFNVStyle);
            }
            if (fnvStyle) {
                EnsureFNVWidget();
                if (Widget_mc != null) {
                    fnvWidget.x = Widget_mc.x;
                    fnvWidget.y = Widget_mc.y;
                    fnvWidget.scaleX = Widget_mc.scaleX;
                    fnvWidget.scaleY = Widget_mc.scaleY;
                }
            }
            ApplyFNVVisibility();
            RenderFNVWidget();
        }

        public function SetUnjamVisible(isVisible:Boolean):void {
            if (UnjamWidget_mc != null) {
                UnjamWidget_mc.visible = isVisible;
                if (isVisible) {
                    UpdateUnjamPosition(); 
                    UnjamWidget_mc.alpha = 1.0;
                    SetUnjamProgress(0.0);
                } else {
                    UnjamWidget_mc.alpha = 0.0;
                }
            }
        }

        public function SetUnjamProgress(percent:Number):void {
            if (UnjamWidget_mc == null) return;
            
            percent = Math.max(0.0, Math.min(1.0, percent));
            var g:flash.display.Graphics = UnjamWidget_mc.graphics;
            g.clear();
            
            var rot:Number = -percent * Math.PI; 
            var holeR:Number = 28;        
            var gearBodyR:Number = 42;    
            var gearTeethR:Number = 50;   
            var tCount:int = 8;            
            var tOuterHalfWidth:Number = 0.19; 
            var tInnerHalfWidth:Number = 0.27; 

            g.lineStyle(0, 0, 0); 
            g.beginFill(0xFFFFFF, 0.85); 

            var first:Boolean = true;
            for (var t:int = 0; t < tCount; t++) {
                var ang:Number = rot + t * (Math.PI * 2 / tCount);
                
                var a1_inner:Number = ang - tInnerHalfWidth;
                var a2_inner:Number = ang + tInnerHalfWidth;
                var a1_outer:Number = ang - tOuterHalfWidth;
                var a2_outer:Number = ang + tOuterHalfWidth;

                if (first) {
                    g.moveTo(Math.cos(a1_inner) * gearBodyR, Math.sin(a1_inner) * gearBodyR);
                    first = false;
                } else {
                    g.lineTo(Math.cos(a1_inner) * gearBodyR, Math.sin(a1_inner) * gearBodyR);
                }

                g.lineTo(Math.cos(a1_outer) * gearTeethR, Math.sin(a1_outer) * gearTeethR); 
                g.lineTo(Math.cos(a2_outer) * gearTeethR, Math.sin(a2_outer) * gearTeethR); 
                g.lineTo(Math.cos(a2_inner) * gearBodyR, Math.sin(a2_inner) * gearBodyR);   
            }
            var startA1:Number = rot - tInnerHalfWidth;
            g.lineTo(Math.cos(startA1) * gearBodyR, Math.sin(startA1) * gearBodyR);

            g.moveTo(holeR, 0);
            var holeSteps:int = 60;
            var holeStepA:Number = -(Math.PI * 2) / holeSteps; 
            for(var k:int = 1; k <= holeSteps; k++) {
                var hA:Number = k * holeStepA;
                g.lineTo(Math.cos(hA) * holeR, Math.sin(hA) * holeR);
            }
            g.endFill(); 

            var progR:Number = 25; 
            g.lineStyle(4, 0xFFFFFF, 0.25); 
            g.drawCircle(0, 0, progR);

            if (percent > 0) {
                g.lineStyle(4, 0xFFFFFF, 1.0); 
                var startA:Number = -Math.PI / 2; 
                var currentA:Number = percent * Math.PI * 2;
                
                g.moveTo(Math.cos(startA) * progR, Math.sin(startA) * progR);
                
                var steps:int = Math.max(1, Math.floor(percent * 100));
                var aStep:Number = currentA / steps;
                for (var i:int = 1; i <= steps; i++) {
                    var cA:Number = startA + (i * aStep);
                    g.lineTo(Math.cos(cA) * progR, Math.sin(cA) * progR);
                }
            }
        }

        public function SetDurability(percent:Number, points:Number = -1):void {
            fnvPercent = Math.max(0, Math.min(2, percent));
            if (Widget_mc != null) {
                Widget_mc.SetDurability(percent, points);
            }
            RenderFNVWidget();
        }

        public function SetJamThreshold(percent:Number):void {
            if (Widget_mc != null) {
                Widget_mc.SetJamThreshold(percent);
            }
        }

        public function SetFNVThreshold(percent:Number):void {
            fnvThreshold = Math.max(0, Math.min(1, percent));
            RenderFNVWidget();
        }

        public function SetTextVisible(isVisible:Boolean):void {
            fnvTextVisible = isVisible;
            if (Widget_mc != null) {
                Widget_mc.SetTextVisible(isVisible);
            }
            if (fnvText != null) fnvText.visible = isVisible;
            if (fnvMultiplier != null) fnvMultiplier.visible = isVisible && fnvPercent > 1.0;
        }

        public function SetWidgetVisible(isVisible:Boolean):void {
            if (isVisible) {
                targetAlpha = 1.0; 
            } else {
                targetAlpha = 0.0; 
                if (Widget_mc != null) {
                    Widget_mc.alpha = 0.0;
                    Widget_mc.visible = false;
                }
                if (fnvWidget != null) {
                    fnvWidget.alpha = 0.0;
                    fnvWidget.visible = false;
                }
                if (heatWidget != null) {
                    heatWidget.alpha = 0.0;
                    heatWidget.visible = false;
                }
            }
            ApplyFNVVisibility();
        }

        private function onEnterFrame(e:Event):void {
            if (Widget_mc != null && targetAlpha > 0.0) {
                if (fnvStyle) {
                    Widget_mc.visible = false;
                    if (fnvWidget != null) {
                        fnvWidget.visible = true;
                        fnvWidget.alpha += (targetAlpha - fnvWidget.alpha) * 0.35;
                        if (Math.abs(targetAlpha - fnvWidget.alpha) < 0.01) fnvWidget.alpha = targetAlpha;
                    }
                } else {
                    Widget_mc.visible = true;
                    Widget_mc.alpha += (targetAlpha - Widget_mc.alpha) * 0.35;
                    if (Math.abs(targetAlpha - Widget_mc.alpha) < 0.01) {
                        Widget_mc.alpha = targetAlpha;
                    }
                }
            }
            
            // 同步安全屋的坐标和透明度，紧贴绿条
            if (Widget_mc != null && redBarContainer != null) {
                redBarContainer.x = Widget_mc.x;
                redBarContainer.y = Widget_mc.y;
                redBarContainer.scaleX = Widget_mc.scaleX;
                redBarContainer.scaleY = Widget_mc.scaleY;
                redBarContainer.alpha = Widget_mc.alpha;
                
                // 【核心修改】：如果内部的红条被判定隐藏(过量维修状态)，外面的壳子也要跟着隐藏！
                var jamVisible:Boolean = (Widget_mc.jamThresholdBar != null) ? Widget_mc.jamThresholdBar.visible : true;
                redBarContainer.visible = Widget_mc.visible && jamVisible;
            }

            if (heatWidget != null) {
                UpdateHeatPosition();
                heatWidget.visible = heatStateVisible && targetAlpha > 0.0;
                if (heatWidget.visible) {
                    heatWidget.alpha += (targetAlpha - heatWidget.alpha) * 0.35;
                    if (Math.abs(targetAlpha - heatWidget.alpha) < 0.01) heatWidget.alpha = targetAlpha;
                } else {
                    heatWidget.alpha = 0.0;
                }
            }
        }
    }
}
