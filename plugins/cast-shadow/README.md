# Cast Shadow（落ち影）— After Effects エフェクト

レイヤーの形から、光源と反対の方向へ伸びる影を作るエフェクトです。光源を動かすと影も追従します。

- 対応: After Effects 2022 以降（SmartFX、8 / 16 / 32bpc、マルチフレームレンダリング対応）
- エフェクトメニュー: **エフェクト > Manga FX > Cast Shadow**
- 仕上がりの例: [previews/](previews/)

## 仕組み

出力の各画素から光源の方向へ Length まで形を探し、見つかった形の濃さを影の濃さにします。光源から遠いほど薄く（Fade）、ぼけて（Softness）いきます。影はレイヤーの外へはみ出せるように出力範囲を広げます。

## パラメータ

| 表示名 | 意味 | 既定値 |
|---|---|---|
| Light Type | **Point**＝光源の位置から放射状に伸びる / **Directional**＝全体が同じ向きに伸びる（太陽光） | Point |
| Light Position | Point のときの光源の位置 | 左上 |
| Direction | Directional のときの影の向き（0°＝上、90°＝右、180°＝下） | 135° |
| Length | 影の長さ（px） | 120 |
| Start Offset | 形から離して影を始める距離（px）。浮いている物の影に | 0 |
| Fade | 先端でどれだけ薄くなるか。0% で先端まで同じ濃さ | 100% |
| Softness | 先端でのぼかし半径（px）。根元はくっきり、先端ほどぼける | 10 |
| Shadow Color | 影の色 | 黒 |
| Opacity | 影の不透明度 | 60% |
| Output | **Behind Layer**＝レイヤーの後ろに影 / **Shadow Only**＝影だけ（別レイヤーで合成モードを変えたいとき） | Behind Layer |

背景が透明なレイヤー（キャラクターや文字）に使います。px 単位の値はプレビューの解像度に合わせて自動で縮みます。

## ビルド方法

[Ink Pool の README](../ink-pool/README.md#ビルド方法) と同じ手順です（フォルダを `cast-shadow`、出力を `CastShadow.aex` / `CastShadow.plugin` に読み替えてください）。

## AE なしで試す

```sh
python3 tools/make_previews.py /tmp/castshadow   # castshadow_cli をビルドして previews/ を作り直す
```
