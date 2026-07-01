#include "dual_ssg.h"

namespace ymfm {

y8960ssg::y8960ssg(ymfm_interface &intf)
	: m_ssg1(intf), m_ssg2(intf), m_ssg_resampler()
{
}

void y8960ssg::reset()
{
	m_ssg1.reset();
	m_ssg2.reset();
	m_ssg_resampler.reset();
}

void y8960ssg::save_restore(ymfm_saved_state &state)
{
	m_ssg1.save_restore(state);
	m_ssg2.save_restore(state);
	m_ssg_resampler.save_restore(state);
}

uint8_t y8960ssg::read_data()
{
    if (m_address > 0x1f) {
	    return m_ssg2.read_data();
    }
    return m_ssg1.read_data();
}

uint8_t y8960ssg::read(uint32_t offset)
{
    if (offset > 0x1f) {
        return m_ssg2.read(offset - 0x20);
    }
	return m_ssg1.read(offset);
}

void y8960ssg::write_address(uint8_t data)
{
	m_address = data;
}

void y8960ssg::write_data(uint8_t data)
{
    if (m_address > 0x1f) {
        m_ssg2.write(m_address - 0x20, data);
    } else {
        m_ssg1.write(m_address, data);
    }
}

void y8960ssg::write(uint32_t offset, uint8_t data)
{
    if (offset & 1 == 0) {
        write_address(data);
    } else {
        write_data(data);
    }
}

void y8960ssg::generate(output_data *output, uint32_t numsamples)
{
	m_ssg1.generate(output, numsamples);
	m_ssg2.generate(output, numsamples);
}

}