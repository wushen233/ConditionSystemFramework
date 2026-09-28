package {
    import flash.display.MovieClip;
    import flash.display.DisplayObject;
    import flash.display.Bitmap;
    import flash.text.TextField;
    import flash.text.TextFormat;
    import flash.events.Event;
    import flash.geom.ColorTransform;

    public class DurabilityWidget extends MovieClip {
        
        public var cndBar:MovieClip;
        public var trailBar:MovieClip;
        public var jamThresholdBar:MovieClip; 
        public var cndText:TextField;
        
        // 【新增】：绑定你在 FLA 中创建的那个用来显示 x2 的动态文本框
        public var tierTxt:TextField; 

        private var targetPercent:Number = 1.0;
        private var currentTrailPercent:Number = 1.0;
        private var delayFrames:int = 0;
        private var currentPoints:Number = 1000;
        private var currentThreshold:Number = 0.5;
        private var prismaStyle:Boolean = false;
        private var prismaBars:MovieClip;
        private var prismaBarBg:MovieClip;
        private var prismaTrailBar:MovieClip;
        private var prismaFillBar:MovieClip;
        private var prismaThresholdMarker:MovieClip;
        private var prismaBarX:Number = 48;
        private var prismaBarY:Number = 5;
        private var prismaBarW:Number = 65;
        private var prismaBarH:Number = 14;
        private var hudColor:uint = 0x18FF00;
        private var textVisible:Boolean = true;

        public function DurabilityWidget() {
            super();
            if (trailBar != null) trailBar.alpha = 0.4;
            if (cndText != null) {
                // The FLA field used a wildcard font face ($MAIN_Font*),
                // which renders numeric glyphs as boxes in this standalone
                // HUD movie. Use the same HUD font alias as the runtime
                // TextFields and let Scaleform resolve it at runtime.
                cndText.defaultTextFormat = new TextFormat("$MAIN_Font_Bold", 10, 0xFFFFFF, true);
                cndText.embedFonts = false;
                cndText.selectable = false;
            }
            if (tierTxt != null) tierTxt.visible = false; // 初始隐藏
            SetDurability(1.0, 1000); 
            addEventListener(Event.ENTER_FRAME, onEnterFrame);
        }

        public function SetCndStyle(usePrismaStyle:Boolean):void {
            prismaStyle = usePrismaStyle;
            if (prismaStyle) {
                EnsurePrismaBars();
                SetFlaBorderVisible(false);
                if (cndBar != null) cndBar.visible = false;
                if (trailBar != null) trailBar.visible = false;
                if (jamThresholdBar != null) jamThresholdBar.visible = false;
                if (cndText != null) cndText.visible = false;
                if (tierTxt != null) tierTxt.visible = false;
                RenderPrismaBars();
            } else {
                if (prismaBars != null) prismaBars.visible = false;
                SetFlaBorderVisible(true);
                if (cndBar != null) cndBar.visible = true;
                if (trailBar != null) trailBar.visible = true;
                if (cndText != null) {
                    cndText.visible = textVisible;
                    cndText.text = "CND: " + Math.floor(currentPoints);
                }
                UpdateVanillaThresholdAndTier();
            }
        }

        private function SetFlaBorderVisible(isVisible:Boolean):void {
            for (var i:int = 0; i < numChildren; i++) {
                var child:DisplayObject = getChildAt(i);
                if (child is Bitmap) {
                    child.visible = isVisible;
                }
            }
        }

        public function SetHudColor(color:uint):void {
            hudColor = color;
            ApplyFlaColor(color);
            RenderPrismaBars();
        }

        private function ApplyFlaColor(color:uint):void {
            var colorTransform:ColorTransform = new ColorTransform();
            colorTransform.color = color;

            if (cndBar != null) cndBar.transform.colorTransform = colorTransform;
            if (trailBar != null) trailBar.transform.colorTransform = colorTransform;
            if (cndText != null) cndText.textColor = color;
            if (tierTxt != null) tierTxt.textColor = color;

            var label:TextField = getChildByName("Lbl_CND") as TextField;
            if (label != null) label.textColor = color;

            for (var i:int = 0; i < numChildren; i++) {
                var child:DisplayObject = getChildAt(i);
                if (!(child == jamThresholdBar || child == prismaBars || child is TextField)) {
                    child.transform.colorTransform = colorTransform;
                }
            }

            if (jamThresholdBar != null) {
                var thresholdTransform:ColorTransform = new ColorTransform();
                thresholdTransform.color = 0x8B0000;
                jamThresholdBar.transform.colorTransform = thresholdTransform;
            }
        }

        private function EnsurePrismaBars():void {
            if (prismaBars != null) {
                prismaBars.visible = true;
                return;
            }

            if (cndBar != null) {
                prismaBarX = cndBar.x;
                prismaBarY = cndBar.y;
                prismaBarW = cndBar.width / (cndBar.scaleX == 0 ? 1 : cndBar.scaleX);
                prismaBarH = cndBar.height;
            }

            prismaBars = new MovieClip();
            prismaBarBg = new MovieClip();
            prismaTrailBar = new MovieClip();
            prismaFillBar = new MovieClip();
            prismaThresholdMarker = new MovieClip();
            prismaBars.x = prismaBarX;
            prismaBars.y = prismaBarY;
            prismaBars.addChild(prismaBarBg);
            prismaBars.addChild(prismaTrailBar);
            prismaBars.addChild(prismaFillBar);
            prismaBars.addChild(prismaThresholdMarker);
            addChildAt(prismaBars, 0);
        }

        private function DrawRect(target:MovieClip, color:uint, alpha:Number, width:Number, height:Number):void {
            target.graphics.clear();
            target.graphics.beginFill(color, alpha);
            target.graphics.drawRect(0, 0, width, height);
            target.graphics.endFill();
        }

        private function DrawThresholdMarker():void {
            prismaThresholdMarker.graphics.clear();
            prismaThresholdMarker.graphics.beginFill(0x1C160F, 0.94);
            prismaThresholdMarker.graphics.moveTo(-3, 0);
            prismaThresholdMarker.graphics.lineTo(3, 0);
            prismaThresholdMarker.graphics.lineTo(0, 5);
            prismaThresholdMarker.graphics.lineTo(-3, 0);
            prismaThresholdMarker.graphics.moveTo(-3, prismaBarH);
            prismaThresholdMarker.graphics.lineTo(3, prismaBarH);
            prismaThresholdMarker.graphics.lineTo(0, prismaBarH - 5);
            prismaThresholdMarker.graphics.lineTo(-3, prismaBarH);
            prismaThresholdMarker.graphics.endFill();
            prismaThresholdMarker.x = prismaBarW * currentThreshold;
            prismaThresholdMarker.visible = targetPercent <= 1.0;
        }

        private function RenderPrismaBars():void {
            if (!prismaStyle || prismaBars == null) return;
            prismaBars.visible = true;
            DrawRect(prismaBarBg, 0x1C160F, 0.48, prismaBarW, prismaBarH);
            DrawRect(prismaTrailBar, hudColor, 0.28, prismaBarW, prismaBarH);
            DrawRect(prismaFillBar, hudColor, 1.0, prismaBarW, prismaBarH);
            prismaTrailBar.scaleX = Math.max(0, Math.min(1, currentTrailPercent > 1 ? currentTrailPercent - 1 : currentTrailPercent));
            prismaFillBar.scaleX = Math.max(0, Math.min(1, targetPercent > 1 ? targetPercent - 1 : targetPercent));
            DrawThresholdMarker();
        }

        private function UpdateVanillaThresholdAndTier():void {
            var isTier2:Boolean = (targetPercent > 1.0);
            if (tierTxt != null) {
                tierTxt.text = "x2";
                tierTxt.visible = isTier2;
            }
            if (jamThresholdBar != null) {
                jamThresholdBar.visible = !isTier2;
                jamThresholdBar.scaleX = currentThreshold;
            }
        }

        public function SetDurability(percent:Number, currentPoints:Number = -1):void {
            // 【核心修改】：解除 1.0 限制，允许接收 C++ 传来的最高 2.0 (200%) 数据
            percent = Math.max(0.0, Math.min(2.0, percent));
            
            if (percent < targetPercent) {
                if (Math.abs(currentTrailPercent - targetPercent) < 0.005) {
                    currentTrailPercent = targetPercent; 
                }
                delayFrames = 5; 
            } else if (percent > targetPercent) {
                currentTrailPercent = percent;
            }
            
            targetPercent = percent;
            if (currentPoints >= 0) this.currentPoints = currentPoints;

            // 1. 周目计算逻辑
            var isTier2:Boolean = (targetPercent > 1.0);
            var visualPct:Number = isTier2 ? (targetPercent - 1.0) : targetPercent;

            // 2. 设置主进度条
            if (prismaStyle) {
                RenderPrismaBars();
            } else if (cndBar != null) {
                cndBar.scaleX = visualPct;
            }
            
            // 3. 控制 x2 文字的显示与隐藏
            if (tierTxt != null) {
                if (isTier2) {
                    tierTxt.text = "x2";
                    tierTxt.visible = true;
                } else {
                    tierTxt.visible = false;
                }
            }

            // 4. 过量护盾状态下，隐藏卡壳红条！
            if (jamThresholdBar != null) {
                jamThresholdBar.visible = !prismaStyle && !isTier2;
            }

            if (cndText != null) {
                var displayValue:int = currentPoints >= 0 ? Math.floor(currentPoints) : Math.floor(targetPercent * 1000);
                cndText.text = prismaStyle ? "CND" : "CND: " + displayValue;
            }
        }

        public function SetJamThreshold(percent:Number):void {
            percent = Math.max(0.0, Math.min(1.0, percent));
            currentThreshold = percent;
            if (prismaStyle) {
                RenderPrismaBars();
            } else if (jamThresholdBar != null) {
                jamThresholdBar.scaleX = percent;
            }
        }

        public function SetTextVisible(isVisible:Boolean):void {
            textVisible = isVisible;
            if (cndText != null) cndText.visible = isVisible && !prismaStyle;
        }

        private function onEnterFrame(e:Event):void {
            if (trailBar != null) {
                if (currentTrailPercent > targetPercent) {
                    if (delayFrames > 0) delayFrames--; 
                    else {
                        currentTrailPercent += (targetPercent - currentTrailPercent) * 0.25;
                        if (currentTrailPercent - targetPercent < 0.001) currentTrailPercent = targetPercent;
                    }
                } else {
                    currentTrailPercent = targetPercent;
                }
                
                // 【核心修改】：拖影白条也必须计算周目！这样从 110% 掉到 90% 时，拖影会完美地跨界缩短
                var isTrailTier2:Boolean = (currentTrailPercent > 1.0);
                var visualTrailPct:Number = isTrailTier2 ? (currentTrailPercent - 1.0) : currentTrailPercent;
                
                if (prismaStyle) {
                    RenderPrismaBars();
                } else if (trailBar != null) {
                    trailBar.scaleX = visualTrailPct;
                }
            }
        }
    }
}
