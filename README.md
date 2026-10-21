# sweepiler

プログラムを、ふつうのマインスイーパの盤面（0〜8の数字と地雷だけ）に変換するコンパイラと、その盤面で動くゲームを遊べるドット絵のアプリです。Windows 10/11 x64向けの C++20・Win32・Direct3D 11・Dear ImGui・Z3 で書いています。

- 自作のC風言語MineCで書いたプログラムを、NORゲートだけの回路にコンパイルし、配置・配線して盤面にします。
- 1ティックは1枚の盤面です。隣り合う盤面が境目の列を共有し、状態が次のティックへ渡ります。キーは入力マスの地雷、画面は出力マスの地雷として読みます。
- 毎ティックの盤面は、個数と包含のふつうの推論だけで全マスが一つに決まります。決まらない盤面は受け入れません。
- サンプルは19本です（入門用の6本、テトリス、ブロック崩し、スネーク、ポン、ライフゲーム、マンデルブロ集合など）。

アプリの使い方と構成は[SWEEPILER.md](SWEEPILER.md)、言語は[MINEC.md](MINEC.md)にあります。

ライセンスは[MIT](LICENSE)です。同梱・取得する外部ライブラリ（Dear ImGui、Z3）のライセンスは[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)にあります。

## ビルドと起動

Visual Studio 2022以降の「C++によるデスクトップ開発」、Windows SDK、CMake 3.24以降、Gitが必要です。Developer PowerShellでこのREADMEのフォルダから実行します。

~~~powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\Release\sweepiler.exe
~~~

Visual Studio 2026では、ジェネレーターを`Visual Studio 18 2026`にします。Dear ImGui v1.91.9bとZ3 4.13.4公式MSVC x64版を初回に取得し、Z3アーカイブのSHA-256を検査します。取得済みなら次を指定できます。

~~~powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DFETCHCONTENT_SOURCE_DIR_IMGUI=C:/deps/imgui -DIMS_Z3_ROOT=C:/deps/z3-4.13.4-x64-win
~~~

`sweepiler.exe`、テスト、`libz3.dll`が`build/Release`にできます。`sweepiler.exe`は同じフォルダの`libz3.dll`を使います。

## 数学的な範囲

数字nの安全マスには、周囲8マスに地雷がn個必要です。地雷自身には個数の条件がありません。ゲートの等式や参照計算の結果を、ソルバーへの条件として足すことはしません。

通常版の符号化では、地雷の有無をBoolean変数にし、安全マスの数字を周囲の地雷数から復元します。元の規則と等価です。SATモデルは別の整数カウント検査器で確かめます。期待値の計算は検査用のコードだけで使います。

部品の回路は、正しい出力でSAT、誤った出力でUNSATになることをZ3で確かめます。ゲームは、同じ言語を整数で実行する参照インタプリタと、テストで照合します（参照インタプリタは検査専用で、ゲームの進行には使いません）。

## ドキュメント

- [SWEEPILER.md](SWEEPILER.md)：アプリ（画面、操作、検証）と、UIを持たない操作層MachineSession。
- [MINEC.md](MINEC.md)：C風のMineC言語、例題、焼きなまし配置と求解の高速化。
- [MACHINES.md](MACHINES.md)：.imsg言語、コンパイラ、参照インタプリタ、任意プログラムのフレーム連結。
- [CLASSIC_KERNELS.md](CLASSIC_KERNELS.md)：NOR・交差・配線などの部品の数字と座標。

## 構成

- `src/`：コア（MineCと.imsgのコンパイラ、NORネットリスト、フレームの配置・配線、ソルバー、操作層）
- `ui/`：ドット絵のアプリ（Win32・Direct3D 11・Dear ImGui）
- `tests/`：部品のSAT/UNSAT検証、配置、言語、セッションのテスト
- `examples/`：サンプルのプログラム（`.mc`、`.imsg`）

通常版の小型カーネルはThieme・Bastenの構成を参考にし、接続は独自に設計しています。Kayeの拡張記号による構成を、表示だけ変えて通常版にしたものではありません。
