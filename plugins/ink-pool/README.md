# Ink Pool（墨溜まり）— After Effects エフェクト

線が交わる所や分かれる所、近づく所で、線どうしがインクのように丸く溶け合う「墨溜まり」を作るエフェクトです。線を太らせて溶け合わせるだけで、閉じた空間や線と線の間を塗りつぶすことはしません（ごく近い線どうしは溶けてつながります）。

- 対応: After Effects 2022 以降（SmartFX、8 / 16 / 32bpc、マルチフレームレンダリング対応）
- エフェクトメニュー: **エフェクト > Manga FX > Ink Pool**
- 仕上がりの例: [previews/](previews/)（左が元画像、右が適用後）

## 仕組み

1. 線を見つける（アルファ、または明るさから「線の濃さ」を作る）。目・鼻・点のように線とつながっていない小さな形（Melt Size の約8倍まで）や、色を塗った面（Melt Size の約1.5倍より太い、黒以外の塗り）は溶かさず、そのまま残す。色の面を縁取る線（面と色が違う線）や、黒い文字・太い線は普通の線として溶かす
2. 線を「Melt Size」でぼかして **線の密度** を測る。線が交わる所、近づく所、内側に曲がる所ほど密度が高くなる
3. 線が Y 字・T 字に分かれる所（足先の付け根など）は角度が開いていると密度が上がりにくいので、分かれ目を見つけて、線と線のあいだのへこんだ角にだけ溜まりを作る（Junction Pool）。線の外側に丸く膨らませることはしない
4. 密度が「1本線の上」より高い所だけ線を太らせる（メタボールの要領）。1本だけの線はそのままで、交点や近い線どうしが丸く溶け合う。線から「Max Spread」より離れた所には広げないので、広い空間が塗りつぶされることはない
5. 角を丸める（Smoothness）。溶けた部分と線の付け根のへこんだ角を丸いフィレットにし、とがった先や線端も丸くする。小さな閉じた隙間がまるごと埋まる場合は埋めない
6. すき間を残す（Keep Gaps）。各画素の近くで線から一番遠い距離＝そこのすき間の幅を測り、溶けてもその一定割合は塗らない。線で囲まれた小さな穴（鼻の穴など）はまるごと元のまま残す
7. 線の端を細めて丸める（End Taper）。仕上がった形を細線化して線の端（口の両端など）を見つけ、端に近いほど少し細くして先を丸くする。文字の払いも同じように細くなる。目のような点の近くの端はそのままで、削るのはその端がつながっている線の中だけ（近くの別の線は削らない）
8. 近くの線の色で塗って元の線画に合成する

## パラメータ

| 表示名 | 意味 | 既定値 |
|---|---|---|
| Line Source | 線の見つけ方。**Alpha**＝透明背景の線画 / **Dark Lines**＝白い紙に暗い線 / **Bright Lines**＝暗い地に明るい線 | Dark Lines |
| Line Threshold | この濃さ以上を線とみなす（アンチエイリアスの境目） | 50% |
| Melt Size | 溶ける範囲（px）。大きいほど離れた線どうしも溶け合い、溶けた形も大きくなる | 6 |
| Melt Strength | 溶け具合。上げるほど浅い交差や少し離れた線も溶ける。100% 近くでは線全体も少し太る | 50% |
| Max Spread | 線から広がってよい距離（Melt Size に対する %）。上げるほど大きく溶ける。下げると細い溶けだけになる | 60% |
| Junction Pool | 分かれ目の溜まり。線が Y 字・T 字に分かれる所（足先の付け根、腕の付け根など）の、線と線のあいだの角に溜まりを作る。上げるほど大きい。0% で密度だけで判断（角度の開いた分かれ目はほとんど溶けない） | 50% |
| Keep Gaps | すき間の維持。線のあいだの細い溝や小さな穴を、元の幅のこの割合までは残す（どれだけ狭くても 2px ほどは残る）。0% で制限なし。交点の角のように外へ開いた所は今までどおり丸く溶ける。0% より大きいと、線で囲まれた小さな穴は埋めない | 50% |
| Smoothness | 角を丸める半径（px）。大きいほど付け根や先端がなだらかに丸くなる | 3 |
| End Taper | 線の端の細め。端の太さをこの割合だけ細くして丸める。0% で端はそのまま | 50% |
| Melt Color | **From Lines**＝一番近い線の色をそのまま使う（色を混ぜたグラデーションにはしない。カラー線画向け） / **Custom**＝指定色 | From Lines |
| Custom Color | Melt Color が Custom のときの色 | 黒 |
| Output | **Composite**＝線画に合成 / **Melted Parts Only**＝増えた部分だけ（別レイヤーで色や合成モードを変えたいとき） | Composite |

px 単位の値はプレビューの解像度（1/2, 1/4 など）に合わせて自動で縮むので、どの解像度でも見た目は同じです。

使い方のコツ
- まず Melt Size で溶ける大きさを決め、溶ける場所が多すぎる・少なすぎるときは Melt Strength で調整します。
- 線が太いときは Melt Size を線幅の 1.5〜2 倍くらいにすると自然です。
- 線画が白い背景ごと1枚になっている場合は Line Source を Dark Lines、背景が透明なら Alpha にします。

## ビルド方法

After Effects SDK（Adobe 公式、無料）が必要です。[Adobe Developer Console](https://developer.adobe.com/after-effects/) から SDK をダウンロードし、展開した中の `Examples` フォルダの場所を `AE_SDK_DIR` に指定します。

### Windows（Visual Studio 2022）

「x64 Native Tools Command Prompt for VS 2022」を開いて:

```bat
cd ink-pool
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DAE_SDK_DIR="C:/AfterEffectsSDK/Examples"
cmake --build build --config Release
```

`build\Release\InkPool.aex` ができます。PiPL の変換で `cl` を使うので、必ず上記の Native Tools プロンプトから実行してください。

### macOS（Xcode）

```sh
cd ink-pool
cmake -S . -B build -G Xcode -DAE_SDK_DIR="$HOME/AfterEffectsSDK/Examples"
cmake --build build --config Release
```

`build/Release/InkPool.plugin` ができます（Apple Silicon / Intel 両対応のユニバーサル）。配布する場合は署名と公証が必要です。

### うまくいかないときの代替手順

CMake でのビルドがうまくいかない場合は、SDK 付属のサンプル `Examples/Template/Skeleton` をフォルダごとコピーし、次のように差し替えるのが確実です。

1. `Skeleton.cpp` / `Skeleton.h` をプロジェクトから外し、`src/InkPool.cpp` / `src/InkPool.h` / `src/InkPoolCore.h` を追加
2. `SkeletonPiPL.r` を `src/InkPoolPiPL.r` に置き換え（Windows はカスタムビルドの対象ファイル名、Mac は Rez のファイル）
3. C++17 を有効にしてビルド

### インストール

できた `InkPool.aex`（Windows）または `InkPool.plugin`（Mac）を次の場所に置いて After Effects を再起動します。

- Windows: `C:\Program Files\Adobe\Common\Plug-ins\7.0\MediaCore\`
- macOS: `/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/`

## AE なしで試す（テストツール）

`tools/` に、同じコア処理で画像を処理するコマンドラインツールがあります（Windows / Mac / Linux）。

```sh
g++ -O2 -std=c++17 -pthread tools/inkpool_cli.cpp -o inkpool_cli
python3 tools/rgba_convert.py to-raw input.png in.iprf
./inkpool_cli in.iprf out.iprf detect=1 size=8 strength=0.5 taper=0.5
python3 tools/rgba_convert.py to-png out.iprf output.png
```

キーは `detect`(0=Alpha,1=Dark,2=Bright) `threshold` `size` `strength`(0〜1) `spread`(0〜3) `keep`(0〜1) `smooth`(角を丸める半径) `taper`(0〜1) `junction`(0〜1) `colormode`(0/1) `color=r,g,b` `output`(0/1)。1920×1080 で 0.6〜1 秒程度です（4コア）。

## ファイル構成

```
src/InkPoolCore.h   画像処理の本体（AE に依存しない）
src/InkPool.cpp/.h  After Effects エフェクトとしての組み込み
src/InkPoolPiPL.r   エフェクトの登録情報
CMakeLists.txt      Windows / Mac 用ビルド設定
mac/Info.plist.in   Mac バンドル情報
tools/              AE なしで試すツール
previews/           仕上がり例
```

## 制限

- 溶けた部分はレイヤーの範囲内に収まります。
- 処理はフレームごとに独立しているので、動く線画では交差点の位置に合わせて溜まりも動きます（ちらつき防止のための時間方向の平滑化はありません）。
- このリポジトリには Adobe SDK のファイルは含まれていません。ビルドには公式 SDK を別途用意してください。
