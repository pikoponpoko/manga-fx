# Grow Fill（塗り広がり）— After Effects エフェクト

線画で囲まれた所を見つけて、起点から形に沿って塗りが広がっていくアニメーションを作るエフェクトです。

- 対応: After Effects 2022 以降（SmartFX、8 / 16 / 32bpc、マルチフレームレンダリング対応）
- エフェクトメニュー: **エフェクト > Manga FX > Grow Fill**
- 仕上がりの例: [previews/](previews/)

## 仕組み

1. 線を見つける（アルファ、または明るさ）。Close Gaps の分だけ線を太らせて、小さな切れ目を閉じる
2. 線で区切られた領域に分ける。画像の端に触れる領域（外側）は Include Outside をオンにしない限り塗らない
3. 各領域で起点に一番近い所から、領域の中だけを通る距離を測る。曲がった腕の先などへも形に沿って届く
4. Progress に合わせて塗りの前線を進める。Stagger を上げると起点から遠い領域ほど遅れて塗り始める
5. 塗りを線の下まで広げ（Under Lines）、線画の下に合成する

## パラメータ

| 表示名 | 意味 | 既定値 |
|---|---|---|
| Line Source | 線の見つけ方。**Alpha**＝透明背景の線画 / **Dark Lines**＝白い紙に暗い線 / **Bright Lines**＝暗い地に明るい線 | Dark Lines |
| Line Threshold | この濃さ以上を線とみなす | 50% |
| Close Gaps | 線の切れ目を閉じる半径（px）。幅がこの 2 倍までの切れ目は閉じたものとして扱う | 2 |
| Fill Target | **All Enclosed Areas**＝囲まれた所すべて / **Area at Origin**＝起点がある所だけ | All Enclosed Areas |
| Include Outside | 画像の端に触れる領域も塗る | オフ |
| Min Area | これより小さい領域（px²）は塗らない | 10 |
| Origin | 塗り始める点。各領域はこの点に一番近い所から広がる | 中央 |
| Progress | 進み具合。0% で何も塗らず、100% で全部塗り終わる。キーフレームでアニメーションさせる | 100% |
| Stagger | 領域ごとの時間差。0% で全領域が同時に始まって同時に終わる | 0% |
| Edge Softness | 塗りの前線のぼかし幅（px） | 1.5 |
| Wobble | 前線の揺れの大きさ（px）。0 できれいな同心円状の前線 | 0 |
| Wobble Size | 揺れの模様の大きさ（px） | 24 |
| Evolution | 揺れの模様を変化させる | 0° |
| Under Lines | 線の下に塗りを広げる量（px）。線と塗りの間のすき間を消す | 2 |
| Fill Color | 塗りの色 | 赤 |
| Opacity | 塗りの不透明度 | 100% |
| Output | **Composite**＝線画の下に合成 / **Fill Only**＝塗りだけ（別レイヤーで合成モードを変えたいとき） | Composite |

px 単位の値はプレビューの解像度（1/2, 1/4 など）に合わせて自動で縮むので、どの解像度でも見た目は同じです。

## ビルド方法

[Ink Pool の README](../ink-pool/README.md#ビルド方法) と同じ手順です（フォルダを `grow-fill`、出力を `GrowFill.aex` / `GrowFill.plugin` に読み替えてください）。

## AE なしで試す

```sh
python3 tools/make_previews.py /tmp/growfill   # growfill_cli をビルドして previews/ を作り直す
```
