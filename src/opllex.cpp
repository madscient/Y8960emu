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
//  プリセット音色データ (デフォルト)
//
//  出典: "Copyright free OPLL(x) ROM patches"
//        https://github.com/plgDavid/misc/wiki/Copyright-free-OPLL(x)-ROM-patches
//  作者: David Viens, Hubert Lamontagne
//  ライセンス: CC BY-SA (Attribution-ShareAlike)
//
//  各バンクとも18音色 x 8byte = 120byte。opllex_registers::INSTDATA_SIZE (0x90=144byte)
//*********************************************************

namespace {

// April 2015 David Viens, tweaked May 19-21th 2015 Hubert Lamontagne
uint8_t const s_opll_melody[][8] = {
	{ 0x71, 0x61, 0x1E, 0x17, 0xEF, 0x7F, 0x00, 0x17 }, // Violin
	{ 0x13, 0x41, 0x1A, 0x0D, 0xF8, 0xF7, 0x23, 0x13 }, // Guitar
	{ 0x13, 0x01, 0x99, 0x00, 0xF2, 0xC4, 0x11, 0x23 }, // Piano
	{ 0x31, 0x61, 0x0E, 0x07, 0x98, 0x64, 0x70, 0x27 }, // Flute
	{ 0x22, 0x21, 0x1E, 0x06, 0xBF, 0x76, 0x00, 0x28 }, // Clarinet
	{ 0x31, 0x22, 0x16, 0x05, 0xE0, 0x71, 0x0F, 0x18 }, // Oboe
	{ 0x21, 0x61, 0x1D, 0x07, 0x82, 0x8F, 0x10, 0x07 }, // Trumpet
	{ 0x23, 0x21, 0x2D, 0x14, 0xFF, 0x7F, 0x00, 0x07 }, // Organ
	{ 0x41, 0x61, 0x1B, 0x06, 0x64, 0x65, 0x10, 0x17 }, // Horn
	{ 0x61, 0x61, 0x0B, 0x18, 0x85, 0xFF, 0x81, 0x07 }, // Synthesizer
	{ 0x13, 0x01, 0x83, 0x11, 0xFA, 0xE4, 0x10, 0x04 }, // Harpsichord
	{ 0x17, 0x81, 0x23, 0x07, 0xF8, 0xF8, 0x22, 0x12 }, // Vibraphone
	{ 0x61, 0x50, 0x0C, 0x05, 0xF2, 0xF5, 0x29, 0x42 }, // Synthesizer Bass
	{ 0x01, 0x01, 0x54, 0x03, 0xC3, 0x92, 0x03, 0x02 }, // Acoustic Bass
	{ 0x41, 0x41, 0x89, 0x03, 0xF1, 0xE5, 0x11, 0x13 }, // Electric Guitar
	{ 0x01, 0x01, 0x18, 0x0F, 0xDF, 0xF8, 0x6A, 0x6D }, //rhythm 1
	{ 0x01, 0x01, 0x00, 0x00, 0xC8, 0xD8, 0xA7, 0x48 }, //rhythm 2
	{ 0x05, 0x01, 0x00, 0x00, 0xF8, 0xAA, 0x59, 0x55 }  //rhythm 3
};

// May 4-6 2016 Hubert Lamontagne
uint8_t const s_opllx_melody[][8] = {
	{ 0x61, 0x61, 0x1B, 0x07, 0x94, 0x5F, 0x10, 0x06 }, // Strings
	{ 0x93, 0xB1, 0x51, 0x04, 0xF3, 0xF2, 0x70, 0xFB }, // Guitar
	{ 0x41, 0x21, 0x11, 0x85, 0xF2, 0xF2, 0x70, 0x75 }, // Electric Guitar
	{ 0x93, 0xB2, 0x28, 0x07, 0xF3, 0xF2, 0x70, 0xB4 }, // Electric Piano 2
	{ 0x72, 0x31, 0x97, 0x05, 0x51, 0x6F, 0x60, 0x09 }, // Flute
	{ 0x13, 0x30, 0x18, 0x06, 0xF7, 0xF4, 0x50, 0x85 }, // Marimba
	{ 0x51, 0x31, 0x1C, 0x07, 0x51, 0x71, 0x20, 0x26 }, // Trumpet
	{ 0x41, 0xF4, 0x1B, 0x07, 0x74, 0x34, 0x00, 0x06 }, // Harmonica
	{ 0x50, 0x30, 0x4D, 0x03, 0x42, 0x65, 0x20, 0x06 }, // Tuba
	{ 0x40, 0x20, 0x10, 0x85, 0xF3, 0xF5, 0x20, 0x04 }, // Synth Brass 2
	{ 0x61, 0x61, 0x1B, 0x07, 0xC5, 0x96, 0xF3, 0xF6 }, // Short Saw
	{ 0xF9, 0xF1, 0xDC, 0x00, 0xF5, 0xF3, 0x77, 0xF2 }, // Vibraphone
	{ 0x60, 0xA2, 0x91, 0x03, 0x94, 0xC1, 0xF7, 0xF7 }, // Electric Guitar 2
	{ 0x30, 0x30, 0x17, 0x06, 0xF3, 0xF1, 0xB7, 0xFC }, // Synth Bass 2
	{ 0x31, 0x36, 0x0D, 0x05, 0xF2, 0xF4, 0x27, 0x9C }, // Sitar
	{ 0x01, 0x01, 0x18, 0x0F, 0xDF, 0xF8, 0x6A, 0x6D }, //rhythm 1
	{ 0x01, 0x01, 0x00, 0x00, 0xC8, 0xD8, 0xA7, 0x48 }, //rhythm 2
	{ 0x05, 0x01, 0x00, 0x00, 0xF8, 0xAA, 0x59, 0x55 }  //rhythm 3
};

// May 14th 2015 Hubert Lamontagne
uint8_t const s_opllp_melody[][8] = {
	{ 0x72, 0x21, 0x1A, 0x07, 0xF6, 0x64, 0x01, 0x16 }, // Clarinet / Electric String
	{ 0x00, 0x10, 0x45, 0x00, 0xF6, 0x83, 0x73, 0x63 }, // Synth Bass / Bow wow
	{ 0x13, 0x01, 0x96, 0x00, 0xF1, 0xF4, 0x31, 0x23 }, // Piano / Electric Guitar
	{ 0x71, 0x21, 0x0B, 0x0F, 0xF9, 0x64, 0x70, 0x17 }, // Flute / Organ
	{ 0x02, 0x21, 0x1E, 0x06, 0xF9, 0x76, 0x00, 0x28 }, // Square Wave / Clarinet
	{ 0x00, 0x61, 0x82, 0x0E, 0xF9, 0x61, 0x20, 0x27 }, // Space Oboe / Saxophone
	{ 0x21, 0x61, 0x1B, 0x07, 0x84, 0x8F, 0x10, 0x07 }, // Trumpet
	{ 0x37, 0x32, 0xCA, 0x02, 0x66, 0x64, 0x47, 0x29 }, // Wow Bell / Street Organ
	{ 0x41, 0x41, 0x07, 0x03, 0xF5, 0x70, 0x51, 0xF5 }, // Electric Guitar / Synth Brass
	{ 0x36, 0x01, 0x5E, 0x07, 0xF2, 0xF3, 0xF7, 0xF7 }, // Vibes / Electric Piano
	{ 0x00, 0x00, 0x18, 0x06, 0xC5, 0xF3, 0x20, 0xF2 }, // Bass
	{ 0x17, 0x81, 0x25, 0x07, 0xF7, 0xF3, 0x21, 0xF7 }, // Vibraphone
	{ 0x35, 0x64, 0x00, 0x00, 0xFF, 0xF3, 0x77, 0xF5 }, // Vibrato Bell / Chime
	{ 0x11, 0x31, 0x00, 0x07, 0xDD, 0xF3, 0xFF, 0xFB }, // Click Sine / Tom Tom II
	{ 0x3A, 0x21, 0x00, 0x07, 0x95, 0x84, 0x0F, 0xF5 }, // Noise and Tone
	{ 0x01, 0x01, 0x18, 0x0F, 0xDF, 0xF8, 0x6A, 0x6D }, //rhythm 1
	{ 0x01, 0x01, 0x00, 0x00, 0xC8, 0xD8, 0xA7, 0x48 }, //rhythm 2
	{ 0x05, 0x01, 0x00, 0x00, 0xF8, 0xAA, 0x59, 0x55 }  //rhythm 3
};

// May 15th 2015 Hubert Lamontagne & David Viens
uint8_t const s_vrc7_melody[][8] = {
	{ 0x03, 0x21, 0x05, 0x06, 0xC8, 0x81, 0x42, 0x27 }, // Buzzy Bell
	{ 0x13, 0x41, 0x14, 0x0D, 0xF8, 0xF7, 0x23, 0x12 }, // Guitar
	{ 0x31, 0x11, 0x08, 0x08, 0xFA, 0xC2, 0x28, 0x22 }, // Wurly
	{ 0x31, 0x61, 0x0C, 0x07, 0xF8, 0x64, 0x60, 0x27 }, // Flute
	{ 0x22, 0x21, 0x1E, 0x06, 0xFF, 0x76, 0x00, 0x28 }, // Clarinet
	{ 0x02, 0x01, 0x05, 0x00, 0xAC, 0xF2, 0x03, 0x02 }, // Synth
	{ 0x21, 0x61, 0x1D, 0x07, 0x82, 0x8F, 0x10, 0x07 }, // Trumpet
	{ 0x23, 0x21, 0x22, 0x17, 0xFF, 0x73, 0x00, 0x17 }, // Organ
	{ 0x15, 0x11, 0x25, 0x00, 0x41, 0x71, 0x00, 0xF1 }, // Bells
	{ 0x95, 0x01, 0x10, 0x0F, 0xB8, 0xAA, 0x50, 0x02 }, // Vibes
	{ 0x17, 0xC1, 0x5E, 0x07, 0xFA, 0xF8, 0x22, 0x12 }, // Vibraphone
	{ 0x71, 0x23, 0x11, 0x06, 0x65, 0x74, 0x10, 0x16 }, // Tutti
	{ 0x01, 0x02, 0xD3, 0x05, 0xF3, 0x92, 0x83, 0xF2 }, // Fretless
	{ 0x61, 0x63, 0x0C, 0x00, 0xA4, 0xFF, 0x30, 0x06 }, // Synth Bass
	{ 0x21, 0x62, 0x0D, 0x00, 0xA1, 0xFF, 0x50, 0x08 }, // Sweep
	{ 0x01, 0x01, 0x18, 0x0F, 0xDF, 0xF8, 0x6A, 0x6D }, //rhythm 1
	{ 0x01, 0x01, 0x00, 0x00, 0xC8, 0xD8, 0xA7, 0x48 }, //rhythm 2
	{ 0x05, 0x01, 0x00, 0x00, 0xF8, 0xAA, 0x59, 0x55 }  //rhythm 3
};

// 15音色x8byte(120byte)を INSTDATA_SIZE(144byte)に展開し、
// 残り24byte(リズム3音色分)はゼロ埋めする。
void build_bank_table(uint8_t const (*melody)[8], uint8_t out[opllex_registers::INSTDATA_SIZE])
{
	std::memset(out, 0, opllex_registers::INSTDATA_SIZE);
	std::memcpy(out, melody, opllex_registers::INSTDATA_SIZE);
}

} // namespace


//*********************************************************
//  y8960opllex
//*********************************************************

y8960opllex::y8960opllex(ymfm_interface &intf)
	: m_address(0)
	, m_fm(intf)
{
	uint8_t table[opllex_registers::INSTDATA_SIZE];

	build_bank_table(s_opll_melody, table);
	m_fm.regs().set_bank_instrument_data(BANK_OPLL, table);

	build_bank_table(s_opllx_melody, table);
	m_fm.regs().set_bank_instrument_data(BANK_OPLL_X, table);

	build_bank_table(s_opllp_melody, table);
	m_fm.regs().set_bank_instrument_data(BANK_OPLL_P, table);

	build_bank_table(s_vrc7_melody, table);
	m_fm.regs().set_bank_instrument_data(BANK_VRC7, table);
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
