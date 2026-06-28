#include "ymfm.h"
#include "ymfm_opl.h"

namespace ymfm {

class opllex_registers : public opll_registers {
public:
    opllex_registers();
    // OPLL-specific constants
    static constexpr uint32_t INSTDATA_SIZE = 0x1f8;
    // handle writes to the register array
    bool write(uint16_t index, uint8_t data, uint32_t &chan, uint32_t &opmask);
    uint32_t ch_instrument(uint32_t choffs) const    { return byte(0x30, 4, 4, choffs); }
private:
	// return a bitfield extracted from a byte
	uint32_t byte(uint32_t offset, uint32_t start, uint32_t count, uint32_t extra_offset = 0) const
	{
		return bitfield(m_regdata[offset + extra_offset], start, count);
	}
	uint8_t m_instdata[INSTDATA_SIZE];    // instrument data
   
};

class y8960opllex : public opll_base {
public:
    using fm_engine = fm_engine_base<opllex_registers>;
    y8960opllex(ymfm_interface &intf);
protected:
    // internal state
    static uint8_t const s_default_instruments[];
};

}
