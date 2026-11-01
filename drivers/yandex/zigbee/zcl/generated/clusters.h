#pragma once

#include "generic.h"

namespace zigbee::zcl {

class BasicServerCluster : public GenericCluster {
public:
    static constexpr uint16_t Id = 0x0000;

    enum class AttributeId : uint16_t {
        ZCLVersion = 0x0000,
        ApplicationVersion = 0x0001,
        StackVersion = 0x0002,
        HWVersion = 0x0003,
        ManufacturerName = 0x0004,
        ModelIdentifier = 0x0005,
        DateCode = 0x0006,
        PowerSource = 0x0007,
        GenericDeviceClass = 0x0008,
        GenericDeviceType = 0x0009,
        ProductCode = 0x000A,
        ProductURL = 0x000B,
        ManufacturerVersionDetails = 0x000C,
        SerialNumber = 0x000D,
        ProductLabel = 0x000E,
        LocationDescription = 0x0010,
        PhysicalEnvironment = 0x0011,
        DeviceEnabled = 0x0012,
        AlarmMask = 0x0013,
        DisableLocalConfig = 0x0014,
        SWBuildID = 0x4000,
        ClusterRevision = 0xFFFD,
        AttributeReportingStatus = 0xFFFE,
    };

    enum class ServerCommandId : uint8_t {
        ResetToFactoryDefaults = 0x00,
    };

    enum class ClientCommandId : uint8_t {
    };

public:
    BasicServerCluster(std::weak_ptr<Endpoint> endpoint);

public:
    void resetToFactoryDefaults(
    );

};

class OnOffServerCluster : public GenericCluster {
public:
    static constexpr uint16_t Id = 0x0006;

    enum class AttributeId : uint16_t {
        OnOff = 0x0000,
        GlobalSceneControl = 0x4000,
        OnTime = 0x4001,
        OffWaitTime = 0x4002,
        StartUpOnOff = 0x4003,
        ClusterRevision = 0xFFFD,
        AttributeReportingStatus = 0xFFFE,
    };

    enum class ServerCommandId : uint8_t {
        Off = 0x00,
        On = 0x01,
        Toggle = 0x02,
        OffWithEffect = 0x40,
        OnWithRecallGlobalScene = 0x41,
        OnWithTimedOff = 0x42,
    };

    enum class ClientCommandId : uint8_t {
    };

public:
    OnOffServerCluster(std::weak_ptr<Endpoint> endpoint);

public:
    void off(
    );

    void on(
    );

    void toggle(
    );

    void offWithEffect(
        zcl::OffWithEffectEffectIdentifier effectIdentifier,
        zcl::Uint8 effectVariant
    );

    void onWithRecallGlobalScene(
    );

    void onWithTimedOff(
        zcl::OnWithTimedOffOnOffControl onOffControl,
        zcl::Uint16 onTime,
        zcl::Uint16 offWaitTime
    );

};

class LevelServerCluster : public GenericCluster {
public:
    static constexpr uint16_t Id = 0x0008;

    enum class AttributeId : uint16_t {
        CurrentLevel = 0x0000,
        RemainingTime = 0x0001,
        MinLevel = 0x0002,
        MaxLevel = 0x0003,
        CurrentFrequency = 0x0004,
        MinFrequency = 0x0005,
        MaxFrequency = 0x0006,
        Options = 0x000F,
        OnOffTransitionTime = 0x0010,
        OnLevel = 0x0011,
        OnTransitionTime = 0x0012,
        OffTransitionTime = 0x0013,
        DefaultMoveRate = 0x0014,
        StartUpCurrentLevel = 0x4000,
        ClusterRevision = 0xFFFD,
        AttributeReportingStatus = 0xFFFE,
    };

    enum class ServerCommandId : uint8_t {
        MoveToLevel = 0x00,
        Move = 0x01,
        Step = 0x02,
        Stop = 0x03,
        MoveToLevelWithOnOff = 0x04,
        MoveWithOnOff = 0x05,
        StepWithOnOff = 0x06,
        StopWithOnOff = 0x07,
        MoveToClosestFrequency = 0x08,
    };

    enum class ClientCommandId : uint8_t {
    };

public:
    LevelServerCluster(std::weak_ptr<Endpoint> endpoint);

public:
    void moveToLevel(
        zcl::Uint8 level,
        zcl::Uint16 transitionTime,
        zcl::LevelOptions optionsMask,
        zcl::LevelOptions optionsOverride
    );

    void move(
        zcl::MoveStepMode moveMode,
        zcl::Uint8 rate,
        zcl::LevelOptions optionsMask,
        zcl::LevelOptions optionsOverride
    );

    void step(
        zcl::MoveStepMode stepMode,
        zcl::Uint8 stepSize,
        zcl::Uint16 transitionTime,
        zcl::LevelOptions optionsMask,
        zcl::LevelOptions optionsOverride
    );

    void stop(
        zcl::LevelOptions optionsMask,
        zcl::LevelOptions optionsOverride
    );

    void moveToLevelWithOnOff(
        zcl::Uint8 level,
        zcl::Uint16 transitionTime,
        zcl::LevelOptions optionsMask,
        zcl::LevelOptions optionsOverride
    );

    void moveWithOnOff(
        zcl::MoveStepMode moveMode,
        zcl::Uint8 rate,
        zcl::LevelOptions optionsMask,
        zcl::LevelOptions optionsOverride
    );

    void stepWithOnOff(
        zcl::MoveStepMode stepMode,
        zcl::Uint8 stepSize,
        zcl::Uint16 transitionTime,
        zcl::LevelOptions optionsMask,
        zcl::LevelOptions optionsOverride
    );

    void stopWithOnOff(
        zcl::LevelOptions optionsMask,
        zcl::LevelOptions optionsOverride
    );

    void moveToClosestFrequency(
        zcl::Uint16 frequency
    );

};

class AnalogInputServerCluster : public GenericCluster {
public:
    static constexpr uint16_t Id = 0x000C;

    enum class AttributeId : uint16_t {
        Description = 0x001C,
        MaxPresentValue = 0x0041,
        MinPresentValue = 0x0045,
        OutOfService = 0x0051,
        PresentValue = 0x0055,
        Reliability = 0x0067,
        Resolution = 0x006A,
        StatusFlags = 0x006F,
        EngineeringUnits = 0x0075,
        ApplicationType = 0x0100,
        ClusterRevision = 0xFFFD,
        AttributeReportingStatus = 0xFFFE,
    };

    enum class ServerCommandId : uint8_t {
    };

    enum class ClientCommandId : uint8_t {
    };

public:
    AnalogInputServerCluster(std::weak_ptr<Endpoint> endpoint);

public:
};

class AnalogOutputServerCluster : public GenericCluster {
public:
    static constexpr uint16_t Id = 0x000D;

    enum class AttributeId : uint16_t {
        Description = 0x001C,
        MaxPresentValue = 0x0041,
        MinPresentValue = 0x0045,
        OutOfService = 0x0051,
        PresentValue = 0x0055,
        PriorityArray = 0x0057,
        Reliability = 0x0067,
        Resolution = 0x006A,
        StatusFlags = 0x006F,
        EngineeringUnits = 0x0075,
        ApplicationType = 0x0100,
        ClusterRevision = 0xFFFD,
        AttributeReportingStatus = 0xFFFE,
    };

    enum class ServerCommandId : uint8_t {
    };

    enum class ClientCommandId : uint8_t {
    };

public:
    AnalogOutputServerCluster(std::weak_ptr<Endpoint> endpoint);

public:
};

class MultistateInputServerCluster : public GenericCluster {
public:
    static constexpr uint16_t Id = 0x0012;

    enum class AttributeId : uint16_t {
        StateText = 0x000E,
        Description = 0x001C,
        NumberOfStates = 0x004A,
        OutOfService = 0x0051,
        PresentValue = 0x0055,
        Reliability = 0x0067,
        StatusFlags = 0x006F,
        ApplicationType = 0x0100,
        ClusterRevision = 0xFFFD,
        AttributeReportingStatus = 0xFFFE,
    };

    enum class ServerCommandId : uint8_t {
    };

    enum class ClientCommandId : uint8_t {
    };

public:
    MultistateInputServerCluster(std::weak_ptr<Endpoint> endpoint);

public:
};

class MultistateOutputServerCluster : public GenericCluster {
public:
    static constexpr uint16_t Id = 0x0013;

    enum class AttributeId : uint16_t {
        StateText = 0x000E,
        Description = 0x001C,
        NumberOfStates = 0x004A,
        OutOfService = 0x0051,
        PresentValue = 0x0055,
        PriorityArray = 0x0057,
        Reliability = 0x0067,
        RelinquishDefault = 0x0068,
        StatusFlags = 0x006F,
        ApplicationType = 0x0100,
        ClusterRevision = 0xFFFD,
        AttributeReportingStatus = 0xFFFE,
    };

    enum class ServerCommandId : uint8_t {
    };

    enum class ClientCommandId : uint8_t {
    };

public:
    MultistateOutputServerCluster(std::weak_ptr<Endpoint> endpoint);

public:
};

class DoorLockServerCluster : public GenericCluster {
public:
    static constexpr uint16_t Id = 0x0101;

    enum class AttributeId : uint16_t {
        LockState = 0x0000,
        LockType = 0x0001,
        ActuatorEnabled = 0x0002,
        DoorState = 0x0003,
        DoorOpenEvents = 0x0004,
        DoorClosedEvents = 0x0005,
        OpenPeriod = 0x0006,
        NumberOfLogRecordsSupported = 0x0010,
        NumberOfTotalUsersSupported = 0x0011,
        NumberOfPINUsersSupported = 0x0012,
        NumberOfRFIDUsersSupported = 0x0013,
        NumberOfWeekDaySchedulesSupportedPerUser = 0x0014,
        NumberOfYearDaySchedulesSupportedPerUser = 0x0015,
        NumberOfHolidaySchedulesSupported = 0x0016,
        MaxPINCodeLength = 0x0017,
        MinPINCodeLength = 0x0018,
        MaxRFIDCodeLength = 0x0019,
        MinRFIDCodeLength = 0x001A,
        EnableLogging = 0x0020,
        Language = 0x0021,
        LEDSettings = 0x0022,
        AutoRelockTime = 0x0023,
        SoundVolume = 0x0024,
        OperatingMode = 0x0025,
        SupportedOperatingModes = 0x0026,
        DefaultConfigurationRegister = 0x0027,
        EnableLocalProgramming = 0x0028,
        EnableOneTouchLocking = 0x0029,
        EnableInsideStatusLED = 0x002A,
        EnablePrivacyModeButton = 0x002B,
        WrongCodeEntryLimit = 0x0030,
        UserCodeTemporaryDisableTime = 0x0031,
        SendPINOverTheAir = 0x0032,
        RequirePINforRFOperation = 0x0033,
        SecurityLevel = 0x0034,
        AlarmMask = 0x0040,
        KeypadOperationEventMask = 0x0041,
        RFOperationEventMask = 0x0042,
        ManualOperationEventMask = 0x0043,
        RFIDOperationEventMask = 0x0044,
        KeypadProgrammingEventMask = 0x0045,
        RFProgrammingEventMask = 0x0046,
        RFIDProgrammingEventMask = 0x0047,
        ClusterRevision = 0xFFFD,
        AttributeReportingStatus = 0xFFFE,
    };

    enum class ServerCommandId : uint8_t {
        LockDoor = 0x00,
        UnlockDoor = 0x01,
        Toggle = 0x02,
        UnlockWithTimeout = 0x03,
        GetLogRecord = 0x04,
        SetPINCode = 0x05,
        GetPINCode = 0x06,
        ClearPINCode = 0x07,
        ClearAllPINCodes = 0x08,
        SetUserStatus = 0x09,
        GetUserStatus = 0x0A,
        SetWeekdaySchedule = 0x0B,
        GetWeekdaySchedule = 0x0C,
        ClearWeekdaySchedule = 0x0D,
        SetYearDaySchedule = 0x0E,
        GetYearDaySchedule = 0x0F,
        ClearYearDaySchedule = 0x10,
        SetHolidaySchedule = 0x11,
        GetHolidaySchedule = 0x12,
        ClearHolidaySchedule = 0x13,
        SetUserType = 0x14,
        GetUserType = 0x15,
        SetRFIDCode = 0x16,
        GetRFIDCode = 0x17,
        ClearRFIDCode = 0x18,
        ClearAllRFIDCodes = 0x19,
    };

    enum class ClientCommandId : uint8_t {
        LockDoorResponse = 0x00,
        UnlockDoorResponse = 0x01,
        ToggleResponse = 0x02,
        UnlockWithTimeoutResponse = 0x03,
        GetLogRecordResponse = 0x04,
        SetPINCodeResponse = 0x05,
        GetPINCodeResponse = 0x06,
        ClearPINCodeResponse = 0x07,
        ClearAllPINCodesResponse = 0x08,
        SetUserStatusResponse = 0x09,
        GetUserStatusResponse = 0x0A,
        SetWeekdayScheduleResponse = 0x0B,
        GetWeekdayScheduleResponse = 0x0C,
        ClearWeekdayScheduleResponse = 0x0D,
        SetYearDayScheduleResponse = 0x0E,
        GetYearDayScheduleResponse = 0x0F,
        ClearYearDayScheduleResponse = 0x10,
        SetHolidayScheduleResponse = 0x11,
        GetHolidayScheduleResponse = 0x12,
        ClearHolidayScheduleResponse = 0x13,
        SetUserTypeResponse = 0x14,
        GetUserTypeResponse = 0x15,
        SetRFIDCodeResponse = 0x16,
        GetRFIDCodeResponse = 0x17,
        ClearRFIDCodeResponse = 0x18,
        ClearAllRFIDCodesResponse = 0x19,
        OperatingEventNotification = 0x20,
        ProgrammingEventNotification = 0x21,
    };

    class IListener {
    public:
        virtual ~IListener();

        virtual void onLockDoorResponse(
            const zcl::ZclStatus& status
        ) = 0;

        virtual void onUnlockDoorResponse(
            const zcl::ZclStatus& status
        ) = 0;

        virtual void onToggleResponse(
            const zcl::ZclStatus& status
        ) = 0;

        virtual void onUnlockWithTimeoutResponse(
            const zcl::ZclStatus& status
        ) = 0;

        virtual void onGetLogRecordResponse(
            const zcl::Uint16& logEntryID,
            const zcl::Uint32& timestamp,
            const zcl::GetLogRecordResponseEventType& eventType,
            const zcl::DrlkOperEventSource& sourceOperationEvent,
            const zcl::Uint8& eventIDOrAlarmCode,
            const zcl::Uint16& userID,
            const zcl::Octstr& pin
        ) = 0;

        virtual void onSetPINCodeResponse(
            const zcl::DrlkSetCodeStatus& status
        ) = 0;

        virtual void onGetPINCodeResponse(
            const zcl::Uint16& userID,
            const zcl::DrlkUserStatus& userStatus,
            const zcl::DrlkUserType& userType,
            const zcl::Octstr& code
        ) = 0;

        virtual void onClearPINCodeResponse(
            const zcl::DrlkPassFailStatus& status
        ) = 0;

        virtual void onClearAllPINCodesResponse(
            const zcl::DrlkPassFailStatus& status
        ) = 0;

        virtual void onSetUserStatusResponse(
            const zcl::DrlkPassFailStatus& status
        ) = 0;

        virtual void onGetUserStatusResponse(
            const zcl::Uint16& userID,
            const zcl::DrlkUserStatus& userStatus
        ) = 0;

        virtual void onSetWeekdayScheduleResponse(
            const zcl::DrlkPassFailStatus& status
        ) = 0;

        virtual void onGetWeekdayScheduleResponse(
            const zcl::Uint8& scheduleID,
            const zcl::Uint16& userID,
            const zcl::ZclStatus& status,
            const zcl::DrlkDaysMask& daysMask,
            const zcl::Uint8& startHour,
            const zcl::Uint8& startMinute,
            const zcl::Uint8& endHour,
            const zcl::Uint8& endMinute
        ) = 0;

        virtual void onClearWeekdayScheduleResponse(
            const zcl::DrlkPassFailStatus& status
        ) = 0;

        virtual void onSetYearDayScheduleResponse(
            const zcl::DrlkPassFailStatus& status
        ) = 0;

        virtual void onGetYearDayScheduleResponse(
            const zcl::Uint8& scheduleID,
            const zcl::Uint16& userID,
            const zcl::ZclStatus& status,
            const zcl::Uint32& localStartTime,
            const zcl::Uint32& localEndTime
        ) = 0;

        virtual void onClearYearDayScheduleResponse(
            const zcl::DrlkPassFailStatus& status
        ) = 0;

        virtual void onSetHolidayScheduleResponse(
            const zcl::DrlkPassFailStatus& status
        ) = 0;

        virtual void onGetHolidayScheduleResponse(
            const zcl::Uint8& holidayScheduleID,
            const zcl::ZclStatus& status,
            const zcl::Uint32& localStartTime,
            const zcl::Uint32& localEndTime,
            const zcl::DrlkOperMode& operatingModeDuringHoliday
        ) = 0;

        virtual void onClearHolidayScheduleResponse(
            const zcl::DrlkPassFailStatus& status
        ) = 0;

        virtual void onSetUserTypeResponse(
            const zcl::DrlkPassFailStatus& status
        ) = 0;

        virtual void onGetUserTypeResponse(
            const zcl::Uint16& userID,
            const zcl::DrlkUserType& userType
        ) = 0;

        virtual void onSetRFIDCodeResponse(
            const zcl::DrlkSetCodeStatus& status
        ) = 0;

        virtual void onGetRFIDCodeResponse(
            const zcl::Uint16& userID,
            const zcl::DrlkUserStatus& userStatus,
            const zcl::DrlkUserType& userType,
            const zcl::Octstr& rFIDCode
        ) = 0;

        virtual void onClearRFIDCodeResponse(
            const zcl::DrlkPassFailStatus& status
        ) = 0;

        virtual void onClearAllRFIDCodesResponse(
            const zcl::DrlkPassFailStatus& status
        ) = 0;

        virtual void onOperatingEventNotification(
            const zcl::DrlkOperEventSource& operationEventSource,
            const zcl::OperatingEventNotificationOperationEventCode& operationEventCode,
            const zcl::Uint16& userID,
            const zcl::Octstr& pin,
            const zcl::Uint32& localTime,
            const zcl::String& data
        ) = 0;

        virtual void onProgrammingEventNotification(
            const zcl::ProgrammingEventNotificationProgramEventSource& programEventSource,
            const zcl::ProgrammingEventNotificationProgramEventCode& programEventCode,
            const zcl::Uint16& userID,
            const zcl::Octstr& pin,
            const zcl::DrlkUserType& userType,
            const zcl::DrlkUserStatus& userStatus,
            const zcl::Uint32& localTime,
            const zcl::String& data
        ) = 0;
    };

public:
    DoorLockServerCluster(std::weak_ptr<Endpoint> endpoint);

public:
    void lockDoor(
        zcl::Octstr pINOrRFIDCode
    );

    void unlockDoor(
        zcl::Octstr pINOrRFIDCode
    );

    void toggle(
        zcl::Octstr pINOrRFIDCode
    );

    void unlockWithTimeout(
        zcl::Uint16 timeoutInSeconds,
        zcl::Octstr pINOrRFIDCode
    );

    void getLogRecord(
        zcl::Uint16 logIndex
    );

    void setPINCode(
        zcl::DrlkPINUserID userID,
        zcl::DrlkSettableUserStatus userStatus,
        zcl::DrlkUserType userType,
        zcl::Octstr pin
    );

    void getPINCode(
        zcl::DrlkPINUserID userID
    );

    void clearPINCode(
        zcl::DrlkPINUserID userID
    );

    void clearAllPINCodes(
    );

    void setUserStatus(
        zcl::DrlkTotalUserID userID,
        zcl::DrlkSettableUserStatus userStatus
    );

    void getUserStatus(
        zcl::DrlkTotalUserID userID
    );

    void setWeekdaySchedule(
        zcl::DrlkWeekDayScheduleID scheduleID,
        zcl::DrlkTotalUserID userID,
        zcl::DrlkDaysMask daysMask,
        zcl::Uint8 startHour,
        zcl::Uint8 startMinute,
        zcl::Uint8 endHour,
        zcl::Uint8 endMinute
    );

    void getWeekdaySchedule(
        zcl::DrlkWeekDayScheduleID scheduleID,
        zcl::DrlkTotalUserID userID
    );

    void clearWeekdaySchedule(
        zcl::DrlkWeekDayScheduleID scheduleID,
        zcl::DrlkTotalUserID userID
    );

    void setYearDaySchedule(
        zcl::DrlkYearDayScheduleID scheduleID,
        zcl::DrlkTotalUserID userID,
        zcl::Uint32 localStartTime,
        zcl::Uint32 localEndTime
    );

    void getYearDaySchedule(
        zcl::DrlkYearDayScheduleID scheduleID,
        zcl::DrlkTotalUserID userID
    );

    void clearYearDaySchedule(
        zcl::DrlkYearDayScheduleID scheduleID,
        zcl::DrlkTotalUserID userID
    );

    void setHolidaySchedule(
        zcl::DrlkHolidayScheduleID holidayScheduleID,
        zcl::Uint32 localStartTime,
        zcl::Uint32 localEndTime,
        zcl::DrlkOperMode operatingModeDuringHoliday
    );

    void getHolidaySchedule(
        zcl::DrlkHolidayScheduleID holidayScheduleID
    );

    void clearHolidaySchedule(
        zcl::DrlkHolidayScheduleID holidayScheduleID
    );

    void setUserType(
        zcl::DrlkTotalUserID userID,
        zcl::DrlkUserType userType
    );

    void getUserType(
        zcl::DrlkTotalUserID userID
    );

    void setRFIDCode(
        zcl::DrlkRFIDUserID userID,
        zcl::DrlkSettableUserStatus userStatus,
        zcl::DrlkUserType userType,
        zcl::Octstr rFIDCode
    );

    void getRFIDCode(
        zcl::DrlkRFIDUserID userID
    );

    void clearRFIDCode(
        zcl::DrlkRFIDUserID userID
    );

    void clearAllRFIDCodes(
    );

    void handleClusterSpecificCommand(const ZclHeader& header, ZclStream& stream) override;

    void addClusterListener(std::weak_ptr<IListener> listener);

private:
    std::vector<std::weak_ptr<IListener>> listeners_;
};

class ColorControlServerCluster : public GenericCluster {
public:
    static constexpr uint16_t Id = 0x0300;

    enum class AttributeId : uint16_t {
        CurrentHue = 0x0000,
        CurrentSaturation = 0x0001,
        RemainingTime = 0x0002,
        CurrentX = 0x0003,
        CurrentY = 0x0004,
        DriftCompensation = 0x0005,
        CompensationText = 0x0006,
        ColorTemperatureMireds = 0x0007,
        ColorMode = 0x0008,
        Options = 0x000F,
        NumberOfPrimaries = 0x0010,
        Primary1X = 0x0011,
        Primary1Y = 0x0012,
        Primary1Intensity = 0x0013,
        Primary2X = 0x0015,
        Primary2Y = 0x0016,
        Primary2Intensity = 0x0017,
        Primary3X = 0x0019,
        Primary3Y = 0x001A,
        Primary3Intensity = 0x001B,
        Primary4X = 0x0020,
        Primary4Y = 0x0021,
        Primary4Intensity = 0x0022,
        Primary5X = 0x0024,
        Primary5Y = 0x0025,
        Primary5Intensity = 0x0026,
        Primary6X = 0x0028,
        Primary6Y = 0x0029,
        Primary6Intensity = 0x002A,
        WhitePointX = 0x0030,
        WhitePointY = 0x0031,
        ColorPointRX = 0x0032,
        ColorPointRY = 0x0033,
        ColorPointRIntensity = 0x0034,
        ColorPointGX = 0x0036,
        ColorPointGY = 0x0037,
        ColorPointGIntensity = 0x0038,
        ColorPointBX = 0x003A,
        ColorPointBY = 0x003B,
        ColorPointBIntensity = 0x003C,
        EnhancedCurrentHue = 0x4000,
        EnhancedColorMode = 0x4001,
        ColorLoopActive = 0x4002,
        ColorLoopDirection = 0x4003,
        ColorLoopTime = 0x4004,
        ColorLoopStartEnhancedHue = 0x4005,
        ColorLoopStoredEnhancedHue = 0x4006,
        ColorCapabilities = 0x400A,
        ColorTempPhysicalMinMireds = 0x400B,
        ColorTempPhysicalMaxMireds = 0x400C,
        CoupleColorTempToLevelMinMireds = 0x400D,
        StartUpColorTemperatureMireds = 0x4010,
        ClusterRevision = 0xFFFD,
        AttributeReportingStatus = 0xFFFE,
    };

    enum class ServerCommandId : uint8_t {
        MoveToHue = 0x00,
        MoveHue = 0x01,
        StepHue = 0x02,
        MoveToSaturation = 0x03,
        MoveSaturation = 0x04,
        StepSaturation = 0x05,
        MoveToHueAndSaturation = 0x06,
        MoveToColor = 0x07,
        MoveColor = 0x08,
        StepColor = 0x09,
        MoveToColorTemperature = 0x0A,
        EnhancedMoveToHue = 0x40,
        EnhancedMoveHue = 0x41,
        EnhancedStepHue = 0x42,
        EnhancedMoveToHueAndSaturation = 0x43,
        ColorLoopSet = 0x44,
        StopMoveStep = 0x47,
        MoveColorTemperature = 0x4B,
        StepColorTemperature = 0x4C,
    };

    enum class ClientCommandId : uint8_t {
    };

public:
    ColorControlServerCluster(std::weak_ptr<Endpoint> endpoint);

public:
    void moveToHue(
        zcl::Uint8 hue,
        zcl::CCDirection direction,
        zcl::Uint16 transitionTime,
        zcl::CCColorOptions optionsMask,
        zcl::CCColorOptions optionsOverride
    );

    void moveHue(
        zcl::CCMoveMode moveMode,
        zcl::Uint8 rate,
        zcl::CCColorOptions optionsMask,
        zcl::CCColorOptions optionsOverride
    );

    void stepHue(
        zcl::CCStepMode stepMode,
        zcl::Uint8 stepSize,
        zcl::Uint8 transitionTime,
        zcl::CCColorOptions optionsMask,
        zcl::CCColorOptions optionsOverride
    );

    void moveToSaturation(
        zcl::Uint8 saturation,
        zcl::Uint16 transitionTime,
        zcl::CCColorOptions optionsMask,
        zcl::CCColorOptions optionsOverride
    );

    void moveSaturation(
        zcl::CCMoveMode moveMode,
        zcl::Uint8 rate,
        zcl::CCColorOptions optionsMask,
        zcl::CCColorOptions optionsOverride
    );

    void stepSaturation(
        zcl::CCStepMode stepMode,
        zcl::Uint8 stepSize,
        zcl::Uint8 transitionTime,
        zcl::CCColorOptions optionsMask,
        zcl::CCColorOptions optionsOverride
    );

    void moveToHueAndSaturation(
        zcl::Uint8 hue,
        zcl::Uint8 saturation,
        zcl::Uint16 transitionTime,
        zcl::CCColorOptions optionsMask,
        zcl::CCColorOptions optionsOverride
    );

    void moveToColor(
        zcl::Uint16 colorX,
        zcl::Uint16 colorY,
        zcl::Uint16 transitionTime,
        zcl::CCColorOptions optionsMask,
        zcl::CCColorOptions optionsOverride
    );

    void moveColor(
        zcl::Int16 rateX,
        zcl::Int16 rateY,
        zcl::CCColorOptions optionsMask,
        zcl::CCColorOptions optionsOverride
    );

    void stepColor(
        zcl::Int16 stepX,
        zcl::Int16 stepY,
        zcl::Uint16 transitionTime,
        zcl::CCColorOptions optionsMask,
        zcl::CCColorOptions optionsOverride
    );

    void moveToColorTemperature(
        zcl::Uint16 colorTemperatureMireds,
        zcl::Uint16 transitionTime,
        zcl::CCColorOptions optionsMask,
        zcl::CCColorOptions optionsOverride
    );

    void enhancedMoveToHue(
        zcl::Uint16 enhancedHue,
        zcl::CCDirection direction,
        zcl::Uint16 transitionTime,
        zcl::CCColorOptions optionsMask,
        zcl::CCColorOptions optionsOverride
    );

    void enhancedMoveHue(
        zcl::CCMoveMode moveMode,
        zcl::Uint16 rate,
        zcl::CCColorOptions optionsMask,
        zcl::CCColorOptions optionsOverride
    );

    void enhancedStepHue(
        zcl::CCStepMode stepMode,
        zcl::Uint16 stepSize,
        zcl::Uint16 transitionTime,
        zcl::CCColorOptions optionsMask,
        zcl::CCColorOptions optionsOverride
    );

    void enhancedMoveToHueAndSaturation(
        zcl::Uint16 enhancedHue,
        zcl::Uint8 saturation,
        zcl::Uint16 transitionTime,
        zcl::CCColorOptions optionsMask,
        zcl::CCColorOptions optionsOverride
    );

    void colorLoopSet(
        zcl::ColorLoopSetUpdateFlags updateFlags,
        zcl::ColorLoopSetAction action,
        zcl::CCColorLoopDirection direction,
        zcl::Uint16 time,
        zcl::Uint16 startHue,
        zcl::CCColorOptions optionsMask,
        zcl::CCColorOptions optionsOverride
    );

    void stopMoveStep(
        zcl::CCColorOptions optionsMask,
        zcl::CCColorOptions optionsOverride
    );

    void moveColorTemperature(
        zcl::CCMoveMode moveMode,
        zcl::Uint16 rate,
        zcl::CCMinMiredsField colorTemperatureMinimumMireds,
        zcl::CCMaxMiredsField colorTemperatureMaximumMireds,
        zcl::CCColorOptions optionsMask,
        zcl::CCColorOptions optionsOverride
    );

    void stepColorTemperature(
        zcl::CCStepMode stepMode,
        zcl::Uint16 stepSize,
        zcl::Uint16 transitionTime,
        zcl::CCMinMiredsField colorTemperatureMinimumMireds,
        zcl::CCMaxMiredsField colorTemperatureMaximumMireds,
        zcl::CCColorOptions optionsMask,
        zcl::CCColorOptions optionsOverride
    );

};

class IlluminanceMeasurementServerCluster : public GenericCluster {
public:
    static constexpr uint16_t Id = 0x0400;

    enum class AttributeId : uint16_t {
        MeasuredValue = 0x0000,
        MinMeasuredValue = 0x0001,
        MaxMeasuredValue = 0x0002,
        Tolerance = 0x0003,
        LightSensorType = 0x0004,
        ClusterRevision = 0xFFFD,
        AttributeReportingStatus = 0xFFFE,
    };

    enum class ServerCommandId : uint8_t {
    };

    enum class ClientCommandId : uint8_t {
    };

public:
    IlluminanceMeasurementServerCluster(std::weak_ptr<Endpoint> endpoint);

public:
};

class TemperatureMeasurementServerCluster : public GenericCluster {
public:
    static constexpr uint16_t Id = 0x0402;

    enum class AttributeId : uint16_t {
        MeasuredValue = 0x0000,
        MinMeasuredValue = 0x0001,
        MaxMeasuredValue = 0x0002,
        Tolerance = 0x0003,
        ClusterRevision = 0xFFFD,
        AttributeReportingStatus = 0xFFFE,
    };

    enum class ServerCommandId : uint8_t {
    };

    enum class ClientCommandId : uint8_t {
    };

public:
    TemperatureMeasurementServerCluster(std::weak_ptr<Endpoint> endpoint);

public:
};

class PressureMeasurementServerCluster : public GenericCluster {
public:
    static constexpr uint16_t Id = 0x0403;

    enum class AttributeId : uint16_t {
        MeasuredValue = 0x0000,
        MinMeasuredValue = 0x0001,
        MaxMeasuredValue = 0x0002,
        Tolerance = 0x0003,
        ScaledValue = 0x0010,
        MinScaledValue = 0x0011,
        MaxScaledValue = 0x0012,
        ScaledTolerance = 0x0013,
        Scale = 0x0014,
        ClusterRevision = 0xFFFD,
        AttributeReportingStatus = 0xFFFE,
    };

    enum class ServerCommandId : uint8_t {
    };

    enum class ClientCommandId : uint8_t {
    };

public:
    PressureMeasurementServerCluster(std::weak_ptr<Endpoint> endpoint);

public:
};

class RelativityHumidityServerCluster : public GenericCluster {
public:
    static constexpr uint16_t Id = 0x0405;

    enum class AttributeId : uint16_t {
        MeasuredValue = 0x0000,
        MinMeasuredValue = 0x0001,
        MaxMeasuredValue = 0x0002,
        Tolerance = 0x0003,
        ClusterRevision = 0xFFFD,
        AttributeReportingStatus = 0xFFFE,
    };

    enum class ServerCommandId : uint8_t {
    };

    enum class ClientCommandId : uint8_t {
    };

public:
    RelativityHumidityServerCluster(std::weak_ptr<Endpoint> endpoint);

public:
};

class OccupancySensingServerCluster : public GenericCluster {
public:
    static constexpr uint16_t Id = 0x0406;

    enum class AttributeId : uint16_t {
        Occupancy = 0x0000,
        OccupancySensorType = 0x0001,
        OccupancySensorTypeBitmap = 0x0002,
        PIROccupiedToUnoccupiedDelay = 0x0010,
        PIRUnoccupiedToOccupiedDelay = 0x0011,
        PIRUnoccupiedToOccupiedThreshold = 0x0012,
        UltrasonicOccupiedToUnoccupiedDelay = 0x0020,
        UltrasonicUnoccupiedToOccupiedDelay = 0x0021,
        UltrasonicUnoccupiedToOccupiedThreshold = 0x0022,
        PhysicalContactOccupiedToUnoccupiedDelay = 0x0030,
        PhysicalContactUnoccupiedToOccupiedDelay = 0x0031,
        PhysicalContactUnoccupiedToOccupiedThreshold = 0x0032,
        ClusterRevision = 0xFFFD,
        AttributeReportingStatus = 0xFFFE,
    };

    enum class ServerCommandId : uint8_t {
    };

    enum class ClientCommandId : uint8_t {
    };

public:
    OccupancySensingServerCluster(std::weak_ptr<Endpoint> endpoint);

public:
};

class IASZoneServerCluster : public GenericCluster {
public:
    static constexpr uint16_t Id = 0x0500;

    enum class AttributeId : uint16_t {
        ZoneState = 0x0000,
        ZoneType = 0x0001,
        ZoneStatus = 0x0002,
        IASCIEAddress = 0x0010,
        ZoneID = 0x0011,
        NumberOfZoneSensitivityLevelsSupported = 0x0012,
        CurrentZoneSensitivityLevel = 0x0013,
        ClusterRevision = 0xFFFD,
        AttributeReportingStatus = 0xFFFE,
    };

    enum class ServerCommandId : uint8_t {
        ZoneEnrollResponse = 0x00,
        InitiateNormalOperationMode = 0x01,
        InitiateTestMode = 0x02,
    };

    enum class ClientCommandId : uint8_t {
        ZoneStatusChangeNotification = 0x00,
        ZoneEnrollRequest = 0x01,
    };

    class IListener {
    public:
        virtual ~IListener();

        virtual void onZoneStatusChangeNotification(
            const zcl::IasZoneStatus& zoneStatus,
            const zcl::Map8& extendedStatus,
            const zcl::Uint8& zoneID,
            const zcl::Uint16& delay
        ) = 0;

        virtual void onZoneEnrollRequest(
            const zcl::IasZoneType& zoneType,
            const zcl::Uint16& manufacturerCode
        ) = 0;
    };

public:
    IASZoneServerCluster(std::weak_ptr<Endpoint> endpoint);

public:
    void zoneEnrollResponse(
        zcl::ZoneEnrollResponseEnrollResponseCode enrollResponseCode,
        zcl::Uint8 zoneID
    );

    void initiateNormalOperationMode(
    );

    void initiateTestMode(
        zcl::Uint8 testModeDuration,
        zcl::Uint8 currentZoneSensitivityLevel
    );

    void handleClusterSpecificCommand(const ZclHeader& header, ZclStream& stream) override;

    void addClusterListener(std::weak_ptr<IListener> listener);

private:
    std::vector<std::weak_ptr<IListener>> listeners_;
};

class LumiServerCluster : public GenericCluster {
public:
    static constexpr uint16_t Id = 0xFCC0;
    static constexpr uint16_t ManufacturerCode = 0x115F;

    enum class AttributeId : uint16_t {
        Mode = 0x0009,
        MotionDetectionInterval = 0x0102,
        MotionIlluminance = 0x0112,
        ClusterRevision = 0xFFFD,
        AttributeReportingStatus = 0xFFFE,
    };

    enum class ServerCommandId : uint8_t {
    };

    enum class ClientCommandId : uint8_t {
    };

public:
    LumiServerCluster(std::weak_ptr<Endpoint> endpoint);

public:
};


} // namespace zigbee::zcl
