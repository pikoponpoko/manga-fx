// NumberFormatter.jsx — 数値・タイマー表示を作る After Effects スクリプト（パネル対応）
//
// テキストレイヤーにエクスプレッション制御（NF Value など）を付け、ソーステキストの
// エクスプレッションで書式を整えて表示する。値はスライダーをキーフレームで動かすか、
// ピックウィップで他のプロパティにつなぐ。書式もエフェクトコントロールであとから変えられる。
// 前置き・後置きの文字だけはエクスプレッションの中に入るので、パネルの「Apply to Selected」で変える。
//
// 使い方: ScriptUI Panels フォルダに置くと ウィンドウ メニューからパネルとして開ける。
//         ファイル > スクリプト > スクリプトファイルを実行 でも動く。
(function (thisObj) {
    var SEPARATORS = ["None", "Comma ,", "Dot .", "Space", "Apostrophe '"];
    var DECIMAL_MARKS = ["Dot .", "Comma ,"];
    var TIME_FORMATS = ["H:MM:SS", "MM:SS", "MM:SS.cc", "SS.cc", "H:MM:SS.mmm", "D:HH:MM:SS"];
    var MODES = ["Number", "Time"];

    // エクスプレッション内の文字列リテラル
    function quote(s) {
        return '"' + String(s).replace(/\\/g, "\\\\").replace(/"/g, '\\"').replace(/\r?\n/g, "\\r") + '"';
    }

    // ソーステキストのエクスプレッション（ブラウザのお試し版と同じ書式処理）
    function buildExpression(prefix, suffix) {
        return [
            "// Number Formatter",
            "var PRE = " + quote(prefix) + ", SUF = " + quote(suffix) + ";",
            "var V = effect(\"NF Value\")(1).value * effect(\"NF Multiplier\")(1).value;",
            "var MODE = effect(\"NF Mode\")(1).value;",
            "var D = Math.max(0, Math.min(10, Math.round(effect(\"NF Decimals\")(1).value)));",
            "var P = Math.max(0, Math.min(20, Math.round(effect(\"NF Padding\")(1).value)));",
            "var S = [\"\", \",\", \".\", \" \", \"'\"][effect(\"NF Separator\")(1).value - 1] || \"\";",
            "var DM = [\".\", \",\"][effect(\"NF Decimal Mark\")(1).value - 1] || \".\";",
            "var PL = effect(\"NF Plus Sign\")(1).value;",
            "var TF = effect(\"NF Time Format\")(1).value;",
            "function pad(s, n) { s = String(s); while (s.length < n) s = \"0\" + s; return s; }",
            "function group(s) { return S ? s.replace(/\\B(?=(\\d{3})+(?!\\d))/g, S) : s; }",
            "function sign(neg, nonzero) { return neg && nonzero ? \"-\" : (PL && nonzero && !neg ? \"+\" : \"\"); }",
            "function num(v) {",
            "  var neg = v < 0, f = Math.pow(10, D), r = Math.round(Math.abs(v) * f);",
            "  var ip = Math.floor(r / f), fp = r - ip * f;",
            "  var s = group(pad(ip.toFixed(0), P));",
            "  if (D > 0) s += DM + pad(fp.toFixed(0), D);",
            "  return sign(neg, r > 0) + s;",
            "}",
            "function tm(v) {",
            "  var neg = v < 0, ms = Math.floor(Math.abs(v) * 1000 + 1e-6);",
            "  var d = Math.floor(ms / 86400000), h = Math.floor(ms / 3600000), m = Math.floor(ms / 60000), s = Math.floor(ms / 1000);",
            "  var cs = Math.floor(ms / 10) % 100, mm = ms % 1000, out;",
            "  if (TF == 1) out = pad(h, Math.max(P, 1)) + \":\" + pad(m % 60, 2) + \":\" + pad(s % 60, 2);",
            "  else if (TF == 2) out = pad(m, Math.max(P, 2)) + \":\" + pad(s % 60, 2);",
            "  else if (TF == 3) out = pad(m, Math.max(P, 2)) + \":\" + pad(s % 60, 2) + DM + pad(cs, 2);",
            "  else if (TF == 4) out = pad(s, Math.max(P, 2)) + DM + pad(cs, 2);",
            "  else if (TF == 5) out = pad(h, Math.max(P, 1)) + \":\" + pad(m % 60, 2) + \":\" + pad(s % 60, 2) + DM + pad(mm, 3);",
            "  else out = pad(d, Math.max(P, 1)) + \":\" + pad(h % 24, 2) + \":\" + pad(m % 60, 2) + \":\" + pad(s % 60, 2);",
            "  return sign(neg, ms > 0) + out;",
            "}",
            "PRE + (MODE == 2 ? tm(V) : num(V)) + SUF;"
        ].join("\n");
    }

    function findEffect(layer, name) {
        var parade = layer.property("ADBE Effect Parade");
        for (var i = 1; i <= parade.numProperties; i++)
            if (parade.property(i).name === name) return parade.property(i);
        return null;
    }

    // 同じ名前の制御がなければ足す。ドロップダウンは項目を設定する
    function ensureControl(layer, matchName, name, value, items) {
        var fx = findEffect(layer, name);
        if (!fx) {
            fx = layer.property("ADBE Effect Parade").addProperty(matchName);
            fx.name = name;
            if (items) {
                fx.property(1).setPropertyParameters(items);
                fx = findEffect(layer, name);  // setPropertyParameters のあとは参照を取り直す
            }
            if (value !== undefined) fx.property(1).setValue(value);
        } else if (value !== undefined && fx.property(1).numKeys === 0 && !fx.property(1).expressionEnabled) {
            fx.property(1).setValue(value);
        }
        return fx;
    }

    function applyTo(layer, o) {
        ensureControl(layer, "ADBE Slider Control", "NF Value", o.from);
        ensureControl(layer, "ADBE Slider Control", "NF Multiplier", undefined);
        if (findEffect(layer, "NF Multiplier").property(1).value === 0) findEffect(layer, "NF Multiplier").property(1).setValue(1);
        ensureControl(layer, "ADBE Dropdown Control", "NF Mode", o.mode, MODES);
        ensureControl(layer, "ADBE Slider Control", "NF Decimals", o.decimals);
        ensureControl(layer, "ADBE Slider Control", "NF Padding", o.padding);
        ensureControl(layer, "ADBE Dropdown Control", "NF Separator", o.separator, SEPARATORS);
        ensureControl(layer, "ADBE Dropdown Control", "NF Decimal Mark", o.decimalMark, DECIMAL_MARKS);
        ensureControl(layer, "ADBE Checkbox Control", "NF Plus Sign", o.plus ? 1 : 0);
        ensureControl(layer, "ADBE Dropdown Control", "NF Time Format", o.timeFormat, TIME_FORMATS);

        var value = findEffect(layer, "NF Value").property(1);
        if (o.animate) {
            while (value.numKeys > 0) value.removeKey(1);
            value.setValueAtTime(layer.inPoint, o.from);
            value.setValueAtTime(layer.outPoint, o.to);
            if (o.ease) {
                var e = [new KeyframeEase(0, 33.33)];
                value.setTemporalEaseAtKey(1, e, e);
                value.setTemporalEaseAtKey(2, e, e);
            }
        }
        layer.property("ADBE Text Properties").property("ADBE Text Document").expression = buildExpression(o.prefix, o.suffix);
    }

    function readOptions(ui) {
        return {
            mode: ui.mode.selection.index + 1,
            prefix: ui.prefix.text,
            suffix: ui.suffix.text,
            decimals: parseInt(ui.decimals.text, 10) || 0,
            padding: parseInt(ui.padding.text, 10) || 0,
            separator: ui.separator.selection.index + 1,
            decimalMark: ui.decimalMark.selection.index + 1,
            plus: ui.plus.value,
            timeFormat: ui.timeFormat.selection.index + 1,
            animate: ui.animate.value,
            ease: ui.ease.value,
            from: parseFloat(ui.from.text) || 0,
            to: parseFloat(ui.to.text) || 0
        };
    }

    function activeComp() {
        var c = app.project.activeItem;
        if (!(c instanceof CompItem)) { alert("Open a composition first."); return null; }
        return c;
    }

    function buildUI(thisObj) {
        var win = thisObj instanceof Panel ? thisObj : new Window("palette", "Number Formatter", undefined, { resizeable: true });
        win.orientation = "column";
        win.alignChildren = ["fill", "top"];
        win.spacing = 6;
        win.margins = 10;
        var ui = {};

        function row(label) {
            var g = win.add("group");
            g.orientation = "row";
            g.alignChildren = ["left", "center"];
            var st = g.add("statictext", undefined, label);
            st.preferredSize.width = 90;
            return g;
        }
        function edit(g, text, w) { var e = g.add("edittext", undefined, text); e.characters = w || 8; return e; }
        function drop(g, items, sel) { var d = g.add("dropdownlist", undefined, items); d.selection = sel || 0; return d; }

        ui.mode = drop(row("Mode"), MODES, 0);
        var g = row("Prefix / Suffix");
        ui.prefix = edit(g, "", 6);
        ui.suffix = edit(g, "", 6);
        g = row("Decimals / Pad");
        ui.decimals = edit(g, "0", 4);
        ui.padding = edit(g, "0", 4);
        ui.separator = drop(row("Thousands"), SEPARATORS, 1);
        ui.decimalMark = drop(row("Decimal Mark"), DECIMAL_MARKS, 0);
        ui.plus = row("").add("checkbox", undefined, "Plus Sign");
        ui.timeFormat = drop(row("Time Format"), TIME_FORMATS, 1);
        g = row("From / To");
        ui.from = edit(g, "0", 6);
        ui.to = edit(g, "100", 6);
        g = row("");
        ui.animate = g.add("checkbox", undefined, "Keyframes");
        ui.animate.value = true;
        ui.ease = g.add("checkbox", undefined, "Ease");

        var b = win.add("group");
        b.orientation = "row";
        var btnNew = b.add("button", undefined, "New Text Layer");
        var btnApply = b.add("button", undefined, "Apply to Selected");

        btnNew.onClick = function () {
            var comp = activeComp();
            if (!comp) return;
            app.beginUndoGroup("Number Formatter");
            try {
                var layer = comp.layers.addText("0");
                layer.name = "Number";
                applyTo(layer, readOptions(ui));
            } catch (e) { alert(e.toString()); }
            app.endUndoGroup();
        };
        btnApply.onClick = function () {
            var comp = activeComp();
            if (!comp) return;
            var layers = comp.selectedLayers, n = 0;
            app.beginUndoGroup("Number Formatter");
            try {
                for (var i = 0; i < layers.length; i++) {
                    if (!(layers[i] instanceof TextLayer)) continue;
                    applyTo(layers[i], readOptions(ui));
                    n++;
                }
            } catch (e) { alert(e.toString()); }
            app.endUndoGroup();
            if (n === 0) alert("Select text layers.");
        };

        win.onResizing = win.onResize = function () { this.layout.resize(); };
        if (win instanceof Window) { win.center(); win.show(); } else win.layout.layout(true);
    }

    buildUI(thisObj);
})(this);
