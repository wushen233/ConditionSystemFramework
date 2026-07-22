package {
    import Shared.GlobalFunc;
    import flash.display.DisplayObject;
    import flash.display.DisplayObjectContainer;
    import flash.display.Bitmap;
    import flash.display.BitmapData;
    import flash.display.MovieClip;
    import flash.display.Sprite;
    import flash.display.Shape;
    import flash.events.Event;
    import flash.events.MouseEvent;
    import flash.text.TextField;
    import flash.text.TextFormat;
    import flash.text.TextFormatAlign;
    import flash.text.AntiAliasType;
    import flash.geom.Rectangle;
    import flash.geom.ColorTransform;
    import flash.geom.Matrix;
    import flash.utils.Dictionary;
    import flash.utils.getDefinitionByName;
    import flash.utils.getQualifiedClassName;
    import scaleform.gfx.TextFieldEx;
    import scaleform.gfx.Extensions;

    public dynamic class JuryRiggingMenu extends MovieClip {

        public var JuryRigging_Callback:Function = null; 
        public var UpdateRepairKitsState_Callback:Function = null;
        
        private var originalMaterialsArray:Array = []; 
        private var materialsArray:Array = [];
        private var originalKitsArray:Array = []; 
        private var kitsArray:Array = [];
        
        public var showKitsMode:Boolean = false;
        private var totalKitsCount:int = 0; 
        
        private var selectedIndex:int = 0;
        private var currentTargetUID:Number = 0;
        private var targetBasePct:Number = 0.0;
        private var targetName:String = "";
        private var targetFav:Boolean = false;
        private var targetLeg:Boolean = false;
        private var targetSearch:Boolean = false;
        private var targetThresholdPct:Number = -1.0;
        private var targetThresholdPct2:Number = -1.0;
        
        private var playerMaxLimit:Number = 1.0;
        private var juryRiggingSkillStr:String = "";
        
        private var _stolenNameCache:Object = {};
        private var _uiStates:Dictionary = new Dictionary(true);
        private var isProcessingRepair:Boolean = false;
        
        private var _isMaxLimitFormatted:Boolean = false;
        
        private var themeColor:uint = 0xFFFFFF;        
        private var listTextColor:uint = 0xFFFFFF;    
        private var selectedTextColor:uint = 0x000000;
        private var menuTitleStr:String = "$Repair";

        // 用于保存反射窃取来的 Pipboy 标题格式
        private var pipboyHeaderFormat:TextFormat = null;
        private var pipboyHeaderFilters:Array = null;
        private var pipboyHeaderEmbedFonts:Boolean = false;
        private var pipboyHeaderAntiAlias:String = AntiAliasType.NORMAL;

        private var mainCanvas:MovieClip;
        private var listContainer:MovieClip;
        private var titleTxt:TextField;
        private var titleFrame:Shape; 
        private var headerTxt:TextField;
        private var headerEntryMC:MovieClip; 
        
        private var headerMat:MovieClip;
        private var headerPct:MovieClip;
        private var headerCond:MovieClip;
        
        private var currentSortCol:String = "name"; 
        private var currentSortDir:int = 0; 
        
        private var targetInfoBar:MovieClip; 
        private var previewStatsPanel:MovieClip;
        
        public var needsRefreshMacro:Boolean = false;
        public var macroIndex:int = -1;
        public var pendingReselectFrames:int = 0;
        
        private var repairIcon:MovieClip;

        private var pulseAlpha:Number = 0.2;
        private var pulseDir:Number = 0.025; 

        private var titleX:Number = 80; private var titleY:Number = 50; private var titleW:Number = 460; private var titleH:Number = 40;
        private var previewX:Number = 80; private var previewY:Number = 100; private var previewW:Number = 460; private var previewH:Number = 47;
        private var previewBarWidth:Number = 120; 
        private var listX:Number = 80; private var listY:Number = 150; private var listW:Number = 460; private var listH:Number = 448;     
        private var statsX:Number = 620; private var statsY:Number = 365; private var statsW:Number = 280; private var statsH:Number = 250;
        
        private var barX:Number = 0; private var barY:Number = 0; private var barW:Number = 30; private var barH:Number = 100;
        
        private var maxVisibleItems:int = 14; 
        private var rowH:Number = 32;       
        private var headerH:Number = 18;
        private var listOffset:int = 0;
        
        private var colNameX:Number = 40; 
        private var colNameW:Number = 195; 
        private var colPctX:Number = 285; private var colPctW:Number = 110;
        private var colCondX:Number = 403; private var colCondW:Number = 92;
        private var cndBarFixedW:Number = 92;
        private var cndBarFixedH:Number = 20;
        private var previewCndBarW:Number = 82;
        private var previewCndBarH:Number = 18;
        private var listHighlightRightExtension:Number = 14;
        private var juryStatsOffsetX:Number = 0;
        private var juryStatsOffsetY:Number = 0;
        private var juryStatsRowSpacing:Number = 92;
        private var juryStatsColorMode:int = 0;
        
        private var scrollBarBg:Sprite;
        private var scrollBarThumb:Sprite;
        private var isDragging:Boolean = false;
        private var isThumbHovered:Boolean = false;
        private var dragStartY:Number = 0;
        private var startThumbY:Number = 0;
        
        private var CachedEntryClass:Class = null;
        private var isEntryClassResolved:Boolean = false;

        public function JuryRiggingMenu() {
            super();
            Extensions.enabled = true;
            InitLayout();
            
            this["JuryRigging_OnReady"] = this.HandleOnReady;
            this["ReceiveJuryRiggingData"] = this.HandleReceiveData;

            this.addEventListener(Event.ADDED_TO_STAGE, onAddedToStage);
            this.addEventListener(Event.REMOVED_FROM_STAGE, onRemovedFromStage);
        }
        
        private function get activeArray():Array { return showKitsMode ? kitsArray : materialsArray; }
        private function get activeOriginalArray():Array { return showKitsMode ? originalKitsArray : originalMaterialsArray; }

        private function PlayUISound(soundName:String):void {
            try {
                if (Object(root).PlaySound_Call != null) Object(root).PlaySound_Call(soundName);
                else if (this.stage && this.stage.getChildAt(0) && this.stage.getChildAt(0)["PlaySound_Call"] != null) this.stage.getChildAt(0)["PlaySound_Call"](soundName);
                else if (this["JuryRigging_Callback"] != null) this["JuryRigging_Callback"]("PlaySound", soundName);
            } catch(e:Error) {}
        }

        private function onAddedToStage(e:Event):void {
            this.removeEventListener(Event.ADDED_TO_STAGE, onAddedToStage);
            this.addEventListener(Event.ENTER_FRAME, onEnterFrameLoop);
            this.stage.addEventListener(MouseEvent.MOUSE_WHEEL, onMouseWheel);
            
            try {
                var realRoot:Object = this.stage.getChildAt(0);
                if (realRoot != null) {
                    realRoot["JuryRigging_OnReady"] = this.HandleOnReady;
                    realRoot["ReceiveJuryRiggingData"] = this.HandleReceiveData;
                }
            } catch(err:Error) { }
            
            if (this["JuryRigging_Callback"] != null) {
                this["JuryRigging_Callback"]("RequestData");
            } else {
                try {
                    var r:Object = this.stage.getChildAt(0);
                    if (r != null && r["ConditionSystem_Call"] != null) r["ConditionSystem_Call"]("RequestData");
                    else if (Object(root).ConditionSystem_Call != null) Object(root).ConditionSystem_Call("RequestData");
                } catch(err:Error) {}
            }
        }

        private function onRemovedFromStage(e:Event):void {
            this.removeEventListener(Event.ENTER_FRAME, onEnterFrameLoop);
            if (this.stage) {
                this.stage.removeEventListener(MouseEvent.MOUSE_WHEEL, onMouseWheel);
                this.stage.removeEventListener(MouseEvent.MOUSE_MOVE, onThumbDrag);
                this.stage.removeEventListener(MouseEvent.MOUSE_UP, onThumbMouseUp);
            }
        }

        private function findTargetObject(container:DisplayObjectContainer, targetName:String):DisplayObject {
            if (!container) return null;
            var direct:DisplayObject = container.getChildByName(targetName);
            if (direct != null) return direct;
            for (var i:int = 0; i < container.numChildren; i++) {
                var child:DisplayObjectContainer = container.getChildAt(i) as DisplayObjectContainer;
                if (child != null && child != mainCanvas && child.name != "mainCanvas") { 
                    var found:DisplayObject = findTargetObject(child, targetName);
                    if (found != null) return found;
                }
            }
            return null;
        }

        private function findTargetObjectDeep(container:DisplayObjectContainer, targetName:String):DisplayObject {
            if (!container) return null;
            var direct:DisplayObject = container.getChildByName(targetName);
            if (direct != null) return direct;
            for (var i:int = 0; i < container.numChildren; i++) {
                var child:DisplayObjectContainer = container.getChildAt(i) as DisplayObjectContainer;
                if (child != null) {
                    var found:DisplayObject = findTargetObjectDeep(child, targetName);
                    if (found != null) return found;
                }
            }
            return null;
        }

        private function InitLayout():void {
            if (mainCanvas != null) return;
            mainCanvas = new MovieClip();
            mainCanvas.name = "mainCanvas";
            mainCanvas.x = 0; mainCanvas.y = 0;
            mainCanvas.graphics.beginFill(0x000000, 0);
            mainCanvas.graphics.drawRect(-1000, -1000, 3000, 3000);
            mainCanvas.graphics.endFill();
            addChild(mainCanvas);

            var tb:DisplayObject = findTargetObject(this, "titleBounds_mc");
            if (tb != null) {
                var tRect:Rectangle = tb.getBounds(this);
                titleX = tRect.x; titleY = tRect.y; titleW = Math.max(50, tRect.width); titleH = Math.max(20, tRect.height);
                tb.visible = false; 
            }

            var pb:DisplayObject = findTargetObject(this, "previewBounds_mc");
            if (pb != null) {
                var pRect:Rectangle = pb.getBounds(this);
                previewX = pRect.x; previewY = pRect.y; previewW = Math.max(100, pRect.width); previewH = Math.max(20, pRect.height);
                pb.visible = false; 
            }

            var lb:DisplayObject = findTargetObject(this, "listBounds_mc");
            if (lb != null) {
                var lRect:Rectangle = lb.getBounds(this);
                listX = lRect.x; listY = lRect.y; listW = Math.max(150, lRect.width); listH = Math.max(50, lRect.height);
                lb.visible = false; 
            }

            var statb:DisplayObject = findTargetObject(this, "statPreviewBounds_mc");
            var useStatBottomLeftAnchor:Boolean = statb != null;
            if (statb == null) statb = findTargetObject(this, "statsBounds_mc");
            if (statb == null) statb = findTargetObject(this, "itemCardBounds_mc");
            if (statb == null) statb = findTargetObject(this, "cardBounds_mc");
            if (statb != null) {
                var stRect:Rectangle = statb.getBounds(this);
                statsW = Math.min(stRect.width, 220);
                statsH = Math.max(120, stRect.height);
                if (useStatBottomLeftAnchor) {
                    statsX = statb.x;
                    statsY = statb.y - statsH;
                } else {
                    statsX = stRect.x;
                    statsY = stRect.y;
                }
                statb.visible = false;
            } else {
                statsX = Math.max(listX + listW + 70, 585);
                statsY = Math.max(listY + listH - statsH + 14, 330);
            }

            rowH = listH / maxVisibleItems; 
            headerH = rowH * 0.6;

            var textY:Number = listY + (headerH - 22) / 2; 

            headerMat = CreateHeaderButton("$CSF_HeaderItem", listX + colNameX, textY, colNameW, 26, "left", "name");
            mainCanvas.addChild(headerMat);
            headerPct = CreateHeaderButton("$CSF_HeaderRepairOverflow", listX + colPctX, textY, colPctW, 26, "center", "addPct");
            mainCanvas.addChild(headerPct);
            headerCond = CreateHeaderButton("$CSF_ConditionShortName", listX + colCondX, textY, colCondW, 26, "center", "pct");
            mainCanvas.addChild(headerCond);

            var sb:DisplayObject = findTargetObject(this, "scrollBounds_mc");
            if (sb != null) {
                var sRect:Rectangle = sb.getBounds(this);
                barX = sRect.x; barY = sRect.y; barW = sRect.width; barH = sRect.height;
                sb.visible = false; 
            } else {
                barW = 12; barX = listX - barW - 15; barY = listY + headerH; barH = listH - headerH;
            }

            titleFrame = new Shape();
            mainCanvas.addChild(titleFrame);

            titleTxt = CreateTextField(titleX, titleY, titleW, titleH, 34, themeColor, true);
            mainCanvas.addChild(titleTxt);
            
            if (titleTxt != null) {
                titleTxt.htmlText = menuTitleStr;
                titleTxt.setTextFormat(titleTxt.defaultTextFormat);
            }

            targetInfoBar = new MovieClip(); mainCanvas.addChild(targetInfoBar);
            previewStatsPanel = new MovieClip(); mainCanvas.addChild(previewStatsPanel);
            
            scrollBarBg = new Sprite(); scrollBarBg.buttonMode = true; 
            scrollBarBg.addEventListener(MouseEvent.MOUSE_DOWN, onTrackClick);
            mainCanvas.addChild(scrollBarBg);
            
            scrollBarThumb = new Sprite(); scrollBarThumb.buttonMode = true; 
            scrollBarThumb.addEventListener(MouseEvent.MOUSE_OVER, function(e:Event):void { isThumbHovered = true; UpdateScrollbar(); });
            scrollBarThumb.addEventListener(MouseEvent.MOUSE_OUT, function(e:Event):void { isThumbHovered = false; UpdateScrollbar(); });
            scrollBarThumb.addEventListener(MouseEvent.MOUSE_DOWN, onThumbMouseDown);
            mainCanvas.addChild(scrollBarThumb);

            listContainer = new MovieClip();
            listContainer.x = listX; listContainer.y = listY + headerH; 
            listContainer.addEventListener(MouseEvent.MOUSE_MOVE, onListMouseMove);
            mainCanvas.addChild(listContainer);
        }

        private function getPipboyInvPage():Object {
            var curr:DisplayObject = this;
            while (curr) {
                if (curr.hasOwnProperty("List_mc") && curr["List_mc"] != null) {
                    if (curr.hasOwnProperty("List_mc") && curr["List_mc"].hasOwnProperty("entryList") || curr["List_mc"].hasOwnProperty("dataProvider")) {
                        return curr; 
                    }
                }
                curr = curr.parent;
            }
            return null;
        }

        private function cleanStringForMatch(s:String):String {
            if (!s) return "";
            var res:String = s;
            res = res.replace(/<[^>]*>/g, ""); 
            res = res.replace(/\[.*?\]|\(.*?\)|\{.*?\}/g, ""); 
            res = res.replace(/[\uE000-\uF8FF]/g, ""); 
            res = res.replace(/[\s\|\-\_\+\=\*\&\^\%\$\#\@\!\~\`\'\"\;:\<\>\,\.\?\/\\\[\]\(\)\{\}]/g, ""); 
            return res.toLowerCase();
        }

        private function sanitizeWireName(rawName:String):String {
            if (!rawName) return "";
            var cleaned:String = rawName.replace(/:::[\-\d\.]+:::[\-\d\.]+.*$/g, "");
            cleaned = cleaned.replace(/:::DMG:.*$/g, "");
            cleaned = cleaned.replace(/:::DR:.*$/g, "");
            cleaned = cleaned.replace(/:::VAL:.*$/g, "");
            cleaned = cleaned.replace(/:::META:.*$/g, "");
            return cleaned;
        }

        private function parseUiMeta(raw:String):Object {
            var res:Object = { hasMeta: false, favorite: false, isLegendary: false, hasTaggedForSearch: false, taggedForSearch: false };
            if (!raw || raw == "") return res;
            var rows:Array = raw.split(",");
            for each (var row:String in rows) {
                if (row == null) continue;
                if (row.indexOf("META:") != 0) continue;
                res.hasMeta = true;
                var p:Array = row.split(":");
                for (var i:int = 1; i < p.length; i++) {
                    var token:String = String(p[i]);
                    if (token.length < 2) continue;
                    var key:String = token.charAt(0);
                    var val:Boolean = token.substr(1) == "1";
                    if (key == "F") res.favorite = val;
                    else if (key == "L") res.isLegendary = val;
                    else if (key == "T") { res.hasTaggedForSearch = true; res.taggedForSearch = val; }
                }
            }
            return res;
        }

        private function stripUiMeta(raw:String):String {
            if (!raw || raw == "") return "";
            var rows:Array = raw.split(",");
            var kept:Array = [];
            for each (var row:String in rows) {
                if (row != null && row.indexOf("META:") != 0) kept.push(row);
            }
            return kept.join(",");
        }

        private function stealItemData(rawName:String, uid:Number = 0):Object {
            rawName = sanitizeWireName(rawName);
            var res:Object = { name: rawName, favorite: false, isLegendary: false, taggedForSearch: false };
            try {
                var invPage:Object = getPipboyInvPage();
                if (!invPage || !invPage.List_mc) return res;

                var pipList:Array = null;
                if (invPage.List_mc.entryList) pipList = invPage.List_mc.entryList;
                else if (invPage.List_mc.dataProvider) pipList = invPage.List_mc.dataProvider as Array;
                else if (invPage.List_mc.Entries) pipList = invPage.List_mc.Entries as Array;

                if (pipList && pipList.length > 0) {
                    var compRaw:String = cleanStringForMatch(rawName);
                    var matchedItem:Object = null;
                    
                    for (var i:int = 0; i < pipList.length; i++) {
                        var item:Object = pipList[i];
                        if (item) {
                            if (uid > 0 && ((item.hasOwnProperty("id") && item.id == uid) || 
                                            (item.hasOwnProperty("handleID") && item.handleID == uid) || 
                                            (item.hasOwnProperty("formID") && item.formID == uid))) {
                                matchedItem = item; break;
                            }
                            if (item.text && cleanStringForMatch(item.text) == compRaw) {
                                matchedItem = item; break;
                            }
                        }
                    }
                    
                    if (!matchedItem) {
                        for (var j:int = 0; j < pipList.length; j++) {
                            var itemF:Object = pipList[j];
                            if (itemF && itemF.text) {
                                var compPip:String = cleanStringForMatch(itemF.text);
                                if (compPip.indexOf(compRaw) != -1 || compRaw.indexOf(compPip) != -1) {
                                    matchedItem = itemF; break;
                                }
                            }
                        }
                    }

                    if (matchedItem) {
                        if (matchedItem.text != null && matchedItem.text != "") {
                            var tagRegExp:RegExp = /^([\[\(\{].*?[\]\)\}])\s*/;
                            var tagMatch:Object = tagRegExp.exec(matchedItem.text);
                            if (tagMatch && tagMatch.length > 1) res.name = tagMatch[1] + " " + rawName;
                            else res.name = rawName;
                        }
                        
                        if (matchedItem.hasOwnProperty("favorite")) res.favorite = matchedItem.favorite;
                        else if (matchedItem.hasOwnProperty("isFavorite")) res.favorite = matchedItem.isFavorite;
                        
                        if (matchedItem.hasOwnProperty("isLegendary")) res.isLegendary = matchedItem.isLegendary;
                        
                        if (matchedItem.hasOwnProperty("taggedForSearch")) res.taggedForSearch = matchedItem.taggedForSearch;
                        else if (matchedItem.hasOwnProperty("isTaggedForSearch")) res.taggedForSearch = matchedItem.isTaggedForSearch;
                    }
                }
            } catch(e:Error) {}
            return res; 
        }

        private function applyFallUIFormatting(uiTarget:*, rawName:String, textColor:uint, isHeader:Boolean, rowHeight:Number):void {
            var tf:TextField = null;
            var isMC:Boolean = false;
            var mc:MovieClip = null;
            
            if (uiTarget is TextField) {
                tf = uiTarget as TextField;
            } else if (uiTarget != null && uiTarget.hasOwnProperty("textField") && uiTarget["textField"] != null) {
                mc = uiTarget as MovieClip;
                tf = mc["textField"] as TextField;
                isMC = true;
            }
            
            if (tf == null) return;

            if (_uiStates[uiTarget] == null) {
                var initEqX:Number = (isMC && mc["EquipIcon_mc"]) ? mc["EquipIcon_mc"].x : 10;
                _uiStates[uiTarget] = { origX: tf.x, origEqX: initEqX, icon: null };
            }
            var state:Object = _uiStates[uiTarget];

            if (state.icon != null && state.icon.parent != null) {
                state.icon.parent.removeChild(state.icon);
            }
            state.icon = null;
            tf.x = state.origX;

            if (isMC && mc["EquipIcon_mc"] != null && mc["EquipIcon_mc"].visible) {
                var eqMc:DisplayObject = mc["EquipIcon_mc"];
                var eqB:Rectangle = eqMc.getBounds(eqMc);
                eqMc.x = state.origEqX;
                eqMc.y = Math.round((rowHeight / 2) - (eqB.y + eqB.height / 2));
            }

            var cleanName:String = rawName.replace(/<font face="\$FallUI_Icons">.*?<\/font>/gi, "");
            cleanName = cleanName.replace(/[\uE000-\uF8FF]/g, "");

            var M8rMods:Class = null;
            var M8rIconLib:Class = null;
            try {
                var p:DisplayObject = this.parent;
                while (p) {
                    if (p.loaderInfo && p.loaderInfo.applicationDomain) {
                        if (p.loaderInfo.applicationDomain.hasDefinition("M8r.Mods")) M8rMods = p.loaderInfo.applicationDomain.getDefinition("M8r.Mods") as Class;
                        if (p.loaderInfo.applicationDomain.hasDefinition("M8r.Service.IconLibrary")) M8rIconLib = p.loaderInfo.applicationDomain.getDefinition("M8r.Service.IconLibrary") as Class;
                    }
                    if (M8rMods != null && M8rIconLib != null) break;
                    p = p.parent;
                }
            } catch(e1:Error) {}

            var tagString:String = "";
            var tagMatch:Object = /^\[(.*?)\]\s*/.exec(cleanName);
            if (tagMatch) {
                tagString = tagMatch[1];
                cleanName = cleanName.replace(/^\[.*?\]\s*/, "");
            }
            if (M8rMods && M8rMods.configLoaded && M8rMods.config.Itemlist.sItemTagDetectRegExp) {
                var tagRegExp:RegExp = new RegExp(M8rMods.config.Itemlist.sItemTagDetectRegExp, "i");
                var tm:Object = tagRegExp.exec(cleanName);
                if (tm && tm.length > 1) {
                    tagString = tm[1];
                    cleanName = cleanName.replace(tagRegExp, "").replace(/^\s+/, "");
                }
            }

            var mainPart:String = cleanName;
            var subPart:String = "";
            var isMulti:Boolean = false;
            var pipeIdx:int = cleanName.indexOf("|");
            
            if (pipeIdx != -1) {
                mainPart = cleanName.substring(0, pipeIdx).replace(/\s+$/, "");
                subPart = cleanName.substring(pipeIdx + 1).replace(/^\s+/, "");
                isMulti = true;
            } else {
                var parenMatch:Object = /^(.*?)\s*(\(.*?\))$/.exec(cleanName);
                if (parenMatch && parenMatch.length >= 3) {
                    mainPart = parenMatch[1].replace(/\s+$/, "");
                    subPart = parenMatch[2].replace(/^\s+/, "");
                    isMulti = true;
                }
            }

            var iconSprite:Sprite = null;
            var iconSize:Number = isHeader ? 24 : 20;
            if (tagString != "" && M8rIconLib && M8rIconLib.instance && M8rIconLib.instance.isLoaded) {
                try {
                    iconSprite = M8rIconLib.instance.makeTagIcon(tagString, iconSize) as Sprite;
                    if (!iconSprite) iconSprite = M8rIconLib.instance.makeIconByLegacyIconClassName(tagString, iconSize) as Sprite;
                } catch(e2:Error) {}
            }

            var mainHex:String = (textColor & 0xFFFFFF).toString(16);
            while (mainHex.length < 6) mainHex = "0" + mainHex;
            var mainSize:int = isHeader ? 24 : 20;
            var finalHtml:String = "";

            if (isMulti && subPart != "") {
                var brightness:Number = 0.65;
                var sizeMod:Number = 0;
                if (M8rMods && M8rMods.configLoaded) {
                    brightness = M8rMods.config.Itemlist.iItemSubTitleBrightness / 100;
                    sizeMod = M8rMods.config.Itemlist.fItemSubTitleSizeMod;
                }
                var r:uint = ((textColor >> 16) & 0xFF) * brightness;
                var g:uint = ((textColor >> 8) & 0xFF) * brightness;
                var b:uint = (textColor & 0xFF) * brightness;
                var dimHex:String = ((r << 16) | (g << 8) | b).toString(16);
                while (dimHex.length < 6) dimHex = "0" + dimHex;
                
                var subSize:int = (isHeader ? 16 : 13) + sizeMod;
                finalHtml = "<textformat leading='-4'>" + 
                            "<font color='#" + mainHex + "' size='" + mainSize + "'>" + mainPart + "</font>\n" + 
                            "<font color='#" + dimHex + "' size='" + subSize + "'>" + subPart + "</font>" + 
                            "</textformat>";
                tf.multiline = true;
            } else {
                if (tagString != "" && iconSprite == null) mainPart = "[" + tagString + "] " + mainPart;
                finalHtml = "<font color='#" + mainHex + "' size='" + mainSize + "'>" + mainPart + "</font>";
                tf.multiline = false;
            }

            tf.wordWrap = false;
            tf.htmlText = finalHtml;
            tf.height = tf.textHeight + 6; 
            
            tf.y = Math.round((rowHeight - tf.textHeight) / 2) - 2; 
            if (isMC) mc.y = 0; 

            var currentX:Number = state.origX;

            if (iconSprite) {
                var icB:Rectangle = iconSprite.getBounds(iconSprite);
                iconSprite.x = currentX;
                iconSprite.y = Math.round((rowHeight / 2) - (icB.y + icB.height / 2));
                if (isMC) mc.addChild(iconSprite);
                else {
                    var pCont:DisplayObjectContainer = tf.parent;
                    if (pCont) pCont.addChild(iconSprite);
                }
                state.icon = iconSprite;
                currentX += icB.width + 6;
            }

            tf.x = currentX;

            var textEndX:Number = tf.x + tf.textWidth + 8;
            
            if (isMC) {
                var postIcons:Array = ["FavIcon_mc", "LegendaryIcon_mc", "SearchIcon_mc"];
                for each (var pIc:String in postIcons) {
                    if (mc.hasOwnProperty(pIc) && mc[pIc] != null && mc[pIc].visible) {
                        var pmc:DisplayObject = mc[pIc];
                        var pb:Rectangle = pmc.getBounds(pmc);
                        
                        var visualCenterX:Number = pb.x + pb.width / 2;
                        var visualCenterY:Number = pb.y + pb.height / 2;
                        
                        pmc.x = textEndX + 10 - visualCenterX;
                        pmc.y = Math.round(rowHeight / 2) - visualCenterY;
                        
                        textEndX += 22; 
                    }
                }
            }
            
            state.textEndX = textEndX; 
        }

        private function DrawRepairIcon():void {
            var flaIcon:MovieClip = findTargetObject(this, "repairIcon_mc") as MovieClip;
            if (flaIcon != null) flaIcon.visible = false;
            if (repairIcon != null) repairIcon.visible = false;
        }

        private function CreateHeaderButton(labelStr:String, x:Number, y:Number, w:Number, h:Number, alignStr:String, colID:String):MovieClip {
            var btn:MovieClip = new MovieClip(); btn.x = x; btn.y = y;
            var tf:TextField = CreateTextField(0, 0, w, h, 16, themeColor, false, alignStr); tf.name = "tf"; SetLocalizedText(tf, labelStr); btn.addChild(tf);
            var arrowIcon:Shape = new Shape(); arrowIcon.name = "arrowIcon"; btn.addChild(arrowIcon);
            var hit:Shape = new Shape(); hit.graphics.beginFill(0, 0); hit.graphics.drawRect(0, 0, w, h); hit.graphics.endFill(); btn.addChild(hit);
            btn.buttonMode = true; btn.mouseChildren = false; btn.colID = colID; btn.isHover = false;
            btn.addEventListener(MouseEvent.CLICK, onHeaderClick);
            btn.addEventListener(MouseEvent.MOUSE_OVER, function(e:Event):void { btn.isHover = true; UpdateHeaderLabels(); PlayUISound("UISelectOn"); });
            btn.addEventListener(MouseEvent.MOUSE_OUT,  function(e:Event):void { btn.isHover = false; UpdateHeaderLabels(); });
            return btn;
        }

        private function onHeaderClick(e:MouseEvent):void {
            var btn:MovieClip = e.currentTarget as MovieClip; var col:String = btn.colID;
            if (col == "addPct" || col == "pct") { if (currentSortCol == col) currentSortDir = (currentSortDir == -1) ? 1 : -1; else { currentSortCol = col; currentSortDir = -1; } } 
            else if (col == "name") { if (currentSortCol != "name") { currentSortCol = "name"; currentSortDir = 0; } else { if (currentSortDir == 0) currentSortDir = 1; else if (currentSortDir == 1) currentSortDir = -1; else currentSortDir = 0; } }
            ApplySorting(); selectedIndex = 0; listOffset = 0; UpdateHeaderLabels(); RenderUI(); UpdateSelection(); PlayUISound("UIMenuOK");
        }

        private function ApplySorting():void {
            var targetArr:Array = showKitsMode ? kitsArray : materialsArray;
            
            if (currentSortDir == 0 || currentSortCol == "none") {
                if (showKitsMode) kitsArray = originalKitsArray.concat();
                else materialsArray = originalMaterialsArray.concat();
            } else {
                targetArr.sort(function(a:Object, b:Object):int {
                    if (currentSortCol == "name") { var nameA:String = a.name.toLowerCase(); var nameB:String = b.name.toLowerCase(); if (nameA < nameB) return -1 * currentSortDir; if (nameA > nameB) return 1 * currentSortDir; return 0; } 
                    else if (currentSortCol == "addPct") {
                        var neededA:Number = Math.max(0, a.limit - targetBasePct);
                        var neededB:Number = Math.max(0, b.limit - targetBasePct);
                        var overA:Number = Math.max(0, a.addPct - neededA); 
                        var overB:Number = Math.max(0, b.addPct - neededB);
                        if (overA < overB) return -1 * currentSortDir; if (overA > overB) return 1 * currentSortDir;
                        if (a.addPct < b.addPct) return -1 * currentSortDir; if (a.addPct > b.addPct) return 1 * currentSortDir; return 0;
                    } else { var valA:Number = a[currentSortCol]; var valB:Number = b[currentSortCol]; if (valA < valB) return -1 * currentSortDir; if (valA > valB) return 1 * currentSortDir; return 0; }
                });
            }
        }

        private function UpdateHeaderLabels():void {
            var updateArrow:Function = function(btn:MovieClip, col:String):void {
                var arrowIcon:Shape = btn.getChildByName("arrowIcon") as Shape; var tf:TextField = btn.getChildByName("tf") as TextField;
                arrowIcon.graphics.clear(); tf.textColor = themeColor;
                if (currentSortCol != col || currentSortDir == 0) return;
                var aw:Number = 8; var ah:Number = 6; var fillColor:uint = themeColor;
                arrowIcon.graphics.beginFill(fillColor);
                if (currentSortDir == 1) { 
                    arrowIcon.graphics.moveTo(aw/2, 0); arrowIcon.graphics.lineTo(aw, ah); arrowIcon.graphics.lineTo(0, ah); 
                } else { 
                    arrowIcon.graphics.moveTo(0, 0); arrowIcon.graphics.lineTo(aw, 0); arrowIcon.graphics.lineTo(aw/2, ah); 
                }
                arrowIcon.graphics.endFill();
                arrowIcon.y = (headerH - ah) / 2 + 1; 
                if (col == "name") arrowIcon.x = -12; else arrowIcon.x = (tf.width / 2) + (tf.textWidth / 2) + 6; 
            };
            updateArrow(headerMat, "name"); updateArrow(headerPct, "addPct"); updateArrow(headerCond, "pct");
        }

        private function ExtractNativeColors(fallbackColor:uint):void {
            themeColor = fallbackColor; listTextColor = fallbackColor; selectedTextColor = 0x000000;
            try {
                var uiRoot:Object = Object(root);
                if (uiRoot && uiRoot.Menu_mc) {
                    if (uiRoot.Menu_mc.Header_mc && uiRoot.Menu_mc.Header_mc.NameText) {
                        var hdr:TextField = uiRoot.Menu_mc.Header_mc.NameText;
                        themeColor = hdr.textColor;
                        
                        pipboyHeaderFormat = hdr.getTextFormat();
                        pipboyHeaderFilters = hdr.filters;
                        pipboyHeaderEmbedFonts = hdr.embedFonts;
                        pipboyHeaderAntiAlias = hdr.antiAliasType;
                    }
                    else if (uiRoot.Menu_mc.BottomBar_mc && uiRoot.Menu_mc.BottomBar_mc.button1 && uiRoot.Menu_mc.BottomBar_mc.button1.tf) {
                        themeColor = uiRoot.Menu_mc.BottomBar_mc.button1.tf.textColor;
                    }
                }
            } catch(e:Error) {}

            listTextColor = themeColor; 

            var EntryCls:Class = getInvListEntryClass();
            if (EntryCls) {
                try {
                    var dummy:MovieClip = new EntryCls() as MovieClip;
                    var itemData:Object = { text: "ColorTest", count: 1, equipState: 1, isEquipped: true, favorite: false };
                    dummy["itemIndex"] = 0; dummy["SetEntryText"](itemData, "text");
                    if (dummy.hasOwnProperty("textField") && dummy["textField"] != null) {
                        dummy["selected"] = false; listTextColor = dummy["textField"].textColor; 
                        dummy["selected"] = true; selectedTextColor = dummy["textField"].textColor;
                    }
                    dummy["selected"] = false; var eqIcon:Object = null;
                    if (dummy.hasOwnProperty("equipIcon")) eqIcon = dummy["equipIcon"];
                    else if (dummy.hasOwnProperty("EquipIcon_mc")) eqIcon = dummy["EquipIcon_mc"];
                    if (eqIcon != null) {
                        if (eqIcon is TextField) { var tc:uint = eqIcon.textColor; if (tc != 0 && tc != 0xFFFFFF) themeColor = tc; } 
                        else if (eqIcon is DisplayObject) {
                            var ct:ColorTransform = eqIcon.transform.colorTransform;
                            if (ct.color != 0 && ct.color != 0xFFFFFF) themeColor = ct.color;
                            else if (ct.redOffset > 0 || ct.greenOffset > 0 || ct.blueOffset > 0) themeColor = (ct.redOffset << 16) | (ct.greenOffset << 8) | ct.blueOffset;
                        }
                    }
                } catch(e:Error) {}
            }
            if (themeColor == fallbackColor || themeColor == 0 || themeColor == 0xFFFFFF) {
                try { var uiRoot2:Object = Object(root); if (uiRoot2 && uiRoot2.Menu_mc && uiRoot2.Menu_mc.Header_mc && uiRoot2.Menu_mc.Header_mc.NameText) themeColor = uiRoot2.Menu_mc.Header_mc.NameText.textColor; } catch(e:Error) {}
            }
            if (themeColor == 0) themeColor = fallbackColor; if (listTextColor == 0) listTextColor = themeColor;
        }

        public function HandleOnReady():void { if (this["JuryRigging_Callback"] != null) this["JuryRigging_Callback"]("RequestData"); }

        public function HandleReceiveData(dataStr:String, isRefresh:* = false, colorVal:Number = -1, customTitle:String = "", juryStatsConfig:String = ""):void {
            var cVal:uint = (colorVal != -1) ? uint(colorVal) : 0x18FF00;
            ExtractNativeColors(cVal);
            ApplyJuryStatsConfig(juryStatsConfig);
            if (customTitle != null && customTitle != "") menuTitleStr = customTitle;
            
            isProcessingRepair = false; 

            if (!dataStr || dataStr == "") { if (headerTxt) headerTxt.text = "NO TARGET"; return; }
            ParseData(dataStr); ApplySorting(); ClampIndices(); RenderUI(); UpdateSelection(); UpdateHeaderLabels();
            if (isRefresh === true) { PlayUISound("UIMenuOK"); }
        }

        private function ApplyJuryStatsConfig(configStr:String):void {
            if (!configStr || configStr == "") return;
            var parts:Array = configStr.split(";");
            if (parts.length > 0 && !isNaN(parseFloat(parts[0]))) juryStatsOffsetX = parseFloat(parts[0]);
            if (parts.length > 1 && !isNaN(parseFloat(parts[1]))) juryStatsOffsetY = parseFloat(parts[1]);
            if (parts.length > 2 && !isNaN(parseFloat(parts[2]))) juryStatsRowSpacing = Math.max(84, Math.min(110, parseFloat(parts[2])));
            if (parts.length > 3 && !isNaN(parseInt(parts[3]))) juryStatsColorMode = Math.max(0, Math.min(1, parseInt(parts[3])));
        }

        private function onMouseWheel(e:MouseEvent):void {
            e.stopPropagation(); if (activeArray.length <= maxVisibleItems) return;
            var oldOffset:int = listOffset;
            if (e.delta > 0) listOffset--; else if (e.delta < 0) listOffset++;
            var maxOffset:int = activeArray.length - maxVisibleItems;
            if (listOffset < 0) listOffset = 0; if (listOffset > maxOffset) listOffset = maxOffset;
            if (oldOffset != listOffset) {
                var mx:Number = listContainer.mouseX; var my:Number = listContainer.mouseY;
                if (mx >= 0 && mx <= listW && my >= 0 && my <= maxVisibleItems * rowH) {
                    var hoverLocalIdx:int = Math.floor(my / rowH); selectedIndex = listOffset + hoverLocalIdx;
                    if (selectedIndex >= activeArray.length) selectedIndex = activeArray.length - 1;
                } else {
                    if (selectedIndex < listOffset) selectedIndex = listOffset;
                    else if (selectedIndex >= listOffset + maxVisibleItems) selectedIndex = listOffset + maxVisibleItems - 1;
                }
                RenderUI(); UpdateSelection(); if (e.delta > 0) PlayUISound("UISelectOff"); else PlayUISound("UISelectOn");
            }
        }

        private function onEnterFrameLoop(e:Event):void {
            pulseAlpha += pulseDir;
            if (pulseAlpha > 0.7) { pulseAlpha = 0.7; pulseDir = -0.025; } else if (pulseAlpha < 0.2) { pulseAlpha = 0.2; pulseDir = 0.025; }
            if (targetInfoBar != null) { var addShape:Shape = targetInfoBar.getChildByName("addShape") as Shape; if (addShape != null && addShape.visible) addShape.alpha = pulseAlpha; }
        }

        private function onThumbMouseDown(e:MouseEvent):void {
            if (activeArray.length <= maxVisibleItems) return;
            isDragging = true; dragStartY = e.stageY; startThumbY = scrollBarThumb.y;
            UpdateScrollbar(); PlayUISound("UIMenuOK");
            stage.addEventListener(MouseEvent.MOUSE_MOVE, onThumbDrag); stage.addEventListener(MouseEvent.MOUSE_UP, onThumbMouseUp);
        }

        private function onThumbDrag(e:MouseEvent):void {
            if (!isDragging) return;
            var thumbH:Number = Math.max(30, (Number(maxVisibleItems) / Number(activeArray.length)) * barH);
            var maxScrollY:Number = barH - thumbH;
            var deltaY:Number = e.stageY - dragStartY; var newY:Number = startThumbY + deltaY;
            if (newY < barY) newY = barY; if (newY > barY + maxScrollY) newY = barY + maxScrollY;
            scrollBarThumb.y = newY; 
            var scrollPct:Number = (newY - barY) / maxScrollY;
            var newOffset:int = Math.round(scrollPct * (activeArray.length - maxVisibleItems));
            if (newOffset != listOffset) { listOffset = newOffset; if (selectedIndex < listOffset) selectedIndex = listOffset; else if (selectedIndex >= listOffset + maxVisibleItems) selectedIndex = listOffset + maxVisibleItems - 1; RenderUI(); UpdateSelection(); }
        }

        private function onThumbMouseUp(e:MouseEvent):void {
            isDragging = false;
            if(stage) { stage.removeEventListener(MouseEvent.MOUSE_MOVE, onThumbDrag); stage.removeEventListener(MouseEvent.MOUSE_UP, onThumbMouseUp); }
            UpdateScrollbar(); 
        }

        private function onTrackClick(e:MouseEvent):void {
            if (isDragging || activeArray.length <= maxVisibleItems) return;
            var thumbH:Number = Math.max(30, (Number(maxVisibleItems) / Number(activeArray.length)) * barH);
            var clickY:Number = scrollBarBg.mouseY - barY - (thumbH / 2); var maxScrollY:Number = barH - thumbH;
            var pct:Number = clickY / maxScrollY;
            if (pct < 0) pct = 0; if (pct > 1) pct = 1;
            var newOffset:int = Math.round(pct * (activeArray.length - maxVisibleItems));
            if (newOffset != listOffset) { listOffset = newOffset; if (selectedIndex < listOffset) selectedIndex = listOffset; else if (selectedIndex >= listOffset + maxVisibleItems) selectedIndex = listOffset + maxVisibleItems - 1; RenderUI(); UpdateSelection(); PlayUISound("UISelectOn"); }
        }

        private function ClampIndices():void {
            if (activeArray.length == 0) { selectedIndex = 0; listOffset = 0; return; }
            if (selectedIndex >= activeArray.length) selectedIndex = activeArray.length - 1;
            if (selectedIndex < 0) selectedIndex = 0;
            if (listOffset > selectedIndex) listOffset = selectedIndex;
            else if (listOffset < selectedIndex - maxVisibleItems + 1) listOffset = selectedIndex - maxVisibleItems + 1;
            var maxOffset:int = activeArray.length - maxVisibleItems; if (maxOffset < 0) maxOffset = 0; if (listOffset > maxOffset) listOffset = maxOffset;
        }

        private function parsePreviewStats(raw:String):Array {
            var result:Array = [];
            if (!raw || raw == "") return result;
            var rows:Array = raw.split(",");
            for each (var row:String in rows) {
                var p:Array = row.split(":");
                if (p.length >= 3) {
                    var cur:Number = parseFloat(p[1]);
                    var repaired:Number = parseFloat(p[2]);
                    if (!isNaN(cur) && !isNaN(repaired)) {
                        result.push({ label: p[0], currentValue: cur, repairedValue: repaired });
                    }
                }
            }
            return result;
        }

        private function formatStatValue(v:Number):String {
            if (isNaN(v)) return "--";
            var rounded:Number = Math.round(v * 10) / 10;
            if (Math.abs(rounded - Math.round(rounded)) < 0.05) return String(Math.round(rounded));
            return rounded.toFixed(1);
        }

        private function GetLocalizedStatLabel(label:String):String {
            if (label == "CND") return TranslateText("$condition");
            if (label == "DAM" || label == "DMG") return TranslateText("$ItemInfo_DMG");
            if (label == "DR") return TranslateText("$ItemInfo_AR");
            if (label == "VAL") return TranslateText("$ItemInfo_VAL");
            return label;
        }

        private function GetStatsColor():uint {
            return juryStatsColorMode == 1 ? listTextColor : themeColor;
        }

        private function DrawDownArrow(mc:MovieClip, xPos:Number, yPos:Number, scaleVal:Number = 1.0, color:uint = 0xFFFFFF):void {
            var arrow:Shape = new Shape();
            arrow.graphics.lineStyle(2.1, color, 0.95);
            var stemGap:Number = 7 * scaleVal;
            var stemH:Number = 16 * scaleVal;
            var wingW:Number = 9 * scaleVal;
            var wingDrop:Number = 2 * scaleVal;
            var tipY:Number = yPos + 28 * scaleVal;
            var leftX:Number = xPos - stemGap;
            var rightX:Number = xPos + stemGap;
            var jointY:Number = yPos + stemH + wingDrop;

            arrow.graphics.moveTo(leftX, yPos);
            arrow.graphics.lineTo(leftX, yPos + stemH);
            arrow.graphics.lineTo(leftX - wingW, jointY);
            arrow.graphics.lineTo(xPos, tipY);
            arrow.graphics.lineTo(rightX + wingW, jointY);
            arrow.graphics.lineTo(rightX, yPos + stemH);
            arrow.graphics.lineTo(rightX, yPos);
            mc.addChild(arrow);
        }

        private function getDefinitionFromParents(defName:String):Class {
            try {
                var p:DisplayObject = this;
                while (p) {
                    if (p.loaderInfo && p.loaderInfo.applicationDomain && p.loaderInfo.applicationDomain.hasDefinition(defName)) {
                        return p.loaderInfo.applicationDomain.getDefinition(defName) as Class;
                    }
                    p = p.parent;
                }
            } catch(e:Error) {}
            try {
                return getDefinitionByName(defName) as Class;
            } catch(e2:Error) {}
            return null;
        }

        private function getLocalCapsIconSource():DisplayObject {
            try {
                var directCaps:DisplayObject = this["CapsIcon"] as DisplayObject;
                if (directCaps != null) return directCaps;
            } catch(e0:Error) {}

            var names:Array = ["CapsIcon", "CapsIcon_mc", "capsIcon_mc", "valCapsIcon_mc"];
            for each (var n:String in names) {
                var inst:DisplayObject = findTargetObjectDeep(this, n);
                if (inst) return inst;
            }

            var classNames:Array = ["CSF_CapsIcon", "CapsIconSymbol", "CapsIcon", "PipboyMenu_fla.CapsIcon", "JuryRiggingMenu_fla.CapsIcon"];
            for each (var cn:String in classNames) {
                var IconCls:Class = getDefinitionFromParents(cn);
                if (IconCls != null) {
                    try {
                        var iconObj:DisplayObject = new IconCls() as DisplayObject;
                        if (iconObj) return iconObj;
                    } catch(e:Error) {}
                }
            }
            return null;
        }

        private function getChildTextFieldByProp(container:DisplayObjectContainer, propName:String):TextField {
            if (!container) return null;
            try {
                var tf:TextField = container[propName] as TextField;
                if (tf) return tf;
            } catch(e:Error) {}
            return null;
        }

        private function findContainerWithTextFieldProp(container:DisplayObjectContainer, propName:String):DisplayObjectContainer {
            if (!container) return null;
            if (getChildTextFieldByProp(container, propName) != null) return container;
            for (var i:int = 0; i < container.numChildren; i++) {
                var child:DisplayObjectContainer = container.getChildAt(i) as DisplayObjectContainer;
                if (!child || child == mainCanvas || child.name == "mainCanvas") continue;
                var found:DisplayObjectContainer = findContainerWithTextFieldProp(child, propName);
                if (found) return found;
            }
            return null;
        }

        private function findCapsIconCandidate(container:DisplayObjectContainer, capsTf:TextField, rootSpace:DisplayObjectContainer):DisplayObject {
            if (!container || !capsTf || !rootSpace) return null;
            var capsB:Rectangle = capsTf.getBounds(rootSpace);
            var capsCY:Number = capsB.y + capsB.height / 2;
            var best:DisplayObject = null;
            var bestScore:Number = 999999;

            function scan(node:DisplayObjectContainer):void {
                for (var i:int = 0; i < node.numChildren; i++) {
                    var child:DisplayObject = node.getChildAt(i);
                    if (!child || child == capsTf || !child.visible || child.alpha <= 0) continue;

                    var childContainer:DisplayObjectContainer = child as DisplayObjectContainer;
                    if (childContainer) scan(childContainer);

                    if (child is TextField) continue;
                    var b:Rectangle;
                    try { b = child.getBounds(rootSpace); } catch(e:Error) { continue; }
                    if (!b || b.width < 3 || b.height < 3 || b.width > 90 || b.height > 90) continue;

                    var cy:Number = b.y + b.height / 2;
                    var cx:Number = b.x + b.width / 2;
                    if (Math.abs(cy - capsCY) > 42) continue;
                    if (cx > capsB.x + 14 || cx < capsB.x - 220) continue;

                    var score:Number = Math.abs(cy - capsCY) * 3 + Math.abs((capsB.x - 18) - cx) + Math.abs(b.width - b.height) * 0.5;
                    if (score < bestScore) {
                        bestScore = score;
                        best = child;
                    }
                }
            }

            scan(container);
            return best;
        }

        private function getReflectedCapsIconSource():DisplayObject {
            var localIcon:DisplayObject = getLocalCapsIconSource();
            if (localIcon) return localIcon;

            var stageRoot:DisplayObjectContainer = this.stage as DisplayObjectContainer;
            if (stageRoot) {
                var namedStageIcon:DisplayObject = findTargetObjectDeep(stageRoot, "CapsIcon");
                if (namedStageIcon) return namedStageIcon;
            }

            var top:DisplayObject = this;
            while (top.parent) top = top.parent;

            var namedTopIcon:DisplayObject = findTargetObjectDeep(top as DisplayObjectContainer, "CapsIcon");
            if (namedTopIcon) return namedTopIcon;

            var info:DisplayObjectContainer = findContainerWithTextFieldProp(top as DisplayObjectContainer, "Caps_tf");
            var namedLiveIcon:DisplayObject = findTargetObjectDeep(info, "CapsIcon");
            if (namedLiveIcon) return namedLiveIcon;

            var capsTf:TextField = getChildTextFieldByProp(info, "Caps_tf");
            if (info && capsTf) {
                var liveCandidate:DisplayObject = findCapsIconCandidate(info, capsTf, info);
                if (liveCandidate) return liveCandidate;
            }

            var InfoCls:Class = getDefinitionFromParents("PipboyMenu_fla.BottomBar_Info_3");
            if (InfoCls == null) InfoCls = getDefinitionFromParents("BottomBar_Info");
            if (InfoCls == null) InfoCls = getDefinitionFromParents("PipboyMenu_fla.BottomBar_Info_2");
            if (InfoCls == null) InfoCls = getDefinitionFromParents("PipboyMenu_fla.BottomBar_Info_1");
            if (InfoCls != null) {
                try {
                    var tempInfo:DisplayObjectContainer = new InfoCls() as DisplayObjectContainer;
                    if (tempInfo) {
                        var tempMc:MovieClip = tempInfo as MovieClip;
                        var frameCount:int = tempMc ? Math.max(1, tempMc.totalFrames) : 1;
                        for (var frame:int = 1; frame <= frameCount; frame++) {
                            if (tempMc) {
                                try { tempMc.gotoAndStop(frame); } catch(eFrame:Error) {}
                            }
                            var tempCapsTf:TextField = getChildTextFieldByProp(tempInfo, "Caps_tf");
                            var namedTempIcon:DisplayObject = findTargetObjectDeep(tempInfo, "CapsIcon");
                            if (namedTempIcon) return namedTempIcon;

                            if (tempCapsTf) {
                                var tempCandidate:DisplayObject = findCapsIconCandidate(tempInfo, tempCapsTf, tempInfo);
                                if (tempCandidate) return tempCandidate;
                            }
                        }
                    }
                } catch(e:Error) {}
            }
            return null;
        }

        private function DrawFallbackCapsIcon(mc:MovieClip, xPos:Number, yPos:Number):void {
        }

        private function DrawCapsStatIcon(mc:MovieClip, xPos:Number, yPos:Number, color:uint):void {
            var source:DisplayObject = getReflectedCapsIconSource();
            if (!source) {
                DrawFallbackCapsIcon(mc, xPos, yPos);
                return;
            }

            try {
                var drawSource:DisplayObject = source;
                try {
                    var sourceClassName:String = getQualifiedClassName(source);
                    var SourceCls:Class = getDefinitionFromParents(sourceClassName);
                    if (SourceCls != null) {
                        var clonedSource:DisplayObject = new SourceCls() as DisplayObject;
                        if (clonedSource != null) drawSource = clonedSource;
                    }
                } catch(eClone:Error) {
                }

                var b:Rectangle = drawSource.getBounds(drawSource);
                var maxSize:Number = 20;
                var scaleVal:Number = Math.min(maxSize / Math.max(1, b.width), maxSize / Math.max(1, b.height));
                drawSource.scaleX = drawSource.scaleY = scaleVal;
                drawSource.x = Math.round(xPos - (b.x + b.width / 2) * scaleVal);
                drawSource.y = Math.round(yPos - (b.y + b.height / 2) * scaleVal);
                drawSource.visible = true;
                drawSource.alpha = 1.0;
                drawSource.transform.colorTransform = new ColorTransform(0, 0, 0, 1, (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF, 0);
                mc.addChild(drawSource);
            } catch(e:Error) {
                DrawFallbackCapsIcon(mc, xPos, yPos);
            }
        }

        private function DrawLinkedStatSymbol(mc:MovieClip, xPos:Number, yPos:Number, classNames:Array, maxSize:Number = 20, color:uint = 0xFFFFFF):Boolean {
            for each (var className:String in classNames) {
                var SymbolCls:Class = getDefinitionFromParents(className);
                if (SymbolCls == null) continue;
                try {
                    var symbol:DisplayObject = new SymbolCls() as DisplayObject;
                    if (!symbol) continue;

                    var b:Rectangle = symbol.getBounds(symbol);
                    if (!b || b.width <= 0 || b.height <= 0) continue;

                    var scaleVal:Number = Math.min(maxSize / Math.max(1, b.width), maxSize / Math.max(1, b.height));
                    symbol.scaleX = symbol.scaleY = scaleVal;
                    symbol.x = Math.round(xPos - (b.x + b.width / 2) * scaleVal);
                    symbol.y = Math.round(yPos - (b.y + b.height / 2) * scaleVal);
                    symbol.visible = true;
                    symbol.alpha = 1.0;
                    symbol.transform.colorTransform = new ColorTransform(0, 0, 0, 1, (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF, 0);
                    mc.addChild(symbol);
                    return true;
                } catch(e:Error) {}
            }
            return false;
        }

        private function DrawSmallStatIcon(mc:MovieClip, xPos:Number, yPos:Number, label:String, color:uint):void {
            if (label == "DAM") {
                DrawLinkedStatSymbol(mc, xPos, yPos, ["CSF_DAM"], 20, color);
                return;
            } else if (label == "VAL") {
                DrawCapsStatIcon(mc, xPos, yPos, color);
                return;
            } else {
                DrawLinkedStatSymbol(mc, xPos, yPos, ["CSF_DR"], 20, color);
                return;
            }
        }

        private function drawPreviewStatRow(yPos:Number, label:String, currentText:String, repairedText:String, deltaText:String, drawCndBars:Boolean, currentPct:Number = 0, repairedPct:Number = 0):void {
            var rowW:Number = 190;
            var rowH:Number = drawCndBars ? 86 : 86;
            var edgePad:Number = 7;
            var labelX:Number = edgePad;
            var labelW:Number = 42;
            var statColumnX:Number = 54;
            var statIconX:Number = statColumnX + 10;
            var midX:Number = drawCndBars ? statColumnX : 62;
            var midW:Number = drawCndBars ? previewCndBarW : 52;
            var rightX:Number = drawCndBars ? 136 : 128;
            var valueW:Number = Math.max(34, rowW - rightX - edgePad);
            var topY:Number = yPos + 5;
            var bottomY:Number = yPos + rowH - 27;
            var midY:Number = yPos + Math.round(rowH / 2) - 9;
            var valueX:Number = rightX;
            var deltaX:Number = rightX;
            var barXPos:Number = midX;
            var barW:Number = drawCndBars ? previewCndBarW : cndBarFixedW;
            var arrowX:Number = statColumnX + Math.round(previewCndBarW / 2);
            var statsColor:uint = GetStatsColor();
            var box:Shape = new Shape();
            box.graphics.lineStyle(1.4, statsColor, 0.75);
            box.graphics.drawRect(0, yPos, rowW, rowH);
            previewStatsPanel.addChild(box);

            var labelTopTf:TextField = CreateTextField(labelX, topY, labelW, 24, 18, statsColor, true, "left");
            labelTopTf.text = GetLocalizedStatLabel(label);
            previewStatsPanel.addChild(labelTopTf);

            var labelBottomTf:TextField = CreateTextField(labelX, bottomY, labelW, 24, 18, statsColor, true, "left");
            labelBottomTf.text = GetLocalizedStatLabel(label);
            previewStatsPanel.addChild(labelBottomTf);

            if (drawCndBars) {
                var barA:MovieClip = new MovieClip();
                barA.x = Math.round(barXPos); barA.y = Math.round(topY + Math.round((24 - previewCndBarH) / 2));
                DrawMiniBar(barA, barW, previewCndBarH, currentPct, statsColor, targetThresholdPct, targetThresholdPct2);
                previewStatsPanel.addChild(barA);

                var barB:MovieClip = new MovieClip();
                barB.x = Math.round(barXPos); barB.y = Math.round(bottomY + Math.round((24 - previewCndBarH) / 2));
                DrawMiniBar(barB, barW, previewCndBarH, repairedPct, statsColor, targetThresholdPct, targetThresholdPct2);
                previewStatsPanel.addChild(barB);
            }

            var curTf:TextField = CreateTextField(valueX, topY, valueW, 24, 18, statsColor, false, "right");
            curTf.text = currentText;
            previewStatsPanel.addChild(curTf);

            DrawDownArrow(previewStatsPanel, arrowX, midY, drawCndBars ? 0.85 : 0.95, statsColor);

            var deltaTf:TextField = CreateTextField(deltaX, yPos + Math.round((rowH - 24) / 2), valueW, 24, 18, statsColor, true, "right");
            deltaTf.text = deltaText;
            previewStatsPanel.addChild(deltaTf);

            var repTf:TextField = CreateTextField(valueX, bottomY, valueW, 24, 18, statsColor, false, "right");
            repTf.text = repairedText;
            previewStatsPanel.addChild(repTf);

            if (!drawCndBars) {
                DrawSmallStatIcon(previewStatsPanel, statIconX, topY + 11, label, statsColor);
                DrawSmallStatIcon(previewStatsPanel, statIconX, bottomY + 11, label, statsColor);
            }
        }

        private function DrawPreviewStatsPanel(statsRaw:String, repairedPct:Number):void {
            if (!previewStatsPanel) return;
            while (previewStatsPanel.numChildren > 0) previewStatsPanel.removeChildAt(0);

            previewStatsPanel.x = statsX + 10 + juryStatsOffsetX;
            previewStatsPanel.y = statsY + 20 + juryStatsOffsetY;
            if (previewStatsPanel.parent == mainCanvas) {
                mainCanvas.setChildIndex(previewStatsPanel, mainCanvas.numChildren - 1);
            }

            var currentPct:Number = targetBasePct;
            var finalPct:Number = repairedPct;
            if (finalPct < currentPct) finalPct = currentPct;
            var deltaPct:Number = Math.max(0, finalPct - currentPct);

            drawPreviewStatRow(0, "CND", Math.round(currentPct * 100) + "%", Math.round(finalPct * 100) + "%", "+" + Math.round(deltaPct * 100) + "%", true, currentPct, finalPct);

            var rows:Array = parsePreviewStats(statsRaw);
            var yPos:Number = juryStatsRowSpacing;
            for each (var r:Object in rows) {
                var diff:Number = r.repairedValue - r.currentValue;
                var sign:String = diff >= 0 ? "+" : "";
                var label:String = r.label == "DMG" ? "DAM" : r.label;
                drawPreviewStatRow(yPos, label, formatStatValue(r.currentValue), formatStatValue(r.repairedValue), sign + formatStatValue(diff), false);
                yPos += juryStatsRowSpacing;
            }
        }

        private function RenderUI():void {
            if (titleTxt != null) { 
                
                var finalFormat:TextFormat;

                if (pipboyHeaderFormat != null) {
                    finalFormat = pipboyHeaderFormat;
                    finalFormat.font = "$MAIN_Font_Bold"; 
                    finalFormat.color = themeColor;
                    
                    titleTxt.defaultTextFormat = finalFormat;
                    titleTxt.embedFonts = pipboyHeaderEmbedFonts;
                    titleTxt.antiAliasType = pipboyHeaderAntiAlias;
                    
                    if (pipboyHeaderFilters != null && pipboyHeaderFilters.length > 0) {
                        titleTxt.filters = pipboyHeaderFilters;
                    } else {
                        titleTxt.filters = [];
                    }
                } else {
                    finalFormat = titleTxt.defaultTextFormat;
                    finalFormat.font = "$MAIN_Font_Bold"; 
                    finalFormat.size = 34;
                    finalFormat.color = themeColor;
                    titleTxt.defaultTextFormat = finalFormat;
                    titleTxt.filters = [];
                }
                
                titleTxt.text = menuTitleStr;
                titleTxt.setTextFormat(finalFormat);
                titleTxt.autoSize = "left"; 
                
                // 【核心算法】：利用 centerX 和 动态计算的 titleTxt.width 保持完美居中自适应
                var centerX:Number = titleX + titleW / 2;
                titleTxt.x = Math.round(centerX - titleTxt.width / 2);
                
                // 💡 [调整点 1] outerHookH: 最外侧向下收尾的短线高度
                var outerHookH:Number = 8; 
                // 💡 [调整点 2] 整个装饰框的垂直基准高度。改变最后的数字可控制整个线框的上下位置。
                // (如 -4 改成 -6 就是线框往上，改成 -2 就是线框往下)
                var lineY:Number = titleY + titleH - outerHookH - 4; 
                
                // 💡 [调整点 3] 仅仅控制文字的上下位置（不影响边框）。
                // 原来是 -8。往下移2个像素，所以改成 -6。(数字变大=往下移动，数字变小=往上移动)
                var textVertOffset:Number = -6; 
                titleTxt.y = lineY - titleTxt.textHeight + 2 + textVertOffset; 
                
                titleFrame.graphics.clear();
                titleFrame.graphics.lineStyle(2, themeColor, 0.8);
                
                // 💡 [调整点 4] 括号和边框的其他控制参数
                var bracketH:Number = 29;   // 括住文字的向上竖线的高度
                var hookW:Number = 8;       // 竖线顶端向内拐角的横线长度
                var gap:Number = 10;        // 竖线距离文字左右边缘的空白间距
                
                // 【绝不动摇的外锚点】：锁定死绝对坐标，不随文字变化
                var frameLeft:Number = titleX + 5;
                var frameRight:Number = titleX + titleW - 8;
                
                // 【能屈能伸的内锚点】：紧贴动态计算出来的 titleTxt.x，实现自适应
                var bracketLeftX:Number = titleTxt.x - gap;
                var bracketRightX:Number = titleTxt.x + titleTxt.width + gap;
                
                // 画左边（从固定的外锚点，画向浮动的内锚点，橡皮筋原理！）
                titleFrame.graphics.moveTo(frameLeft, lineY + outerHookH);
                titleFrame.graphics.lineTo(frameLeft, lineY);
                titleFrame.graphics.lineTo(bracketLeftX, lineY);
                titleFrame.graphics.lineTo(bracketLeftX, lineY - bracketH);
                titleFrame.graphics.lineTo(bracketLeftX + hookW, lineY - bracketH); 
                
                // 画右边
                titleFrame.graphics.moveTo(bracketRightX - hookW, lineY - bracketH); 
                titleFrame.graphics.lineTo(bracketRightX, lineY - bracketH);
                titleFrame.graphics.lineTo(bracketRightX, lineY);
                titleFrame.graphics.lineTo(frameRight, lineY);
                titleFrame.graphics.lineTo(frameRight, lineY + outerHookH);
            }
            
            if (headerMat) {
                var tfMat:TextField = headerMat.getChildByName("tf") as TextField;
                if (tfMat) SetLocalizedText(tfMat, showKitsMode ? "$CSF_HeaderTool" : "$CSF_HeaderItem");
            }
            
            DrawRepairIcon(); 
            
            var headRowHeight:Number = previewH; 
            var pBarH:Number = cndBarFixedH; 
            var headerBaseX:Number = listX + 30;
            var currentTextW:Number = 0; 

            if (headerTxt != null && mainCanvas.contains(headerTxt)) { mainCanvas.removeChild(headerTxt); headerTxt = null; }
            if (headerEntryMC != null && mainCanvas.contains(headerEntryMC)) { mainCanvas.removeChild(headerEntryMC); headerEntryMC = null; }

            var EntryCls:Class = getInvListEntryClass();
            if (EntryCls) {
                try {
                    headerEntryMC = new EntryCls() as MovieClip;
                    headerEntryMC.name = "headerEntryMC";
                    
                    var headerData:Object = { text: " ", count: 1, equipState: 0, isEquipped: false, favorite: targetFav, isLegendary: targetLeg, taggedForSearch: targetSearch };
                    headerEntryMC["itemIndex"] = -1;
                    headerEntryMC["SetEntryText"](headerData, "text");

                    applyFallUIFormatting(headerEntryMC, targetName, themeColor, true, headRowHeight);

                    if (headerEntryMC.hasOwnProperty("border") && headerEntryMC["border"] != null) headerEntryMC["border"].visible = false;
                    
                    headerEntryMC.x = listX; 
                    headerEntryMC.y = previewY; 
                    mainCanvas.addChild(headerEntryMC);
                    
                    var hState:Object = _uiStates[headerEntryMC];
                    if (hState && hState.textEndX != undefined) {
                        currentTextW = (hState.textEndX - hState.origX); 
                    } else if (headerEntryMC.hasOwnProperty("textField") && headerEntryMC["textField"] != null) {
                        currentTextW = headerEntryMC["textField"].textWidth;
                    } else {
                        currentTextW = 150;
                    }
                } catch (e:Error) { headerEntryMC = null; }
            }

            if (headerEntryMC == null) {
                headerTxt = CreateTextField(listX + colNameX, previewY, 300, headRowHeight, 20, themeColor, false, "left");
                applyFallUIFormatting(headerTxt, targetName, themeColor, true, headRowHeight);
                mainCanvas.addChild(headerTxt);
                
                var hState2:Object = _uiStates[headerTxt];
                if (hState2 && hState2.textEndX != undefined) {
                    currentTextW = (hState2.textEndX - hState2.origX);
                } else {
                    currentTextW = headerTxt.textWidth;
                }
            }
            
            var repairBarRight:Number = listX + colCondX + colCondW;
            var barStartX:Number = repairBarRight - cndBarFixedW; 
            targetInfoBar.x = Math.round(barStartX); 
            targetInfoBar.y = Math.round(previewY + (previewH - pBarH) / 2); 
            
            previewBarWidth = cndBarFixedW + 35; 

            while(listContainer.numChildren > 0) listContainer.removeChildAt(0);
            
            if (activeArray.length == 0) {
                var safeW:Number = Math.max(50, listW - 50);
                var emptyTxt:TextField = CreateTextField(25, 10, safeW, 30, 18, listTextColor, false, "center");
                SetLocalizedText(emptyTxt, showKitsMode ? "$CSF_NoRepairToolsAvailable" : "$CSF_NoCompatibleItemsAvailable");
                listContainer.addChild(emptyTxt); UpdateScrollbar(); 
            } else {
                var endIndex:int = Math.min(activeArray.length, listOffset + maxVisibleItems);
                for (var i:int = listOffset; i < endIndex; i++) {
                    var item:MovieClip = CreateListEntry(activeArray[i], i);
                    item.y = (i - listOffset) * rowH; listContainer.addChild(item);
                }
                UpdateScrollbar(); 
            }
        }

        private function UpdateScrollbar():void {
            if (activeArray.length <= maxVisibleItems) { scrollBarBg.visible = false; scrollBarThumb.visible = false; return; }
            scrollBarBg.visible = true; scrollBarThumb.visible = true;
            scrollBarBg.graphics.clear(); scrollBarBg.graphics.lineStyle(1.0, themeColor, 0.2); scrollBarBg.graphics.drawRect(barX, barY, barW, barH); 
            var thumbH:Number = Math.max(30, (Number(maxVisibleItems) / Number(activeArray.length)) * barH);
            if (!isDragging) { var thumbY:Number = barY + (Number(listOffset) / Number(activeArray.length - maxVisibleItems)) * (barH - thumbH); scrollBarThumb.y = thumbY; scrollBarThumb.x = barX; }
            DrawThumb(thumbH);
        }

        private function DrawThumb(h:Number):void {
            scrollBarThumb.graphics.clear(); var currentAlpha:Number = (isThumbHovered || isDragging) ? 0.5 : 1.0; 
            scrollBarThumb.graphics.beginFill(selectedTextColor, 0.0); scrollBarThumb.graphics.drawRect(-5, 0, barW + 10, h); scrollBarThumb.graphics.endFill();
            scrollBarThumb.graphics.beginFill(themeColor, currentAlpha); scrollBarThumb.graphics.drawRect(0, 0, barW, h); scrollBarThumb.graphics.endFill();
        }

        private function UpdateSelection():void {
            var limit:Number = 1.0;
            var add:Number = 0;
            
            if (activeArray.length > 0) {
                ClampIndices();
                limit = activeArray[selectedIndex].limit;
                add = activeArray[selectedIndex].addPct; 
            }
            
            var maxLimitTF:TextField = findTargetObject(this, "maxLimitText") as TextField;
            if (maxLimitTF != null) {
                if (!_isMaxLimitFormatted) {
                    var tfFormat:TextFormat = maxLimitTF.defaultTextFormat;
                    tfFormat.font = "$MAIN_Font"; 
                    maxLimitTF.defaultTextFormat = tfFormat;
                    maxLimitTF.embedFonts = false; 
                    maxLimitTF.y = Math.round(maxLimitTF.y);
                    _isMaxLimitFormatted = true; 
                }

                var newText:String = showKitsMode ? (TranslateText("$CSF_RepairLimitPrefix") + Math.round(playerMaxLimit * 100) + "%") : juryRiggingSkillStr;
                
                if (maxLimitTF.text != newText) {
                    maxLimitTF.text = newText;
                    maxLimitTF.setTextFormat(maxLimitTF.defaultTextFormat);
                    maxLimitTF.textColor = themeColor; 
                }
                maxLimitTF.visible = true; 
                maxLimitTF.alpha = 1.0;    
            }

            if (activeArray.length == 0) { 
                DrawBar(targetInfoBar, previewBarWidth, cndBarFixedH, targetBasePct, 0, targetThresholdPct, targetThresholdPct2); 
                DrawPreviewStatsPanel("", targetBasePct);
                return; 
            }
            
            var ctBlack:ColorTransform = new ColorTransform(0, 0, 0, 1, 0, 0, 0, 0); var ctNormal:ColorTransform = new ColorTransform(1, 1, 1, 1, 0, 0, 0, 0);

            for (var j:int = 0; j < listContainer.numChildren; j++) {
                var row:MovieClip = listContainer.getChildAt(j) as MovieClip; if (row == null || row.name == "emptyTxt") continue; 
                var actualIndex:int = row.idx; var isSelected:Boolean = (actualIndex == selectedIndex);
                var bg:Shape = row.getChildByName("bg") as Shape; var valTxt:TextField = row.getChildByName("valTxt") as TextField; var matBar:MovieClip = row.getChildByName("matBar") as MovieClip; var entryMC:MovieClip = row.getChildByName("entryMC") as MovieClip; var fallbackTxt:TextField = row.getChildByName("txt") as TextField; var eqIcon:TextField = row.getChildByName("eqIcon") as TextField;
                bg.graphics.clear(); 
                
                if (isSelected) {
                    bg.graphics.beginFill(themeColor, 1.0); bg.graphics.drawRect(0, 0, colCondX + colCondW + listHighlightRightExtension, rowH); bg.graphics.endFill(); bg.alpha = 0.9;
                    if (valTxt) valTxt.transform.colorTransform = ctBlack;
                    if (matBar) { DrawMiniBar(matBar, cndBarFixedW, cndBarFixedH, activeArray[actualIndex].pct, themeColor, targetThresholdPct, targetThresholdPct2); matBar.transform.colorTransform = ctNormal; }
                    if (entryMC) { entryMC.transform.colorTransform = ctBlack; try { entryMC["selected"] = true; } catch(e:Error) {} }
                    if (fallbackTxt) fallbackTxt.transform.colorTransform = ctBlack;
                    if (eqIcon) eqIcon.transform.colorTransform = ctBlack;
                } else {
                    bg.alpha = 0.0;
                    if (valTxt) valTxt.transform.colorTransform = ctNormal;
                    if (matBar) { DrawMiniBar(matBar, cndBarFixedW, cndBarFixedH, activeArray[actualIndex].pct, themeColor, targetThresholdPct, targetThresholdPct2); matBar.transform.colorTransform = ctNormal; }
                    if (entryMC) { entryMC.transform.colorTransform = ctNormal; try { entryMC["selected"] = false; } catch(e:Error) {} }
                    if (fallbackTxt) fallbackTxt.transform.colorTransform = ctNormal;
                    if (eqIcon) eqIcon.transform.colorTransform = ctNormal;
                }
            }
            
            if (targetBasePct >= limit - 0.001) add = 0;
            
            var dynBarH:Number = cndBarFixedH; 
            DrawBar(targetInfoBar, previewBarWidth, dynBarH, targetBasePct, add, targetThresholdPct, targetThresholdPct2);
            var repairedPct:Number = Math.min(limit, targetBasePct + add);
            var statsRaw:String = activeArray.length > 0 && activeArray[selectedIndex].stats != undefined ? activeArray[selectedIndex].stats : "";
            DrawPreviewStatsPanel(statsRaw, repairedPct);
            if (!isDragging) UpdateScrollbar();
        }

        private function CreateTextField(x:Number, y:Number, w:Number, h:Number, size:int, color:uint = 0xFFFFFF, bold:Boolean = false, alignStr:String = "left"):TextField {
            var tf:TextField = new TextField();
            var format:TextFormat = new TextFormat("$MAIN_Font", size, color, bold); format.align = alignStr;
            tf.defaultTextFormat = format; 
            tf.embedFonts = false; 
            tf.antiAliasType = AntiAliasType.ADVANCED;
            tf.x = x; tf.y = y; tf.width = Math.max(10, w); tf.height = Math.max(10, h); tf.selectable = false; return tf;
        }

        private function TranslateText(key:String):String {
            var tf:TextField = new TextField();
            GlobalFunc.SetText(tf, key, false);
            return tf.text;
        }

        private function SetLocalizedText(tf:TextField, key:String):void {
            if (!tf) return;
            tf.text = TranslateText(key);
            tf.setTextFormat(tf.defaultTextFormat);
        }

        private function getInvListEntryClass():Class {
            if (isEntryClassResolved) return CachedEntryClass;
            isEntryClassResolved = true; 
            try {
                var p:DisplayObject = this.parent;
                while(p) {
                    if (p.loaderInfo && p.loaderInfo.applicationDomain) {
                        if (p.loaderInfo.applicationDomain.hasDefinition("InvListEntry")) {
                            CachedEntryClass = p.loaderInfo.applicationDomain.getDefinition("InvListEntry") as Class;
                            return CachedEntryClass;
                        }
                    }
                    p = p.parent;
                }
            } catch(e:Error) {}
            return CachedEntryClass;
        }

        private function CreateListEntry(data:Object, idx:int):MovieClip {
            var row:MovieClip = new MovieClip(); row.idx = idx;
            var bg:Shape = new Shape(); bg.name = "bg"; row.addChildAt(bg, 0);

            var EntryCls:Class = getInvListEntryClass();
            var entryMC:MovieClip = null;
            if (EntryCls) {
                try {
                    entryMC = new EntryCls() as MovieClip; entryMC.name = "entryMC";
                    
                    var itemData:Object = { text: " ", count: 1, equipState: data.isEquipped ? 1 : 0, isEquipped: data.isEquipped, favorite: data.favorite, isLegendary: data.isLegendary, taggedForSearch: data.taggedForSearch };
                    entryMC["itemIndex"] = idx;
                    entryMC["SetEntryText"](itemData, "text");
                    
                    applyFallUIFormatting(entryMC, data.name, listTextColor, false, rowH);

                    if (entryMC.hasOwnProperty("border") && entryMC["border"] != null) entryMC["border"].visible = false;
                    
                    entryMC.x = 0; 
                    entryMC.y = 0;
                    row.addChild(entryMC);
                } catch (e:Error) { entryMC = null; }
            }

            if (entryMC == null) {
                var eqIcon:TextField = CreateTextField(15, (rowH - 16) / 2, 20, 26, 11, listTextColor); 
                eqIcon.name = "eqIcon"; eqIcon.text = data.isEquipped ? "■" : ""; row.addChild(eqIcon);
                var txt:TextField = CreateTextField(colNameX, 0, colNameW, rowH, 18, listTextColor, false, "left"); 
                txt.name = "txt"; 
                applyFallUIFormatting(txt, data.name, listTextColor, false, rowH);
                row.addChild(txt);
            }

            var valTxt:TextField = CreateTextField(colPctX, 0, colPctW, 26, 16, listTextColor, true, "center"); valTxt.name = "valTxt"; 
            var valHtml:String = "";
            
            var limit:Number = data.limit;
            if (targetBasePct >= limit - 0.001) {
                valHtml = "MAX";
            } else {
                var totalAdd:int = Math.floor(data.addPct * 100); 
                var needed:int = Math.round((limit - targetBasePct) * 100);
                
                if (totalAdd > needed) { 
                    var actualAdd:int = needed; 
                    var overflow:int = totalAdd - needed; 
                    valHtml = "<font color='#33FF33'>+" + actualAdd + "%</font>  <font color='#FF3333'>(+" + overflow + "%)</font>"; 
                } else { 
                    valHtml = "<font color='#33FF33'>+" + totalAdd + "%</font>"; 
                }
            }
            
            valTxt.htmlText = valHtml;
            valTxt.height = valTxt.textHeight + 6;
            valTxt.y = Math.round((rowH - valTxt.textHeight) / 2) - 2; 
            row.addChild(valTxt);

            var matBar:MovieClip = new MovieClip(); matBar.name = "matBar"; matBar.x = Math.round(colCondX + colCondW - cndBarFixedW); matBar.y = Math.round((rowH - cndBarFixedH) / 2);
            DrawMiniBar(matBar, cndBarFixedW, cndBarFixedH, data.pct, themeColor, targetThresholdPct, targetThresholdPct2); row.addChild(matBar);

            var hitArea:Shape = new Shape(); hitArea.graphics.beginFill(0x000000, 0); hitArea.graphics.drawRect(0, 0, listW, rowH); hitArea.graphics.endFill(); row.addChild(hitArea);

            row.mouseChildren = false; row.buttonMode = true; row.idx = idx;
            row.addEventListener(MouseEvent.ROLL_OVER, onMouseOverItem); row.addEventListener(MouseEvent.CLICK, onMouseClickItem);
            return row;
        }

        private function DrawMiniBar(mc:MovieClip, w:Number, h:Number, pct:Number, color:uint, thresholdPct:Number = -1, thresholdPct2:Number = -1):void {
            mc.graphics.clear(); 
            
            var tierTxt:TextField = mc.getChildByName("tierTxt") as TextField;
            var bgShape:Shape = mc.getChildByName("barBgShape") as Shape;
            var fillShape:Shape = mc.getChildByName("barFillShape") as Shape;
            var thresholdShape:Shape = mc.getChildByName("barThresholdShape") as Shape;
            if (!bgShape) {
                bgShape = new Shape();
                bgShape.name = "barBgShape";
                mc.addChild(bgShape);
                fillShape = new Shape();
                fillShape.name = "barFillShape";
                mc.addChild(fillShape);
                thresholdShape = new Shape();
                thresholdShape.name = "barThresholdShape";
                mc.addChild(thresholdShape);
            }
            if (!fillShape) {
                fillShape = new Shape();
                fillShape.name = "barFillShape";
                mc.addChild(fillShape);
            }
            if (!thresholdShape) {
                thresholdShape = new Shape();
                thresholdShape.name = "barThresholdShape";
                mc.addChild(thresholdShape);
            }
            if (!tierTxt) {
                tierTxt = CreateTextField(w + 2, Math.round((h - 20) / 2), 30, 20, 13, color, true, "left");
                tierTxt.name = "tierTxt"; 
                mc.addChild(tierTxt);
            }
            
            var isTier2:Boolean = (pct > 1.0);
            var tier:int = isTier2 ? 2 : 1;
            var visualPct:Number = isTier2 ? (pct - 1.0) : pct;
            
            var padding:Number = 4;
            var maxFillW:Number = w - padding * 2;
            var fillH:Number = h - padding * 2;
            if (maxFillW <= 0 || fillH <= 0) return;

            bgShape.graphics.clear();
            bgShape.graphics.lineStyle(2, color, 0.8); 
            bgShape.graphics.beginFill(MixWithBlack(color, 0.18), 1.0); 
            bgShape.graphics.drawRect(0, 0, w, h); 
            bgShape.graphics.endFill();

            fillShape.graphics.clear();
            fillShape.x = padding;
            fillShape.y = padding;
            thresholdShape.graphics.clear();
            thresholdShape.x = padding;
            thresholdShape.y = padding;
            
            if (visualPct > 0) { 
                var curW:Number = maxFillW * Math.min(1.0, Math.max(0.0, visualPct)); 
                if (curW > 0) {
                    DrawItemCardFillSegment(fillShape, 0, 0, 0, curW, fillH, maxFillW, -1, -1, color, false);
                }
            }
            if (!isTier2) DrawItemCardThresholdNotches(thresholdShape, 0, 0, fillH, maxFillW, thresholdPct, thresholdPct2, color);
            
            if (tier >= 2) {
                tierTxt.text = "x2";
                tierTxt.textColor = color;
                tierTxt.visible = true;
            } else {
                tierTxt.visible = false;
            }
        }

        private function DrawItemCardThresholdNotches(mc:*, xPos:Number, yPos:Number, fillH:Number, maxFillW:Number, thresholdPct:Number, thresholdPct2:Number, color:uint):void {
            if (maxFillW <= 2 || fillH <= 2) return;
            var markList:Array = [];
            if (thresholdPct >= 0 && thresholdPct <= 1) {
                var mp1:Number = maxFillW * thresholdPct;
                if (mp1 > 1 && mp1 < maxFillW) markList.push(mp1);
            }
            if (thresholdPct2 >= 0 && thresholdPct2 <= 1) {
                var mp2:Number = maxFillW * thresholdPct2;
                if (mp2 > 1 && mp2 < maxFillW) {
                    var isDup2:Boolean = false;
                    for (var di2:int = 0; di2 < markList.length; di2++) {
                        if (Math.abs(markList[di2] - mp2) < 5) { isDup2 = true; break; }
                    }
                    if (!isDup2) markList.push(mp2);
                }
            }
            if (markList.length == 0) return;
            if (markList.length > 1 && markList[0] > markList[1]) {
                var mtmp:Number = markList[0];
                markList[0] = markList[1];
                markList[1] = mtmp;
            }

            var triH:Number = Math.max(1, Math.round(fillH / 3));
            var triBw:Number = Math.max(1, Math.round(fillH * 0.25));
            var triColor:uint = MixWithBlack(color, 0.18);
            mc.graphics.beginFill(triColor, 1.0);
            for (var mi:int = 0; mi < markList.length; mi++) {
                var mx:Number = xPos + Number(markList[mi]);
                mc.graphics.moveTo(mx - triBw, yPos);
                mc.graphics.lineTo(mx, yPos + triH);
                mc.graphics.lineTo(mx + triBw, yPos);
                mc.graphics.lineTo(mx - triBw, yPos);

                mc.graphics.moveTo(mx - triBw, yPos + fillH);
                mc.graphics.lineTo(mx, yPos + fillH - triH);
                mc.graphics.lineTo(mx + triBw, yPos + fillH);
                mc.graphics.lineTo(mx - triBw, yPos + fillH);
            }
            mc.graphics.endFill();
        }

        private function DrawItemCardFillSegment(mc:*, xPos:Number, yPos:Number, segmentX:Number, segmentW:Number, fillH:Number, maxFillW:Number, thresholdPct:Number, thresholdPct2:Number, color:uint, drawNotch:Boolean):void {
            if (segmentW <= 0 || maxFillW <= 2 || fillH <= 2) return;

            var markList:Array = [];
            if (drawNotch) {
                if (thresholdPct >= 0 && thresholdPct <= 1) {
                    var mp1:Number = maxFillW * thresholdPct;
                    if (mp1 > segmentX + 1 && mp1 + Math.max(1, Math.round(fillH * 0.25)) < segmentX + segmentW) markList.push(mp1);
                }
                if (thresholdPct2 >= 0 && thresholdPct2 <= 1) {
                    var mp2:Number = maxFillW * thresholdPct2;
                    if (mp2 > segmentX + 1 && mp2 + Math.max(1, Math.round(fillH * 0.25)) < segmentX + segmentW) {
                        var isDup2:Boolean = false;
                        for (var di2:int = 0; di2 < markList.length; di2++) {
                            if (Math.abs(markList[di2] - mp2) < 5) { isDup2 = true; break; }
                        }
                        if (!isDup2) markList.push(mp2);
                    }
                }
            }
            if (markList.length > 1 && markList[0] > markList[1]) {
                var mtmp:Number = markList[0];
                markList[0] = markList[1];
                markList[1] = mtmp;
            }

            var nubH:Number = fillH * 0.5;
            var nubY:Number = (fillH - nubH) / 2;
            var triH:Number = Math.max(1, Math.round(fillH / 3));
            var triBw:Number = Math.max(1, Math.round(fillH * 0.25));
            var mainStart:Number = segmentX <= 0 ? 1 : segmentX;
            var mainEnd:Number = segmentX + segmentW;

            mc.graphics.beginFill(color, 0.8);
            if (segmentX <= 0) {
                var nubW:Number = Math.min(1, segmentW);
                if (nubW > 0) mc.graphics.drawRect(xPos, yPos + nubY, nubW, nubH);
            }
            if (mainEnd > mainStart) {
                mc.graphics.moveTo(xPos + mainStart, yPos);
                for (var topI:int = 0; topI < markList.length; topI++) {
                    var topMark:Number = Number(markList[topI]);
                    if (topMark <= mainStart || topMark >= mainEnd) continue;
                    var topMx:Number = xPos + topMark;
                    mc.graphics.lineTo(topMx - triBw, yPos);
                    mc.graphics.lineTo(topMx, yPos + triH);
                    mc.graphics.lineTo(topMx + triBw, yPos);
                }
                mc.graphics.lineTo(xPos + mainEnd, yPos);
                mc.graphics.lineTo(xPos + mainEnd, yPos + fillH);
                for (var botI:int = markList.length - 1; botI >= 0; botI--) {
                    var botMark:Number = Number(markList[botI]);
                    if (botMark <= mainStart || botMark >= mainEnd) continue;
                    var botMx:Number = xPos + botMark;
                    mc.graphics.lineTo(botMx + triBw, yPos + fillH);
                    mc.graphics.lineTo(botMx, yPos + fillH - triH);
                    mc.graphics.lineTo(botMx - triBw, yPos + fillH);
                }
                mc.graphics.lineTo(xPos + mainStart, yPos + fillH);
                mc.graphics.lineTo(xPos + mainStart, yPos);
            }
            mc.graphics.endFill();
        }

        private function MixWithBlack(color:uint, scale:Number):uint {
            var r:int = Math.max(1, Math.round(((color >> 16) & 0xFF) * scale));
            var g:int = Math.max(1, Math.round(((color >> 8) & 0xFF) * scale));
            var b:int = Math.max(1, Math.round((color & 0xFF) * scale));
            return (r << 16) | (g << 8) | b;
        }

        private function DrawBar(mc:MovieClip, w:Number, h:Number, pct:Number, add:Number = 0, thresholdPct:Number = -1, thresholdPct2:Number = -1):void {
            var bgShape:Shape = mc.getChildByName("bgShape") as Shape; 
            var curShape:Shape = mc.getChildByName("curShape") as Shape; 
            var addShape:Shape = mc.getChildByName("addShape") as Shape;
            var thresholdShape:Shape = mc.getChildByName("thresholdShape") as Shape;
            var tierTxt:TextField = mc.getChildByName("tierTxt") as TextField;

            var barW:Number = w - 35; 

            if (!bgShape) { 
                bgShape = new Shape(); bgShape.name = "bgShape"; mc.addChild(bgShape); 
                curShape = new Shape(); curShape.name = "curShape"; mc.addChild(curShape); 
                addShape = new Shape(); addShape.name = "addShape"; mc.addChild(addShape); 
                thresholdShape = new Shape(); thresholdShape.name = "thresholdShape"; mc.addChild(thresholdShape);
                
                tierTxt = CreateTextField(barW + 8, Math.round((h - 26) / 2), 40, 26, 18, themeColor, true, "left");
                tierTxt.name = "tierTxt"; 
                mc.addChild(tierTxt);
            }
            if (!thresholdShape) {
                thresholdShape = new Shape();
                thresholdShape.name = "thresholdShape";
                mc.addChild(thresholdShape);
            }
            
            var bThick:Number = 2; 
            var padding:Number = 4;
            var maxFillW:Number = barW - padding * 2; 
            var fillH:Number = h - padding * 2;
            if (maxFillW <= 0 || fillH <= 0) return;

            var isTier2:Boolean = (pct > 1.0) || (pct == 1.0 && add > 0);
            var tier:int = isTier2 ? 2 : 1;
            var visualPct:Number = isTier2 ? (pct - 1.0) : pct;
            
            var visualAdd:Number = add;
            if (visualPct + visualAdd > 1.0) {
                visualAdd = 1.0 - visualPct; 
            }

            bgShape.graphics.clear(); 
            bgShape.graphics.lineStyle(bThick, themeColor, 0.8); 
            bgShape.graphics.beginFill(MixWithBlack(themeColor, 0.18), 1.0); 
            bgShape.graphics.drawRect(0, 0, barW, h); 
            bgShape.graphics.endFill();
            
            var curW:Number = maxFillW * Math.min(1.0, Math.max(0.0, visualPct));
            curShape.graphics.clear(); 
            curShape.x = padding; 
            curShape.y = padding;
            if (curW > 0) { 
                DrawItemCardFillSegment(curShape, 0, 0, 0, curW, fillH, maxFillW, -1, -1, themeColor, false);
            }
            
            var addW:Number = maxFillW * Math.max(0.0, visualAdd);
            addShape.graphics.clear(); 
            addShape.x = padding; 
            addShape.y = padding;
            if (addW > 0) { 
                DrawItemCardFillSegment(addShape, 0, 0, curW, addW, fillH, maxFillW, -1, -1, themeColor, false);
                addShape.visible = true; 
                addShape.alpha = pulseAlpha; 
            } else { 
                addShape.visible = false; 
            }

            thresholdShape.graphics.clear();
            thresholdShape.x = padding;
            thresholdShape.y = padding;
            if (!isTier2) DrawItemCardThresholdNotches(thresholdShape, 0, 0, fillH, maxFillW, thresholdPct, thresholdPct2, themeColor);

            if (tier >= 2) {
                tierTxt.text = "x2";
                tierTxt.textColor = themeColor;
                tierTxt.visible = true;
            } else {
                tierTxt.visible = false;
            }
            
            tierTxt.x = barW + 8;
            tierTxt.y = Math.round((h - 26) / 2);
        }

        private function parseItems(rawStr:String, targetArr:Array):void {
            var mergedItems:Array = rawStr.split("|||");
            
            for each (var s:String in mergedItems) {
                if (s == null || s == "") continue;
                var d:Array = s.split(":::");
                if (d.length >= 4) { 
                    var eqIndex:int = d.length - 1;
                    while (eqIndex >= 0) {
                        var eqCandidate:String = String(d[eqIndex]);
                        eqCandidate = eqCandidate.replace(/^\s+|\s+$/g, "");
                        if (eqCandidate == "0" || eqCandidate == "1") break;
                        eqIndex--;
                    }
                    if (eqIndex < 3) continue;

                    var eq:Boolean = (String(d[eqIndex]).replace(/^\s+|\s+$/g, "") == "1");
                    var limitVal:Number = 1.0;
                    var addPctVal:Number = 0.0;
                    var pctVal:Number = 0.0;
                    var statsVal:String = "";
                    var namePartsCount:int = 0;

                    var limitIndex:int = eqIndex - 1;
                    if (limitIndex >= 0 && isNaN(parseFloat(d[limitIndex]))) {
                        statsVal = d[limitIndex];
                        limitIndex--;
                    }

                    if (limitIndex >= 3 && !isNaN(parseFloat(d[limitIndex])) && !isNaN(parseFloat(d[limitIndex - 1])) && !isNaN(parseFloat(d[limitIndex - 2]))) {
                        limitVal = parseFloat(d[limitIndex]);
                        addPctVal = parseFloat(d[limitIndex - 1]);
                        pctVal = parseFloat(d[limitIndex - 2]);
                        namePartsCount = limitIndex - 3;
                    } else if (eqIndex >= 3 && !isNaN(parseFloat(d[eqIndex - 1])) && !isNaN(parseFloat(d[eqIndex - 2]))) {
                        limitVal = 1.0;
                        addPctVal = parseFloat(d[eqIndex - 1]);
                        pctVal = parseFloat(d[eqIndex - 2]);
                        namePartsCount = eqIndex - 3;
                    } else {
                        continue;
                    }
                    
                    var nParts:Array = [];
                    for (var j:int = 1; j <= namePartsCount; j++) nParts.push(d[j]);
                    
                    var itemMeta:Object = parseUiMeta(statsVal);
                    statsVal = stripUiMeta(statsVal);
                    var finalStolen:Object = stealItemData(nParts.join(":::"), parseFloat(d[0]));
                    targetArr.push({
                        uid: parseFloat(d[0]), 
                        name: finalStolen.name, 
                        pct: pctVal, 
                        addPct: addPctVal, 
                        limit: limitVal, 
                        stats: statsVal,
                        isEquipped: eq,
                        favorite: itemMeta.hasMeta ? itemMeta.favorite : finalStolen.favorite,
                        isLegendary: itemMeta.hasMeta ? itemMeta.isLegendary : finalStolen.isLegendary,
                        taggedForSearch: itemMeta.hasTaggedForSearch ? itemMeta.taggedForSearch : finalStolen.taggedForSearch
                    }); 
                }
            }
        }

        private function ParseData(rawData:String):void {
            materialsArray = [];
            kitsArray = [];
            targetThresholdPct = -1.0;
            targetThresholdPct2 = -1.0;
            var parts:Array = rawData.split("@@@"); if (parts.length < 1) return;
            
            var t:Array = parts[0].split(":::");
            var targetMeta:Object = { hasMeta: false, favorite: false, isLegendary: false, hasTaggedForSearch: false, taggedForSearch: false };
            if (t.length > 0 && String(t[t.length - 1]).indexOf("META:") == 0) {
                targetMeta = parseUiMeta(String(t.pop()));
            }
            if (t.length >= 7 && !isNaN(parseFloat(t[t.length-5])) && !isNaN(parseFloat(t[t.length-4])) && !isNaN(parseFloat(t[t.length-2])) && !isNaN(parseFloat(t[t.length-1]))) {
                currentTargetUID = parseFloat(t[0]);
                targetBasePct = parseFloat(t[t.length-5]);
                playerMaxLimit = parseFloat(t[t.length-4]);
                juryRiggingSkillStr = t[t.length-3];
                targetThresholdPct = parseFloat(t[t.length-2]);
                targetThresholdPct2 = parseFloat(t[t.length-1]);
                var tNamePartsNew:Array = [];
                for (var tnNew:int = 1; tnNew <= t.length - 6; tnNew++) tNamePartsNew.push(t[tnNew]);
                var stolenHNew:Object = stealItemData(tNamePartsNew.join(":::"), currentTargetUID);
                targetName = stolenHNew.name;
                targetFav = targetMeta.hasMeta ? targetMeta.favorite : stolenHNew.favorite;
                targetLeg = targetMeta.hasMeta ? targetMeta.isLegendary : stolenHNew.isLegendary;
                targetSearch = targetMeta.hasTaggedForSearch ? targetMeta.taggedForSearch : stolenHNew.taggedForSearch;
            } else if (t.length >= 6 && !isNaN(parseFloat(t[t.length-4])) && !isNaN(parseFloat(t[t.length-3]))) {
                currentTargetUID = parseFloat(t[0]);
                targetBasePct = parseFloat(t[t.length-4]);
                playerMaxLimit = parseFloat(t[t.length-3]);
                juryRiggingSkillStr = t[t.length-2];
                var tNameParts:Array = [];
                for (var tn:int = 1; tn <= t.length - 5; tn++) tNameParts.push(t[tn]);
                var stolenH:Object = stealItemData(tNameParts.join(":::"), currentTargetUID);
                targetName = stolenH.name;
                targetFav = targetMeta.hasMeta ? targetMeta.favorite : stolenH.favorite;
                targetLeg = targetMeta.hasMeta ? targetMeta.isLegendary : stolenH.isLegendary;
                targetSearch = targetMeta.hasTaggedForSearch ? targetMeta.taggedForSearch : stolenH.taggedForSearch;
            } else if (t.length >= 5 && !isNaN(parseFloat(t[t.length-3])) && !isNaN(parseFloat(t[t.length-2]))) {
                currentTargetUID = parseFloat(t[0]);
                targetBasePct = parseFloat(t[t.length-3]);
                playerMaxLimit = parseFloat(t[t.length-2]);
                juryRiggingSkillStr = t[t.length-1];
                var tNamePartsCompat:Array = [];
                for (var tnCompat:int = 1; tnCompat <= t.length - 4; tnCompat++) tNamePartsCompat.push(t[tnCompat]);
                var stolenHCompat:Object = stealItemData(tNamePartsCompat.join(":::"), currentTargetUID);
                targetName = stolenHCompat.name;
                targetFav = targetMeta.hasMeta ? targetMeta.favorite : stolenHCompat.favorite;
                targetLeg = targetMeta.hasMeta ? targetMeta.isLegendary : stolenHCompat.isLegendary;
                targetSearch = targetMeta.hasTaggedForSearch ? targetMeta.taggedForSearch : stolenHCompat.taggedForSearch;
            } else if (t.length >= 3) {
                currentTargetUID = parseFloat(t[0]);
                targetBasePct = parseFloat(t[t.length - 1]); 
                playerMaxLimit = 1.0;
                juryRiggingSkillStr = "Vanilla Jury Rigging (No skill requirement)";
                var tNamePartsOld:Array = [];
                for (var tnOld:int = 1; tnOld <= t.length - 2; tnOld++) tNamePartsOld.push(t[tnOld]);
                var stolenHOld:Object = stealItemData(tNamePartsOld.join(":::"), currentTargetUID);
                targetName = stolenHOld.name;
                targetFav = targetMeta.hasMeta ? targetMeta.favorite : stolenHOld.favorite;
                targetLeg = targetMeta.hasMeta ? targetMeta.isLegendary : stolenHOld.isLegendary;
                targetSearch = targetMeta.hasTaggedForSearch ? targetMeta.taggedForSearch : stolenHOld.taggedForSearch;
            }
            
            if (parts.length > 1 && parts[1] != "") {
                parseItems(parts[1], kitsArray);
            }
            originalKitsArray = kitsArray.concat();

            var matStr:String = "";
            if (parts.length > 2 && parts[2] != "") {
                matStr = parts[2];
            } else if (parts.length == 2 && kitsArray.length == 0 && parts[1].indexOf("|||") != -1) {
                matStr = parts[1];
            }

            if (matStr != "") {
                parseItems(matStr, materialsArray);
            }
            originalMaterialsArray = materialsArray.concat();

            if (kitsArray.length == 0) showKitsMode = false;
            
            totalKitsCount = 0;
            for (var k:int = 0; k < kitsArray.length; k++) {
                var match:Array = kitsArray[k].name.match(/x(\d+)$/);
                if (match && match.length > 1) {
                    totalKitsCount += parseInt(match[1]);
                } else {
                    totalKitsCount += 1;
                }
            }
            
            try {
                if (this["UpdateRepairKitsState_Callback"] != null) {
                    this["UpdateRepairKitsState_Callback"](showKitsMode, totalKitsCount);
                } else if (Object(root).UpdateRepairKitsState_Call != null) {
                    Object(root).UpdateRepairKitsState_Call(showKitsMode, totalKitsCount);
                }
            } catch(e:Error) {}
        }

        private function onMouseOverItem(e:MouseEvent):void {
            if (isDragging) return;
            var item:MovieClip = e.currentTarget as MovieClip;
            if (item && item.idx != undefined) SetSelectedIndex(item.idx, true);
        }

        private function onListMouseMove(e:MouseEvent):void {
            if (isDragging || activeArray.length == 0) return;
            var localIdx:int = Math.floor(listContainer.mouseY / rowH);
            if (localIdx < 0 || localIdx >= maxVisibleItems) return;
            var idx:int = listOffset + localIdx;
            if (idx >= activeArray.length) return;
            SetSelectedIndex(idx, true);
        }

        private function SetSelectedIndex(idx:int, playSound:Boolean = false):void {
            if (idx < 0 || idx >= activeArray.length || idx == selectedIndex) return;
            selectedIndex = idx;
            ClampIndices();
            UpdateSelection();
            if (playSound) PlayUISound("UISelectOn");
        }

        private function onMouseClickItem(e:MouseEvent):void {
            var item:MovieClip = e.currentTarget as MovieClip;
            if (item && item.idx != undefined) { SetSelectedIndex(item.idx, false); ProcessUserEvent("Accept", false); }
        }

        public function ProcessUserEvent(p1:String, p2:Boolean):Boolean {
            if (p2) return false;
            if (p1 == "Cancel") { PlayUISound("UIMenuCancel"); return false; }
            
            if (p1 == "XButton" || p1 == "Action") {
                if (kitsArray.length > 0) {
                    showKitsMode = !showKitsMode; 
                    selectedIndex = 0; listOffset = 0; 
                    ApplySorting();
                    RenderUI();
                    UpdateSelection();
                    PlayUISound("UIMenuOK");
                    
                    try {
                        if (this["UpdateRepairKitsState_Callback"] != null) {
                            this["UpdateRepairKitsState_Callback"](showKitsMode, totalKitsCount);
                        } else if (Object(root).UpdateRepairKitsState_Call != null) {
                            Object(root).UpdateRepairKitsState_Call(showKitsMode, totalKitsCount);
                        }
                    } catch(e:Error) {}
                } else {
                    PlayUISound("UIMenuCancel"); 
                }
                return true;
            }

            if (activeArray.length == 0) return false;
            
            if (p1 == "Up" && selectedIndex > 0) { selectedIndex--; if (selectedIndex < listOffset) { listOffset = selectedIndex; RenderUI(); } UpdateSelection(); PlayUISound("UISelectOff"); return true; }
            if (p1 == "Down" && selectedIndex < activeArray.length - 1) { selectedIndex++; if (selectedIndex >= listOffset + maxVisibleItems) { listOffset = selectedIndex - maxVisibleItems + 1; RenderUI(); } UpdateSelection(); PlayUISound("UISelectOn"); return true; }
            
            if (p1 == "Accept") { 
                if (isProcessingRepair) return true; 

                var limit:Number = activeArray[selectedIndex].limit;
                if (targetBasePct >= limit - 0.001) { 
                    PlayUISound("UIMenuCancel"); 
                    return true; 
                }
                
                if (activeArray.length > 0) {
                    isProcessingRepair = true; 

                    if (showKitsMode) {
                        PlayUISound("UIModsBreakDown"); 
                    } else {
                        PlayUISound("UIModsBreakDown"); 
                    }

                    if (Object(root).ExecuteJuryRigging_Call != null) {
                        Object(root).ExecuteJuryRigging_Call(currentTargetUID, activeArray[selectedIndex].uid);
                    } else if (this["JuryRigging_Callback"] != null) {
                        this["JuryRigging_Callback"]("ExecuteJuryRigging", currentTargetUID, activeArray[selectedIndex].uid);
                    }
                }
                return true;
            }
            return false;
        }
    }
}

