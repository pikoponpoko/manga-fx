# Goo Merge（くっつき合体）— After Effects エフェクト

近くにある形同士を、水あめのようにくびれた橋でつなぎ、角を丸めるエフェクトです。形を動かして離すと、橋が細くなってちぎれます。

- 対応: After Effects 2022 以降（SmartFX、8 / 16 / 32bpc、マルチフレームレンダリング対応）
- エフェクトメニュー: **エフェクト > Manga FX > Goo Merge**
- 仕上がりの例: [previews/](previews/)
- ブラウザで試す: https://claude.ai/artifact/Uug5Ro6QDTKzyAAiEEv1gU

## 使い方

- 1 つのレイヤーの中の形（シェイプレイヤーの複数のパス、文字、描いた絵）をつなぐ: そのレイヤーに適用
- 複数のレイヤーをつなぐ: 背景が透明なコンポで、上に調整レイヤーを置いて適用（またはプリコンポーズして適用）

## 仕組み

形を円で Merge Radius だけ膨らませてから、同じだけ縮めます（クロージング）。Merge Radius の 2 倍より狭いすき間はくびれた橋でつながり、へこんだ角は丸く埋まります。元の形が削られることはありません。Round Corners を上げると、出っ張った角も丸くなります。

## パラメータ

| 表示名 | 意味 | 既定値 |
|---|---|---|
| Merge Radius | つながる距離（px）。この 2 倍までのすき間がつながる | 16 |
| Round Corners | 出っ張った角を丸める半径（px）。これの 2 倍より細い部分は消える | 0 |
| Style | **Solid**＝塗りつぶし / **Bubble**＝透明な泡 | Solid |
| Color | **Layer Colors**＝元の色（橋は両側の色が混ざる） / **Fill Color**＝1 色で塗る | Layer Colors |
| Fill Color | Color が Fill Color のときの色 | 白 |
| Bubble > Bubble Opacity | 泡の中の不透明度 | 25% |
| Bubble > Rim Width | 泡の縁の太さ（px） | 6 |
| Bubble > Highlight | 光の映り込みの強さ | 80% |
| Bubble > Light Direction | 光の来る向き（0°＝上、時計回り） | -45° |

px 単位の値はプレビューの解像度に合わせて自動で縮みます。

## ビルド方法

[Ink Pool の README](../ink-pool/README.md#ビルド方法) と同じ手順です（フォルダを `goo-merge`、出力を `GooMerge.aex` / `GooMerge.plugin` に読み替えてください）。

## AE なしで試す

```sh
python3 tools/make_previews.py /tmp/goomerge   # goomerge_cli をビルドして previews/ を作り直す
```
