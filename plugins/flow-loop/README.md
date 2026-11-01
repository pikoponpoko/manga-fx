# Flow Loop（流れるループ）— After Effects エフェクト

止め絵の一部（川・滝・雲・煙・髪など）を、描いた向きに沿って流し続けるエフェクトです。Loop Duration ごとに最初の絵に戻るので、つなぎ目のないループ素材になります。

- 対応: After Effects 2022 以降（SmartFX、8 / 16 / 32bpc、マルチフレームレンダリング対応）
- エフェクトメニュー: **エフェクト > Manga FX > Flow Loop**
- 仕上がりの例: [previews/](previews/)
- ブラウザで試す: https://claude.ai/artifact/LcMboXNbGLZvHTBderCey3

## 使い方（マスクで流れを描く）

1. 絵のレイヤーにペンツールで**開いたパス**を描く。描き始め → 描き終わりの向きに流れる。何本でも描ける
2. **閉じたパス**を描くと、その内側だけが流れる（なければ全体）。山や人物など動かしたくない所を外す
3. どちらもマスクの合成モードを **なし** にする（合成モードが「なし」以外のマスクは普段どおりレイヤーを切り抜き、流れには使われない）
4. Flow Loop を適用し、Speed と Loop Duration を調整する

コンポの長さを Loop Duration の整数倍にすると、書き出した映像がそのままループします。

## 仕組み

各画素の流れ（1 ループで進む距離）を決め、絵を流れの上流から取ってきます。半周ずらした 2 枚を重ね、片方が伸び切る瞬間にはもう片方だけが見えるように重みを付けるので、絵が伸び切らずに流れ続けます。Phase Noise で場所ごとにタイミングをずらすと、全体が同時に脈打つのが目立たなくなります。流れる範囲の外の色は取り込まないので、止まっている物が流れに引きずられません。

## パラメータ

| 表示名 | 意味 | 既定値 |
|---|---|---|
| Flow Source | **Masks**＝マスクの向き / **Direction**＝全体が同じ向き / **Radial Out**・**Radial In**＝中心から外へ・中心へ / **Swirl**＝中心の周りを回る / **Flow Map Layer**＝別レイヤーの色で指定 | Masks |
| Loop Duration | 1 ループの長さ（秒） | 2 |
| Speed | 1 ループで進む距離（px）。マイナスで逆向き | 40 |
| Phase Offset | ループの開始位置をずらす（複製したレイヤーをずらすときに） | 0% |
| Direction | Direction のときの向き（0°＝上、90°＝右） | 90° |
| Center | Radial / Swirl の中心 | 中央 |
| Guide Reach | マスクの線が届く距離（px）。離れるほど弱くなる | 60 |
| Flow Map Layer | Flow Map Layer のときの地図。R＝横（右が明るい）、G＝縦（下が明るい）、0.5（128）で止まる。アルファ＝流れる強さ | なし |
| Map Smoothing | 地図をならす半径（px） | 8 |
| Area Layer | このレイヤーの不透明な所だけ流れる（閉じたマスクと併用可） | なし |
| Area Feather | 流れる範囲の境目をなだらかにする幅（px） | 10 |
| Phase Noise | 場所ごとのタイミングのばらつき | 50% |
| Noise Scale | ばらつきの模様の大きさ（px） | 120 |
| Wrap Edges | 画面の端を反対側とつなぐ（タイル状の素材向け） | オフ |

px 単位の値はプレビューの解像度に合わせて自動で縮みます。Flow Map Layer と Area Layer は、このレイヤーと同じ大きさ・位置のレイヤーを想定しています（トランスフォームは反映しません）。

## ビルド方法

[Ink Pool の README](../ink-pool/README.md#ビルド方法) と同じ手順です（フォルダを `flow-loop`、出力を `FlowLoop.aex` / `FlowLoop.plugin` に読み替えてください）。

## AE なしで試す

```sh
python3 tools/make_previews.py /tmp/flowloop   # flowloop_cli をビルドして previews/ を作り直す
```
