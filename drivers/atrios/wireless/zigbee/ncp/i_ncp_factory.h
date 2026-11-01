#pragma once

#include "i_ncp.h"

namespace zigbee {

    class INcpFactory {
    public:
        virtual ~INcpFactory();

        virtual std::unique_ptr<INcp> createNcp() const = 0;
    };

} // namespace zigbee
