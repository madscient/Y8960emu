# Y8960emu

[Y8960](https://github.com/hra1129/Y8960_Cartridge)（MSX用サウンドカートリッジ
Y8960 Cartridge に搭載予定の統合音源チップ）のソフトウェアエミュレーター。

[ymfm](https://github.com/aaronsgiles/ymfm) をコアとして、Y8960固有の拡張ブロック
（拡張OPL2部・拡張OPLL部）を個別クラスとして実装し、
[FmEngineApi](https://github.com/madscient/FMEngineTest) 互換のDLL
(`Y8960EngineApi`) にまとめたもの。

設計の詳細は [`doc/Y8960emu_Architecture.md`](doc/Y8960emu_Architecture.md)、
開発経緯は [`doc/CHANGELOG.md`](doc/CHANGELOG.md) を参照。

## 対応チップ

`FmEngine_AddChip()` に渡すチップ名文字列と、対応するY8960の機能ブロック。

| チップ名 | Y8960の機能ブロック | 実装クラス | 備考 |
|---|---|---|---|
| `Y8960_OPL2`  | 拡張OPL2部 | `ymfm::y8960opl2ex` | YM3812相当 + ADPCM-B、1回路分。実機は2回路搭載のため`AddChip`を2回呼ぶ |
| `Y8960_OPLLX` | 拡張OPLL部 | `ymfm::y8960opllex` | YM2413相当 + チャンネル独立プリセット音色バンク切替、1回路分。同様に2回路分は`AddChip`を2回 |

SSG・DCSG・SCCは本プロジェクトのスコープ外。[スコープについて](#スコープ-ssg--dcsg--scc-は対象外)を参照。

## 拡張OPLL部: チャンネル独立プリセット音色バンク

標準OPLL(YM2413)レジスタ(0x00-0x3F)に加え、新設のBANKレジスタ(0x40-0x48、
ch0-ch8に対応)でチャンネルごとに音色プリセットを選べる。

| BANK値 | プリセット |
|---|---|
| 0 | OPLL (YM2413 標準) |
| 1 | OPLL-X (YM2423相当) |
| 2 | OPLL-P (YMF281相当) |
| 3 | VRC7 (DS1001相当) |

チャンネルごとに異なるバンクを同時に選択・再生できる（`_test/opllex_bank_test.cpp`で回帰テスト済み）。

プリセット音色データはコンストラクタが自動設定する。メロディ15音色分は
[Copyright free OPLL(x) ROM patches](https://github.com/plgDavid/misc/wiki/Copyright-free-OPLL(x)-ROM-patches)
(CC BY-SA)、リズム3音色分（4バンク共通）は同一出典を採用している ymfm
(`ym2413::s_default_instruments`) から転記したもの。差し替えたい場合は
`y8960opllex::set_bank_instrument_data()` を使う。

## 出力とミキシング

各`FmChip`インスタンス（Y8960_OPL2/Y8960_OPLLXそれぞれ）は内部でモノラル(L=R)を
生成する。実機はチップ外側に独立したデジタルミキサーを持ち、機能ブロックごとに
パンポットを指定する設計のため、左右のパン・音量は呼び出し側が
`FmEngine_SetGain(handle, chip_id, gain_l, gain_r)` で指定する。

拡張OPL2部・拡張OPLL部は実機側もそれぞれ独立したI/Oポート対を2組（-1/-2）持つため、
`AddChip`を2回呼んで得た2つの`chip_id`にそれぞれ`SetGain`を指定すればよい。

SSGを含める場合（後述）は、SSG1/SSG2用に`chip_id`を2つ用意し、
アプリ側でアドレスレジスタの値（`< 0x10` か `>= 0x10` か）を見て
どちらのチップへ書き込むかをルーティングする必要がある
（実機のSSGブロックは単一のI/Oポート対でSSG1/SSG2を共有しているため）。

## スコープ: SSG / DCSG / SCC は対象外

SSG・DCSG・SCCはY8960においても機能拡張が無い（単体のYM2149相当／SN76489相当／
Konami SCC相当をそのまま搭載しているだけ）ため、本プロジェクトでは対象としない。
[DSAemuEngine](https://github.com/madscient/DSAemuEngine)
（FmEngineApi準拠、チップ名 `SSG` / `DCSG` / `SCC` を提供）をアプリ側で
併用する想定とする。

SSGについては、Y8960のI/O構造上SSG1/SSG2が単一ポート対を共有する関係で
「アプリ側でSSG1/SSG2を独立してパン設定する」ためにはアドレス値による
振り分けロジックが追加で必要になる点に注意（前述の「出力とミキシング」参照）。

## ビルド

```bash
git submodule add https://github.com/aaronsgiles/ymfm extern/ymfm
git submodule update --init --recursive
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

成果物: `build/bin/Y8960EngineApi.dll`（Windows）/ `build/lib/libY8960EngineApi.so`（Linux）

Linux上でCMakeを使わず直接ビルドする場合:

```bash
g++ -std=c++17 -O2 -fPIC -fvisibility=hidden -DFMENGINE_EXPORTS \
  -I src -I extern/ymfm/src -shared -o libY8960EngineApi.so \
  src/FmEngineApi.cpp src/opl2ex.cpp src/opllex.cpp \
  extern/ymfm/src/ymfm_opl.cpp \
  extern/ymfm/src/ymfm_adpcm.cpp extern/ymfm/src/ymfm_pcm.cpp
```

## テスト

```bash
g++ -std=c++17 -O2 -I src _test/smoke_test.cpp -L. -lY8960EngineApi -o smoke_test
LD_LIBRARY_PATH=. ./smoke_test

g++ -std=c++17 -O2 -I src -I extern/ymfm/src _test/opllex_bank_test.cpp src/opllex.cpp -o opllex_bank_test
./opllex_bank_test
```

`_test/smoke_test.cpp` はDLLの2チップすべてのAddChip/Write/Generateが正常動作すること
（NaN/Infを出さないこと）を確認する。`_test/opllex_bank_test.cpp` は拡張OPLL部の
チャンネル独立バンク切替が正しく機能することをホワイトボックスで検証する。

## 未対応・既知の課題

1. **拡張OPLLのリズム音色データ**: 4バンク共通のymfm転記データを暫定的に使用。
   実機のリズム音色ROMがバンクごとに異なるかどうかは未確認。
2. **`FmEngineApi`層からの`set_bank_instrument_data()`呼び出し経路が未整備**:
   現状は `y8960opllex` を直接使うC++コードからしか設定できない。
   `FmEngine_SetMemory` の `FmMemoryType` 拡張などでDLL越しに設定できるようにするか、
   要検討。

## ライセンス

本プロジェクト自体は `LICENSE`（MIT）に従う。

ただし `src/opllex.cpp` に内蔵しているOPLLプリセット音色データ
(`s_opll_melody` / `s_opllx_melody` / `s_opllp_melody` / `s_vrc7_melody`) のみ、
出典が異なるライセンス（CC BY-SA, Attribution-ShareAlike）のため、
このデータを含む形で再配布する場合は出典を明記すること:

> "Copyright free OPLL(x) ROM patches"
> https://github.com/plgDavid/misc/wiki/Copyright-free-OPLL(x)-ROM-patches
> David Viens, Hubert Lamontagne 作, CC BY-SA
