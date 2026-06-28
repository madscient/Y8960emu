#pragma once

#include "ymfm.h"
#include "ymfm_ssg.h"

namespace ymfm {

class y8960ssg
{
public:
	static constexpr uint32_t SSG_OUTPUTS = ssg_engine::OUTPUTS * 2;
	static constexpr uint32_t OUTPUTS = SSG_OUTPUTS;
	using output_data = ymfm_output<OUTPUTS>;

	// constructor
	y8960ssg(ymfm_interface &intf);

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

	// generate one sample of sound
	void generate(output_data *output, uint32_t numsamples = 1);

protected:
	// internal helpers
	uint8_t m_address;                  // address register

	// internal state
	ssg_engine m_ssg1;                   // SSG engine
	ssg_engine m_ssg2;                   // SSG engine
	ssg_resampler<output_data, 1, false> m_ssg_resampler; // SSG resampler helper
};

}