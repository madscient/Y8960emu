//
// dual_ssg.cpp
//
#include "dual_ssg.h"

namespace ymfm {

y8960ssg::y8960ssg(ymfm_interface &intf)
	: m_address(0)
	, m_ssg1(intf)
	, m_ssg2(intf)
{
}

void y8960ssg::reset()
{
	m_ssg1.reset();
	m_ssg2.reset();
}

void y8960ssg::save_restore(ymfm_saved_state &state)
{
	state.save_restore(m_address);
	m_ssg1.save_restore(state);
	m_ssg2.save_restore(state);
}

uint8_t y8960ssg::read_data()
{
	if (m_address > 0x1f)
		return m_ssg2.read(m_address - 0x20);
	return m_ssg1.read(m_address);
}

uint8_t y8960ssg::read(uint32_t offset)
{
	// offset: 0=address port(未定義, 0xffを返す), 1=data port
	if ((offset & 1) == 1)
		return read_data();
	return 0xff;
}

void y8960ssg::write_address(uint8_t data)
{
	m_address = data;
}

void y8960ssg::write_data(uint8_t data)
{
	if (m_address > 0x1f)
		m_ssg2.write(m_address - 0x20, data);
	else
		m_ssg1.write(m_address, data);
}

void y8960ssg::write(uint32_t offset, uint8_t data)
{
	// 修正: 元コードは `offset & 1 == 0` (== が & より優先されるため常に false 側)
	// になっていた演算子優先順位バグを修正。
	if ((offset & 1) == 0)
		write_address(data);
	else
		write_data(data);
}

void y8960ssg::generate(output_data *output, uint32_t numsamples)
{
	for (uint32_t samp = 0; samp < numsamples; samp++, output++)
	{
		ssg_engine::output_data out1, out2;

		m_ssg1.clock();
		m_ssg1.output(out1.clear());

		m_ssg2.clock();
		m_ssg2.output(out2.clear());

		// SSG1 -> data[0..2], SSG2 -> data[3..5]
		for (uint32_t i = 0; i < ssg_engine::OUTPUTS; i++)
		{
			output->data[i]                         = out1.data[i];
			output->data[i + ssg_engine::OUTPUTS]    = out2.data[i];
		}
	}
}

}
