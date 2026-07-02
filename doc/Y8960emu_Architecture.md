# Y8960emu アーキテクチャ設計書

版: 1.0
対象リポジトリ: `Y8960emu`

本書は実装済みの最終アーキテクチャを説明する。開発経緯（バグ修正・設計変更の経緯）は
`CHANGELOG.md` を参照。

## 0. 参照資料

| 資料 | 内容 |
|---|---|
| [Y8960_Cartridge](https://github.com/hra1129/Y8960_Cartridge) `doc/manual` | プログラマーズマニュアル(docx)。I/Oマップ・メモリマップの一次情報 |
| 同 `doc/spec/Y8960_Specifications.xlsx` | レジスタ仕様(xlsx)。`ssg` / `OPLL-EX` / `OPL2+ADPCM` シートを使用（`dcsg` / `SCC` シートは空欄のためスコープ外） |
| [FMEngineTest](https://github.com/madscient/FMEngineTest) `docs/FmEngineApi.md` | 成果物DLLが準拠するC ABI仕様 |
| [YMEngine](https://github.com/madscient/YMEngine)（madscient） | 標準ymfmチップをFmEngineApi準拠DLLにする実装。Layer 2〜4のテンプレートとして流用 |
| [DSAemuEngine](https://github.com/madscient/DSAemuEngine)（madscient） | digital-sound-antiques系コアをFmEngineApi準拠DLLにする実装。`SCC`/`DCSG`を提供 |
| `extern/ymfm`（aaronsgiles/ymfm, submodule） | コアDSP実装。SSG(`ssg_engine`)/OPL2(`ym3812`)/OPLL(`opll_base`, `opll_registers`)/ADPCM-B(`adpcm_b_engine`)を利用 |

## 1. スコープ: DCSG / SCC は対象外

Y8960は SSG・OPLL・OPL2 の3ブロックについては YM2149/YM2413/YM3812 に対して
機能拡張（2回路化、ADPCM-B追加、プリセット音色バンク切替）を行っているが、
DCSGとSCCについてはそのような拡張が無く、単体のSN76489相当／Konami SCC相当を
そのまま2回路・1回路搭載しているだけである。

拡張のないチップをこのプロジェクトで再実装する意味は薄く、また
[DSAemuEngine](https://github.com/madscient/DSAemuEngine) が
FmEngineApi準拠のDLLとして `DCSG` / `SCC` のチップ名文字列を既に提供している
（`extern/emu76489`, `extern/emu2212` をラップしたもの）。

したがって、**DCSG/SCCの実装は本プロジェクトのスコープに含めない**。
Y8960を使うアプリケーション側で、本プロジェクトの成果物DLLと
DSAemuEngineのDLLの両方をロードし、`FmEngine_AddChip(handle, "DCSG", ...)`
`FmEngine_AddChip(handle, "SCC", ...)` のように使い分ける想定とする。

本プロジェクトが対象とするチップ種別は以下の3つ。

| Y8960機能ブロック | チップ名文字列 | 実装クラス |
|---|---|---|
| 拡張SSG部  | `Y8960_SSG`   | `ymfm::y8960ssg`（`ssg_engine`x2の合成） |
| 拡張OPL2部 | `Y8960_OPL2`  | `ymfm::y8960opl2ex`（`ym3812`の合成 + `adpcm_b_engine`） |
| 拡張OPLL部 | `Y8960_OPLLX` | `ymfm::y8960opllex`（`opllex_registers`という独自フォーク） |

## 2. レイヤー構成

YMEngineの3層構造に、Y8960固有の拡張チップ層を加えた4層構成。

```
┌─────────────────────────────────────────────┐
│ Layer 4: FmEngineApi.cpp / .h / .def          │  ← DLL公開Cファサード（不透明ハンドル）
├─────────────────────────────────────────────┤
│ Layer 3: FmEngine.h                           │  ← 複数チップ管理・SPSCキュー・ゲイン・ミックス
├─────────────────────────────────────────────┤
│ Layer 2: FmChip.h                             │  ← ChipType enum・FmChipImpl<T,Type>テンプレート
│                                                │     ・LinearResampler
├─────────────────────────────────────────────┤
│ Layer 1: y8960ssg / y8960opl2ex / y8960opllex │  ← ymfm派生クラス（本リポジトリ src/ の担当範囲）
├─────────────────────────────────────────────┤
│ Layer 0: extern/ymfm                          │  ← ssg_engine / ym3812 / opll_base / adpcm_b_engine
└─────────────────────────────────────────────┘
```

Layer 2〜4はYMEngineの `FmChip.h` / `FmEngine.h` / `FmEngineApi.cpp` をベースに、
`ChipType` enum と `FmChipImpl` 特殊化に `Y8960_SSG` / `Y8960_OPL2` / `Y8960_OPLLX`
を追加したもの（`FmEngine.h`・`FmEngineApi.*` は無変更で流用）。

## 3. Layer 1: 派生クラス設計

### 3.1 y8960ssg（拡張SSG部）

- ymfmの `ssg_engine` を2個保持する**合成**（継承ではない）方式。
  `ssg_engine` 自体が最終チップクラスではなく内部エンジンであるため。
- レジスタ空間: `0x00-0x0F` = SSG1、`0x10-0x1F` = SSG2
  （`Y8960_Specifications.xlsx` シート`ssg`と一致）。
- I/Oポート: `7FEAh`(Addr) / `7FEBh`(Data) の単一ポート対
  （`SSG Register Address/Data Port`）。SSG1/SSG2はこの1ポート対を共有するため、
  1つの`y8960ssg`インスタンスが2回路分をまとめて内包する設計とした
  （`FmEngine_AddChip`を2回呼ぶ必要はない）。
- `generate()` は `ssg_engine::clock()`/`output()` を直接呼び出して2回路分を
  ミックスする（`ssg_resampler`はFM系チップへの内蔵を前提とした補助クラスのため
  単独チップ用途では使わない）。

### 3.2 y8960opl2ex（拡張OPL2部 = OPL2 + ADPCM-B）

- ymfmの `ym3812` を**合成**（継承ではない）で保持し、`adpcm_b_engine` も合成で追加。
  Y8950（OPL + ADPCM-B）と同型の構成をOPL2ベースで実現したもの。
  `ym3812` の公開API（`write_address`/`write_data`/`write`/`read_status`/`read`/`generate`）
  のみを経由して操作し、ymfm内部の `fm_engine_base<opl_registers_base<N>>` には
  直接アクセスしない（理由は`CHANGELOG.md`参照）。
- レジスタ `0x07`, `0x09-0x12`, `0x15-0x17` をADPCM-B系として横取りし、
  それ以外は `ym3812` にそのまま委譲する。これは `ymfm::y8950` の実装
  （`ymfm_opl.cpp`）と同一の配置で、`OPL2+ADPCM` シートの記載内容とも整合する
  （`0x13`以降のうち未使用領域は「廃止」表記のため対応不要）。
- I/Oポート: `7FECh/7FEDh`(OPL2-2), `7FEEh/7FEFh`(OPL2-1) の2系統×Addr/Data。
  実機は2回路搭載のため、`FmEngine_AddChip(handle, "Y8960_OPL2", ...)` を
  2回呼んで2インスタンスを作る想定。
- ADPCM-Bメモリは `FmEngine_SetMemory(FM_MEM_ADPCM_B, ...)` 経由で外部から
  供給される（`ymfm_interface::ymfm_external_read/write` 経由、
  YMEngineの `MemoryYmfmInterface` パターンを流用）。

### 3.3 y8960opllex（拡張OPLL部）

`ymfm::opll_registers` の `m_instdata`/`m_chinst`/`m_opinst` は `private` かつ
関連アクセサが非virtualなため、単純継承でチャンネルごとに異なる音色バンクを
持たせることはできない（詳細は`CHANGELOG.md`のv1→v2設計変更の項を参照）。

そのため `opll_registers` を丸ごとフォークした `opllex_registers` を実装し、
以下を追加した。

- **チャンネルごとのBANK選択を保持**する `m_channel_bank[CHANNELS]`。
  新設レジスタ `0x40`-`0x48`（ch0-ch8に対応、`OPLL-EX`シート記載）への
  書き込みで更新する。
- **4バンク分の音色テーブルを同時に保持**する `m_bank_instdata[4][INSTDATA_SIZE]`
  （`ymfm::opll_registers`は1テーブルのみ保持する設計だった点を変更）。
- 音色キャッシュ計算 (`cache_operator_data()`) 時に、
  「そのチャンネルが選んでいるバンクのテーブル」(`ch_bank(choffs)`)を参照する。
  リズムチャンネル（ch6-8, リズムモード時）も同じロジックを分岐せず共通で使う。

これにより、チャンネルごとに異なるバンク（OPLL/OPLL-X/OPLL-P/VRC7）を
**同時に**選択・再生できる。`opllex.cpp`内の数式（ノイズ/LFO、フェーズステップ計算）は
`ymfm::opl_registers_base`系が使う計算式と同一だが、該当関数がymfm側で
static/非公開inline定義のため直接流用できず、同一の計算式を本ファイル内に
コピーしている（ymfmサブモジュール自体は無改造）。

プリセット音色データはコンストラクタが4バンク分自動設定する
（出典・ライセンスはREADME参照）。

## 4. Layer 2〜4: DLL側の設計

YMEngineの実装をベースラインとして、以下の差分のみを加えている。

- `FmChip.h` の `ChipType` enumに `Y8960_SSG`, `Y8960_OPL2`, `Y8960_OPLLX` を追加。
- `FmChipImpl` のテンプレート特殊化を追加。`y8960ssg` は `OUTPUTS=6`
  （SSG1 3ch + SSG2 3ch）のため、出力分類ロジックに専用の集約処理を追加している。
- **出力ミキシング方針**: 実機はチップ外側に独立したデジタルミキサーを持ち、
  機能ブロックごとにパンポットを指定する設計であるため、各`FmChip`インスタンスは
  内部でモノラル(L=R)を生成するだけにとどめ、実際のパン/ゲインは呼び出し側が
  `FmEngine_SetGain(handle, chip_id, gain_l, gain_r)` で指定する。
- `FmEngine_SetMemory` はY8960_OPL2(ADPCM-B)のみ対応。
  OPLLexの拡張音色ROMは現状コンパイル時埋め込みのみで、
  `FmEngineApi`層からの動的差し替え経路は未整備（README「未対応・既知の課題」参照）。

## 5. ディレクトリ構成

```
Y8960emu/
├── extern/
│   └── ymfm/                      (submodule)
├── src/
│   ├── dual_ssg.h/.cpp            拡張SSG部
│   ├── opl2ex.h/.cpp              拡張OPL2部
│   ├── opllex.h/.cpp              拡張OPLL部
│   ├── FmChip.h                   Layer2: チップラッパー
│   ├── FmEngine.h                 Layer3: 複数チップ管理
│   ├── FmEngineApi.h/.cpp/.def/.rc Layer4: DLL公開Cファサード
├── smoke_test.cpp                 DLL経由の動作確認テスト
├── opllex_bank_test.cpp           拡張OPLLのチャンネル独立バンク回帰テスト
├── CMakeLists.txt
├── README.md
├── CHANGELOG.md
└── Y8960emu_Architecture.md       (本書)
```
