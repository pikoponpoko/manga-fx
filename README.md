# Manga FX for After Effects

漫画表現用の After Effects エフェクトプラグイン集です。エフェクトメニューの「Manga FX」に入ります。

| エフェクト | 説明 |
|---|---|
| [Concentration Lines](plugins/concentration-lines/README.md) | 集中線・流線 |
| [KZ Aura Contour](plugins/aura/README.md) | オーラ生成 |
| [KZ InkPool](plugins/ink-pool/README.md) | 墨溜まり |
| [KZ Iris Kaleidoscope](plugins/iris-kaleidoscope/README.md) | 瞳の万華鏡 |

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
