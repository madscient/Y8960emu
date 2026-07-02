//
// opllex.cpp
//
// opllex_registers の内部演算 (ノイズ/LFO, フェーズステップ計算) は
// ymfm::opl_registers_base 系が使う数式と同一だが、該当関数が ymfm_opl.cpp
// 内で static/非公開inline定義のため直接流用できない。そのため同じ計算式を
// y8960_* の名前で本ファイル内にコピーしている(ymfmサブモジュール自体は
// 無改造)。
//
#include "opllex.h"
#include "ymfm_fm.ipp"   // fm_engine_base<opllex_registers> の実体化に必要
#include <cstring>
#include <algorithm>

namespace ymfm {

//-------------------------------------------------
//  y8960_opl_key_scale_atten
//  (ymfm::opl_key_scale_atten と同一の計算式)
//-------------------------------------------------
static inline uint32_t y8960_opl_key_scale_atten(uint32_t block, uint32_t fnum_4msb)
{
	static uint8_t const fnum_to_atten[16] = { 0,24,32,37,40,43,45,47,48,50,51,52,53,54,55,56 };
	int32_t result = fnum_to_atten[fnum_4msb] - 8 * (block ^ 7);
	return std::max<int32_t>(0, result);
}

//-------------------------------------------------
//  y8960_opl_clock_noise_and_lfo
//  (ymfm::opl_clock_noise_and_lfo と同一の計算式)
//-------------------------------------------------
static int32_t y8960_opl_clock_noise_and_lfo(uint32_t &noise_lfsr, uint16_t &lfo_am_counter, uint16_t &lfo_pm_counter, uint8_t &lfo_am, uint32_t am_depth, uint32_t pm_depth)
{
	noise_lfsr <<= 1;
	noise_lfsr |= bitfield(noise_lfsr, 23) ^ bitfield(noise_lfsr, 9) ^ bitfield(noise_lfsr, 8) ^ bitfield(noise_lfsr, 1);

	uint32_t am_counter = lfo_am_counter++;
	if (am_counter >= 210*64 - 1)
		lfo_am_counter = 0;

	int shift = 9 - 2 * am_depth;
	lfo_am = ((am_counter < 105*64) ? am_counter : (210*64+63 - am_counter)) >> shift;

	uint32_t pm_counter = lfo_pm_counter++;
	static int8_t const pm_scale[8] = { 8, 4, 0, -4, -8, -4, 0, 4 };
	return pm_scale[bitfield(pm_counter, 10, 3)] >> (pm_depth ^ 1);
}

//-------------------------------------------------
//  y8960_opl_compute_phase_step
//  (ymfm::opl_compute_phase_step と同一の計算式)
//-------------------------------------------------
static uint32_t y8960_opl_compute_phase_step(uint32_t block_freq, uint32_t multiple, int32_t lfo_raw_pm)
{
	uint32_t fnum = bitfield(block_freq, 0, 10) << 2;
	fnum += (lfo_raw_pm * bitfield(block_freq, 7, 3)) >> 1;
	fnum &= 0xfff;

	uint32_t block = bitfield(block_freq, 10, 3);
	uint32_t phase_step = (fnum << block) >> 2;

	return (phase_step * multiple) >> 1;
}


//*********************************************************
//  opllex_registers
//*********************************************************

opllex_registers::opllex_registers()
	: m_lfo_am_counter(0)
	, m_lfo_pm_counter(0)
	, m_noise_lfsr(1)
	, m_lfo_am(0)
{
	// waveform tables (OPLL相当: sin + half-sin)
	for (uint32_t index = 0; index < WAVEFORM_LENGTH; index++)
		m_waveform[0][index] = abs_sin_attenuation(index) | (bitfield(index, 9) << 15);

	uint16_t zeroval = m_waveform[0][0];
	for (uint32_t index = 0; index < WAVEFORM_LENGTH; index++)
		m_waveform[1][index] = bitfield(index, 9) ? zeroval : m_waveform[0][index];

	for (uint32_t choffs = 0; choffs < CHANNELS; choffs++)
		m_chinst[choffs] = &m_regdata[0];
	for (uint32_t opoffs = 0; opoffs < OPERATORS; opoffs++)
		m_opinst[opoffs] = &m_regdata[bitfield(opoffs, 0)];

	std::memset(m_bank_instdata, 0, sizeof(m_bank_instdata));
	std::memset(m_channel_bank, 0, sizeof(m_channel_bank));
}

void opllex_registers::reset()
{
	std::fill_n(&m_regdata[0], REGISTERS, 0);
	std::memset(m_channel_bank, 0, sizeof(m_channel_bank));
}

void opllex_registers::save_restore(ymfm_saved_state &state)
{
	state.save_restore(m_lfo_am_counter);
	state.save_restore(m_lfo_pm_counter);
	state.save_restore(m_lfo_am);
	state.save_restore(m_noise_lfsr);
	state.save_restore(m_regdata);
	state.save_restore(m_channel_bank);
}

void opllex_registers::operator_map(operator_mapping &dest) const
{
	static const operator_mapping s_fixed_map =
	{ {
		operator_list(  0,  1 ),
		operator_list(  2,  3 ),
		operator_list(  4,  5 ),
		operator_list(  6,  7 ),
		operator_list(  8,  9 ),
		operator_list( 10, 11 ),
		operator_list( 12, 13 ),
		operator_list( 14, 15 ),
		operator_list( 16, 17 ),
	} };
	dest = s_fixed_map;
}

bool opllex_registers::write(uint16_t index, uint8_t data, uint32_t &channel, uint32_t &opmask)
{
	// Y8960拡張: チャンネルごとの音色バンク選択レジスタ (0x40-0x48)
	if (index >= BANK_REG_BASE && index < BANK_REG_BASE + BANK_REG_COUNT)
	{
		m_channel_bank[index - BANK_REG_BASE] = data & (BANK_COUNT - 1);
		return false;
	}

	// 以下は ymfm::opll_registers::write() と同一
	if (index >= REGISTERS)
		return false;

	m_regdata[index] = data;

	if (index == 0x0e)
	{
		channel = RHYTHM_CHANNEL;
		opmask = bitfield(data, 5) ? bitfield(data, 0, 5) : 0;
		return true;
	}

	if ((index & 0xf0) == 0x20)
	{
		channel = index & 0x0f;
		if (channel < CHANNELS)
		{
			opmask = bitfield(data, 4) ? 3 : 0;
			return true;
		}
	}
	return false;
}

int32_t opllex_registers::clock_noise_and_lfo()
{
	return y8960_opl_clock_noise_and_lfo(m_noise_lfsr, m_lfo_am_counter, m_lfo_pm_counter, m_lfo_am, 1, 1);
}

void opllex_registers::cache_operator_data(uint32_t choffs, uint32_t opoffs, opdata_cache &cache)
{
	// Y8960拡張: このチャンネルが選択しているバンクのテーブルを参照する
	// (チャンネルごとに独立してOPLL/OPLL-X/OPLL-P/VRC7を同時に選べる)
	//
	// リズムチャンネル(choffs>=6かつrhythm_enable())も含めて bank_table の
	// 参照方法を分けていない。これは意図的な設計で、「リズム音色だけ特別扱い
	// する理由がない限り分岐を増やさない」という方針による。実用上はどの
	// バンクもリズム用の3音色(BD/SD/TOM/CYM/HH相当, テーブル内 index15-17)を
	// 同じ内容で持たせる想定のため、通常はBANKレジスタの値がリズム音色の
	// 聞こえ方に影響しない。ただし将来バンクごとにリズム音色を変えたい場合も、
	// この共通ロジックのままで自然に対応できる。
	uint8_t const *bank_table = &m_bank_instdata[ch_bank(choffs)][0];

	uint32_t instrument = ch_instrument(choffs);
	if (rhythm_enable() && choffs >= 6)
		m_chinst[choffs] = &bank_table[8 * (15 + (choffs - 6))];
	else
		m_chinst[choffs] = (instrument == 0) ? &m_regdata[0] : &bank_table[8 * (instrument - 1)];
	m_opinst[opoffs] = m_chinst[choffs] + bitfield(opoffs, 0);

	cache.waveform = &m_waveform[op_waveform(opoffs) % WAVEFORMS][0];

	uint32_t block_freq = cache.block_freq = ch_block_freq(choffs);
	uint32_t keycode = bitfield(block_freq, 8, 4);

	cache.detune = 0;

	uint32_t multiple = op_multiple(opoffs);
	cache.multiple = ((multiple & 0xe) | bitfield(0xc2aa, multiple)) * 2;
	if (cache.multiple == 0)
		cache.multiple = 1;

	if (op_lfo_pm_enable(opoffs) == 0)
		cache.phase_step = compute_phase_step(choffs, opoffs, cache, 0);
	else
		cache.phase_step = opdata_cache::PHASE_STEP_DYNAMIC;

	if (bitfield(opoffs, 0) == 1 || (rhythm_enable() && choffs >= 7))
		cache.total_level = op_volume(opoffs) * 4;
	else
		cache.total_level = ch_total_level(choffs);
	cache.total_level <<= 3;

	uint32_t ksl = op_ksl(opoffs);
	if (ksl != 0)
		cache.total_level += y8960_opl_key_scale_atten(bitfield(block_freq, 9, 3), bitfield(block_freq, 5, 4)) << ksl;

	cache.eg_sustain = op_sustain_level(opoffs);
	cache.eg_sustain |= (cache.eg_sustain + 1) & 0x10;
	cache.eg_sustain <<= 5;

	constexpr uint8_t DP = 12 * 4;
	constexpr uint8_t RR = 7 * 4;
	constexpr uint8_t RS = 5 * 4;

	uint32_t ksrval = keycode >> (2 * (op_ksr(opoffs) ^ 1));
	cache.eg_rate[EG_DEPRESS] = DP;
	cache.eg_rate[EG_ATTACK] = effective_rate(op_attack_rate(opoffs) * 4, ksrval);
	cache.eg_rate[EG_DECAY] = effective_rate(op_decay_rate(opoffs) * 4, ksrval);
	if (op_eg_sustain(opoffs))
	{
		cache.eg_rate[EG_SUSTAIN] = 0;
		cache.eg_rate[EG_RELEASE] = ch_sustain(choffs) ? RS : effective_rate(op_release_rate(opoffs) * 4, ksrval);
	}
	else
	{
		cache.eg_rate[EG_SUSTAIN] = effective_rate(op_release_rate(opoffs) * 4, ksrval);
		cache.eg_rate[EG_RELEASE] = ch_sustain(choffs) ? RS : RR;
	}
}

uint32_t opllex_registers::compute_phase_step(uint32_t choffs, uint32_t opoffs, opdata_cache const &cache, int32_t lfo_raw_pm)
{
	return y8960_opl_compute_phase_step(cache.block_freq << 1, cache.multiple, op_lfo_pm_enable(opoffs) ? lfo_raw_pm : 0);
}

std::string opllex_registers::log_keyon(uint32_t choffs, uint32_t opoffs)
{
	return "";
}


//*********************************************************
//  y8960opllex
//*********************************************************

y8960opllex::y8960opllex(ymfm_interface &intf)
	: m_address(0)
	, m_fm(intf)
{
}

void y8960opllex::reset()
{
	m_fm.reset();
}

void y8960opllex::save_restore(ymfm_saved_state &state)
{
	state.save_restore(m_address);
	m_fm.save_restore(state);
}

void y8960opllex::write_address(uint8_t data)
{
	m_fm.intf().ymfm_set_busy_end(12);
	m_address = data;
}

void y8960opllex::write_data(uint8_t data)
{
	m_fm.intf().ymfm_set_busy_end(84);
	m_fm.write(m_address, data);
}

void y8960opllex::write(uint32_t offset, uint8_t data)
{
	switch (offset & 1)
	{
		case 0: write_address(data); break;
		case 1: write_data(data);    break;
	}
}

void y8960opllex::generate(output_data *output, uint32_t numsamples)
{
	for (uint32_t samp = 0; samp < numsamples; samp++, output++)
	{
		m_fm.clock(fm_engine::ALL_CHANNELS);

		m_fm.output(output->clear(), 5, 256, fm_engine::ALL_CHANNELS);

		output->data[0] = (output->data[0] * 128) / 9;
		output->data[1] = (output->data[1] * 128) / 9;
	}
}

}
