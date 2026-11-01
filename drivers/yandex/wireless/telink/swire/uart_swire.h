#include "i_swire.h"

#include <smart_devices/libs/tty/tty.h>

namespace telink {

    class UartSwire: public ISwire {
    public:
        UartSwire(const std::string& port);

        uint32_t getClockRate() const override;

        void write(uint8_t id, uint32_t addr, std::span<const uint8_t> data) override;
        void read(uint8_t id, uint32_t addr, std::span<uint8_t> data) override;

    private:
        void transmit(const std::vector<uint16_t>& data);

    private:
        std::unique_ptr<quasar::Tty> tty_;
    };

} // namespace telink
