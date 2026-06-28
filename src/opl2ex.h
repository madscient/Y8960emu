#include "ymfm.h"
#include "ymfm_opl.h"
#include "ymfm_pcm.h"

namespace ymfm {

class y8960opl2ex : public ym3812 {
    public:
	static constexpr uint8_t STATUS_ADPCM_B_EOS = 0x04;
	static constexpr uint8_t STATUS_ADPCM_B_BRDY = 0x08;
	static constexpr uint8_t STATUS_ADPCM_B_ZERO = 0x10;
	static constexpr uint8_t STATUS_ADPCM_B_PLAYING = 0x20;
    y8960opl2ex(ymfm_interface &intf);
    void reset();
    void save_restore(ymfm_saved_state &state);
    uint8_t read_data();
    uint8_t read(uint32_t offset);
    void write_address(uint8_t data);
    void write_data(uint8_t data);
    void write(uint32_t offset, uint8_t data);
    void generate(output_data *output, uint32_t numsamples = 1);
protected:
    // internal state
	adpcm_b_engine m_adpcm_b;           // ADPCM-B engine
    ym3812 m_opl;
};


}