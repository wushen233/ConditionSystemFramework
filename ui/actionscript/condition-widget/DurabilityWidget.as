package {
    import flash.display.MovieClip;
    import flash.text.TextField;
    import flash.events.Event;

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

        public function DurabilityWidget() {
            super();
            if (trailBar != null) trailBar.alpha = 0.4;
            if (tierTxt != null) tierTxt.visible = false; // 初始隐藏
            SetDurability(1.0, 1000); 
            addEventListener(Event.ENTER_FRAME, onEnterFrame);
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

            // 1. 周目计算逻辑
            var isTier2:Boolean = (targetPercent > 1.0);
            var visualPct:Number = isTier2 ? (targetPercent - 1.0) : targetPercent;

            // 2. 设置主进度条
            if (cndBar != null) cndBar.scaleX = visualPct;
            
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
                jamThresholdBar.visible = !isTier2;
            }

            if (cndText != null) {
                var displayValue:int = currentPoints >= 0 ? Math.floor(currentPoints) : Math.floor(targetPercent * 1000);
                cndText.text = "CND: " + displayValue;
            }
        }

        public function SetJamThreshold(percent:Number):void {
            percent = Math.max(0.0, Math.min(1.0, percent));
            if (jamThresholdBar != null) jamThresholdBar.scaleX = percent;
        }

        public function SetTextVisible(isVisible:Boolean):void {
            if (cndText != null) cndText.visible = isVisible;
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
                
                trailBar.scaleX = visualTrailPct;
            }
        }
    }
}