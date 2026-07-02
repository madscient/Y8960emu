//
// opl2ex.cpp
//
#include "opl2ex.h"

namespace ymfm {

y8960opl2ex::y8960opl2ex(ymfm_interface &intf)
	: m_address(0)
	, m_io_ddr(0)
	, m_opl(intf)
	, m_adpcm_b(intf)
	, m_intf(intf)
{
}

void y8960opl2ex::reset()
{
	m_opl.reset();
	m_adpcm_b.reset();
}

void y8960opl2ex::save_restore(ymfm_saved_state &state)
{
	state.save_restore(m_address);
	state.save_restore(m_io_ddr);
	m_opl.save_restore(state);
	m_adpcm_b.save_restore(state);
}

uint8_t y8960opl2ex::read_status()
{
	// OPL2本体のステータス (m_opl.read_status() は内部で |0x06 済み) に
	// ADPCM-Bのライブ状態を重ねる。
	uint8_t status = m_opl.read_status();

	uint8_t adpcm_status = m_adpcm_b.status();
	if ((adpcm_status & adpcm_b_channel::STATUS_EOS) != 0)
		status |= STATUS_ADPCM_B_EOS;
	if ((adpcm_status & adpcm_b_channel::STATUS_BRDY) != 0)
		status |= STATUS_ADPCM_B_BRDY;
	if ((adpcm_status & adpcm_b_channel::STATUS_PLAYING) != 0)
		status |= STATUS_ADPCM_B_PLAYING;

	return status;
}

uint8_t y8960opl2ex::read_data()
{
	uint8_t result = 0xff;
	switch (m_address)
	{
		case 0x0f:  // ADPCM-DATA
		case 0x1a:  // PCM-DATA (廃止, 互換のため 0x0f と同じ扱い)
			result = m_adpcm_b.read(m_address - 0x07);
			break;

		case 0x19:  // I/O DATA (廃止)
			result = m_intf.ymfm_external_read(ACCESS_IO, 0);
			break;

		default:
			break;
	}
	return result;
}

uint8_t y8960opl2ex::read(uint32_t offset)
{
	switch (offset & 1)
	{
		case 0: return read_status();
		case 1: return read_data();
	}
	return 0xff;
}

void y8960opl2ex::write_address(uint8_t data)
{
	m_address = data;
	// OPL2本体側のアドレスレジスタも同期させておく
	// (ADPCM以外のレジスタは最終的に m_opl.write_data() へ委譲するため)
	m_opl.write_address(data);
}

void y8960opl2ex::write_data(uint8_t data)
{
	switch (m_address)
	{
		case 0x04:  // FLAG CONTROL (IRQ制御) -> OPL2本体へ、ステータス再評価
			m_opl.write_data(data);
			read_status();
			break;

		case 0x08:  // b7:CSM b6:NSEL(FM側) / b3-b0:ADPCM-B側 に分割
			m_adpcm_b.write(m_address - 0x07, (data & 0x0f) | 0x80);
			m_opl.write_data(data & 0xc0);
			break;

		case 0x07:
		case 0x09:
		case 0x0a:
		case 0x0b:
		case 0x0c:
		case 0x0d:
		case 0x0e:
		case 0x0f:
		case 0x10:
		case 0x11:
		case 0x12:
		case 0x15:
		case 0x16:
		case 0x17:
			m_adpcm_b.write(m_address - 0x07, data);
			break;

		case 0x18:  // I/O方向レジスタ (廃止扱い)
			m_io_ddr = data & 0x0f;
			break;

		case 0x19:  // I/O DATA (廃止)
			m_intf.ymfm_external_write(ACCESS_IO, 0, data & m_io_ddr);
			break;

		default:    // それ以外はOPL2本体
			m_opl.write_data(data);
			break;
	}
}

void y8960opl2ex::write(uint32_t offset, uint8_t data)
{
	switch (offset & 1)
	{
		case 0: write_address(data); break;
		case 1: write_data(data);    break;
	}
}

void y8960opl2ex::generate(output_data *output, uint32_t numsamples)
{
	for (uint32_t samp = 0; samp < numsamples; samp++, output++)
	{
		output_data opl_out;
		opl_out.clear();
		m_opl.generate(&opl_out, 1);

		m_adpcm_b.clock();

		*output = opl_out;
		m_adpcm_b.output(*output, 3);
		output->clamp16();
	}
}

}
