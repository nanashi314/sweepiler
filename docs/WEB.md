# Web版（試作）

sweepilerのコア（MineC、NORネットリスト、配置・配線、FrameSolver、MachineSession）とドット絵UIを、EmscriptenでWebAssemblyにしてブラウザで動かします。描画はSDL2とWebGL 2、UIのコードはWindows版と同じ`ui/PixelApp`などです。Windows版のビルドとテストは変わりません。

## 構成

| ファイル | 役割 |
| --- | --- |
| `web/CMakeLists.txt` | Web版だけのCMakeプロジェクト。`emcmake`で構成する |
| `web/WebMain.cpp` | SDL2・WebGL 2・Dear ImGuiのメインループ、IndexedDBのキャッシュ、事前構築したフレームの取得 |
| `web/LabWeb.cpp` | `LabCommon`のブラウザ側：OPENはファイル選択、SAVEはダウンロード、日本語フォント |
| `web/PixelDocsWeb.cpp` | リファレンスを別ウィンドウでなく同じ画面に重ねて描く |
| `web/shell.html`、`web/coi-serviceworker.js` | ページと、クロスオリジン分離のためのService Worker |
| `web/FrameTool.cpp` | Node.jsでサンプルを構築して計測し、ページが読むフレームのキャッシュを作る |
| `web/site.cmake` | 公開するファイルを`site/`に集める |
| `ui/LabWin32.cpp`、`ui/PixelDocsWin32.cpp` | Windows版だけの部分（ファイルダイアログ、フォント、画面の保存、リファレンスのウィンドウ） |

## Windows版との違い

- Z3は入れません（`IMS_NO_Z3`）。FrameSolverが通常の推論で全マスを確定できなかったときは、Z3に回さずエラーにします。サンプルはすべて推論だけで確定するので、ゲームの進行には影響しません。Z3による正誤のSAT/UNSAT検証（`tests/`）はWindows版だけで行います。
- リファレンス（REF）は同じ画面に重ねて開き、BACK、Esc、OPEN IN EDITORで戻ります。
- OPENはブラウザのファイル選択で、選んだファイルはエディタに開きます。SAVEはダウンロードです。
- 自動実行と画面の保存（`--smoke`、`--capture`など）はありません。
- 日本語の表示には、ビルド時に取得するBIZ UDゴシック（SIL Open Font License 1.1）を同梱します。
- フレームのキャッシュはブラウザのIndexedDB（`/cache/frames`）に置きます。一度ブラウザで構築したプログラムは、次に開いたときに読み込みだけで済みます。
- 過去のフレームを盤面に残すメモリの上限は128 MB（Windows版は1 GB）です。
- エディタへの日本語入力（IME）と、ほかのアプリからの貼り付けは確かめていません。

## スマートフォン

画面が小さく、整数倍では等倍にしかならないとき（横400×縦250ドットの2倍未満）は、小数倍で拡大します。

- 縦向き：SCREEN、STATUS、PROGRAMSを縦に積んだ配置（264×441ドット、一覧は8行）を画面の幅に合わせます。幅375で約1.4倍です。
- 横向き：いつもの配置を画面に合わせます（812×375で1.5倍）。
- プログラムはSTATUSのキーキャップで操作します（押している間だけキーが入る）。一度に押せるキーは1つです。
- 指の操作はページ（`shell.html`）がマウスとホイールに直して渡します。タップはクリック、押したままはキーキャップの長押し、上下のスワイプはPROGRAMS、SAMPLES、リファレンス、エディタのスクロールです。BOARDでは1本指で盤面を動かし、2本指で拡大・縮小します。どこで指が触れたかは、`PixelApp::areas()`（盤面とエディタの位置）で決めます。
- CODEタブでエディタをタップすると、スマートフォンのキーボードが出ます（見えない入力欄に入った文字、Backspace、Enterをエディタに送る）。
- OPENは、ボタンの上に重ねたブラウザのファイル選択で開きます。スマートフォンのブラウザは、タップそのものからでないとファイル選択を開かないためです。
- リファレンスは、ツリーが狭いときBACKをツリーの下に置きます。本文の表（2列）は、幅が狭いと重なって読みにくくなります。
- ブラウザのスマートフォン表示（375×812、812×375）と、ページに送った指の操作で確かめました。実機では確かめていません。

## スレッドとクロスオリジン分離

`MachineSession`は構築とティックを別スレッド（`std::jthread`）で行います。Web版はこれをそのまま使い、Emscriptenのpthreads（Web Worker）で動かします。WorkerとメモリをSharedArrayBufferで共有するため、ページはクロスオリジン分離（COOPとCOEPのヘッダー）が必要です。

GitHub Pagesはヘッダーを設定できないので、`coi-serviceworker.js`をService Workerとして登録し、すべての応答にヘッダーを付けます。最初の訪問だけ、登録のあとにページを1回再読み込みします。分離されるまではWebAssemblyを読み込みません。Service Workerが使えない環境（Firefoxのプライベートウィンドウなど）では動かず、その旨を表示します。ページはHTTPSか`localhost`で開く必要があります。

構築中もUIは止まりません。ティックはWorkerで回り、メインスレッドは描画だけをします。

## 事前構築したフレーム

ページは、構築するプログラムのフレームがキャッシュにないとき、まず`frames/<名前>-<キー>.frame.gz`を取りに行きます。あれば配置・配線をせずに読み込み、なければブラウザで配置・配線します。取得はビルダーのWorkerが同期リクエストで行い、zlibで展開します。フレームはgzipで約20分の1になります（19本で85 MB → 4 MB）。

`frame_tool`（Node.jsで動くWebAssembly版）がサンプル全部のフレームを作ります。Windows版のキャッシュ（`%LOCALAPPDATA%\InfiniteMinesweeper\frames`）は使えません。MSVCとclangでは関数の引数を評価する順が違い、MineCのコンパイラが作るゲートの番号が入れ替わるため（回路としては同じ）、キャッシュのキーが一致しないからです。同じ理由で、Windows版とWeb版では盤面の配置も違います。

## 計測

同じPCで1回ずつ計測しました。WebAssemblyはNode.js 24（V8）の1スレッド、ブラウザはClaudeのブラウザペイン（Chromium）です。tickはキーを押しながら20ティックの平均と最大です。

| サンプル | NOR | フレーム | 配置・配線 Windows / wasm | 1ティック Windows / wasm |
| --- | ---: | ---: | ---: | ---: |
| Dot | 172 | 0.4 Mマス | 0.3秒 / 1.0秒 | 1.8 / 1.6 ms |
| Pong | 314 | 0.6 Mマス | 0.2秒 / 0.2秒 | 2.5 / 2.3 ms |
| Breakout | 998 | 1.7 Mマス | 2.0秒 / 2.4秒 | 15 / 15 ms |
| Snake | 2127 | 8.3 Mマス | — / 8.7秒 | — / 88 ms |
| Life | 4458 | 19.2 Mマス | — / 23.1秒 | — / 217 ms |
| Tetris | 4636 | 20.5 Mマス | 30.6秒 / 24.7秒 | 259 / 247 ms（最大321 ms） |

WebAssemblyの速さはWindows版とほぼ同じです。19本すべての配置・配線は、wasmで合計約100秒でした。

ブラウザでの値：

- 最初に読み込むもの：`sweepiler.wasm` 1.3 MB（gzip 0.5 MB）、`sweepiler.js` 0.24 MB、`sweepiler.data`（フォント）4.5 MB。
- Tetris：BUILDから2.1秒で準備完了（`tetris-*.frame.gz` 1.2 MBの取得と展開、FrameSolverの索引）。1ティック256〜265 ms。WebAssemblyのメモリは452 MB。BOARDタブを開いたまま30 fps。
- Dot、Pong、Catch：1ティック2〜3 ms。
- Snakeに大域変数を1つ足したもの（事前構築なし）：F5から準備完了まで約12秒。その間もUIは約60 fpsで描かれ、進み具合（Placement、Routing、Emit、Adjacency）が表示される。

### テトリスの見積もり

- 事前構築したフレームがあれば実用的です。準備は約2秒、1ティック約250 ms（Windows版と同程度）、メモリは約450 MB（実行が続くと過去フレームの128 MBまで増える）。
- ブラウザで配置・配線する場合は、`frame_tool`での24.7秒とSnakeでの差（ブラウザで約1.3倍）から約30秒かかり、メモリのピークは約810 MBです（`frame_tool`で計測）。wasm32の上限4 GBに収まりますが、スマートフォンなどメモリの少ない環境では失敗するおそれがあります。

## ビルド

Emscripten（6.0.13で確認）、CMake、Ninja、Node.jsが必要です。

```bash
emcmake cmake -S web -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
cmake --build build-web --target web_frames
cmake --build build-web --target web_site
```

`web_frames`はサンプル全部のフレームを`build-web/frames`に作ります（約2分。既にあるものは読み込むだけ）。`web_site`は公開するファイルを`build-web/site`に集めます（約10 MB）。手元では`build-web/site`をHTTPサーバーで開きます。

```bash
python -m http.server 8000 --directory build-web/site
```

## GitHub Pagesで公開する

1. リポジトリのSettings → PagesのSourceを「GitHub Actions」にする。
2. Actionsタブで「Web build on GitHub Pages」（`.github/workflows/pages.yml`）を手動で実行する。Emscriptenを入れてビルドし、`web_frames`と`web_site`のあと、`build-web/site`を公開する。
3. `https://<ユーザー名>.github.io/<リポジトリ名>/`を開く。最初の1回は、Service Workerの登録後に自動で再読み込みされる。

GitHub Pagesはリポジトリが公開でないと使えないプランがあります。サイトの上限は1 GB、ファイルは100 MBまでで、Web版はどちらにも収まります。
