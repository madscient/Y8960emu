#include "opl2ex.h"

namespace ymfm {

y8960opl2ex::y8960opl2ex(ymfm_interface &intf)
	: ym3812(intf)
{
}

void y8960opl2ex::reset()
{
	ym3812::reset();
}

void y8960opl2ex::save_restore(ymfm_saved_state &state)
{
	ym3812::save_restore(state);
}

uint8_t y8960opl2ex::read_data()
{
	return ym3812::read_data();
}

uint8_t y8960opl2ex::read(uint32_t offset)
{
	return ym3812::read(offset);
}

void y8960opl2ex::write_address(uint8_t data)
{
	ym3812::write_address(data);
}

void y8960opl2ex::write_data(uint8_t data)
{
	if (0x07 <= m_address && m_address <= 0x12) {
		switch (m_address) {
			case 0x07:
				m_adpcm_b.write(0, data & 0xf1);
				break;
			case 0x08:
				m_adpcm_b.write(1, data & 0x00);
				break;
			default:
				m_adpcm_b.write(m_address - 0x07, data);
				break;
		}
	}
	ym3812::write_data(data);
}

void y8960opl2ex::write(uint32_t offset, uint8_t data)
{
	ym3812::write(offset, data);
}

void y8960opl2ex::generate(output_data *output, uint32_t numsamples)
{
	ym3812::generate(output, numsamples);
}

}