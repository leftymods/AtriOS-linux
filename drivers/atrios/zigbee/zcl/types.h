#pragma once

#include "generated/types.h"

namespace zigbee::zcl {

    // DoorLock (0x0101)
    using DrlkPINUserID = Uint16;
    using DrlkRFIDUserID = Uint16;
    using DrlkTotalUserID = Uint16;
    using DrlkWeekDayScheduleID = Uint8;
    using DrlkYearDayScheduleID = Uint8;
    using DrlkHolidayScheduleID = Uint8;

    // ColorControl (0x0300)
    using CCMinMiredsField = Uint16;
    using CCMaxMiredsField = Uint16;

} // namespace zigbee::zcl
