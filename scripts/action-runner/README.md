# Action Runner（アクション実行）— After Effects スクリプト

よく使う操作の手順を「アクション」として並べておき、1 クリックで実行するパネルです。

## 入れ方

`ActionRunner.jsx` を After Effects の `Scripts/ScriptUI Panels` フォルダに入れ、再起動後に **ウィンドウ > ActionRunner.jsx** で開きます。アクションの保存にファイルへの書き込みを使うので、**環境設定 > スクリプトとエクスプレッション > スクリプトによるファイルへの書き込みとネットワークへのアクセスを許可** をオンにしてください。

## 使い方

1. **New** でアクションを作る
2. 下のメニューでステップを選び、必要なら引数を入れて **Add**。並べ替えは ↑ ↓、直すときはステップを選んで変えてから **Update**
3. アクションを選んで **Run**（またはダブルクリック）。全体が 1 回の取り消しにまとまります

**Export .jsx** でアクションを単独のスクリプトに書き出せます。`Scripts` フォルダに置けば **ファイル > スクリプト** から実行でき、キーボードショートカットや KBar のボタンにも割り当てられます。

## ステップ

ステップは基本的に、開いているコンポの選んだレイヤーに対して動きます。

| ステップ | 引数 | 内容 |
|---|---|---|
| Menu Command | メニューの名前（After Effects の表示言語のまま）か ID | メニューの操作を実行 |
| Precompose | コンポ名 | 選んだレイヤーをプリコンポーズ（すべての属性を移動） |
| Anchor Point to Center | | 見た目を動かさずにアンカーポイントを中身の中心へ |
| Center in Comp | | 中身の中心をコンポの中心へ |
| Fit to Comp | Both / Width / Height | コンポに収まるように拡大縮小 |
| Add Null Parent | ヌル名 | 選んだレイヤーの中心にヌルを作って親にする |
| Sequence Layers | 重なり（フレーム） | 上から順に、前のレイヤーの終わりに続けて並べる |
| Stagger Layers | ずらし（フレーム） | 上から順に少しずつ開始をずらす |
| Move to Current Time | | 開始を現在の時間へ |
| Trim to Work Area | | ワークエリアの外を切る |
| Ease Selected Keys | 影響（%） | 選んだキーフレームにイーズ |
| Add Effect | エフェクト名か matchName | エフェクトを足す |
| Set Property | `Transform/Opacity=50` | 値を入れる（キーフレームがあれば現在の時間にキー）。配列は `[100,100]` |
| Set Expression | `Transform/Rotation=time*90` | エクスプレッションを入れる |
| Set Label | 0〜16 | ラベルの色 |
| Set Blend Mode | `MULTIPLY` など | 描画モード |
| Add Adjustment Layer | レイヤー名 | 選んだレイヤーの上に調整レイヤー |
| Duplicate Layers | | 複製 |
| Layer Switch | `shy=on` など | シャイ・ロック・モーションブラー・3D・ガイドの切り替え |
| Select All Layers | | 全レイヤーを選ぶ |
| Run Script File | .jsx のパス | 別のスクリプトを実行 |

プロパティの道筋は、表示名でも matchName（`ADBE Transform Group/ADBE Opacity`）でも書けます。表示言語に左右されないのは matchName です。

## 制限

After Effects のスクリプトからは操作を記録できないため、手順は一覧から組み立てます。メニューにある操作は Menu Command で呼べます。

アクションは ユーザーフォルダの `MangaFX/ActionRunner/actions.kzact` に保存されます（Windows は `%APPDATA%`、Mac は `~/Library/Application Support`）。
