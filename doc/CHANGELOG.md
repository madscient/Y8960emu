# CHANGELOG

WIPからの主な変更点の記録（開発経緯）。最終的な仕様は `README.md` および
`Y8960emu_Architecture.md` を参照。

## スコープ変更: 拡張SSG部（dual_ssg）を削除

当初は拡張SSG部（`ymfm::y8960ssg`, `src/dual_ssg.h/.cpp`）を本プロジェクトの
スコープに含め、SSG1/SSG2の2回路を1つの`FmChip`インスタンスに内包する設計で
実装していた。しかし以下の理由により、DCSG/SCCと同様にスコープ外とし、
`dual_ssg.h/.cpp` および関連コードを削除した。

- SSGはY8960においても機能拡張が無い（単体のYM2149相当を2回路搭載しているだけ）。
  拡張のないチップをこのプロジェクトで再実装する意味は薄く、
  [DSAemuEngine](https://github.com/madscient/DSAemuEngine) が
  FmEngineApi準拠のDLLとして `SSG` のチップ名文字列を既に提供している。
- 出力ミキシング方針（機能ブロックごとに `FmEngine_SetGain()` でパンを
  指定する）を採用したことで、OPL2/OPLLは実機のI/Oポート構成に合わせて
  `AddChip`を2回呼べば自然にパン設定できる一方、SSGは実機が単一I/Oポート対
  (`7FEAh`/`7FEBh`)でSSG1/SSG2を共有する構造のため、1インスタンスに2回路を
  内包する現行設計のままでは`SetGain`がSSG1/SSG2に対して同一のパンにしか
  ならないという非対称な問題が判明した。DSAemuEngineの`SSG`を使えば
  OPL2/OPLLと同様に`AddChip`を2回呼ぶ形に統一でき、この非対称性も解消できる。

削除したファイル: `src/dual_ssg.h`, `src/dual_ssg.cpp`。
`FmChip.h`・`CMakeLists.txt`・`_test/smoke_test.cpp`からも関連記述を削除。

## Layer 1: 拡張チップ実装 (src/)

### dual_ssg.h/.cpp （削除済み。以下は削除前の開発記録）
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
- SSG も同様の理由に加え、実機のI/Oポート構造上の非対称性（本ページ冒頭参照）から、
  後日スコープ外に変更（DSAemuEngineの`SSG`を利用する方針に統一）。

## 命名の変更: チップ名シンボルとDLL名

- チップ名シンボルから`Y8960_`接頭辞を外し、`Y8960_OPL2` → `OPL2EX`、
  `Y8960_OPLLX` → `OPLLEX` に変更（`ChipType` enum・`FmEngine_AddChip()`に
  渡すチップ名文字列・`FmClock`定数・`FmChip::name()`の戻り値）。
  DLL名にすでにY8960が含まれており、チップ名側で重ねる必要がないため。
  ymfm派生クラス名 (`ymfm::y8960opl2ex` / `ymfm::y8960opllex`) は変更していない。
- CMakeターゲット名を `Y8960EngineApi` → `Y8960emuEngine` に変更。
  あわせて `FmEngineApi.def` の`LIBRARY`名と `FmEngineApi.rc` の
  `ProductName`/`InternalName`/`OriginalFilename` に残っていたYMEngine由来の
  `YMFMEngine` を修正した。

## 修正: 短い間隔の KEY OFF → KEY ON で KEY OFF が無視される

症状: 同じチャンネルに KEY OFF と KEY ON を短い間隔で書き込むと、KEY OFF が
効かず（リリースも再アタックも起きず）前の音がそのまま続く。

原因: `FmEngine::generate()` がキューの書き込みを全部適用してから一括生成して
いた。ymfm はキーオンビットの書き込みを即座にエンベロープへ反映せず、次の
サンプル生成時 (`fm_operator::clock_keystate`) にその時点のキー状態だけを見る
ため、1バッファ内の KEY OFF → KEY ON は「ずっと KEY ON」に見える。

対処: 兄弟リポジトリ YMEngine で同じ症状に入れた修正
（同一チャンネル衝突時のみ約2ms先行生成する方式、リズムレジスタの打楽器別
スロット分解を含む）を移植した。
- `FmChip::keyOnTransitionMask()` を追加し、`FmChipImpl` でチップ種別ごとに
  キーオン関連ビット (OPL2EX: 0xB0-0xB8 bit5 / 0xBD bit0-5、
  OPLLEX: 0x20-0x28 bit4 / 0x0E bit0-5) の変化をチャンネルスロットに変換。
- YMEngine 版からの差分: 対象を Y8960 の2チップに限定。両チップとも
  port によらず同一レジスタ空間なので、直前値キャッシュは port で分けない。
  OPL2 のチャンネルキーオンは 0xB0-0xBF ではなく実在する 0xB0-0xB8 に絞った。
- ADPCM-B の START (reg 0x07) は ymfm 側で書き込み時に即時処理される
  (`adpcm_b_channel::write` → `load_start()`) ため対象外とした。

見送り: `LinearResampler::process()` は毎回 +2 の余裕分を含めてチップを
進めるが、実際に消費した分しか位相から引かないため、呼び出しを分割するほど
チップ時間が出力時間より先行する可能性がある（コードを読んだ見立て・未検証。
YMEngine も同一実装）。本修正で衝突時に分割呼び出しが増えるが、今回の症状とは
別問題として手を付けていない。

## 修正: KEY OFF → KEY ON の衝突が多い／呼び出しが細かいと KEY OFF が消える

利用側（Y8960Sequencer）からの報告。前項の対策は、先行生成をその呼び出しの
`samples` の中でしか行わず、使い切った後の衝突は間を空けずに書き込んでいた。
- 衝突が多い: 240 サンプルずつの呼び出しで5チャンネル以上が同時に
  KEY OFF → KEY ON すると 96 サンプル × 数回で余地が尽き、後ろの
  チャンネル（リズム 0x0E など）の KEY OFF が消える。
- 呼び出しが細かい: generate(1) などでは先行生成が端数しか取れず、KEY OFF が
  1サンプル程度しか観測されない（リリースが聞こえない）。

対処: 衝突した書き込みを保留し、前の状態のまま `minKeyOnTickSamples()` 分を
生成し終えてから適用する。保留中の生成は呼び出しをまたいで数え、後続の書き込み
も保留が解けるまで適用しない。未観測の追跡（`m_keyDirtyMask`）は呼び出しの頭
ではなく、1サンプル以上生成した時点で打ち切るようにした。

見送り: 利用側の案「余地が尽きたら衝突した書き込み以降を次の呼び出しの頭で
適用する」は採らなかった。余地が尽きる直前に適用した書き込み（例: KEY OFF）は
まだ1サンプルも生成されていないので、次の呼び出しの頭で続き（KEY ON）を適用
すると、やはり観測されないため。
代償: 衝突1回ごとに適用が最大約2ms遅れ、同じ呼び出しに衝突が N 回あると最大
N × 約2ms 遅れる（設計から導いた値・遅れ量は測っていない）。この代償は
「KEY OFF/ON の状態は最低約2ms観測させる」を前提にしている。発音タイミングを
優先するなら、衝突時に生成する長さを縮めることになる。

## ymfm を 17decfa → 81aec25 に更新

上流の4コミットのうち、本DLLに効くのは 2252890（OPL2 の WSE ビットを実機どおり
扱う）だけ。81aec25（OPN）・6cc0c4a（OPZ）・e9f57df（サンプルのビルド）は、
本DLLがビルドしないファイルへの変更。

- OPL2EX: 波形選択（reg 0xE0-0xF5）が、reg 0x01 bit5（WSE）=1 のときだけ効く
  ようになった。以前は WSE と無関係に効いていた（`ymfm_opl.cpp` の
  `cache_operator_data()` が WSE を見ていなかった）。拡張OPL2部は YM3812 相当
  なので、実機に近づく変更として取り込んだ。この判断は「Y8960 の拡張OPL2部が
  YM3812 と同じく WSE を持つ」限り成立する。WSE を立てずに波形選択していた
  曲は、サインで鳴るようになる。`opl2ex_wse_test.cpp` を追加（17decfa では
  WSE=0 の項が FAIL）。
- OPLLEX: 影響なし。フォーク元の `opll_registers` と、フォークが借りている
  `ymfm_opl.h` の共通関数は変わっていない（上流の `ymfm_opl.h` の差分は
  `opl_registers_base` のコメント1行と `op_waveform()` のみ）。
  `opllex_bank_test.cpp` は更新前後とも全項 OK。

## 動作確認

`smoke_test.cpp`:
```
supported chips: 2
  - OPL2EX
  - OPLLEX
[OK] AddChip(OPL2EX) -> id=0 nativeRate=49715
[OK] AddChip(OPLLEX) -> id=1 nativeRate=49715
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

`keyoff_retrigger_test.cpp`:
```
[OK] OPL2EX batch dip : held=0.1238 min1ms=0.0011 tail=0.1238
[OK] OPL2EX crowd dip : held=0.1238 min1ms=0.0013 tail=0.1238
[OK] OPL2EX tiny  dip : held=0.1238 min1ms=0.0000 tail=0.1238
[OK] OPLLEX batch dip : held=0.1100 min1ms=0.0013 tail=0.1101
[OK] OPLLEX crowd dip : held=0.1100 min1ms=0.0009 tail=0.1101
[OK] OPLLEX tiny  dip : held=0.1100 min1ms=0.0000 tail=0.1101
[OK] OPLLEX batch hit : attack=0.2184 held=0.0000 after OFF/ON=0.2168
[OK] OPLLEX crowd hit : attack=0.2184 held=0.0000 after OFF/ON=0.2151
[OK] OPLLEX tiny  hit : attack=0.2184 held=0.0000 after OFF/ON=0.2148
```
batch は `9c687c8` より前のヘッダで FAIL、crowd は `9c687c8` で3件とも FAIL
（リズムは `after OFF/ON=0.0000`）、tiny は `9c687c8` で OPL2EX のみ FAIL
（OPLLEX は `9c687c8` でも通る）。

`opl2ex_wse_test.cpp`:
```
[OK] WSE=0: waveform select ignored (min=-0.1245, expect < -0.05)
[OK] WSE=1: half-sine selected    (min=0.0004, expect > -0.01)
```
