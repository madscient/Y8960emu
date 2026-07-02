#pragma once
//
// opllex.h
// Y8960 拡張OPLL部: YM2413相当(OPLL) + チャンネル独立プリセット音色バンク切替
//   (OPLL / OPLL-X / OPLL-P / VRC7 をチャンネルごとに"同時に"選択可能)
//
// レジスタマップ (Y8960_Specifications.xlsx シート "OPLL-EX" 準拠):
//   0x00-0x3F : 標準OPLL(YM2413)レジスタ
//   0x40-0x48 : 新設。ch0-ch8 ごとのROM音色バンク選択レジスタ (b1-b0=BANK)
//
// --- 設計変更履歴 ---
// v1 (旧実装): ymfm::opll_base を継承し、BANKレジスタ書き込み時に
//   opll_base::set_instrument_data() でチップ全体の音色テーブルを丸ごと
//   差し替える方式だった。この場合、あるチャンネルのBANKレジスタ書き込みが
//   他の全チャンネルの音色まで巻き込んで変えてしまう(後勝ち)問題があった。
//   ymfm::opll_registers の音色テーブルはチップ全体で1つしかなく、
//   個々のチャンネルが独立して異なるバンクを同時再生することはできない。
//
// v2 (本実装): ymfm::opll_registers 自体を丸ごとフォークした
//   opllex_registers を新設。チャンネルごとに現在のBANK選択を保持し、
//   cache_operator_data() 内で「そのチャンネルが選んでいるバンクのテーブル」
//   を参照するようにした。これによりチャンネルごとに異なるバンクの音色を
//   "同時に" 鳴らせる。ymfm::opll_base / ymfm::opll_registers には手を
//   加えない(ymfmサブモジュールは無改造のまま)。
//
#include "ymfm.h"
#include "ymfm_fm.h"
#include "ymfm_opl.h"   // opl_clock_noise_and_lfo, opl_compute_phase_step, opl_key_scale_atten, abs_sin_attenuation

namespace ymfm {

// ======================> opllex_registers
//
// ymfm::opll_registers (ymfm_opl.h/.cpp) のフォーク。
// 差分は cache_operator_data() のバンク参照ロジックと、
// write() での BANKレジスタ(0x40-0x48)ハンドリング、
// および音色テーブルをバンク数分(4つ)持つ点のみ。
//
class opllex_registers : public fm_registers_base
{
public:
	static constexpr uint32_t OUTPUTS = 2;
	static constexpr uint32_t CHANNELS = 9;
	static constexpr uint32_t ALL_CHANNELS = (1 << CHANNELS) - 1;
	static constexpr uint32_t OPERATORS = CHANNELS * 2;
	static constexpr uint32_t WAVEFORMS = 2;
	static constexpr uint32_t REGISTERS = 0x40;
	static constexpr uint32_t REG_MODE = 0x3f;
	static constexpr uint32_t DEFAULT_PRESCALE = 4;
	static constexpr uint32_t EG_CLOCK_DIVIDER = 1;
	static constexpr uint32_t CSM_TRIGGER_MASK = 0;
	static constexpr bool EG_HAS_DEPRESS = true;
	static constexpr bool MODULATOR_DELAY = true;
	static constexpr uint8_t STATUS_TIMERA = 0;
	static constexpr uint8_t STATUS_TIMERB = 0;
	static constexpr uint8_t STATUS_BUSY = 0;
	static constexpr uint8_t STATUS_IRQ = 0;

	// OPLL-specific constants
	static constexpr uint32_t INSTDATA_SIZE = 0x90;   // 1バンクあたり (15melody + 3rhythm) * 8byte

	// Y8960拡張: チャンネルごとの音色バンク
	static constexpr uint32_t BANK_COUNT      = 4;    // OPLL / OPLL-X / OPLL-P / VRC7
	static constexpr uint32_t BANK_REG_BASE   = 0x40; // 新設レジスタのベースアドレス
	static constexpr uint32_t BANK_REG_COUNT  = CHANNELS;

	// constructor
	opllex_registers();

	// reset to initial state
	void reset();

	// save/restore
	void save_restore(ymfm_saved_state &state);

	// map channel number to register offset
	static constexpr uint32_t channel_offset(uint32_t chnum)
	{
		assert(chnum < CHANNELS);
		return chnum;
	}

	// map operator number to register offset
	static constexpr uint32_t operator_offset(uint32_t opnum)
	{
		assert(opnum < OPERATORS);
		return opnum;
	}

	// return an array of operator indices for each channel
	struct operator_mapping { uint32_t chan[CHANNELS]; };
	void operator_map(operator_mapping &dest) const;

	// read a register value
	uint8_t read(uint16_t index) const { return m_regdata[index]; }

	// handle writes to the register array (BANKレジスタもここで横取りする)
	bool write(uint16_t index, uint8_t data, uint32_t &chan, uint32_t &opmask);

	// clock the noise and LFO, if present, returning LFO PM value
	int32_t clock_noise_and_lfo();

	// reset the LFO
	void reset_lfo() { m_lfo_am_counter = m_lfo_pm_counter = 0; }

	// return the AM offset from LFO for the given channel
	uint32_t lfo_am_offset(uint32_t choffs) const { return m_lfo_am; }

	// return LFO/noise states
	uint32_t noise_state() const { return m_noise_lfsr >> 23; }

	// caching helpers
	void cache_operator_data(uint32_t choffs, uint32_t opoffs, opdata_cache &cache);

	// compute the phase step, given a PM value
	uint32_t compute_phase_step(uint32_t choffs, uint32_t opoffs, opdata_cache const &cache, int32_t lfo_raw_pm);

	// log a key-on event
	std::string log_keyon(uint32_t choffs, uint32_t opoffs);

	// Y8960拡張: バンク別プリセットテーブルの設定 (各 INSTDATA_SIZE バイト)
	void set_bank_instrument_data(uint32_t bank, uint8_t const *data)
	{
		assert(bank < BANK_COUNT);
		std::copy_n(data, INSTDATA_SIZE, &m_bank_instdata[bank][0]);
	}

	// system-wide registers
	uint32_t rhythm_enable() const                   { return byte(0x0e, 5, 1); }
	uint32_t rhythm_keyon() const                    { return byte(0x0e, 4, 0); }
	uint32_t test() const                            { return byte(0x0f, 0, 8); }
	uint32_t waveform_enable() const                 { return 1; }
	uint32_t timer_a_value() const                   { return 0; }
	uint32_t timer_b_value() const                   { return 0; }
	uint32_t status_mask() const                     { return 0; }
	uint32_t irq_reset() const                       { return 0; }
	uint32_t reset_timer_b() const                   { return 0; }
	uint32_t reset_timer_a() const                   { return 0; }
	uint32_t enable_timer_b() const                  { return 0; }
	uint32_t enable_timer_a() const                  { return 0; }
	uint32_t load_timer_b() const                    { return 0; }
	uint32_t load_timer_a() const                    { return 0; }
	uint32_t csm() const                             { return 0; }

	// per-channel registers
	uint32_t ch_block_freq(uint32_t choffs) const    { return word(0x20, 0, 4, 0x10, 0, 8, choffs); }
	uint32_t ch_sustain(uint32_t choffs) const       { return byte(0x20, 5, 1, choffs); }
	uint32_t ch_total_level(uint32_t choffs) const   { return instchbyte(0x02, 0, 6, choffs); }
	uint32_t ch_feedback(uint32_t choffs) const      { return instchbyte(0x03, 0, 3, choffs); }
	uint32_t ch_algorithm(uint32_t choffs) const     { return 0; }
	uint32_t ch_instrument(uint32_t choffs) const    { return byte(0x30, 4, 4, choffs); }
	uint32_t ch_output_any(uint32_t choffs) const    { return 1; }
	uint32_t ch_output_0(uint32_t choffs) const      { return !is_rhythm(choffs); }
	uint32_t ch_output_1(uint32_t choffs) const      { return is_rhythm(choffs); }
	uint32_t ch_output_2(uint32_t choffs) const      { return 0; }
	uint32_t ch_output_3(uint32_t choffs) const      { return 0; }

	// Y8960拡張: チャンネルの現在の音色バンク
	uint32_t ch_bank(uint32_t choffs) const          { return m_channel_bank[choffs]; }

	// per-operator registers
	uint32_t op_lfo_am_enable(uint32_t opoffs) const { return instopbyte(0x00, 7, 1, opoffs); }
	uint32_t op_lfo_pm_enable(uint32_t opoffs) const { return instopbyte(0x00, 6, 1, opoffs); }
	uint32_t op_eg_sustain(uint32_t opoffs) const    { return instopbyte(0x00, 5, 1, opoffs); }
	uint32_t op_ksr(uint32_t opoffs) const           { return instopbyte(0x00, 4, 1, opoffs); }
	uint32_t op_multiple(uint32_t opoffs) const      { return instopbyte(0x00, 0, 4, opoffs); }
	uint32_t op_ksl(uint32_t opoffs) const           { return instopbyte(0x02, 6, 2, opoffs); }
	uint32_t op_waveform(uint32_t opoffs) const      { return instchbyte(0x03, 3 + bitfield(opoffs, 0), 1, opoffs >> 1); }
	uint32_t op_attack_rate(uint32_t opoffs) const   { return instopbyte(0x04, 4, 4, opoffs); }
	uint32_t op_decay_rate(uint32_t opoffs) const    { return instopbyte(0x04, 0, 4, opoffs); }
	uint32_t op_sustain_level(uint32_t opoffs) const { return instopbyte(0x06, 4, 4, opoffs); }
	uint32_t op_release_rate(uint32_t opoffs) const  { return instopbyte(0x06, 0, 4, opoffs); }
	uint32_t op_volume(uint32_t opoffs) const        { return byte(0x30, 4 * bitfield(~opoffs, 0), 4, opoffs >> 1); }

private:
	// return a bitfield extracted from a byte
	uint32_t byte(uint32_t offset, uint32_t start, uint32_t count, uint32_t extra_offset = 0) const
	{
		return bitfield(m_regdata[offset + extra_offset], start, count);
	}

	// return a bitfield extracted from a pair of bytes, MSBs listed first
	uint32_t word(uint32_t offset1, uint32_t start1, uint32_t count1, uint32_t offset2, uint32_t start2, uint32_t count2, uint32_t extra_offset = 0) const
	{
		return (byte(offset1, start1, count1, extra_offset) << count2) | byte(offset2, start2, count2, extra_offset);
	}

	// helpers to read from instrument channel/operator data
	uint32_t instchbyte(uint32_t offset, uint32_t start, uint32_t count, uint32_t choffs) const { return bitfield(m_chinst[choffs][offset], start, count); }
	uint32_t instopbyte(uint32_t offset, uint32_t start, uint32_t count, uint32_t opoffs) const { return bitfield(m_opinst[opoffs][offset], start, count); }

	// helper to determine if the this channel is an active rhythm channel
	bool is_rhythm(uint32_t choffs) const
	{
		return rhythm_enable() && choffs >= 6;
	}

	// internal state
	uint16_t m_lfo_am_counter;            // LFO AM counter
	uint16_t m_lfo_pm_counter;            // LFO PM counter
	uint32_t m_noise_lfsr;                // noise LFSR state
	uint8_t m_lfo_am;                     // current LFO AM value
	uint8_t const *m_chinst[CHANNELS];    // pointer to instrument data for each channel
	uint8_t const *m_opinst[OPERATORS];   // pointer to instrument data for each operator
	uint8_t m_regdata[REGISTERS];         // register data
	uint16_t m_waveform[WAVEFORMS][WAVEFORM_LENGTH]; // waveforms

	// Y8960拡張: バンクごとの音色テーブル + チャンネルごとのバンク選択
	uint8_t m_bank_instdata[BANK_COUNT][INSTDATA_SIZE]; // バンク別プリセットテーブル
	uint8_t m_channel_bank[CHANNELS];                   // チャンネルごとに選択中のバンク(0-3)
};


// ======================> y8960opllex
//
// opllex_registers を使う OPLL チップ本体。
// ymfm::opll_base は fm_engine_base<opll_registers> 固定のため使えず、
// 同等の実装を fm_engine_base<opllex_registers> 向けに用意する。
//
class y8960opllex
{
public:
	using fm_engine = fm_engine_base<opllex_registers>;
	using output_data = fm_engine::output_data;
	static constexpr uint32_t OUTPUTS = fm_engine::OUTPUTS;

	enum preset_bank : uint8_t {
		BANK_OPLL   = 0,   // YM2413 標準プリセット
		BANK_OPLL_X = 1,   // YM2423 (OPLL-X) 相当
		BANK_OPLL_P = 2,   // YMF281 (OPLL-P) 相当
		BANK_VRC7   = 3,   // DS1001 (VRC7)   相当
	};

	// constructor
	y8960opllex(ymfm_interface &intf);

	// バンク別プリセットテーブルの設定 (ストリーム開始前に呼ぶこと)
	//
	// !!! TODO !!!
	// 現状コンストラクタで全バンクとも全ゼロで初期化される。実データは
	// 一次情報 (IKAOPLL: https://github.com/ika-musume/IKAOPLL,
	//  Copyright-free OPLL(x) ROM patches:
	//  https://github.com/plgDavid/misc/wiki/Copyright-free-OPLL(x)-ROM-patches)
	// を確認のうえ正確に転記し、本メソッド経由で設定すること。
	void set_bank_instrument_data(preset_bank bank, uint8_t const *data)
	{
		m_fm.regs().set_bank_instrument_data(static_cast<uint32_t>(bank), data);
	}

	// pass-through helpers
	uint32_t sample_rate(uint32_t input_clock) const { return m_fm.sample_rate(input_clock); }
	void invalidate_caches() { m_fm.invalidate_caches(); }

	// reset
	void reset();

	// save/restore
	void save_restore(ymfm_saved_state &state);

	// read access -- OPLLと同様、実質的には持たない
	uint8_t read_status() { return 0x00; }
	uint8_t read(uint32_t offset) { return 0x00; }

	// write access
	void write_address(uint8_t data);
	void write_data(uint8_t data);
	void write(uint32_t offset, uint8_t data);

	// generate samples of sound
	void generate(output_data *output, uint32_t numsamples = 1);

protected:
	// internal state
	uint8_t m_address;               // address register
	fm_engine m_fm;                  // core FM engine
};

}
