package {
    import flash.display.MovieClip;
    import flash.events.Event;
    import flash.filters.DropShadowFilter; 
    import flash.geom.ColorTransform;

    public class ConditionWidget extends MovieClip {
        
        public var Widget_mc:DurabilityWidget; 
        public var UnjamWidget_mc:MovieClip;
        
        // 用来装载抽离出的红条的安全屋
        public var redBarContainer:MovieClip;
        
        // 专门用来隔离颜色、纯粹挂载黑色投影的外壳
        public var shadowContainer:MovieClip;

        private var targetAlpha:Number = 0.0;
        private var mcmOffsetX:Number = 0;
        private var mcmOffsetY:Number = 0;

        public function ConditionWidget() {
            super();
            
            if (Widget_mc != null) {
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
                UnjamWidget_mc.alpha = 0.0;
                UnjamWidget_mc.visible = false;
                // 原封不动保留你原本排障齿轮的投影参数
                var uiShadow:DropShadowFilter = new DropShadowFilter(1.5, 45, 0x000000, 0.9, 2.5, 2.5, 3.0, 1);
                UnjamWidget_mc.filters = [uiShadow];
            }
            
            addEventListener(Event.ENTER_FRAME, onEnterFrame);
            addEventListener(Event.ADDED_TO_STAGE, onAddedToStage);
        }

        private function onAddedToStage(e:Event):void {
            removeEventListener(Event.ADDED_TO_STAGE, onAddedToStage);
            stage.addEventListener(Event.RESIZE, onStageResize);
            UpdateUnjamPosition();
        }

        // ==========================================
        // 【全新安全颜色管理模块】
        // ==========================================
        private var isCustomColor:Boolean = false;
        private var customColorValue:uint = 0xFFFFFF;

        // C++ 用来强制覆盖为 MCM 自定义颜色的接口
        public function SetCustomColor(colorVal:Number):void {
            if (colorVal < 0) {
                isCustomColor = false;
            } else {
                isCustomColor = true;
                customColorValue = uint(colorVal);
                ApplyColor(customColorValue);
            }
        }

        // 引擎原生的 HUD 颜色更新回调！(读档、切图、改设置时，引擎会自动呼叫它)
        public function onApplyColorChange(hudColor:uint):void {
            if (!isCustomColor) {
                ApplyColor(hudColor);
            }
        }

        // 执行染色
        private function ApplyColor(color:uint):void {
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
            if (Widget_mc != null) {
                Widget_mc.SetDurability(percent, points);
            }
        }

        public function SetJamThreshold(percent:Number):void {
            if (Widget_mc != null) {
                Widget_mc.SetJamThreshold(percent);
            }
        }

        public function SetTextVisible(isVisible:Boolean):void {
            if (Widget_mc != null) {
                Widget_mc.SetTextVisible(isVisible);
            }
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
            }
        }

        private function onEnterFrame(e:Event):void {
            if (Widget_mc != null && targetAlpha > 0.0) {
                Widget_mc.visible = true;
                Widget_mc.alpha += (targetAlpha - Widget_mc.alpha) * 0.35;
                if (Math.abs(targetAlpha - Widget_mc.alpha) < 0.01) {
                    Widget_mc.alpha = targetAlpha;
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
        }
    }
}