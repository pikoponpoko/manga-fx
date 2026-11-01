// ObjectLibrary.jsx — レイヤー・コンポ・エフェクト・エクスプレッション・イーズを保存して
// 別のプロジェクトで呼び出す After Effects スクリプト（パネル対応）
//
// 保存先はユーザーフォルダ内の MangaFX/ObjectLibrary（パネルの Folder で変えられる）。
// 1 件 = 1 ファイル（.kzobj。ExtendScript の toSource 形式）＋サムネイル（.png）。
// レイヤーはプロパティの木をたどって値・キーフレーム・エクスプレッションを書き出し、
// 呼び出すときに同じ種類のレイヤーを作って書き戻す。フッテージはファイルの場所を覚えておき、
// プロジェクトになければ読み込む（見つからなければプレースホルダー）。
(function (thisObj) {
    var VERSION = 1;
    var CATEGORIES = ["Layers", "Comps", "Effects", "Expressions", "Ease"];
    var EXT = ".kzobj";
    var SKIP_GROUPS = { "ADBE Layer Styles": 1, "ADBE Audio Group": 1, "ADBE Layer Overrides": 1, "ADBE Data Group": 1 };
    var warnings = [];

    // ---------- 保存先 ----------
    function settingsFile() { return new File(Folder.userData.fsName + "/MangaFX/ObjectLibrary.settings"); }
    function libraryRoot() {
        var f = settingsFile(), path = null;
        if (f.exists && f.open("r")) { path = f.read(); f.close(); }
        var root = new Folder(path && path.length ? path : Folder.userData.fsName + "/MangaFX/ObjectLibrary");
        if (!root.exists) root.create();
        return root;
    }
    function setLibraryRoot(folder) {
        var f = settingsFile();
        if (!f.parent.exists) f.parent.create();
        if (f.open("w")) { f.write(folder.fsName); f.close(); }
    }
    function categoryFolder(cat) {
        var f = new Folder(libraryRoot().fsName + "/" + cat);
        if (!f.exists) f.create();
        return f;
    }
    function safeName(s) { return String(s).replace(/[\\\/:\*\?"<>\|]/g, "_").replace(/^\s+|\s+$/g, "") || "Untitled"; }
    function writeText(file, text) {
        file.encoding = "UTF-8";
        if (!file.open("w")) throw new Error("Cannot write " + file.fsName);
        file.write(text);
        file.close();
    }
    function readText(file) {
        file.encoding = "UTF-8";
        if (!file.open("r")) throw new Error("Cannot read " + file.fsName);
        var t = file.read();
        file.close();
        return t;
    }
    function listItems(cat) {
        var files = categoryFolder(cat).getFiles("*" + EXT), names = [];
        for (var i = 0; i < files.length; i++) names.push(decodeURI(files[i].name).replace(EXT, ""));
        names.sort();
        return names;
    }
    function itemFile(cat, name) { return new File(categoryFolder(cat).fsName + "/" + name + EXT); }
    function thumbFile(cat, name) { return new File(categoryFolder(cat).fsName + "/" + name + ".png"); }

    // ---------- 値の書き出し・書き戻し ----------
    function encodeValue(v, type) {
        if (type === PropertyValueType.SHAPE) {
            return { sh: 1, v: v.vertices, i: v.inTangents, o: v.outTangents, c: v.closed };
        }
        if (type === PropertyValueType.TEXT_DOCUMENT) {
            var o = { td: 1, text: v.text }, keys = ["font", "fontSize", "fillColor", "strokeColor", "strokeWidth", "applyFill",
                "applyStroke", "strokeOverFill", "justification", "tracking", "leading", "autoLeading", "fauxBold", "fauxItalic",
                "allCaps", "smallCaps", "baselineShift", "horizontalScale", "verticalScale"];
            for (var k = 0; k < keys.length; k++) { try { o[keys[k]] = v[keys[k]]; } catch (e) { } }
            return o;
        }
        if (type === PropertyValueType.MARKER) {
            return { mk: 1, comment: v.comment, duration: v.duration, chapter: v.chapter, url: v.url, label: v.label };
        }
        return v;
    }
    function decodeValue(o, prop) {
        if (o && o.sh) {
            var s = new Shape();
            s.vertices = o.v; s.inTangents = o.i; s.outTangents = o.o; s.closed = o.c;
            return s;
        }
        if (o && o.td) {
            var td = prop.value;
            td.text = o.text;
            for (var k in o) {
                if (k === "td" || k === "text") continue;
                try { td[k] = o[k]; } catch (e) { }
            }
            return td;
        }
        if (o && o.mk) {
            var m = new MarkerValue(o.comment);
            try { m.duration = o.duration; m.chapter = o.chapter; m.url = o.url; m.label = o.label; } catch (e) { }
            return m;
        }
        return o;
    }
    function easeArray(arr) {
        var out = [];
        for (var i = 0; i < arr.length; i++) out.push([arr[i].speed, arr[i].influence]);
        return out;
    }
    function makeEase(arr) {
        var out = [];
        for (var i = 0; i < arr.length; i++) out.push(new KeyframeEase(arr[i][0], arr[i][1]));
        return out;
    }

    function serializeKeys(p, base) {
        var keys = [];
        for (var k = 1; k <= p.numKeys; k++) {
            var o = {
                t: p.keyTime(k) - base,
                v: encodeValue(p.keyValue(k), p.propertyValueType),
                ii: p.keyInInterpolationType(k),
                oi: p.keyOutInterpolationType(k)
            };
            try { o.ie = easeArray(p.keyInTemporalEase(k)); o.oe = easeArray(p.keyOutTemporalEase(k)); } catch (e) { }
            try { o.tc = p.keyTemporalContinuous(k); o.ta = p.keyTemporalAutoBezier(k); } catch (e) { }
            if (p.isSpatial) {
                try {
                    o.ist = p.keyInSpatialTangent(k); o.ost = p.keyOutSpatialTangent(k);
                    o.sc = p.keySpatialContinuous(k); o.sa = p.keySpatialAutoBezier(k); o.rv = p.keyRoving(k);
                } catch (e) { }
            }
            keys.push(o);
        }
        return keys;
    }

    function serializeProperty(p, base) {
        if (p.propertyType === PropertyType.PROPERTY) {
            var t = p.propertyValueType;
            if (t === PropertyValueType.NO_VALUE || t === PropertyValueType.CUSTOM_VALUE) return null;
            var modified = true;
            try { modified = p.isModified; } catch (e) { }
            var hasExpr = false;
            try { hasExpr = p.canSetExpression && p.expression !== ""; } catch (e) { }
            if (!modified && p.numKeys === 0 && !hasExpr) return null;
            var o = { k: "p", mn: p.matchName, n: p.name };
            try { if (p.isSeparationLeader) o.sep = p.dimensionsSeparated; } catch (e) { }
            if (o.sep) return o;  // 次元を分けているときは X / Y / Z 側に値がある
            if (p.numKeys > 0) o.keys = serializeKeys(p, base);
            else { try { o.v = encodeValue(p.value, t); } catch (e) { } }
            if (hasExpr) { o.ex = p.expression; o.exOn = p.expressionEnabled; }
            return o;
        }
        if (SKIP_GROUPS[p.matchName]) return null;
        var g = { k: "g", mn: p.matchName, n: p.name, ix: p.propertyType === PropertyType.INDEXED_GROUP, c: [] };
        try { if (p.canSetEnabled && !p.enabled) g.off = true; } catch (e) { }
        if (p.matchName === "ADBE Mask Atom") {
            try { g.maskMode = p.maskMode; g.inv = p.inverted; g.locked = p.locked; g.color = p.color; g.featherFalloff = p.maskFeatherFalloff; } catch (e) { }
        }
        for (var i = 1; i <= p.numProperties; i++) {
            var c = serializeProperty(p.property(i), base);
            if (c) g.c.push(c);
        }
        // 名前付きグループで中身に変更がなければ省く（インデックス付きのグループ＝足したもの は残す）
        if (!g.ix && g.c.length === 0 && !g.off && p.parentProperty && p.parentProperty.propertyType !== PropertyType.INDEXED_GROUP) return null;
        return g;
    }

    function resolve(root, path) {
        var p = root;
        for (var i = 0; i < path.length; i++) p = p.property(path[i]);
        return p;
    }

    function applyValue(prop, o, offset) {
        if (o.sep !== undefined) { try { prop.dimensionsSeparated = o.sep; } catch (e) { } }
        if (o.keys) {
            while (prop.numKeys > 0) prop.removeKey(1);
            var i;
            for (i = 0; i < o.keys.length; i++) prop.setValueAtTime(o.keys[i].t + offset, decodeValue(o.keys[i].v, prop));
            for (i = 0; i < o.keys.length; i++) {
                var k = o.keys[i], idx = i + 1;
                try { if (k.ist) { prop.setSpatialTangentsAtKey(idx, k.ist, k.ost); prop.setSpatialContinuousAtKey(idx, k.sc); prop.setSpatialAutoBezierAtKey(idx, k.sa); } } catch (e) { }
                try { if (k.ie) prop.setTemporalEaseAtKey(idx, makeEase(k.ie), makeEase(k.oe)); } catch (e) { }
                try { prop.setInterpolationTypeAtKey(idx, k.ii, k.oi); } catch (e) { }
                try { if (k.tc !== undefined) { prop.setTemporalContinuousAtKey(idx, k.tc); prop.setTemporalAutoBezierAtKey(idx, k.ta); } } catch (e) { }
                try { if (k.rv) prop.setRovingAtKey(idx, true); } catch (e) { }
            }
        } else if (o.v !== undefined) {
            prop.setValue(decodeValue(o.v, prop));
        }
        if (o.ex !== undefined) {
            try { prop.expression = o.ex; prop.expressionEnabled = o.exOn; } catch (e) { warnings.push(prop.name + ": " + e.toString()); }
        }
    }

    // root（レイヤーなど）から path をたどったグループに、保存した子を書き戻す。
    // プロパティを足すと兄弟への参照が無効になることがあるので、毎回 path から取り直す
    function applyGroup(root, path, g, offset) {
        for (var i = 0; i < g.c.length; i++) {
            var c = g.c[i], group = resolve(root, path), target = null, idx = 0;
            try {
                if (g.ix) {
                    if (!group.canAddProperty(c.mn)) { warnings.push("Cannot add " + c.n); continue; }
                    group.addProperty(c.mn);
                    group = resolve(root, path);
                    idx = group.numProperties;
                    target = group.property(idx);
                    try { target.name = c.n; } catch (e) { }
                } else {
                    target = group.property(c.mn);
                    if (!target) target = group.property(c.n);
                    if (!target) continue;
                    idx = target.propertyIndex;
                }
                var childPath = path.concat([idx]);
                if (c.k === "g") {
                    applyGroup(root, childPath, c, offset);
                    target = resolve(root, childPath);
                    if (c.maskMode !== undefined) {
                        try { target.maskMode = c.maskMode; target.inverted = c.inv; target.color = c.color; target.maskFeatherFalloff = c.featherFalloff; target.locked = c.locked; } catch (e) { }
                    }
                    if (c.off) { try { target.enabled = false; } catch (e) { } }
                } else {
                    applyValue(target, c, offset);
                }
            } catch (e) {
                warnings.push(c.n + ": " + e.toString());
            }
        }
    }

    // ---------- レイヤー・コンポ ----------
    function layerType(layer) {
        if (layer instanceof TextLayer) return "text";
        if (layer instanceof ShapeLayer) return "shape";
        if (layer instanceof CameraLayer) return "camera";
        if (layer instanceof LightLayer) return "light";
        if (layer.nullLayer) return "null";
        var src = layer.source;
        if (src instanceof CompItem) return "comp";
        if (src instanceof FootageItem) {
            if (src.mainSource instanceof SolidSource) return "solid";
            if (src.mainSource instanceof FileSource && src.mainSource.file) return "footage";
            if (src.mainSource instanceof PlaceholderSource) return "placeholder";
        }
        return "other";
    }

    function serializeLayer(layer, indexMap, data) {
        var o = {
            type: layerType(layer), name: layer.name, label: layer.label, enabled: layer.enabled, shy: layer.shy,
            startTime: layer.startTime, inPoint: layer.inPoint, outPoint: layer.outPoint, comment: layer.comment,
            parent: layer.parent ? (indexMap[layer.parent.index] !== undefined ? indexMap[layer.parent.index] : -1) : -1
        };
        if (layer instanceof AVLayer) {
            o.threeD = layer.threeDLayer; o.adjustment = layer.adjustmentLayer; o.blendingMode = layer.blendingMode;
            o.motionBlur = layer.motionBlur; o.collapse = layer.collapseTransformation; o.quality = layer.quality;
            o.stretch = layer.stretch; o.guide = layer.guideLayer; o.timeRemap = layer.timeRemapEnabled;
            o.preserveTransparency = layer.preserveTransparency;
            try { o.trackMatteType = layer.trackMatteType; } catch (e) { }
            try { if (layer.trackMatteLayer) o.trackMatte = indexMap[layer.trackMatteLayer.index]; } catch (e) { }
            var src = layer.source;
            if (o.type === "solid") o.solid = { color: src.mainSource.color, w: src.width, h: src.height, pa: src.pixelAspect, name: src.name };
            if ((o.type === "footage" || o.type === "placeholder" || o.type === "other") && src && src.mainSource)
                o.footage = { path: src.mainSource.file ? src.mainSource.file.fsName : "", name: src.name, w: src.width, h: src.height,
                              fps: src.frameRate || 30, dur: src.duration || 1 };
            if (o.type === "comp") o.compId = serializeComp(src, data);
        }
        if (o.type === "light") o.lightType = layer.lightType;
        o.props = [];
        for (var i = 1; i <= layer.numProperties; i++) {
            var g = serializeProperty(layer.property(i), 0);
            if (g) o.props.push(g);
        }
        return o;
    }

    // コンポを data.comps に入れて ID を返す（入れ子のプリコンポも同じ表に入る）
    function serializeComp(comp, data) {
        var id = "c" + comp.id;
        if (data.comps[id]) return id;
        var c = { name: comp.name, w: comp.width, h: comp.height, pa: comp.pixelAspect, dur: comp.duration, fps: comp.frameRate,
                  bg: comp.bgColor, workStart: comp.workAreaStart, workDur: comp.workAreaDuration, motionBlur: comp.motionBlur,
                  frameBlending: comp.frameBlending, layers: [] };
        data.comps[id] = c;
        var map = {};
        for (var i = 1; i <= comp.numLayers; i++) map[i] = i - 1;
        for (i = 1; i <= comp.numLayers; i++) c.layers.push(serializeLayer(comp.layer(i), map, data));
        return id;
    }

    function findFootage(path) {
        for (var i = 1; i <= app.project.numItems; i++) {
            var it = app.project.item(i);
            if (it instanceof FootageItem && it.mainSource instanceof FileSource && it.mainSource.file && it.mainSource.file.fsName === path) return it;
        }
        return null;
    }

    function libraryFolderItem() {
        for (var i = 1; i <= app.project.numItems; i++) {
            var it = app.project.item(i);
            if (it instanceof FolderItem && it.name === "Object Library") return it;
        }
        return app.project.items.addFolder("Object Library");
    }

    function buildComp(id, data, built) {
        if (built[id]) return built[id];
        var c = data.comps[id];
        var comp = app.project.items.addComp(c.name, c.w, c.h, c.pa, c.dur, c.fps);
        comp.parentFolder = libraryFolderItem();
        comp.bgColor = c.bg;
        try { comp.workAreaStart = c.workStart; comp.workAreaDuration = c.workDur; comp.motionBlur = c.motionBlur; comp.frameBlending = c.frameBlending; } catch (e) { }
        built[id] = comp;
        restoreLayers(comp, c.layers, data, built, 0);
        return comp;
    }

    function createLayer(comp, o, data, built) {
        var layers = comp.layers;
        switch (o.type) {
            case "text": return layers.addText("");
            case "shape": return layers.addShape();
            case "null": return layers.addNull();
            case "camera": return layers.addCamera(o.name, [comp.width / 2, comp.height / 2]);
            case "light": {
                var l = layers.addLight(o.name, [comp.width / 2, comp.height / 2]);
                try { l.lightType = o.lightType; } catch (e) { }
                return l;
            }
            case "solid": return layers.addSolid(o.solid.color, o.solid.name, o.solid.w, o.solid.h, o.solid.pa);
            case "comp": return layers.add(buildComp(o.compId, data, built));
            default: {
                if (!o.footage) throw new Error("Unsupported layer type");
                var f = o.footage, item = f.path ? findFootage(f.path) : null;
                if (!item && f.path && new File(f.path).exists) {
                    item = app.project.importFile(new ImportOptions(new File(f.path)));
                    item.parentFolder = libraryFolderItem();
                }
                if (!item) {
                    item = app.project.importPlaceholder(f.name, f.w, f.h, f.fps, f.dur);
                    warnings.push("Missing file: " + f.path);
                }
                return layers.add(item);
            }
        }
    }

    // 下のレイヤーから作ると、最後に作った（一番上の）レイヤーがコンポの一番上に来る
    function restoreLayers(comp, list, data, built, offset) {
        var made = [];
        for (var i = list.length - 1; i >= 0; i--) {
            var o = list[i], layer;
            try { layer = createLayer(comp, o, data, built); } catch (e) { warnings.push(o.name + ": " + e.toString()); continue; }
            made[i] = layer;
            layer.name = o.name;
            try {
                layer.label = o.label; layer.shy = o.shy; layer.comment = o.comment;
                if (layer instanceof AVLayer) {
                    layer.threeDLayer = o.threeD; layer.adjustmentLayer = o.adjustment; layer.blendingMode = o.blendingMode;
                    layer.motionBlur = o.motionBlur; layer.quality = o.quality; layer.guideLayer = o.guide;
                    layer.preserveTransparency = o.preserveTransparency;
                    if (layer.canSetCollapseTransformation) layer.collapseTransformation = o.collapse;
                    if (layer.canSetTimeRemapEnabled) layer.timeRemapEnabled = o.timeRemap;
                    layer.stretch = o.stretch;
                }
                layer.startTime = o.startTime + offset;
                layer.inPoint = o.inPoint + offset;
                layer.outPoint = o.outPoint + offset;
            } catch (e) { warnings.push(o.name + ": " + e.toString()); }
            for (var g = 0; g < o.props.length; g++) {
                var top = layer.property(o.props[g].mn);
                if (!top) continue;
                // キーの時間はコンポの時間で保存しているので offset だけずらして戻す
                if (o.props[g].k === "p") { try { applyValue(top, o.props[g], offset); } catch (e) { warnings.push(o.name + ": " + e.toString()); } }
                else applyGroup(layer, [top.propertyIndex], o.props[g], offset);
            }
            layer.enabled = o.enabled;
        }
        for (i = 0; i < list.length; i++) {
            if (!made[i]) continue;
            if (list[i].parent >= 0 && made[list[i].parent]) { try { made[i].setParentWithJump(made[list[i].parent]); } catch (e) { } }
            if (list[i].trackMatte !== undefined && made[list[i].trackMatte]) {
                try { made[i].setTrackMatte(made[list[i].trackMatte], list[i].trackMatteType); } catch (e) {
                    try { made[i].trackMatteType = list[i].trackMatteType; } catch (e2) { }
                }
            } else if (list[i].trackMatteType !== undefined && list[i].trackMatteType !== TrackMatteType.NO_TRACK_MATTE) {
                try { made[i].trackMatteType = list[i].trackMatteType; } catch (e) { }
            }
        }
        return made;
    }

    // ---------- 保存 ----------
    function activeComp() {
        var c = app.project.activeItem;
        return c instanceof CompItem ? c : null;
    }

    function selectedProps(comp, filter) {
        var out = [], layers = comp.selectedLayers;
        for (var i = 0; i < layers.length; i++) {
            var ps = layers[i].selectedProperties;
            for (var j = 0; j < ps.length; j++) if (ps[j].propertyType === PropertyType.PROPERTY && filter(ps[j])) out.push(ps[j]);
        }
        return out;
    }

    function capture(cat) {
        var comp = activeComp(), data = { ver: VERSION, kind: cat, comps: {} }, i;
        if (cat === "Layers") {
            if (!comp || comp.selectedLayers.length === 0) throw new Error("Select layers.");
            var sel = comp.selectedLayers.slice(0).sort(function (a, b) { return a.index - b.index; }), map = {};
            for (i = 0; i < sel.length; i++) map[sel[i].index] = i;
            data.layers = [];
            for (i = 0; i < sel.length; i++) data.layers.push(serializeLayer(sel[i], map, data));
            var t0 = 1e9;
            for (i = 0; i < data.layers.length; i++) t0 = Math.min(t0, data.layers[i].inPoint);
            data.start = t0;
            data.defaultName = sel[0].name;
        } else if (cat === "Comps") {
            var items = app.project.selection, c = null;
            for (i = 0; i < items.length; i++) if (items[i] instanceof CompItem) { c = items[i]; break; }
            if (!c) c = comp;
            if (!c) throw new Error("Select a composition.");
            data.compId = serializeComp(c, data);
            data.defaultName = c.name;
        } else if (cat === "Effects") {
            if (!comp || comp.selectedLayers.length === 0) throw new Error("Select a layer.");
            var layer = comp.selectedLayers[0], parade = layer.property("ADBE Effect Parade");
            var chosen = [], ps = layer.selectedProperties;
            for (i = 0; i < ps.length; i++) if (ps[i].parentProperty === parade || (ps[i].parentProperty && ps[i].parentProperty.matchName === "ADBE Effect Parade")) chosen.push(ps[i]);
            if (chosen.length === 0) for (i = 1; i <= parade.numProperties; i++) chosen.push(parade.property(i));
            if (chosen.length === 0) throw new Error("The layer has no effects.");
            data.effects = { k: "g", mn: "ADBE Effect Parade", ix: true, c: [] };
            for (i = 0; i < chosen.length; i++) {
                var g = serializeProperty(chosen[i], layer.startTime);
                if (g) data.effects.c.push(g);
            }
            data.defaultName = chosen[0].name;
        } else if (cat === "Expressions") {
            if (!comp) throw new Error("Open a composition.");
            var ex = selectedProps(comp, function (p) { return p.canSetExpression && p.expression !== ""; });
            if (ex.length === 0) throw new Error("Select a property with an expression.");
            data.expression = ex[0].expression;
            data.defaultName = ex[0].name;
        } else if (cat === "Ease") {
            if (!comp) throw new Error("Open a composition.");
            var kp = selectedProps(comp, function (p) { return p.selectedKeys.length > 0; });
            if (kp.length === 0) throw new Error("Select a keyframe.");
            var p = kp[0], k = p.selectedKeys[0];
            data.ease = { ii: p.keyInInterpolationType(k), oi: p.keyOutInterpolationType(k),
                          ie: easeArray(p.keyInTemporalEase(k))[0], oe: easeArray(p.keyOutTemporalEase(k))[0] };
            data.defaultName = "Ease " + Math.round(data.ease.ie[1]) + "-" + Math.round(data.ease.oe[1]);
        }
        return data;
    }

    function saveThumb(cat, name) {
        var comp = activeComp(), f = thumbFile(cat, name);
        if (cat === "Comps") {
            var items = app.project.selection;
            for (var i = 0; i < items.length; i++) if (items[i] instanceof CompItem) { comp = items[i]; break; }
        }
        if (!comp || (cat !== "Layers" && cat !== "Comps" && cat !== "Effects")) return;
        try { comp.saveFrameToPng(comp.time, f); } catch (e) { }
    }

    // ---------- 呼び出し ----------
    function applyItem(cat, name) {
        var data = eval("(" + readText(itemFile(cat, name)) + ")"), comp = activeComp(), i, j;
        warnings = [];
        if (cat === "Layers") {
            if (!comp) throw new Error("Open a composition.");
            var made = restoreLayers(comp, data.layers, data, {}, comp.time - data.start);
            for (i = 1; i <= comp.numLayers; i++) comp.layer(i).selected = false;
            for (i = 0; i < made.length; i++) if (made[i]) made[i].selected = true;
        } else if (cat === "Comps") {
            var c = buildComp(data.compId, data, {});
            c.openInViewer();
        } else if (cat === "Effects") {
            if (!comp || comp.selectedLayers.length === 0) throw new Error("Select layers.");
            var sel = comp.selectedLayers;
            for (i = 0; i < sel.length; i++) {
                var parade = sel[i].property("ADBE Effect Parade");
                applyGroup(sel[i], [parade.propertyIndex], data.effects, sel[i].startTime);
            }
        } else if (cat === "Expressions") {
            if (!comp) throw new Error("Open a composition.");
            var ps = selectedProps(comp, function (p) { return p.canSetExpression; });
            if (ps.length === 0) throw new Error("Select properties.");
            for (i = 0; i < ps.length; i++) ps[i].expression = data.expression;
        } else if (cat === "Ease") {
            if (!comp) throw new Error("Open a composition.");
            var kp = selectedProps(comp, function (p) { return p.selectedKeys.length > 0; });
            if (kp.length === 0) throw new Error("Select keyframes.");
            for (i = 0; i < kp.length; i++) {
                var p = kp[i], keys = p.selectedKeys;
                for (j = 0; j < keys.length; j++) {
                    var dims = p.keyInTemporalEase(keys[j]).length, ie = [], oe = [];
                    for (var d = 0; d < dims; d++) { ie.push(new KeyframeEase(data.ease.ie[0], data.ease.ie[1])); oe.push(new KeyframeEase(data.ease.oe[0], data.ease.oe[1])); }
                    p.setInterpolationTypeAtKey(keys[j], data.ease.ii, data.ease.oi);
                    p.setTemporalEaseAtKey(keys[j], ie, oe);
                }
            }
        }
        if (warnings.length) alert("Done with warnings:\n" + warnings.slice(0, 15).join("\n"));
    }

    // ---------- パネル ----------
    function buildUI(thisObj) {
        var win = thisObj instanceof Panel ? thisObj : new Window("palette", "Object Library", undefined, { resizeable: true });
        win.orientation = "column";
        win.alignChildren = ["fill", "top"];
        win.spacing = 6;
        win.margins = 8;

        var top = win.add("group");
        top.alignChildren = ["fill", "center"];
        var cat = top.add("dropdownlist", undefined, CATEGORIES);
        cat.selection = 0;
        var list = win.add("listbox", undefined, [], { multiselect: false });
        list.preferredSize = [220, 180];
        list.alignment = ["fill", "fill"];
        var thumb = win.add("group");
        thumb.preferredSize = [220, 124];
        thumb.alignment = ["fill", "top"];
        var thumbImage = null;
        thumb.onDraw = function () {
            var gfx = this.graphics, w = this.size[0], h = this.size[1];
            if (!thumbImage) return;
            var iw = thumbImage.size[0], ih = thumbImage.size[1], s = Math.min(w / iw, h / ih);
            gfx.drawImage(thumbImage, (w - iw * s) / 2, (h - ih * s) / 2, iw * s, ih * s);
        };

        var row1 = win.add("group");
        var btnSave = row1.add("button", undefined, "Save");
        var btnApply = row1.add("button", undefined, "Apply");
        var row2 = win.add("group");
        var btnRename = row2.add("button", undefined, "Rename");
        var btnDelete = row2.add("button", undefined, "Delete");
        var row3 = win.add("group");
        var btnFolder = row3.add("button", undefined, "Folder...");
        var btnOpen = row3.add("button", undefined, "Show");

        function current() { return list.selection ? list.selection.text : null; }
        function refresh(select) {
            list.removeAll();
            var names = listItems(cat.selection.text);
            for (var i = 0; i < names.length; i++) {
                var it = list.add("item", names[i]);
                if (names[i] === select) list.selection = it;
            }
            updateThumb();
        }
        function updateThumb() {
            thumbImage = null;
            var n = current();
            if (n) {
                var f = thumbFile(cat.selection.text, n);
                if (f.exists) { try { thumbImage = ScriptUI.newImage(f); } catch (e) { } }
            }
            thumb.hide(); thumb.show();  // 描き直す
        }

        cat.onChange = function () { refresh(); };
        list.onChange = updateThumb;
        list.onDoubleClick = function () { btnApply.onClick(); };

        btnSave.onClick = function () {
            var c = cat.selection.text, data;
            try { data = capture(c); } catch (e) { alert(e.message || e.toString()); return; }
            var name = prompt("Name", data.defaultName || "Untitled");
            if (name === null) return;
            name = safeName(name);
            if (itemFile(c, name).exists && !confirm("Overwrite \"" + name + "\"?")) return;
            delete data.defaultName;
            try {
                writeText(itemFile(c, name), data.toSource());
                saveThumb(c, name);
            } catch (e) { alert(e.toString()); return; }
            refresh(name);
        };
        btnApply.onClick = function () {
            var n = current();
            if (!n) return;
            app.beginUndoGroup("Object Library: " + n);
            try { applyItem(cat.selection.text, n); } catch (e) { alert(e.message || e.toString()); }
            app.endUndoGroup();
        };
        btnRename.onClick = function () {
            var n = current();
            if (!n) return;
            var nn = prompt("Name", n);
            if (nn === null) return;
            nn = safeName(nn);
            if (nn === n) return;
            if (itemFile(cat.selection.text, nn).exists) { alert("\"" + nn + "\" already exists."); return; }
            itemFile(cat.selection.text, n).rename(nn + EXT);
            var t = thumbFile(cat.selection.text, n);
            if (t.exists) t.rename(nn + ".png");
            refresh(nn);
        };
        btnDelete.onClick = function () {
            var n = current();
            if (!n || !confirm("Delete \"" + n + "\"?")) return;
            itemFile(cat.selection.text, n).remove();
            var t = thumbFile(cat.selection.text, n);
            if (t.exists) t.remove();
            refresh();
        };
        btnFolder.onClick = function () {
            var f = Folder.selectDialog("Library folder");
            if (!f) return;
            setLibraryRoot(f);
            refresh();
        };
        btnOpen.onClick = function () { categoryFolder(cat.selection.text).execute(); };

        win.onResizing = win.onResize = function () { this.layout.resize(); };
        refresh();
        if (win instanceof Window) { win.center(); win.show(); } else win.layout.layout(true);
    }

    buildUI(thisObj);
})(this);
