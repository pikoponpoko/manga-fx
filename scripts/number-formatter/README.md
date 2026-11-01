# Number Formatter（数値・タイマー表示）— After Effects スクリプト

カウンターや時計の表示を、エクスプレッションを書かずに作るスクリプトです。

ブラウザで試す: https://claude.ai/artifact/68a37CDBUb3YJ4YyQdh8jB

## 入れ方

- パネルとして使う: `NumberFormatter.jsx` を After Effects の `Scripts/ScriptUI Panels` フォルダに入れ、再起動後に **ウィンドウ > NumberFormatter.jsx** で開く
- 1 回だけ使う: **ファイル > スクリプト > スクリプトファイルを実行** で選ぶ

## 使い方

1. パネルで書式を選ぶ
2. **New Text Layer** でテキストレイヤーを作る（選んだテキストレイヤーに付けるなら **Apply to Selected**）
3. エフェクトコントロールの **NF Value** を動かす。ピックウィップで他のプロパティ（トリムパスの終了点など）につなぐこともできる

From / To と Keyframes がオンなら、レイヤーの始まりから終わりまで From → To のキーフレームを打ちます（Ease で緩急）。

## レイヤーに付く制御

| 名前 | 意味 |
|---|---|
| NF Value | 表示する値。Time のときは秒 |
| NF Multiplier | NF Value に掛ける数（スライダーの上限 ±1,000,000 を超える値を出すときなど） |
| NF Mode | Number（数値） / Time（時間） |
| NF Decimals | 小数点以下の桁数（Number） |
| NF Padding | 整数部を 0 で埋める桁数。Time では先頭の単位の桁数 |
| NF Separator | 3 桁区切り（なし / , / . / 空白 / '） |
| NF Decimal Mark | 小数点（. / ,） |
| NF Plus Sign | 正の数に + を付ける |
| NF Time Format | H:MM:SS / MM:SS / MM:SS.cc（1/100 秒） / SS.cc / H:MM:SS.mmm / D:HH:MM:SS |

制御はすべてキーフレームを打てます。前置き・後置きの文字（Prefix / Suffix）はエクスプレッションに入るので、変えるときはパネルで入力し直して Apply to Selected を押します。

数字の幅がそろわずに文字が揺れる場合は、数字が等幅のフォントを使ってください。

## 動作環境

After Effects 2020（17.0）以降。ドロップダウンメニュー制御を使います。エクスプレッションエンジンは JavaScript を想定しています。
