#pragma once
//
// dual_ssg.h
// Y8960 拡張SSG部: YM2149相当(SSG) x2回路
//
// レジスタマップ (Y8960_Specifications.xlsx シート "ssg" 準拠):
//   0x00-0x0F : SSG1
//   0x10-0x1F : SSG2
//
// I/Oポート (プログラマーズマニュアル 3.1節 準拠):
//   7FEAh : SSG Register Address Port (write専用)
//   7FEBh : SSG Register Data Port    (write / 一部read)
//
// ssg_engine (ymfm) はFM系チップに内蔵される用途を主眼としており、
// 単独チップとして使う場合は ssg_resampler (ymfm_opn.h) を使わず、
// clock()/output() を直接呼び出せばよい。ssg_engine::CLOCK_DIVIDER=8 により
// SSGのネイティブサンプルレートは (入力クロック / 8) となる。
//
#include "ymfm.h"
#include "ymfm_ssg.h"

namespace ymfm {

class y8960ssg
{
public:
	static constexpr uint32_t SSG_OUTPUTS = ssg_engine::OUTPUTS * 2; // SSG1(3ch) + SSG2(3ch)
	static constexpr uint32_t OUTPUTS = SSG_OUTPUTS;
	static constexpr uint32_t CLOCK_DIVIDER = ssg_engine::CLOCK_DIVIDER;
	using output_data = ymfm_output<OUTPUTS>;

	// constructor
	y8960ssg(ymfm_interface &intf);

	// pass-through helper (FmChip 層から呼ばれる)
	uint32_t sample_rate(uint32_t input_clock) const { return input_clock / CLOCK_DIVIDER; }

	// reset
	void reset();

	// save/restore
	void save_restore(ymfm_saved_state &state);

	// read access
	uint8_t read_data();
	uint8_t read(uint32_t offset);

	// write access
	void write_address(uint8_t data);
	void write_data(uint8_t data);
	void write(uint32_t offset, uint8_t data);

	// generate samples of sound
	void generate(output_data *output, uint32_t numsamples = 1);

protected:
	// internal helpers
	uint8_t m_address;                  // address register

	// internal state
	ssg_engine m_ssg1;                  // SSG1 engine (regs 0x00-0x0F)
	ssg_engine m_ssg2;                  // SSG2 engine (regs 0x10-0x1F)
};

}
