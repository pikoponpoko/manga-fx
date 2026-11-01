# Manga FX for After Effects

漫画表現用の After Effects エフェクトプラグイン集です。エフェクトメニューの「Manga FX」に入ります。

| エフェクト | 説明 |
|---|---|
| [Concentration Lines](plugins/concentration-lines/README.md) | 集中線・流線 |
| [KZ Aura Contour](plugins/aura/README.md) | オーラ生成 |
| [KZ InkPool](plugins/ink-pool/README.md) | 墨溜まり |
| [KZ Iris Kaleidoscope](plugins/iris-kaleidoscope/README.md) | 瞳の万華鏡 |
| [KZ GrowFill](plugins/grow-fill/README.md) | 塗り広がり |
| [KZ CastShadow](plugins/cast-shadow/README.md) | 落ち影 |
| [KZ GooMerge](plugins/goo-merge/README.md) | くっつき合体 |
| [KZ FlowLoop](plugins/flow-loop/README.md) | 流れるループ |

## スクリプト

| スクリプト | 説明 |
|---|---|
| [Number Formatter](scripts/number-formatter/README.md) | 数値・タイマー表示 |
| [Object Library](scripts/object-library/README.md) | オブジェクト保存 |
| [Action Runner](scripts/action-runner/README.md) | アクション実行 |

スクリプトは zip の `Scripts/` に入っています。入れ方は各 README を参照してください。

## ダウンロード

[Releases](../../releases/latest) から `MangaFX-plugins-<版>.zip` をダウンロードします。

## インストール

zip を展開し、`Windows/*.aex` または `Mac/*.plugin` を After Effects の `Plug-ins` フォルダに入れて、After Effects を再起動します。

- Windows: `C:\Program Files\Adobe\Adobe After Effects <版>\Support Files\Plug-ins\`
- Mac: `/Applications/Adobe After Effects <版>/Plug-ins/`

Mac 版は署名なしのため、初回に止められたら次を実行してください。

```
xattr -dr com.apple.quarantine <ファイル>.plugin
```

## ソースからビルド

Adobe After Effects SDK は同梱していません。[Adobe Developer](https://developer.adobe.com/after-effects/) から SDK を入手し、`AE_SDK_DIR` に SDK の `Examples` フォルダを指定してください。

```
cmake -S plugins/<名前> -B build -DAE_SDK_DIR=<SDK>/Examples
cmake --build build --config Release
```
