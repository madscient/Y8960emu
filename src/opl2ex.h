#pragma once
//
// opl2ex.h
// Y8960 拡張OPL2部: YM3812相当(OPL2) + ADPCM-B (Y8950のADPCM部相当)
//
// レジスタマップ (Y8960_Specifications.xlsx シート "OPL2+ADPCM" 準拠、
// ymfm::y8950 の実装 (ymfm_opl.cpp) と同一のアドレス配置):
//   0x07, 0x09-0x12, 0x15-0x17 : ADPCM-B registers
//   0x08                       : FM(上位2bit) / ADPCM-B(下位4bit) 分割書き込み
//   0x18                       : I/O DDR (廃止扱いだが互換のため残す)
//   0x19                       : I/O DATA (廃止)
//   0x1A                       : PCM-DATA (廃止, ADPCM-B 0x0F 相当にマップ)
//   それ以外                    : OPL2 (ym3812) 本体へ
//
// 実装メモ:
//   ymfm::y8950 の内部実装 (ymfm_opl.cpp) は fm_engine_base<opl_registers> の
//   内部寄りのメンバ (m_fm.status()/m_fm.clock()等) を直接叩いているが、
//   これらは ymfm_fm.ipp 側の out-of-line テンプレート実装であり、ymfm本体の
//   .cpp群 (同ipp を include している) の外からは implicit instantiation が
//   安定して行われずリンクエラーになる (opl_registers_base<Revision> 系の
//   非templateヘルパーがコンパイラの最適化都合で該当TUに現れないケースがある)。
//   そのため本クラスは ym3812 を「継承」せず「合成」し、ym3812の公開API
//   (write_address/write_data/write/read_status/read/generate) のみを経由して
//   操作する。これにより ymfm 内部実装の詳細に依存しない安全な実装になる。
//
#include "ymfm.h"
#include "ymfm_opl.h"
#include "ymfm_adpcm.h"

namespace ymfm {

class y8960opl2ex {
public:
	using output_data = ym3812::output_data;
	static constexpr uint32_t OUTPUTS = ym3812::OUTPUTS;

	static constexpr uint8_t STATUS_ADPCM_B_EOS     = 0x10;
	static constexpr uint8_t STATUS_ADPCM_B_BRDY    = 0x08;
	static constexpr uint8_t STATUS_ADPCM_B_PLAYING = 0x01;

	y8960opl2ex(ymfm_interface &intf);

	// pass-through helper (FmChip 層から呼ばれる)
	uint32_t sample_rate(uint32_t input_clock) const { return m_opl.sample_rate(input_clock); }

	// reset
	void reset();

	// save/restore
	void save_restore(ymfm_saved_state &state);

	// read access
	uint8_t read_status();
	uint8_t read_data();
	uint8_t read(uint32_t offset);

	// write access
	void write_address(uint8_t data);
	void write_data(uint8_t data);
	void write(uint32_t offset, uint8_t data);

	// generate one sample of sound
	void generate(output_data *output, uint32_t numsamples = 1);

protected:
	// internal state
	uint8_t m_address;                  // アドレスレジスタ (ADPCM側の判定に使用)
	uint8_t m_io_ddr;                   // I/O方向レジスタ (0x18, 廃止扱いだが保持)
	ym3812 m_opl;                       // OPL2本体 (公開APIのみ経由で使用)
	adpcm_b_engine m_adpcm_b;           // ADPCM-B engine
	ymfm_interface &m_intf;             // 外部I/Oアクセス用 (keyboard I/O等)
};

}
