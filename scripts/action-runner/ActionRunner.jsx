// ActionRunner.jsx — よく使う操作の手順を「アクション」として並べ、1 クリックで実行する
// After Effects スクリプト（パネル対応）
//
// アクション = 手順（ステップ）の並び。ステップは下の STEPS から選び、必要なら引数を付ける。
// メニューコマンドは名前で呼べるので、メニューにある操作ならたいてい手順に入れられる。
// 操作の記録（マクロ記録）は After Effects のスクリプトからはできないため、手順は一覧から組み立てる。
// 「Export .jsx」でアクション 1 つを単独のスクリプトに書き出せる（キーボードショートカットや KBar 用）。
(function (thisObj) {
    // ---------- 共通 ----------
    function comp() {
        var c = app.project.activeItem;
        if (!(c instanceof CompItem)) throw new Error("Open a composition.");
        return c;
    }
    function selected(c) {
        var l = c.selectedLayers;
        if (l.length === 0) throw new Error("Select layers.");
        return l;
    }
    function frames(c, arg) { return (parseFloat(arg) || 0) * c.frameDuration; }
    // "Transform/Opacity" のような道筋（表示名でも matchName でも可）
    function findProp(layer, path) {
        var parts = String(path).split("/"), p = layer;
        for (var i = 0; i < parts.length && p; i++) p = p.property(parts[i].replace(/^\s+|\s+$/g, ""));
        if (!p || p.propertyType !== PropertyType.PROPERTY) throw new Error("Property not found: " + path);
        return p;
    }
    function splitAssign(arg) {
        var i = String(arg).indexOf("=");
        if (i < 0) throw new Error("Use the form Path=Value");
        return [String(arg).substring(0, i), String(arg).substring(i + 1)];
    }
    function parseValue(s) {
        s = String(s).replace(/^\s+|\s+$/g, "");
        if (s.charAt(0) === "[") return eval(s);
        var n = parseFloat(s);
        return isNaN(n) ? s : n;
    }
    // レイヤーの中身の矩形（コンポの時間で）
    function rect(layer, t) { return layer.sourceRectAtTime(t - layer.startTime, false); }
    // レイヤー座標の点をコンポ座標へ（2D。親子付けは無視）
    function anchorTo(layer, t, x, y) {
        var tr = layer.property("ADBE Transform Group");
        var a = tr.property("ADBE Anchor Point").valueAtTime(t, false);
        var p = tr.property("ADBE Position").valueAtTime(t, false);
        var s = tr.property("ADBE Scale").valueAtTime(t, false);
        var r = tr.property("ADBE Rotate Z").valueAtTime(t, false) * Math.PI / 180;
        var dx = (x - a[0]) * s[0] / 100, dy = (y - a[1]) * s[1] / 100;
        return [p[0] + dx * Math.cos(r) - dy * Math.sin(r), p[1] + dx * Math.sin(r) + dy * Math.cos(r)];
    }
    function setStatic(prop, value) {
        if (prop.numKeys > 0) prop.setValueAtTime(prop.propertyGroup(prop.propertyDepth).containingComp.time, value);
        else prop.setValue(value);
    }
    function setVec(prop, v2) {
        var cur = prop.value, v = cur.length > 2 ? [v2[0], v2[1], cur[2]] : v2;
        setStatic(prop, v);
    }

    // ---------- ステップ一覧 ----------
    var STEPS = [
        { id: "menu", label: "Menu Command", arg: "Command name or ID", run: function (a) {
            var id = parseInt(a, 10);
            if (isNaN(id)) id = app.findMenuCommandId(a);
            if (!id) throw new Error("Menu command not found: " + a);
            app.executeCommand(id);
        } },
        { id: "precompose", label: "Precompose", arg: "Comp name", run: function (a) {
            var c = comp(), l = selected(c), idx = [];
            for (var i = 0; i < l.length; i++) idx.push(l[i].index);
            c.layers.precompose(idx, a || (l[0].name + " Comp"), true);
        } },
        { id: "anchorCenter", label: "Anchor Point to Center", run: function () {
            var c = comp(), l = selected(c), t = c.time;
            for (var i = 0; i < l.length; i++) {
                if (!(l[i] instanceof AVLayer)) continue;
                var r = rect(l[i], t), cx = r.left + r.width / 2, cy = r.top + r.height / 2;
                var tr = l[i].property("ADBE Transform Group");
                setVec(tr.property("ADBE Position"), anchorTo(l[i], t, cx, cy));
                setVec(tr.property("ADBE Anchor Point"), [cx, cy]);
            }
        } },
        { id: "centerInComp", label: "Center in Comp", run: function () {
            var c = comp(), l = selected(c), t = c.time;
            for (var i = 0; i < l.length; i++) {
                if (!(l[i] instanceof AVLayer)) continue;
                var r = rect(l[i], t), p = anchorTo(l[i], t, r.left + r.width / 2, r.top + r.height / 2);
                var pos = l[i].property("ADBE Transform Group").property("ADBE Position"), cur = pos.valueAtTime(t, false);
                setVec(pos, [cur[0] + c.width / 2 - p[0], cur[1] + c.height / 2 - p[1]]);
            }
        } },
        { id: "fit", label: "Fit to Comp", arg: "Both / Width / Height", run: function (a) {
            var c = comp(), l = selected(c), t = c.time, mode = String(a || "Both").toLowerCase();
            for (var i = 0; i < l.length; i++) {
                if (!(l[i] instanceof AVLayer)) continue;
                var r = rect(l[i], t);
                if (r.width <= 0 || r.height <= 0) continue;
                var sx = c.width / r.width * 100, sy = c.height / r.height * 100;
                var s = mode.indexOf("w") === 0 ? sx : (mode.indexOf("h") === 0 ? sy : Math.min(sx, sy));
                var sc = l[i].property("ADBE Transform Group").property("ADBE Scale");
                setStatic(sc, sc.value.length > 2 ? [s, s, sc.value[2]] : [s, s]);
            }
        } },
        { id: "nullParent", label: "Add Null Parent", arg: "Null name", run: function (a) {
            var c = comp(), l = selected(c), x = 0, y = 0, top = l[0];
            for (var i = 0; i < l.length; i++) {
                var p = l[i].property("ADBE Transform Group").property("ADBE Position").valueAtTime(c.time, false);
                x += p[0]; y += p[1];
                if (l[i].index < top.index) top = l[i];
            }
            var n = c.layers.addNull();
            n.name = a || "Control";
            n.property("ADBE Transform Group").property("ADBE Position").setValue([x / l.length, y / l.length]);
            n.moveBefore(top);
            for (i = 0; i < l.length; i++) if (!l[i].parent) l[i].parent = n;
        } },
        { id: "sequence", label: "Sequence Layers", arg: "Overlap (frames)", run: function (a) {
            var c = comp(), l = selected(c).slice(0).sort(function (p, q) { return p.index - q.index; }), ov = frames(c, a);
            for (var i = 1; i < l.length; i++) {
                var d = l[i - 1].outPoint - ov - l[i].inPoint;
                l[i].startTime += d;
            }
        } },
        { id: "stagger", label: "Stagger Layers", arg: "Offset (frames)", run: function (a) {
            var c = comp(), l = selected(c).slice(0).sort(function (p, q) { return p.index - q.index; }), off = frames(c, a || 2);
            for (var i = 1; i < l.length; i++) l[i].startTime += off * i;
        } },
        { id: "toTime", label: "Move to Current Time", run: function () {
            var c = comp(), l = selected(c);
            for (var i = 0; i < l.length; i++) l[i].startTime += c.time - l[i].inPoint;
        } },
        { id: "trimWork", label: "Trim to Work Area", run: function () {
            var c = comp(), l = selected(c), a = c.workAreaStart, b = a + c.workAreaDuration;
            for (var i = 0; i < l.length; i++) {
                l[i].inPoint = Math.max(l[i].inPoint, a);
                l[i].outPoint = Math.min(l[i].outPoint, b);
            }
        } },
        { id: "ease", label: "Ease Selected Keys", arg: "Influence %", run: function (a) {
            var c = comp(), l = selected(c), inf = parseFloat(a) || 33.33;
            for (var i = 0; i < l.length; i++) {
                var ps = l[i].selectedProperties;
                for (var j = 0; j < ps.length; j++) {
                    if (ps[j].propertyType !== PropertyType.PROPERTY) continue;
                    var keys = ps[j].selectedKeys;
                    for (var k = 0; k < keys.length; k++) {
                        var dims = ps[j].keyInTemporalEase(keys[k]).length, e = [];
                        for (var d = 0; d < dims; d++) e.push(new KeyframeEase(0, inf));
                        ps[j].setInterpolationTypeAtKey(keys[k], KeyframeInterpolationType.BEZIER, KeyframeInterpolationType.BEZIER);
                        ps[j].setTemporalEaseAtKey(keys[k], e, e);
                    }
                }
            }
        } },
        { id: "addEffect", label: "Add Effect", arg: "Effect name or match name", run: function (a) {
            var c = comp(), l = selected(c);
            for (var i = 0; i < l.length; i++) {
                var fx = l[i].property("ADBE Effect Parade");
                if (!fx.canAddProperty(a)) throw new Error("Effect not found: " + a);
                fx.addProperty(a);
            }
        } },
        { id: "setProp", label: "Set Property", arg: "Path=Value", run: function (a) {
            var c = comp(), l = selected(c), kv = splitAssign(a), v = parseValue(kv[1]);
            for (var i = 0; i < l.length; i++) setStatic(findProp(l[i], kv[0]), v);
        } },
        { id: "setExpr", label: "Set Expression", arg: "Path=Expression", run: function (a) {
            var c = comp(), l = selected(c), kv = splitAssign(a);
            for (var i = 0; i < l.length; i++) findProp(l[i], kv[0]).expression = kv[1];
        } },
        { id: "label", label: "Set Label", arg: "0-16", run: function (a) {
            var c = comp(), l = selected(c);
            for (var i = 0; i < l.length; i++) l[i].label = Math.max(0, Math.min(16, parseInt(a, 10) || 0));
        } },
        { id: "blend", label: "Set Blend Mode", arg: "e.g. MULTIPLY, SCREEN, ADD", run: function (a) {
            var c = comp(), l = selected(c), m = BlendingMode[String(a).toUpperCase().replace(/\s+/g, "_")];
            if (m === undefined) throw new Error("Unknown blend mode: " + a);
            for (var i = 0; i < l.length; i++) if (l[i] instanceof AVLayer) l[i].blendingMode = m;
        } },
        { id: "adjust", label: "Add Adjustment Layer", arg: "Layer name", run: function (a) {
            var c = comp(), l = c.selectedLayers, top = null;
            for (var i = 0; i < l.length; i++) if (!top || l[i].index < top.index) top = l[i];
            var s = c.layers.addSolid([1, 1, 1], a || "Adjustment Layer", c.width, c.height, c.pixelAspect);
            s.adjustmentLayer = true;
            if (top) { s.moveBefore(top); s.inPoint = top.inPoint; s.outPoint = top.outPoint; }
        } },
        { id: "duplicate", label: "Duplicate Layers", run: function () {
            var c = comp(), l = selected(c);
            for (var i = 0; i < l.length; i++) l[i].duplicate();
        } },
        { id: "toggle", label: "Layer Switch", arg: "shy / locked / motionBlur / threeDLayer / guideLayer = on|off", run: function (a) {
            var c = comp(), l = selected(c), kv = splitAssign(a), key = kv[0].replace(/\s+/g, ""), on = /^(on|1|true)$/i.test(kv[1].replace(/\s+/g, ""));
            for (var i = 0; i < l.length; i++) { try { l[i][key] = on; } catch (e) { } }
        } },
        { id: "selectAll", label: "Select All Layers", run: function () {
            var c = comp();
            for (var i = 1; i <= c.numLayers; i++) c.layer(i).selected = true;
        } },
        { id: "script", label: "Run Script File", arg: "Path to .jsx", run: function (a) {
            var f = new File(a);
            if (!f.exists) throw new Error("File not found: " + a);
            $.evalFile(f);
        } }
    ];
    var STEP_BY_ID = {};
    for (var si = 0; si < STEPS.length; si++) STEP_BY_ID[STEPS[si].id] = STEPS[si];

    function runAction(action) {
        app.beginUndoGroup(action.name);
        try {
            for (var i = 0; i < action.steps.length; i++) {
                var st = action.steps[i], def = STEP_BY_ID[st.id];
                if (!def) throw new Error("Unknown step: " + st.id);
                try { def.run(st.arg); } catch (e) { throw new Error("Step " + (i + 1) + " (" + def.label + "): " + (e.message || e.toString())); }
            }
        } catch (e) {
            alert(action.name + "\n" + (e.message || e.toString()));
        }
        app.endUndoGroup();
    }

    // 書き出したスクリプトとして動いているときは、パネルを作らずにそのアクションだけ実行する
    if (typeof KZ_AR_RUN !== "undefined") { runAction(KZ_AR_RUN); return; }

    // ---------- 保存 ----------
    function storeFile() { return new File(Folder.userData.fsName + "/MangaFX/ActionRunner/actions.kzact"); }
    function defaultActions() {
        return [
            { name: "Center and Fit", steps: [{ id: "anchorCenter", arg: "" }, { id: "centerInComp", arg: "" }, { id: "fit", arg: "Both" }] },
            { name: "Null Parent", steps: [{ id: "nullParent", arg: "Control" }] },
            { name: "Stagger 2f", steps: [{ id: "stagger", arg: "2" }] }
        ];
    }
    function load() {
        var f = storeFile();
        if (!f.exists) return defaultActions();
        f.encoding = "UTF-8";
        if (!f.open("r")) return defaultActions();
        var t = f.read();
        f.close();
        try { return eval("(" + t + ")"); } catch (e) { alert("Cannot read " + f.fsName); return defaultActions(); }
    }
    function save(actions) {
        var f = storeFile();
        if (!f.parent.exists) f.parent.create();
        f.encoding = "UTF-8";
        if (!f.open("w")) { alert("Cannot write " + f.fsName + "\nAllow scripts to write files in Preferences."); return; }
        f.write(actions.toSource());
        f.close();
    }
    function copy(o) { return eval("(" + o.toSource() + ")"); }

    function exportScript(action) {
        var own = new File($.fileName);
        own.encoding = "UTF-8";
        if (!own.open("r")) { alert("Cannot read " + own.fsName); return; }
        var src = own.read();
        own.close();
        var out = File.saveDialog("Export action as script", "*.jsx");
        if (!out) return;
        if (!/\.jsx$/i.test(out.name)) out = new File(out.fsName + ".jsx");
        out.encoding = "UTF-8";
        if (!out.open("w")) { alert("Cannot write " + out.fsName); return; }
        out.write("// " + action.name + " (Action Runner)\n(function () {\nvar KZ_AR_RUN = " + action.toSource() + ";\n" + src + "\n})();\n");
        out.close();
    }

    // ---------- パネル ----------
    function buildUI(thisObj) {
        var actions = load();
        var win = thisObj instanceof Panel ? thisObj : new Window("palette", "Action Runner", undefined, { resizeable: true });
        win.orientation = "column";
        win.alignChildren = ["fill", "top"];
        win.spacing = 6;
        win.margins = 8;

        var aList = win.add("listbox", undefined, []);
        aList.preferredSize = [240, 130];
        aList.alignment = ["fill", "fill"];
        var r1 = win.add("group");
        var bRun = r1.add("button", undefined, "Run");
        var bNew = r1.add("button", undefined, "New");
        var bDup = r1.add("button", undefined, "Copy");
        var r2 = win.add("group");
        var bRen = r2.add("button", undefined, "Rename");
        var bDel = r2.add("button", undefined, "Delete");
        var bExp = r2.add("button", undefined, "Export .jsx");

        var sList = win.add("listbox", undefined, []);
        sList.preferredSize = [240, 150];
        sList.alignment = ["fill", "fill"];
        var labels = [];
        for (var i = 0; i < STEPS.length; i++) labels.push(STEPS[i].label);
        var r3 = win.add("group");
        r3.alignChildren = ["fill", "center"];
        var stepDrop = r3.add("dropdownlist", undefined, labels);
        stepDrop.selection = 0;
        var argEdit = win.add("edittext", undefined, "");
        var r4 = win.add("group");
        var bAdd = r4.add("button", undefined, "Add");
        var bSet = r4.add("button", undefined, "Update");
        var bUp = r4.add("button", undefined, "↑");
        var bDown = r4.add("button", undefined, "↓");
        var bRem = r4.add("button", undefined, "Remove");
        bUp.preferredSize.width = bDown.preferredSize.width = 30;

        function cur() { return aList.selection ? actions[aList.selection.index] : null; }
        function stepText(st) {
            var d = STEP_BY_ID[st.id];
            return (d ? d.label : st.id) + (st.arg !== undefined && st.arg !== "" ? ": " + st.arg : "");
        }
        function fillActions(sel) {
            aList.removeAll();
            for (var i = 0; i < actions.length; i++) aList.add("item", actions[i].name);
            if (actions.length) aList.selection = Math.max(0, Math.min(sel || 0, actions.length - 1));
            fillSteps();
        }
        function fillSteps(sel) {
            sList.removeAll();
            var a = cur();
            if (!a) return;
            for (var i = 0; i < a.steps.length; i++) sList.add("item", (i + 1) + ". " + stepText(a.steps[i]));
            if (sel !== undefined && a.steps.length) sList.selection = Math.max(0, Math.min(sel, a.steps.length - 1));
        }
        function updateArgHint() {
            var d = STEPS[stepDrop.selection.index];
            argEdit.enabled = !!d.arg;
            argEdit.helpTip = d.arg || "";
        }
        function commit(selStep) { save(actions); fillSteps(selStep); }

        aList.onChange = function () { fillSteps(); };
        aList.onDoubleClick = function () { bRun.onClick(); };
        stepDrop.onChange = updateArgHint;
        sList.onChange = function () {
            var a = cur();
            if (!a || !sList.selection) return;
            var st = a.steps[sList.selection.index];
            for (var i = 0; i < STEPS.length; i++) if (STEPS[i].id === st.id) stepDrop.selection = i;
            argEdit.text = st.arg || "";
            updateArgHint();
        };

        bRun.onClick = function () { var a = cur(); if (a) runAction(a); };
        bNew.onClick = function () {
            var n = prompt("Action name", "New Action");
            if (!n) return;
            actions.push({ name: n, steps: [] });
            save(actions);
            fillActions(actions.length - 1);
        };
        bDup.onClick = function () {
            var a = cur();
            if (!a) return;
            var c = copy(a);
            c.name = a.name + " copy";
            actions.push(c);
            save(actions);
            fillActions(actions.length - 1);
        };
        bRen.onClick = function () {
            var a = cur();
            if (!a) return;
            var n = prompt("Action name", a.name);
            if (!n) return;
            a.name = n;
            save(actions);
            fillActions(aList.selection.index);
        };
        bDel.onClick = function () {
            var a = cur();
            if (!a || !confirm("Delete \"" + a.name + "\"?")) return;
            var i = aList.selection.index;
            actions.splice(i, 1);
            save(actions);
            fillActions(i);
        };
        bExp.onClick = function () { var a = cur(); if (a) exportScript(a); };
        bAdd.onClick = function () {
            var a = cur();
            if (!a) return;
            var d = STEPS[stepDrop.selection.index];
            a.steps.push({ id: d.id, arg: d.arg ? argEdit.text : "" });
            commit(a.steps.length - 1);
        };
        bSet.onClick = function () {
            var a = cur();
            if (!a || !sList.selection) return;
            var d = STEPS[stepDrop.selection.index];
            a.steps[sList.selection.index] = { id: d.id, arg: d.arg ? argEdit.text : "" };
            commit(sList.selection.index);
        };
        function move(dir) {
            var a = cur();
            if (!a || !sList.selection) return;
            var i = sList.selection.index, j = i + dir;
            if (j < 0 || j >= a.steps.length) return;
            var t = a.steps[i]; a.steps[i] = a.steps[j]; a.steps[j] = t;
            commit(j);
        }
        bUp.onClick = function () { move(-1); };
        bDown.onClick = function () { move(1); };
        bRem.onClick = function () {
            var a = cur();
            if (!a || !sList.selection) return;
            var i = sList.selection.index;
            a.steps.splice(i, 1);
            commit(i);
        };

        updateArgHint();
        fillActions(0);
        win.onResizing = win.onResize = function () { this.layout.resize(); };
        if (win instanceof Window) { win.center(); win.show(); } else win.layout.layout(true);
    }

    buildUI(thisObj);
})(this);
