#include "clusters.h"

using namespace zigbee;
using namespace zigbee::zcl;

BasicServerCluster::BasicServerCluster(std::weak_ptr<Endpoint> endpoint)
    : GenericCluster(endpoint, Id, ZclDirection::CLIENT_TO_SERVER)
{
}

void BasicServerCluster::resetToFactoryDefaults(
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);


    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::ResetToFactoryDefaults), buffer);
}


OnOffServerCluster::OnOffServerCluster(std::weak_ptr<Endpoint> endpoint)
    : GenericCluster(endpoint, Id, ZclDirection::CLIENT_TO_SERVER)
{
}

void OnOffServerCluster::off(
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);


    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::Off), buffer);
}

void OnOffServerCluster::on(
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);


    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::On), buffer);
}

void OnOffServerCluster::toggle(
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);


    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::Toggle), buffer);
}

void OnOffServerCluster::offWithEffect(
    OffWithEffectEffectIdentifier effectIdentifier,
    Uint8 effectVariant
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<OffWithEffectEffectIdentifier>(effectIdentifier);
    stream.write<Uint8>(effectVariant);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::OffWithEffect), buffer);
}

void OnOffServerCluster::onWithRecallGlobalScene(
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);


    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::OnWithRecallGlobalScene), buffer);
}

void OnOffServerCluster::onWithTimedOff(
    OnWithTimedOffOnOffControl onOffControl,
    Uint16 onTime,
    Uint16 offWaitTime
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<OnWithTimedOffOnOffControl>(onOffControl);
    stream.write<Uint16>(onTime);
    stream.write<Uint16>(offWaitTime);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::OnWithTimedOff), buffer);
}


LevelServerCluster::LevelServerCluster(std::weak_ptr<Endpoint> endpoint)
    : GenericCluster(endpoint, Id, ZclDirection::CLIENT_TO_SERVER)
{
}

void LevelServerCluster::moveToLevel(
    Uint8 level,
    Uint16 transitionTime,
    LevelOptions optionsMask,
    LevelOptions optionsOverride
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<Uint8>(level);
    stream.write<Uint16>(transitionTime);
    stream.write<LevelOptions>(optionsMask);
    stream.write<LevelOptions>(optionsOverride);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::MoveToLevel), buffer);
}

void LevelServerCluster::move(
    MoveStepMode moveMode,
    Uint8 rate,
    LevelOptions optionsMask,
    LevelOptions optionsOverride
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<MoveStepMode>(moveMode);
    stream.write<Uint8>(rate);
    stream.write<LevelOptions>(optionsMask);
    stream.write<LevelOptions>(optionsOverride);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::Move), buffer);
}

void LevelServerCluster::step(
    MoveStepMode stepMode,
    Uint8 stepSize,
    Uint16 transitionTime,
    LevelOptions optionsMask,
    LevelOptions optionsOverride
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<MoveStepMode>(stepMode);
    stream.write<Uint8>(stepSize);
    stream.write<Uint16>(transitionTime);
    stream.write<LevelOptions>(optionsMask);
    stream.write<LevelOptions>(optionsOverride);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::Step), buffer);
}

void LevelServerCluster::stop(
    LevelOptions optionsMask,
    LevelOptions optionsOverride
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<LevelOptions>(optionsMask);
    stream.write<LevelOptions>(optionsOverride);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::Stop), buffer);
}

void LevelServerCluster::moveToLevelWithOnOff(
    Uint8 level,
    Uint16 transitionTime,
    LevelOptions optionsMask,
    LevelOptions optionsOverride
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<Uint8>(level);
    stream.write<Uint16>(transitionTime);
    stream.write<LevelOptions>(optionsMask);
    stream.write<LevelOptions>(optionsOverride);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::MoveToLevelWithOnOff), buffer);
}

void LevelServerCluster::moveWithOnOff(
    MoveStepMode moveMode,
    Uint8 rate,
    LevelOptions optionsMask,
    LevelOptions optionsOverride
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<MoveStepMode>(moveMode);
    stream.write<Uint8>(rate);
    stream.write<LevelOptions>(optionsMask);
    stream.write<LevelOptions>(optionsOverride);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::MoveWithOnOff), buffer);
}

void LevelServerCluster::stepWithOnOff(
    MoveStepMode stepMode,
    Uint8 stepSize,
    Uint16 transitionTime,
    LevelOptions optionsMask,
    LevelOptions optionsOverride
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<MoveStepMode>(stepMode);
    stream.write<Uint8>(stepSize);
    stream.write<Uint16>(transitionTime);
    stream.write<LevelOptions>(optionsMask);
    stream.write<LevelOptions>(optionsOverride);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::StepWithOnOff), buffer);
}

void LevelServerCluster::stopWithOnOff(
    LevelOptions optionsMask,
    LevelOptions optionsOverride
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<LevelOptions>(optionsMask);
    stream.write<LevelOptions>(optionsOverride);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::StopWithOnOff), buffer);
}

void LevelServerCluster::moveToClosestFrequency(
    Uint16 frequency
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<Uint16>(frequency);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::MoveToClosestFrequency), buffer);
}


AnalogInputServerCluster::AnalogInputServerCluster(std::weak_ptr<Endpoint> endpoint)
    : GenericCluster(endpoint, Id, ZclDirection::CLIENT_TO_SERVER)
{
}


AnalogOutputServerCluster::AnalogOutputServerCluster(std::weak_ptr<Endpoint> endpoint)
    : GenericCluster(endpoint, Id, ZclDirection::CLIENT_TO_SERVER)
{
}


MultistateInputServerCluster::MultistateInputServerCluster(std::weak_ptr<Endpoint> endpoint)
    : GenericCluster(endpoint, Id, ZclDirection::CLIENT_TO_SERVER)
{
}


MultistateOutputServerCluster::MultistateOutputServerCluster(std::weak_ptr<Endpoint> endpoint)
    : GenericCluster(endpoint, Id, ZclDirection::CLIENT_TO_SERVER)
{
}


DoorLockServerCluster::DoorLockServerCluster(std::weak_ptr<Endpoint> endpoint)
    : GenericCluster(endpoint, Id, ZclDirection::CLIENT_TO_SERVER)
{
}

void DoorLockServerCluster::lockDoor(
    Octstr pINOrRFIDCode
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<Octstr>(pINOrRFIDCode);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::LockDoor), buffer);
}

void DoorLockServerCluster::unlockDoor(
    Octstr pINOrRFIDCode
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<Octstr>(pINOrRFIDCode);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::UnlockDoor), buffer);
}

void DoorLockServerCluster::toggle(
    Octstr pINOrRFIDCode
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<Octstr>(pINOrRFIDCode);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::Toggle), buffer);
}

void DoorLockServerCluster::unlockWithTimeout(
    Uint16 timeoutInSeconds,
    Octstr pINOrRFIDCode
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<Uint16>(timeoutInSeconds);
    stream.write<Octstr>(pINOrRFIDCode);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::UnlockWithTimeout), buffer);
}

void DoorLockServerCluster::getLogRecord(
    Uint16 logIndex
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<Uint16>(logIndex);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::GetLogRecord), buffer);
}

void DoorLockServerCluster::setPINCode(
    DrlkPINUserID userID,
    DrlkSettableUserStatus userStatus,
    DrlkUserType userType,
    Octstr pin
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<DrlkPINUserID>(userID);
    stream.write<DrlkSettableUserStatus>(userStatus);
    stream.write<DrlkUserType>(userType);
    stream.write<Octstr>(pin);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::SetPINCode), buffer);
}

void DoorLockServerCluster::getPINCode(
    DrlkPINUserID userID
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<DrlkPINUserID>(userID);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::GetPINCode), buffer);
}

void DoorLockServerCluster::clearPINCode(
    DrlkPINUserID userID
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<DrlkPINUserID>(userID);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::ClearPINCode), buffer);
}

void DoorLockServerCluster::clearAllPINCodes(
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);


    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::ClearAllPINCodes), buffer);
}

void DoorLockServerCluster::setUserStatus(
    DrlkTotalUserID userID,
    DrlkSettableUserStatus userStatus
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<DrlkTotalUserID>(userID);
    stream.write<DrlkSettableUserStatus>(userStatus);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::SetUserStatus), buffer);
}

void DoorLockServerCluster::getUserStatus(
    DrlkTotalUserID userID
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<DrlkTotalUserID>(userID);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::GetUserStatus), buffer);
}

void DoorLockServerCluster::setWeekdaySchedule(
    DrlkWeekDayScheduleID scheduleID,
    DrlkTotalUserID userID,
    DrlkDaysMask daysMask,
    Uint8 startHour,
    Uint8 startMinute,
    Uint8 endHour,
    Uint8 endMinute
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<DrlkWeekDayScheduleID>(scheduleID);
    stream.write<DrlkTotalUserID>(userID);
    stream.write<DrlkDaysMask>(daysMask);
    stream.write<Uint8>(startHour);
    stream.write<Uint8>(startMinute);
    stream.write<Uint8>(endHour);
    stream.write<Uint8>(endMinute);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::SetWeekdaySchedule), buffer);
}

void DoorLockServerCluster::getWeekdaySchedule(
    DrlkWeekDayScheduleID scheduleID,
    DrlkTotalUserID userID
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<DrlkWeekDayScheduleID>(scheduleID);
    stream.write<DrlkTotalUserID>(userID);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::GetWeekdaySchedule), buffer);
}

void DoorLockServerCluster::clearWeekdaySchedule(
    DrlkWeekDayScheduleID scheduleID,
    DrlkTotalUserID userID
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<DrlkWeekDayScheduleID>(scheduleID);
    stream.write<DrlkTotalUserID>(userID);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::ClearWeekdaySchedule), buffer);
}

void DoorLockServerCluster::setYearDaySchedule(
    DrlkYearDayScheduleID scheduleID,
    DrlkTotalUserID userID,
    Uint32 localStartTime,
    Uint32 localEndTime
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<DrlkYearDayScheduleID>(scheduleID);
    stream.write<DrlkTotalUserID>(userID);
    stream.write<Uint32>(localStartTime);
    stream.write<Uint32>(localEndTime);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::SetYearDaySchedule), buffer);
}

void DoorLockServerCluster::getYearDaySchedule(
    DrlkYearDayScheduleID scheduleID,
    DrlkTotalUserID userID
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<DrlkYearDayScheduleID>(scheduleID);
    stream.write<DrlkTotalUserID>(userID);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::GetYearDaySchedule), buffer);
}

void DoorLockServerCluster::clearYearDaySchedule(
    DrlkYearDayScheduleID scheduleID,
    DrlkTotalUserID userID
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<DrlkYearDayScheduleID>(scheduleID);
    stream.write<DrlkTotalUserID>(userID);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::ClearYearDaySchedule), buffer);
}

void DoorLockServerCluster::setHolidaySchedule(
    DrlkHolidayScheduleID holidayScheduleID,
    Uint32 localStartTime,
    Uint32 localEndTime,
    DrlkOperMode operatingModeDuringHoliday
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<DrlkHolidayScheduleID>(holidayScheduleID);
    stream.write<Uint32>(localStartTime);
    stream.write<Uint32>(localEndTime);
    stream.write<DrlkOperMode>(operatingModeDuringHoliday);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::SetHolidaySchedule), buffer);
}

void DoorLockServerCluster::getHolidaySchedule(
    DrlkHolidayScheduleID holidayScheduleID
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<DrlkHolidayScheduleID>(holidayScheduleID);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::GetHolidaySchedule), buffer);
}

void DoorLockServerCluster::clearHolidaySchedule(
    DrlkHolidayScheduleID holidayScheduleID
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<DrlkHolidayScheduleID>(holidayScheduleID);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::ClearHolidaySchedule), buffer);
}

void DoorLockServerCluster::setUserType(
    DrlkTotalUserID userID,
    DrlkUserType userType
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<DrlkTotalUserID>(userID);
    stream.write<DrlkUserType>(userType);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::SetUserType), buffer);
}

void DoorLockServerCluster::getUserType(
    DrlkTotalUserID userID
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<DrlkTotalUserID>(userID);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::GetUserType), buffer);
}

void DoorLockServerCluster::setRFIDCode(
    DrlkRFIDUserID userID,
    DrlkSettableUserStatus userStatus,
    DrlkUserType userType,
    Octstr rFIDCode
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<DrlkRFIDUserID>(userID);
    stream.write<DrlkSettableUserStatus>(userStatus);
    stream.write<DrlkUserType>(userType);
    stream.write<Octstr>(rFIDCode);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::SetRFIDCode), buffer);
}

void DoorLockServerCluster::getRFIDCode(
    DrlkRFIDUserID userID
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<DrlkRFIDUserID>(userID);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::GetRFIDCode), buffer);
}

void DoorLockServerCluster::clearRFIDCode(
    DrlkRFIDUserID userID
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<DrlkRFIDUserID>(userID);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::ClearRFIDCode), buffer);
}

void DoorLockServerCluster::clearAllRFIDCodes(
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);


    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::ClearAllRFIDCodes), buffer);
}

void DoorLockServerCluster::handleClusterSpecificCommand(const ZclHeader& header, ZclStream& stream) {
    switch (static_cast<ClientCommandId>(header.commandId)) {
        case ClientCommandId::LockDoorResponse: {
            ZclStatus status;

            status = stream.read<ZclStatus>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onLockDoorResponse(
                        status
                    );
                }
            }

            break;
        }
        case ClientCommandId::UnlockDoorResponse: {
            ZclStatus status;

            status = stream.read<ZclStatus>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onUnlockDoorResponse(
                        status
                    );
                }
            }

            break;
        }
        case ClientCommandId::ToggleResponse: {
            ZclStatus status;

            status = stream.read<ZclStatus>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onToggleResponse(
                        status
                    );
                }
            }

            break;
        }
        case ClientCommandId::UnlockWithTimeoutResponse: {
            ZclStatus status;

            status = stream.read<ZclStatus>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onUnlockWithTimeoutResponse(
                        status
                    );
                }
            }

            break;
        }
        case ClientCommandId::GetLogRecordResponse: {
            Uint16 logEntryID;
            Uint32 timestamp;
            GetLogRecordResponseEventType eventType;
            DrlkOperEventSource sourceOperationEvent;
            Uint8 eventIDOrAlarmCode;
            Uint16 userID;
            Octstr pin;

            logEntryID = stream.read<Uint16>();
            timestamp = stream.read<Uint32>();
            eventType = stream.read<GetLogRecordResponseEventType>();
            sourceOperationEvent = stream.read<DrlkOperEventSource>();
            eventIDOrAlarmCode = stream.read<Uint8>();
            userID = stream.read<Uint16>();
            pin = stream.read<Octstr>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onGetLogRecordResponse(
                        logEntryID,
                        timestamp,
                        eventType,
                        sourceOperationEvent,
                        eventIDOrAlarmCode,
                        userID,
                        pin
                    );
                }
            }

            break;
        }
        case ClientCommandId::SetPINCodeResponse: {
            DrlkSetCodeStatus status;

            status = stream.read<DrlkSetCodeStatus>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onSetPINCodeResponse(
                        status
                    );
                }
            }

            break;
        }
        case ClientCommandId::GetPINCodeResponse: {
            Uint16 userID;
            DrlkUserStatus userStatus;
            DrlkUserType userType;
            Octstr code;

            userID = stream.read<Uint16>();
            userStatus = stream.read<DrlkUserStatus>();
            userType = stream.read<DrlkUserType>();
            code = stream.read<Octstr>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onGetPINCodeResponse(
                        userID,
                        userStatus,
                        userType,
                        code
                    );
                }
            }

            break;
        }
        case ClientCommandId::ClearPINCodeResponse: {
            DrlkPassFailStatus status;

            status = stream.read<DrlkPassFailStatus>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onClearPINCodeResponse(
                        status
                    );
                }
            }

            break;
        }
        case ClientCommandId::ClearAllPINCodesResponse: {
            DrlkPassFailStatus status;

            status = stream.read<DrlkPassFailStatus>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onClearAllPINCodesResponse(
                        status
                    );
                }
            }

            break;
        }
        case ClientCommandId::SetUserStatusResponse: {
            DrlkPassFailStatus status;

            status = stream.read<DrlkPassFailStatus>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onSetUserStatusResponse(
                        status
                    );
                }
            }

            break;
        }
        case ClientCommandId::GetUserStatusResponse: {
            Uint16 userID;
            DrlkUserStatus userStatus;

            userID = stream.read<Uint16>();
            userStatus = stream.read<DrlkUserStatus>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onGetUserStatusResponse(
                        userID,
                        userStatus
                    );
                }
            }

            break;
        }
        case ClientCommandId::SetWeekdayScheduleResponse: {
            DrlkPassFailStatus status;

            status = stream.read<DrlkPassFailStatus>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onSetWeekdayScheduleResponse(
                        status
                    );
                }
            }

            break;
        }
        case ClientCommandId::GetWeekdayScheduleResponse: {
            Uint8 scheduleID;
            Uint16 userID;
            ZclStatus status;
            DrlkDaysMask daysMask;
            Uint8 startHour;
            Uint8 startMinute;
            Uint8 endHour;
            Uint8 endMinute;

            scheduleID = stream.read<Uint8>();
            userID = stream.read<Uint16>();
            status = stream.read<ZclStatus>();
            daysMask = stream.read<DrlkDaysMask>();
            startHour = stream.read<Uint8>();
            startMinute = stream.read<Uint8>();
            endHour = stream.read<Uint8>();
            endMinute = stream.read<Uint8>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onGetWeekdayScheduleResponse(
                        scheduleID,
                        userID,
                        status,
                        daysMask,
                        startHour,
                        startMinute,
                        endHour,
                        endMinute
                    );
                }
            }

            break;
        }
        case ClientCommandId::ClearWeekdayScheduleResponse: {
            DrlkPassFailStatus status;

            status = stream.read<DrlkPassFailStatus>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onClearWeekdayScheduleResponse(
                        status
                    );
                }
            }

            break;
        }
        case ClientCommandId::SetYearDayScheduleResponse: {
            DrlkPassFailStatus status;

            status = stream.read<DrlkPassFailStatus>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onSetYearDayScheduleResponse(
                        status
                    );
                }
            }

            break;
        }
        case ClientCommandId::GetYearDayScheduleResponse: {
            Uint8 scheduleID;
            Uint16 userID;
            ZclStatus status;
            Uint32 localStartTime;
            Uint32 localEndTime;

            scheduleID = stream.read<Uint8>();
            userID = stream.read<Uint16>();
            status = stream.read<ZclStatus>();
            localStartTime = stream.read<Uint32>();
            localEndTime = stream.read<Uint32>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onGetYearDayScheduleResponse(
                        scheduleID,
                        userID,
                        status,
                        localStartTime,
                        localEndTime
                    );
                }
            }

            break;
        }
        case ClientCommandId::ClearYearDayScheduleResponse: {
            DrlkPassFailStatus status;

            status = stream.read<DrlkPassFailStatus>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onClearYearDayScheduleResponse(
                        status
                    );
                }
            }

            break;
        }
        case ClientCommandId::SetHolidayScheduleResponse: {
            DrlkPassFailStatus status;

            status = stream.read<DrlkPassFailStatus>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onSetHolidayScheduleResponse(
                        status
                    );
                }
            }

            break;
        }
        case ClientCommandId::GetHolidayScheduleResponse: {
            Uint8 holidayScheduleID;
            ZclStatus status;
            Uint32 localStartTime;
            Uint32 localEndTime;
            DrlkOperMode operatingModeDuringHoliday;

            holidayScheduleID = stream.read<Uint8>();
            status = stream.read<ZclStatus>();
            localStartTime = stream.read<Uint32>();
            localEndTime = stream.read<Uint32>();
            operatingModeDuringHoliday = stream.read<DrlkOperMode>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onGetHolidayScheduleResponse(
                        holidayScheduleID,
                        status,
                        localStartTime,
                        localEndTime,
                        operatingModeDuringHoliday
                    );
                }
            }

            break;
        }
        case ClientCommandId::ClearHolidayScheduleResponse: {
            DrlkPassFailStatus status;

            status = stream.read<DrlkPassFailStatus>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onClearHolidayScheduleResponse(
                        status
                    );
                }
            }

            break;
        }
        case ClientCommandId::SetUserTypeResponse: {
            DrlkPassFailStatus status;

            status = stream.read<DrlkPassFailStatus>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onSetUserTypeResponse(
                        status
                    );
                }
            }

            break;
        }
        case ClientCommandId::GetUserTypeResponse: {
            Uint16 userID;
            DrlkUserType userType;

            userID = stream.read<Uint16>();
            userType = stream.read<DrlkUserType>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onGetUserTypeResponse(
                        userID,
                        userType
                    );
                }
            }

            break;
        }
        case ClientCommandId::SetRFIDCodeResponse: {
            DrlkSetCodeStatus status;

            status = stream.read<DrlkSetCodeStatus>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onSetRFIDCodeResponse(
                        status
                    );
                }
            }

            break;
        }
        case ClientCommandId::GetRFIDCodeResponse: {
            Uint16 userID;
            DrlkUserStatus userStatus;
            DrlkUserType userType;
            Octstr rFIDCode;

            userID = stream.read<Uint16>();
            userStatus = stream.read<DrlkUserStatus>();
            userType = stream.read<DrlkUserType>();
            rFIDCode = stream.read<Octstr>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onGetRFIDCodeResponse(
                        userID,
                        userStatus,
                        userType,
                        rFIDCode
                    );
                }
            }

            break;
        }
        case ClientCommandId::ClearRFIDCodeResponse: {
            DrlkPassFailStatus status;

            status = stream.read<DrlkPassFailStatus>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onClearRFIDCodeResponse(
                        status
                    );
                }
            }

            break;
        }
        case ClientCommandId::ClearAllRFIDCodesResponse: {
            DrlkPassFailStatus status;

            status = stream.read<DrlkPassFailStatus>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onClearAllRFIDCodesResponse(
                        status
                    );
                }
            }

            break;
        }
        case ClientCommandId::OperatingEventNotification: {
            DrlkOperEventSource operationEventSource;
            OperatingEventNotificationOperationEventCode operationEventCode;
            Uint16 userID;
            Octstr pin;
            Uint32 localTime;
            String data;

            operationEventSource = stream.read<DrlkOperEventSource>();
            operationEventCode = stream.read<OperatingEventNotificationOperationEventCode>();
            userID = stream.read<Uint16>();
            pin = stream.read<Octstr>();
            localTime = stream.read<Uint32>();
            data = stream.read<String>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onOperatingEventNotification(
                        operationEventSource,
                        operationEventCode,
                        userID,
                        pin,
                        localTime,
                        data
                    );
                }
            }

            break;
        }
        case ClientCommandId::ProgrammingEventNotification: {
            ProgrammingEventNotificationProgramEventSource programEventSource;
            ProgrammingEventNotificationProgramEventCode programEventCode;
            Uint16 userID;
            Octstr pin;
            DrlkUserType userType;
            DrlkUserStatus userStatus;
            Uint32 localTime;
            String data;

            programEventSource = stream.read<ProgrammingEventNotificationProgramEventSource>();
            programEventCode = stream.read<ProgrammingEventNotificationProgramEventCode>();
            userID = stream.read<Uint16>();
            pin = stream.read<Octstr>();
            userType = stream.read<DrlkUserType>();
            userStatus = stream.read<DrlkUserStatus>();
            localTime = stream.read<Uint32>();
            data = stream.read<String>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onProgrammingEventNotification(
                        programEventSource,
                        programEventCode,
                        userID,
                        pin,
                        userType,
                        userStatus,
                        localTime,
                        data
                    );
                }
            }

            break;
        }
        default:
            break;
    }
}

void DoorLockServerCluster::addClusterListener(std::weak_ptr<IListener> listener) {
    listeners_.push_back(listener);
}

DoorLockServerCluster::IListener::~IListener() = default;

ColorControlServerCluster::ColorControlServerCluster(std::weak_ptr<Endpoint> endpoint)
    : GenericCluster(endpoint, Id, ZclDirection::CLIENT_TO_SERVER)
{
}

void ColorControlServerCluster::moveToHue(
    Uint8 hue,
    CCDirection direction,
    Uint16 transitionTime,
    CCColorOptions optionsMask,
    CCColorOptions optionsOverride
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<Uint8>(hue);
    stream.write<CCDirection>(direction);
    stream.write<Uint16>(transitionTime);
    stream.write<CCColorOptions>(optionsMask);
    stream.write<CCColorOptions>(optionsOverride);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::MoveToHue), buffer);
}

void ColorControlServerCluster::moveHue(
    CCMoveMode moveMode,
    Uint8 rate,
    CCColorOptions optionsMask,
    CCColorOptions optionsOverride
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<CCMoveMode>(moveMode);
    stream.write<Uint8>(rate);
    stream.write<CCColorOptions>(optionsMask);
    stream.write<CCColorOptions>(optionsOverride);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::MoveHue), buffer);
}

void ColorControlServerCluster::stepHue(
    CCStepMode stepMode,
    Uint8 stepSize,
    Uint8 transitionTime,
    CCColorOptions optionsMask,
    CCColorOptions optionsOverride
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<CCStepMode>(stepMode);
    stream.write<Uint8>(stepSize);
    stream.write<Uint8>(transitionTime);
    stream.write<CCColorOptions>(optionsMask);
    stream.write<CCColorOptions>(optionsOverride);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::StepHue), buffer);
}

void ColorControlServerCluster::moveToSaturation(
    Uint8 saturation,
    Uint16 transitionTime,
    CCColorOptions optionsMask,
    CCColorOptions optionsOverride
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<Uint8>(saturation);
    stream.write<Uint16>(transitionTime);
    stream.write<CCColorOptions>(optionsMask);
    stream.write<CCColorOptions>(optionsOverride);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::MoveToSaturation), buffer);
}

void ColorControlServerCluster::moveSaturation(
    CCMoveMode moveMode,
    Uint8 rate,
    CCColorOptions optionsMask,
    CCColorOptions optionsOverride
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<CCMoveMode>(moveMode);
    stream.write<Uint8>(rate);
    stream.write<CCColorOptions>(optionsMask);
    stream.write<CCColorOptions>(optionsOverride);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::MoveSaturation), buffer);
}

void ColorControlServerCluster::stepSaturation(
    CCStepMode stepMode,
    Uint8 stepSize,
    Uint8 transitionTime,
    CCColorOptions optionsMask,
    CCColorOptions optionsOverride
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<CCStepMode>(stepMode);
    stream.write<Uint8>(stepSize);
    stream.write<Uint8>(transitionTime);
    stream.write<CCColorOptions>(optionsMask);
    stream.write<CCColorOptions>(optionsOverride);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::StepSaturation), buffer);
}

void ColorControlServerCluster::moveToHueAndSaturation(
    Uint8 hue,
    Uint8 saturation,
    Uint16 transitionTime,
    CCColorOptions optionsMask,
    CCColorOptions optionsOverride
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<Uint8>(hue);
    stream.write<Uint8>(saturation);
    stream.write<Uint16>(transitionTime);
    stream.write<CCColorOptions>(optionsMask);
    stream.write<CCColorOptions>(optionsOverride);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::MoveToHueAndSaturation), buffer);
}

void ColorControlServerCluster::moveToColor(
    Uint16 colorX,
    Uint16 colorY,
    Uint16 transitionTime,
    CCColorOptions optionsMask,
    CCColorOptions optionsOverride
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<Uint16>(colorX);
    stream.write<Uint16>(colorY);
    stream.write<Uint16>(transitionTime);
    stream.write<CCColorOptions>(optionsMask);
    stream.write<CCColorOptions>(optionsOverride);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::MoveToColor), buffer);
}

void ColorControlServerCluster::moveColor(
    Int16 rateX,
    Int16 rateY,
    CCColorOptions optionsMask,
    CCColorOptions optionsOverride
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<Int16>(rateX);
    stream.write<Int16>(rateY);
    stream.write<CCColorOptions>(optionsMask);
    stream.write<CCColorOptions>(optionsOverride);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::MoveColor), buffer);
}

void ColorControlServerCluster::stepColor(
    Int16 stepX,
    Int16 stepY,
    Uint16 transitionTime,
    CCColorOptions optionsMask,
    CCColorOptions optionsOverride
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<Int16>(stepX);
    stream.write<Int16>(stepY);
    stream.write<Uint16>(transitionTime);
    stream.write<CCColorOptions>(optionsMask);
    stream.write<CCColorOptions>(optionsOverride);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::StepColor), buffer);
}

void ColorControlServerCluster::moveToColorTemperature(
    Uint16 colorTemperatureMireds,
    Uint16 transitionTime,
    CCColorOptions optionsMask,
    CCColorOptions optionsOverride
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<Uint16>(colorTemperatureMireds);
    stream.write<Uint16>(transitionTime);
    stream.write<CCColorOptions>(optionsMask);
    stream.write<CCColorOptions>(optionsOverride);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::MoveToColorTemperature), buffer);
}

void ColorControlServerCluster::enhancedMoveToHue(
    Uint16 enhancedHue,
    CCDirection direction,
    Uint16 transitionTime,
    CCColorOptions optionsMask,
    CCColorOptions optionsOverride
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<Uint16>(enhancedHue);
    stream.write<CCDirection>(direction);
    stream.write<Uint16>(transitionTime);
    stream.write<CCColorOptions>(optionsMask);
    stream.write<CCColorOptions>(optionsOverride);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::EnhancedMoveToHue), buffer);
}

void ColorControlServerCluster::enhancedMoveHue(
    CCMoveMode moveMode,
    Uint16 rate,
    CCColorOptions optionsMask,
    CCColorOptions optionsOverride
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<CCMoveMode>(moveMode);
    stream.write<Uint16>(rate);
    stream.write<CCColorOptions>(optionsMask);
    stream.write<CCColorOptions>(optionsOverride);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::EnhancedMoveHue), buffer);
}

void ColorControlServerCluster::enhancedStepHue(
    CCStepMode stepMode,
    Uint16 stepSize,
    Uint16 transitionTime,
    CCColorOptions optionsMask,
    CCColorOptions optionsOverride
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<CCStepMode>(stepMode);
    stream.write<Uint16>(stepSize);
    stream.write<Uint16>(transitionTime);
    stream.write<CCColorOptions>(optionsMask);
    stream.write<CCColorOptions>(optionsOverride);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::EnhancedStepHue), buffer);
}

void ColorControlServerCluster::enhancedMoveToHueAndSaturation(
    Uint16 enhancedHue,
    Uint8 saturation,
    Uint16 transitionTime,
    CCColorOptions optionsMask,
    CCColorOptions optionsOverride
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<Uint16>(enhancedHue);
    stream.write<Uint8>(saturation);
    stream.write<Uint16>(transitionTime);
    stream.write<CCColorOptions>(optionsMask);
    stream.write<CCColorOptions>(optionsOverride);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::EnhancedMoveToHueAndSaturation), buffer);
}

void ColorControlServerCluster::colorLoopSet(
    ColorLoopSetUpdateFlags updateFlags,
    ColorLoopSetAction action,
    CCColorLoopDirection direction,
    Uint16 time,
    Uint16 startHue,
    CCColorOptions optionsMask,
    CCColorOptions optionsOverride
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<ColorLoopSetUpdateFlags>(updateFlags);
    stream.write<ColorLoopSetAction>(action);
    stream.write<CCColorLoopDirection>(direction);
    stream.write<Uint16>(time);
    stream.write<Uint16>(startHue);
    stream.write<CCColorOptions>(optionsMask);
    stream.write<CCColorOptions>(optionsOverride);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::ColorLoopSet), buffer);
}

void ColorControlServerCluster::stopMoveStep(
    CCColorOptions optionsMask,
    CCColorOptions optionsOverride
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<CCColorOptions>(optionsMask);
    stream.write<CCColorOptions>(optionsOverride);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::StopMoveStep), buffer);
}

void ColorControlServerCluster::moveColorTemperature(
    CCMoveMode moveMode,
    Uint16 rate,
    CCMinMiredsField colorTemperatureMinimumMireds,
    CCMaxMiredsField colorTemperatureMaximumMireds,
    CCColorOptions optionsMask,
    CCColorOptions optionsOverride
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<CCMoveMode>(moveMode);
    stream.write<Uint16>(rate);
    stream.write<CCMinMiredsField>(colorTemperatureMinimumMireds);
    stream.write<CCMaxMiredsField>(colorTemperatureMaximumMireds);
    stream.write<CCColorOptions>(optionsMask);
    stream.write<CCColorOptions>(optionsOverride);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::MoveColorTemperature), buffer);
}

void ColorControlServerCluster::stepColorTemperature(
    CCStepMode stepMode,
    Uint16 stepSize,
    Uint16 transitionTime,
    CCMinMiredsField colorTemperatureMinimumMireds,
    CCMaxMiredsField colorTemperatureMaximumMireds,
    CCColorOptions optionsMask,
    CCColorOptions optionsOverride
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<CCStepMode>(stepMode);
    stream.write<Uint16>(stepSize);
    stream.write<Uint16>(transitionTime);
    stream.write<CCMinMiredsField>(colorTemperatureMinimumMireds);
    stream.write<CCMaxMiredsField>(colorTemperatureMaximumMireds);
    stream.write<CCColorOptions>(optionsMask);
    stream.write<CCColorOptions>(optionsOverride);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::StepColorTemperature), buffer);
}


IlluminanceMeasurementServerCluster::IlluminanceMeasurementServerCluster(std::weak_ptr<Endpoint> endpoint)
    : GenericCluster(endpoint, Id, ZclDirection::CLIENT_TO_SERVER)
{
}


TemperatureMeasurementServerCluster::TemperatureMeasurementServerCluster(std::weak_ptr<Endpoint> endpoint)
    : GenericCluster(endpoint, Id, ZclDirection::CLIENT_TO_SERVER)
{
}


PressureMeasurementServerCluster::PressureMeasurementServerCluster(std::weak_ptr<Endpoint> endpoint)
    : GenericCluster(endpoint, Id, ZclDirection::CLIENT_TO_SERVER)
{
}


RelativityHumidityServerCluster::RelativityHumidityServerCluster(std::weak_ptr<Endpoint> endpoint)
    : GenericCluster(endpoint, Id, ZclDirection::CLIENT_TO_SERVER)
{
}


OccupancySensingServerCluster::OccupancySensingServerCluster(std::weak_ptr<Endpoint> endpoint)
    : GenericCluster(endpoint, Id, ZclDirection::CLIENT_TO_SERVER)
{
}


IASZoneServerCluster::IASZoneServerCluster(std::weak_ptr<Endpoint> endpoint)
    : GenericCluster(endpoint, Id, ZclDirection::CLIENT_TO_SERVER)
{
}

void IASZoneServerCluster::zoneEnrollResponse(
    ZoneEnrollResponseEnrollResponseCode enrollResponseCode,
    Uint8 zoneID
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<ZoneEnrollResponseEnrollResponseCode>(enrollResponseCode);
    stream.write<Uint8>(zoneID);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::ZoneEnrollResponse), buffer);
}

void IASZoneServerCluster::initiateNormalOperationMode(
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);


    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::InitiateNormalOperationMode), buffer);
}

void IASZoneServerCluster::initiateTestMode(
    Uint8 testModeDuration,
    Uint8 currentZoneSensitivityLevel
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<Uint8>(testModeDuration);
    stream.write<Uint8>(currentZoneSensitivityLevel);

    sendCommand(ZclFrameType::CLUSTER_SPECIFIC, static_cast<uint8_t>(ServerCommandId::InitiateTestMode), buffer);
}

void IASZoneServerCluster::handleClusterSpecificCommand(const ZclHeader& header, ZclStream& stream) {
    switch (static_cast<ClientCommandId>(header.commandId)) {
        case ClientCommandId::ZoneStatusChangeNotification: {
            IasZoneStatus zoneStatus;
            Map8 extendedStatus;
            Uint8 zoneID;
            Uint16 delay;

            zoneStatus = stream.read<IasZoneStatus>();
            extendedStatus = stream.read<Map8>();
            zoneID = stream.read<Uint8>();
            delay = stream.read<Uint16>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onZoneStatusChangeNotification(
                        zoneStatus,
                        extendedStatus,
                        zoneID,
                        delay
                    );
                }
            }

            break;
        }
        case ClientCommandId::ZoneEnrollRequest: {
            IasZoneType zoneType;
            Uint16 manufacturerCode;

            zoneType = stream.read<IasZoneType>();
            manufacturerCode = stream.read<Uint16>();

            for (auto wlistener : listeners_) {
                if (auto listener = wlistener.lock()) {
                    listener->onZoneEnrollRequest(
                        zoneType,
                        manufacturerCode
                    );
                }
            }

            break;
        }
        default:
            break;
    }
}

void IASZoneServerCluster::addClusterListener(std::weak_ptr<IListener> listener) {
    listeners_.push_back(listener);
}

IASZoneServerCluster::IListener::~IListener() = default;

LumiServerCluster::LumiServerCluster(std::weak_ptr<Endpoint> endpoint)
    : GenericCluster(endpoint, Id, ManufacturerCode, ZclDirection::CLIENT_TO_SERVER)
{
}


