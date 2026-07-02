# CHANGELOG

WIPからの主な変更点の記録（開発経緯）。最終的な仕様は `README.md` および
`Y8960emu_Architecture.md` を参照。

## Layer 1: 拡張チップ実装 (src/)

### dual_ssg.h/.cpp
- `write()` の演算子優先順位バグ (`offset & 1 == 0` は `==` が `&` より優先されるため
  常に偽側に倒れていた) を `(offset & 1) == 0` に修正。
- 旧WIPは `ssg_resampler`（ymfm内部でFM系チップにSSGを内蔵する際の補助クラス）を
  誤用しておりコンパイル不可だった。単独チップとして `ssg_engine` を直接
  `clock()`/`output()` する `generate()` に書き直した。

### opl2ex.h/.cpp
- 当初 `ym3812` を「継承」する設計だったが、「合成」に変更した。
  ymfm内部の `fm_engine_base<opl_registers_base<N>>` は out-of-line
  テンプレート実装 (`ymfm_fm.ipp`) に依存しており、ymfm本体の.cpp群の外から
  直接叩くとビルド環境によってはリンクエラーになることが判明したため。
  `ym3812` の公開APIのみを経由する設計に変更して回避した。
  ADPCM-Bレジスタのルーティングは `ymfm::y8950` の実装（`ymfm_opl.cpp`）に準拠。

### opllex.h/.cpp
- **v1→v2の設計変更。** v1は `opll_base` を継承し、BANKレジスタ書き込み時に
  `set_instrument_data()` でチップ全体の音色テーブルを丸ごと差し替える方式だったが、
  これだと**あるチャンネルのBANK書き込みが他の全チャンネルの音色まで巻き込んで
  変えてしまう（後勝ち）**という、機能の目的（チャンネルごとに独立して
  OPLL/OPLL-X/OPLL-P/VRC7を選べること）そのものを壊す欠陥があった。
  v2では `ymfm::opll_registers` を丸ごとフォークした `opllex_registers` を新設し、
  チャンネルごとに現在のBANK選択を保持したうえで、音色キャッシュ計算時に
  「そのチャンネルが選んでいるバンクのテーブル」を参照するよう変更。
  これによりチャンネルごとに異なるバンクを"同時に"鳴らせるようになった。
  `opllex_bank_test.cpp` で無関係チャンネルへの影響がないこと、
  自チャンネルのBANK切替が実際に音色を変えることを回帰テストしている。
- リズムチャンネル(ch6-8, リズムモード時)もメロディチャンネルと同じ
  `ch_bank()`参照ロジックを分岐せず共通で使う設計とした
  （特別扱いする理由がない限り分岐を増やさない方針）。
- プリセット音色データは [Copyright free OPLL(x) ROM patches](https://github.com/plgDavid/misc/wiki/Copyright-free-OPLL(x)-ROM-patches)
  (David Viens, Hubert Lamontagne 作, CC BY-SA) のメロディ15音色分を4バンク分すべて転記。
  リズム3音色分は一次情報に個別記載が無かったため、同じ出典を採用している
  `ymfm::ym2413` のデフォルト音色データのリズム部分をそのまま転記した
  （4バンク共通。実機のリズム音色がバンクごとに異なるかどうかは未確認）。

## Layer 2〜4: DLLファサード (src/)

- `FmChip.h`: 新規作成。[YMEngine](https://github.com/madscient/YMEngine)
  (madscient) の実装パターンを踏襲し、`ChipType::Y8960_SSG` / `Y8960_OPL2` /
  `Y8960_OPLLX` を追加。
- `FmEngine.h` / `FmEngineApi.h` / `FmEngineApi.cpp` / `FmEngineApi.def` / `FmEngineApi.rc`:
  YMEngineから無変更で流用（チップ非依存の汎用層のため）。
- 出力ミキシング方針の決定: 当初「実機ミキサー仕様待ちの暫定」としていたが、
  実機はチップ外側に独立したデジタルミキサーを持ち機能ブロックごとに
  パンポットを指定する設計であることを踏まえ、チップ内部ではモノラル(L=R)を
  返すだけにとどめ、パン/ゲインは呼び出し側が `FmEngine_SetGain()` で
  指定する方針を最終仕様として確定した。

## スコープ決定

- DCSG / SCC は本プロジェクトのスコープ外と決定。Y8960でもこの2チップは
  機能拡張が無く、[DSAemuEngine](https://github.com/madscient/DSAemuEngine)
  （FmEngineApi準拠、チップ名 `DCSG` / `SCC` を提供）がアプリ側で利用可能なため。

## 動作確認

`smoke_test.cpp`:
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

`opllex_bank_test.cpp`:
```
[OK] ch1 BANK=OPLL write does not affect ch0 output
[OK] ch1 BANK=VRC7 write does not affect ch0 output
[OK] ch0 actually produces sound (sanity check)
[OK] ch0's own BANK change actually alters ch0 output (control test)
[OK] rhythm channel (BD) follows its own channel's BANK too
[OK] default (CC BY-SA) presets produce sound
[OK] default presets differ across all 4 banks
```
