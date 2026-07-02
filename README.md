# Y8960emu

Y8960（Y8960_Cartridgeに搭載予定の統合音源チップ）のエミュレーター。
ymfm をベースに、Y8960固有の拡張ブロック（拡張SSG部・拡張OPL2部・拡張OPLL部）を
個別クラスとして実装し、FmEngineApi互換のDLL (`Y8960EngineApi`) にまとめたもの。

設計の詳細は `Y8960emu_Architecture.md`（アーキテクチャ設計書）を参照。

## このセッションでの変更点

### Layer 1: 拡張チップ実装 (src/)

- **`dual_ssg.h/.cpp`**: `write()` の演算子優先順位バグ (`offset & 1 == 0`) を修正。
  `ssg_engine` を直接 `clock()`/`output()` して2回路分をミックスする `generate()` を実装
  （旧WIPは `ssg_resampler` を誤用しておりコンパイル不可だった）。
- **`opl2ex.h/.cpp`**: `ym3812` への「継承」をやめ「合成」に変更。
  ymfm内部の `fm_engine_base<opl_registers_base<N>>` は out-of-line
  テンプレート実装 (`ymfm_fm.ipp`) に依存しており、ymfm本体の.cpp群の外から
  直接叩くとビルド環境によってはリンクエラーになる。`ym3812` の公開APIのみを
  経由することでこれを回避。ADPCM-Bレジスタのルーティングは `ymfm::y8950`
  の実装（`ymfm_opl.cpp`）に準拠。
- **`opllex.h/.cpp`**: **v2設計に変更（当初のv1設計には実害のあるバグがあったため）。**
  v1は `opll_base` を継承し、BANKレジスタ書き込み時に `set_instrument_data()` で
  チップ全体の音色テーブルを丸ごと差し替える方式だったが、これだと
  **あるチャンネルのBANK書き込みが他の全チャンネルの音色まで巻き込んで
  変えてしまう（後勝ち）** という、機能の目的（チャンネルごとに独立して
  OPLL/OPLL-X/OPLL-P/VRC7を選べること）そのものを壊す欠陥があった。
  v2では `ymfm::opll_registers` を丸ごとフォークした `opllex_registers` を新設し、
  チャンネルごとに現在のBANK選択を保持したうえで、音色キャッシュ計算時に
  「そのチャンネルが選んでいるバンクのテーブル」を参照するよう変更。
  これによりチャンネルごとに異なるバンクを"同時に"鳴らせる。
  リズムチャンネル(ch6-8, リズムモード時)もメロディチャンネルと同じ
  `ch_bank()`参照ロジックを分岐せず共通で使っており、BANKレジスタに従う
  （実用上は全バンクのリズム音色が同一内容である想定のため通常は聞こえ方に
  影響しないが、特別扱いする理由がない限り分岐を増やさない方針とした）。
  `opllex_bank_test.cpp` で無関係チャンネルへの影響がないこと、
  自チャンネルのBANK切替が実際に音色を変えること、
  リズムチャンネルもBANKに従うことの3点を回帰テスト済み。
  **プリセット音色データ (`set_bank_instrument_data()` に渡す実データ) は
  現状呼び出し側で用意する前提で、初期状態は全ゼロ。**
  実データは一次情報 (IKAOPLL, Copyright-free OPLL(x) ROM patches) を
  確認のうえ転記が必要。

### Layer 2〜4: DLLファサード (src/)

- `FmChip.h`: 新規作成。YMEngine (madscient/YMEngine) の実装パターンを踏襲し、
  `ChipType::Y8960_SSG` / `Y8960_OPL2` / `Y8960_OPLLX` を追加。
- `FmEngine.h` / `FmEngineApi.h` / `FmEngineApi.cpp` / `FmEngineApi.def` / `FmEngineApi.rc`:
  YMEngineから無変更で流用（チップ非依存の汎用層のため）。

### 動作確認

`smoke_test.cpp` で以下を確認済み（Linux, g++ 13, `-std=c++17`）:

```
supported chips: 3
  - Y8960_SSG
  - Y8960_OPL2
  - Y8960_OPLLX
[OK] AddChip(Y8960_SSG) -> id=0 nativeRate=447443
[OK] AddChip(Y8960_OPL2) -> id=1 nativeRate=49715
[OK] AddChip(Y8960_OPLLX) -> id=2 nativeRate=49715
[OK] AddChip(NOSUCHCHIP) -> -2 (expect FM_ERR_UNKNOWN_CHIP)
[OK] Generate -> 0
[OK] NaN check
[OK] Inf check
```

`opllex_bank_test.cpp` でチャンネル独立バンクの回帰テストも実施済み:

```
[OK] ch1 BANK=OPLL write does not affect ch0 output
[OK] ch1 BANK=VRC7 write does not affect ch0 output
[OK] ch0 actually produces sound (sanity check)
[OK] ch0's own BANK change actually alters ch0 output (control test)
[OK] rhythm channel (BD) follows its own channel's BANK too
```

## ビルド

```bash
git submodule add https://github.com/aaronsgiles/ymfm extern/ymfm
git submodule update --init --recursive
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Linux上でCMakeを使わず直接確認する場合:

```bash
g++ -std=c++17 -O2 -fPIC -fvisibility=hidden -DFMENGINE_EXPORTS \
  -I src -I extern/ymfm/src -shared -o libY8960EngineApi.so \
  src/FmEngineApi.cpp src/dual_ssg.cpp src/opl2ex.cpp src/opllex.cpp \
  extern/ymfm/src/ymfm_opl.cpp extern/ymfm/src/ymfm_ssg.cpp \
  extern/ymfm/src/ymfm_adpcm.cpp extern/ymfm/src/ymfm_pcm.cpp

g++ -std=c++17 -O2 -I src smoke_test.cpp -L. -lY8960EngineApi -o smoke_test
LD_LIBRARY_PATH=. ./smoke_test
```

## 未対応・既知の課題（アーキテクチャ設計書 5節も参照）

1. **DCSG / SCC**: `Y8960_Specifications.xlsx` の該当シートが空欄のため未実装。
2. **OPLLプリセット音色データ**: 上記の通り現状プレースホルダ(全ゼロ)。
   `y8960opllex::set_bank_instrument_data()` で4バンク分をストリーム開始前に設定すること。
3. **SSG/OPL2/OPLLの出力ミキシング**: 実機のI2S/ミキサー仕様が未確定のため、
   `FmChip.h` 側は暫定的に全チャンネル等分加算のモノラルミックスにしている。
4. **read()系の実機仕様との突合**: 特にSSGのRead系ポート（`7FEAh`系の読み出し仕様）は
   プログラマーズマニュアルのI/Oマップ止まりで詳細未記載。実機資料が増え次第見直すこと。
5. **FmEngineApi層からの`set_bank_instrument_data()`呼び出し経路が未整備**:
   現状は `y8960opllex` を直接使うC++コードからしか設定できない。
   `FmEngine_SetMemory` の `FmMemoryType` 拡張などでDLL越しに設定できるようにするか、
   要検討。
