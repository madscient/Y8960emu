// opllex_bank_test.cpp
// 回帰テスト: あるチャンネルのBANKレジスタ書き込みが、
// 別チャンネルの音色/出力に影響を与えないことを確認する。
//
// (旧設計のバグ: set_instrument_data()でチップ全体のテーブルを丸ごと
//  差し替えていたため、あるチャンネルのBANK変更が他チャンネルの音色まで
//  変えてしまっていた)
//
#include "opllex.h"
#include <cstdio>
#include <cstring>
#include <vector>

using namespace ymfm;

// テスト用の最小 ymfm_interface 実装
class NullInterface : public ymfm_interface {
public:
    void    ymfm_set_timer(uint32_t, int32_t) override {}
    void    ymfm_sync_mode_write(uint8_t) override {}
    void    ymfm_sync_check_interrupts() override {}
    void    ymfm_update_irq(bool) override {}
    uint8_t ymfm_external_read(access_class, uint32_t) override { return 0; }
    void    ymfm_external_write(access_class, uint32_t, uint8_t) override {}
};

// channel0を鍵盤ON状態にし、channel1のBANKレジスタだけ変えて生成音を比較する
static std::vector<int32_t> run(uint8_t ch1_bank_value, bool touch_ch1_bank)
{
    NullInterface intf;
    y8960opllex chip(intf);
    chip.reset();

    // 2つの異なるプリセットバンクを用意 (中身は違うがどちらもチャンネル0では使わない)
    uint8_t bankA[opllex_registers::INSTDATA_SIZE];
    uint8_t bankB[opllex_registers::INSTDATA_SIZE];
    std::memset(bankA, 0x11, sizeof(bankA));
    std::memset(bankB, 0x77, sizeof(bankB));

    // instrument index=1 (テーブルoffset 8*(1-1)=0) を「確実に発音する」設定に上書き。
    // [0]=mod multiple, [1]=car multiple, [2]=mod TL, [3]=car ksl,
    // [4]=mod AR/DR, [5]=car AR/DR, [6]=mod SL/RR, [7]=car SL/RR
    uint8_t const audible[8] = { 0x01, 0x01, 0x00, 0x00, 0xF0, 0xF0, 0x00, 0x00 };
    std::memcpy(bankA, audible, sizeof(audible));

    chip.set_bank_instrument_data(y8960opllex::BANK_OPLL,   bankA);
    chip.set_bank_instrument_data(y8960opllex::BANK_VRC7,   bankB);

    // channel0: BANK=OPLL固定、instrument=1, volume=0
    chip.write(0, 0x40 + 0); chip.write(1, y8960opllex::BANK_OPLL); // ch0 BANKレジスタ
    chip.write(0, 0x30 + 0); chip.write(1, (1 << 4) | 0);           // ch0 instrument select
    chip.write(0, 0x10 + 0); chip.write(1, 0x50);                   // ch0 fnum low
    chip.write(0, 0x20 + 0); chip.write(1, 0x10 | 0x08);            // ch0 key-on + block/fnum_hi

    if (touch_ch1_bank)
    {
        // channel1は鍵盤OFFのまま、BANKレジスタだけ書き込む
        chip.write(0, 0x40 + 1); chip.write(1, ch1_bank_value);
    }

    std::vector<int32_t> out;
    for (int i = 0; i < 200; i++)
    {
        y8960opllex::output_data sample{};
        chip.generate(&sample, 1);
        out.push_back(sample.data[0]);
        out.push_back(sample.data[1]);
    }
    return out;
}

// channel0自身のBANKを切り替えたときは出力が変わることを確認する
// (独立性テストが「バンクが何も効いていないから一致する」という
//  偽陽性でないことを担保するための対照実験)
static std::vector<int32_t> run_ch0_bank(uint8_t bank)
{
    NullInterface intf;
    y8960opllex chip(intf);
    chip.reset();

    uint8_t bankA[opllex_registers::INSTDATA_SIZE];
    uint8_t bankB[opllex_registers::INSTDATA_SIZE];
    uint8_t const audibleA[8] = { 0x01, 0x01, 0x00, 0x00, 0xF0, 0xF0, 0x00, 0x00 };
    uint8_t const audibleB[8] = { 0x04, 0x04, 0x00, 0x00, 0xF0, 0xF0, 0x00, 0x00 }; // multiple違い
    std::memset(bankA, 0, sizeof(bankA));
    std::memset(bankB, 0, sizeof(bankB));
    std::memcpy(bankA, audibleA, sizeof(audibleA));
    std::memcpy(bankB, audibleB, sizeof(audibleB));
    chip.set_bank_instrument_data(y8960opllex::BANK_OPLL, bankA);
    chip.set_bank_instrument_data(y8960opllex::BANK_VRC7, bankB);

    chip.write(0, 0x40 + 0); chip.write(1, bank);
    chip.write(0, 0x30 + 0); chip.write(1, (1 << 4) | 0);
    chip.write(0, 0x10 + 0); chip.write(1, 0x50);
    chip.write(0, 0x20 + 0); chip.write(1, 0x10 | 0x08);

    std::vector<int32_t> out;
    for (int i = 0; i < 200; i++)
    {
        y8960opllex::output_data sample{};
        chip.generate(&sample, 1);
        out.push_back(sample.data[0]);
        out.push_back(sample.data[1]);
    }
    return out;
}

// リズムチャンネル(ch6-8, rhythm_enable時)もBANK設定に従うことを確認する。
// (メロディチャンネルと同じ bank_table 参照ロジックを分岐なしで使っている
//  ことのテスト。将来バンクごとにリズム音色を変える場合の回帰にもなる)
static std::vector<int32_t> run_rhythm_bank(uint8_t ch6_bank)
{
    NullInterface intf;
    y8960opllex chip(intf);
    chip.reset();

    // リズム専用エントリ index15 (BD, テーブルoffset 8*15=120) を
    // バンクごとに異なる内容にする
    uint8_t bankA[opllex_registers::INSTDATA_SIZE];
    uint8_t bankB[opllex_registers::INSTDATA_SIZE];
    std::memset(bankA, 0, sizeof(bankA));
    std::memset(bankB, 0, sizeof(bankB));
    uint8_t const rhythmA[8] = { 0x01, 0x01, 0x00, 0x00, 0xF0, 0xF0, 0x00, 0x00 };
    uint8_t const rhythmB[8] = { 0x05, 0x05, 0x00, 0x00, 0xF0, 0xF0, 0x00, 0x00 };
    std::memcpy(&bankA[8 * 15], rhythmA, sizeof(rhythmA));
    std::memcpy(&bankB[8 * 15], rhythmB, sizeof(rhythmB));
    chip.set_bank_instrument_data(y8960opllex::BANK_OPLL, bankA);
    chip.set_bank_instrument_data(y8960opllex::BANK_VRC7, bankB);

    chip.write(0, 0x0e); chip.write(1, 0x20);       // リズムモード有効化 (rhythm_enable)
    chip.write(0, 0x40 + 6); chip.write(1, ch6_bank); // ch6(BD)のBANKレジスタ
    chip.write(0, 0x10 + 6); chip.write(1, 0x50);      // ch6 fnum low
    chip.write(0, 0x20 + 6); chip.write(1, 0x08);      // ch6 block/fnum_hi
    chip.write(0, 0x0e); chip.write(1, 0x20 | 0x10);   // BD keyon (bit4)

    std::vector<int32_t> out;
    for (int i = 0; i < 200; i++)
    {
        y8960opllex::output_data sample{};
        chip.generate(&sample, 1);
        out.push_back(sample.data[0]);
        out.push_back(sample.data[1]);
    }
    return out;
}

// コンストラクタが自動設定する「実プリセットデータ」(CC BY-SA由来)が
// バンクごとにちゃんと異なる音を出すことを確認する
// (set_bank_instrument_data で上書きせず、デフォルトのまま使う)
static std::vector<int32_t> run_default_preset(uint8_t bank, uint32_t instrument)
{
    NullInterface intf;
    y8960opllex chip(intf);  // コンストラクタが4バンク分の実データを自動ロード
    chip.reset();

    chip.write(0, 0x40 + 0); chip.write(1, bank);
    chip.write(0, 0x30 + 0); chip.write(1, (instrument << 4) | 0);
    chip.write(0, 0x10 + 0); chip.write(1, 0x50);
    chip.write(0, 0x20 + 0); chip.write(1, 0x10 | 0x08);

    std::vector<int32_t> out;
    for (int i = 0; i < 4000; i++)   // 実データはAR(アタックレート)が緩やかな楽器もあるため長めに生成
    {
        y8960opllex::output_data sample{};
        chip.generate(&sample, 1);
        out.push_back(sample.data[0]);
        out.push_back(sample.data[1]);
    }
    return out;
}

int main()
{
    auto baseline   = run(0, false);                              // ch1のBANKレジスタに触れない
    auto ch1_bank_A  = run(y8960opllex::BANK_OPLL, true);          // ch1をBANK=OPLLに
    auto ch1_bank_B  = run(y8960opllex::BANK_VRC7, true);          // ch1をBANK=VRC7に(別バンク)

    bool a_matches_baseline = (baseline == ch1_bank_A);
    bool b_matches_baseline = (baseline == ch1_bank_B);

    std::printf("[%s] ch1 BANK=OPLL write does not affect ch0 output\n",
                a_matches_baseline ? "OK" : "FAIL");
    std::printf("[%s] ch1 BANK=VRC7 write does not affect ch0 output\n",
                b_matches_baseline ? "OK" : "FAIL");

    bool any_nonzero = false;
    for (auto v : baseline) if (v != 0) any_nonzero = true;
    std::printf("[%s] ch0 actually produces sound (sanity check)\n",
                any_nonzero ? "OK" : "FAIL");

    // 対照実験: ch0自身のBANKを変えたら出力が変わるか
    auto ch0_bankOPLL = run_ch0_bank(y8960opllex::BANK_OPLL);
    auto ch0_bankVRC7 = run_ch0_bank(y8960opllex::BANK_VRC7);
    bool own_bank_changes_output = (ch0_bankOPLL != ch0_bankVRC7);
    std::printf("[%s] ch0's own BANK change actually alters ch0 output (control test)\n",
                own_bank_changes_output ? "OK" : "FAIL");

    // リズムチャンネルもBANK設定に従うか
    auto rhythm_OPLL = run_rhythm_bank(y8960opllex::BANK_OPLL);
    auto rhythm_VRC7 = run_rhythm_bank(y8960opllex::BANK_VRC7);
    bool rhythm_follows_bank = (rhythm_OPLL != rhythm_VRC7);
    bool rhythm_nonzero = false;
    for (auto v : rhythm_OPLL) if (v != 0) rhythm_nonzero = true;
    std::printf("[%s] rhythm channel (BD) follows its own channel's BANK too\n",
                (rhythm_follows_bank && rhythm_nonzero) ? "OK" : "FAIL");

    // デフォルトプリセット(実データ)がバンクごとに違う音になっているか
    auto default_opll  = run_default_preset(y8960opllex::BANK_OPLL,   1); // Violin/Strings系
    auto default_opllx = run_default_preset(y8960opllex::BANK_OPLL_X, 1);
    auto default_opllp = run_default_preset(y8960opllex::BANK_OPLL_P, 1);
    auto default_vrc7   = run_default_preset(y8960opllex::BANK_VRC7,   1);
    bool default_nonzero = false;
    for (auto v : default_opll) if (v != 0) default_nonzero = true;
    bool default_banks_differ =
        (default_opll != default_opllx) &&
        (default_opll != default_opllp) &&
        (default_opll != default_vrc7)  &&
        (default_opllx != default_opllp) &&
        (default_opllx != default_vrc7) &&
        (default_opllp != default_vrc7);
    std::printf("[%s] default (CC BY-SA) presets produce sound\n",
                default_nonzero ? "OK" : "FAIL");
    std::printf("[%s] default presets differ across all 4 banks\n",
                default_banks_differ ? "OK" : "FAIL");

    return (a_matches_baseline && b_matches_baseline && any_nonzero &&
            own_bank_changes_output && rhythm_follows_bank && rhythm_nonzero &&
            default_nonzero && default_banks_differ) ? 0 : 1;
}
