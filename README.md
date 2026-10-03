# Y8960emu

[Y8960](https://github.com/hra1129/Y8960_Cartridge)（MSX用サウンドカートリッジ
Y8960 Cartridge に搭載予定の統合音源チップ）のソフトウェアエミュレーター。

[ymfm](https://github.com/aaronsgiles/ymfm) をコアとして、Y8960固有の拡張ブロック
（拡張OPL2部・拡張OPLL部）を個別クラスとして実装し、
[FmEngineApi](https://github.com/madscient/FMEngineTest) 互換のDLL
(`Y8960emuEngine`) にまとめたもの。

設計の詳細は [`doc/Y8960emu_Architecture.md`](doc/Y8960emu_Architecture.md)、
開発経緯は [`doc/CHANGELOG.md`](doc/CHANGELOG.md) を参照。

## 対応チップ

`FmEngine_AddChip()` に渡すチップ名文字列と、対応するY8960の機能ブロック。
`clock` は必ず指定する（既定のクロックは持たない。0 は `FM_ERR_INVALID_ARG`）。
例: `FmEngine_AddChip(handle, "OPL2EX", 3579545, &chip_id)`
（3,579,545 Hz は MSX のマスタークロック）。

| チップ名 | Y8960の機能ブロック | 実装クラス | 備考 |
|---|---|---|---|
| `OPL2EX` | 拡張OPL2部 | `ymfm::y8960opl2ex` | YM3812相当 + ADPCM-B、1回路分。実機は2回路搭載のため`AddChip`を2回呼ぶ。波形選択（0xE0-0xF5）はYM3812と同じく、0x01 のbit5（WSE）を1にしたときだけ有効 |
| `OPLLEX` | 拡張OPLL部 | `ymfm::y8960opllex` | YM2413相当 + チャンネル独立プリセット音色バンク切替、1回路分。同様に2回路分は`AddChip`を2回 |

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

各`FmChip`インスタンス（OPL2EX/OPLLEXそれぞれ）は内部でモノラル(L=R)を
生成する。実機はチップ外側に独立したデジタルミキサーを持ち、機能ブロックごとに
パンポットを指定する設計のため、左右のパン・音量は呼び出し側が
`FmEngine_SetGain(handle, chip_id, gain_l, gain_r)` で指定する。

拡張OPL2部・拡張OPLL部は実機側もそれぞれ独立したI/Oポート対を2組（-1/-2）持つため、
`AddChip`を2回呼んで得た2つの`chip_id`にそれぞれ`SetGain`を指定すればよい。

SSGを含める場合（後述）は、SSG1/SSG2用に`chip_id`を2つ用意し、
アプリ側でアドレスレジスタの値（`< 0x10` か `>= 0x10` か）を見て
どちらのチップへ書き込むかをルーティングする必要がある
（実機のSSGブロックは単一のI/Oポート対でSSG1/SSG2を共有しているため）。

## FmEngineApi の対応範囲

仕様は [FMEngineTest](https://github.com/madscient/FMEngineTest) の
`docs/FmEngineApi.md`。`src/FmEngineApi.h` は同リポジトリの
`include/FmEngineApi.h` の写しで、直接は編集しない。

ヘッダが宣言する関数のうち、DLL がエクスポートするのは次のもの。任意の関数は、
呼び出す前に `GetProcAddress` / `dlsym` で有無を確かめること。

| 仕様の区分 | 対応 |
|---|---|
| 必須の11関数 | エクスポートする |
| 部位ごとのゲイン（任意、4関数） | エクスポートしない。どちらのチップも出力は1系統で、ゲインは `FmEngine_SetGain` で指定する |
| 外部メモリ（任意、3関数） | エクスポートする |
| 外部メモリの割り当て（任意、`FmEngine_SetMemoryEx`） | エクスポートしない |

### 外部メモリ

`FmEngine_GetMemoryCount` / `FmEngine_GetMemoryName` で列挙できる。

| チップ名 | 外部メモリの名前 | 内容 |
|---|---|---|
| `OPL2EX` | `ADPCM_B` | ADPCM-B のメモリ |
| `OPLLEX` | なし（`FmEngine_GetMemoryCount` は 0） | |

- `FmEngine_SetMemory` は `data` を複製せず、そのまま参照する。チップを使っている間は
  解放しないこと。2回路の `OPL2EX` に同じ `data` を渡せば、同じ内容を読む。
- チップからの書き込み（レジスタ経由のメモリ書き込み）は `data` に届かず、捨てられる。
- `data` が `NULL` のときと `size` が 0 のときは `FM_ERR_INVALID_ARG`。

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

成果物: `build/bin/Y8960emuEngine.dll`（Windows）/ `build/lib/libY8960emuEngine.so`（Linux）

Linux上でCMakeを使わず直接ビルドする場合:

```bash
g++ -std=c++17 -O2 -fPIC -fvisibility=hidden -DFMENGINE_EXPORTS \
  -I src -I extern/ymfm/src -shared -o libY8960emuEngine.so \
  src/FmEngineApi.cpp src/opl2ex.cpp src/opllex.cpp \
  extern/ymfm/src/ymfm_opl.cpp \
  extern/ymfm/src/ymfm_adpcm.cpp extern/ymfm/src/ymfm_pcm.cpp
```

## テスト

```bash
g++ -std=c++17 -O2 -I src _test/smoke_test.cpp -L. -lY8960emuEngine -o smoke_test
LD_LIBRARY_PATH=. ./smoke_test

g++ -std=c++17 -O2 -I src -I extern/ymfm/src _test/opllex_bank_test.cpp src/opllex.cpp -o opllex_bank_test
./opllex_bank_test
```

`_test/smoke_test.cpp` はDLLがエクスポートする関数をすべて呼び、2チップの
AddChip/Write/Generateが正常動作すること（NaN/Infを出さないこと）、クロック 0 と
未知の名前を拒否すること、外部メモリを名前で指定して `OPL2EX` の ADPCM-B が鳴る
ことを確認する。`_test/opllex_bank_test.cpp` は拡張OPLL部の
チャンネル独立バンク切替が正しく機能することをホワイトボックスで検証する。

## 未対応・既知の課題

1. **拡張OPLLのリズム音色データ**: 4バンク共通のymfm転記データを暫定的に使用。
   実機のリズム音色ROMがバンクごとに異なるかどうかは未確認。
2. **`FmEngineApi`層からの`set_bank_instrument_data()`呼び出し経路が未整備**:
   現状は `y8960opllex` を直接使うC++コードからしか設定できない。
   `OPLLEX` に外部メモリの名前を足して `FmEngine_SetMemory` で渡せるようにするなど、
   DLL越しに設定できるようにするか、要検討。

## ライセンス

本プロジェクト自体は `LICENSE`（MIT）に従う。

ただし `src/opllex.cpp` に内蔵しているOPLLプリセット音色データ
(`s_opll_melody` / `s_opllx_melody` / `s_opllp_melody` / `s_vrc7_melody`) のみ、
出典が異なるライセンス（CC BY-SA, Attribution-ShareAlike）のため、
このデータを含む形で再配布する場合は出典を明記すること:

> "Copyright free OPLL(x) ROM patches"
> https://github.com/plgDavid/misc/wiki/Copyright-free-OPLL(x)-ROM-patches
> David Viens, Hubert Lamontagne 作, CC BY-SA
