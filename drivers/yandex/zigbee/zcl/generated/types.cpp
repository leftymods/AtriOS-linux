#include "types.h"

#include <smart_devices/libs/zigbee/zcl/zcl_stream.h>

using namespace zigbee;
using namespace zigbee::zcl;

template <>
BasicGenericDeviceClass ZclStream::read<BasicGenericDeviceClass>() {
    return static_cast<BasicGenericDeviceClass>(static_cast<uint8_t>(read<Enum8>()));
}

template <>
void ZclOutputStream::write<BasicGenericDeviceClass>(const BasicGenericDeviceClass& value) {
    write<Enum8>(static_cast<uint8_t>(value));
}

template <>
BasicGenericDeviceType ZclStream::read<BasicGenericDeviceType>() {
    return static_cast<BasicGenericDeviceType>(static_cast<uint8_t>(read<Enum8>()));
}

template <>
void ZclOutputStream::write<BasicGenericDeviceType>(const BasicGenericDeviceType& value) {
    write<Enum8>(static_cast<uint8_t>(value));
}

template <>
BasicPhysicalEnvironment ZclStream::read<BasicPhysicalEnvironment>() {
    return static_cast<BasicPhysicalEnvironment>(static_cast<uint8_t>(read<Enum8>()));
}

template <>
void ZclOutputStream::write<BasicPhysicalEnvironment>(const BasicPhysicalEnvironment& value) {
    write<Enum8>(static_cast<uint8_t>(value));
}

template <>
BasicPowerSource ZclStream::read<BasicPowerSource>() {
    return static_cast<BasicPowerSource>(static_cast<uint8_t>(read<Enum8>()));
}

template <>
void ZclOutputStream::write<BasicPowerSource>(const BasicPowerSource& value) {
    write<Enum8>(static_cast<uint8_t>(value));
}

template <>
CCColorLoopDirection ZclStream::read<CCColorLoopDirection>() {
    return static_cast<CCColorLoopDirection>(static_cast<uint8_t>(read<Uint8>()));
}

template <>
void ZclOutputStream::write<CCColorLoopDirection>(const CCColorLoopDirection& value) {
    write<Uint8>(static_cast<uint8_t>(value));
}

template <>
CCDirection ZclStream::read<CCDirection>() {
    return static_cast<CCDirection>(static_cast<uint8_t>(read<Enum8>()));
}

template <>
void ZclOutputStream::write<CCDirection>(const CCDirection& value) {
    write<Enum8>(static_cast<uint8_t>(value));
}

template <>
CCMoveMode ZclStream::read<CCMoveMode>() {
    return static_cast<CCMoveMode>(static_cast<uint8_t>(read<Enum8>()));
}

template <>
void ZclOutputStream::write<CCMoveMode>(const CCMoveMode& value) {
    write<Enum8>(static_cast<uint8_t>(value));
}

template <>
CCStepMode ZclStream::read<CCStepMode>() {
    return static_cast<CCStepMode>(static_cast<uint8_t>(read<Enum8>()));
}

template <>
void ZclOutputStream::write<CCStepMode>(const CCStepMode& value) {
    write<Enum8>(static_cast<uint8_t>(value));
}

template <>
ColorControlColorLoopActive ZclStream::read<ColorControlColorLoopActive>() {
    return static_cast<ColorControlColorLoopActive>(static_cast<uint8_t>(read<Uint8>()));
}

template <>
void ZclOutputStream::write<ColorControlColorLoopActive>(const ColorControlColorLoopActive& value) {
    write<Uint8>(static_cast<uint8_t>(value));
}

template <>
ColorControlColorMode ZclStream::read<ColorControlColorMode>() {
    return static_cast<ColorControlColorMode>(static_cast<uint8_t>(read<Enum8>()));
}

template <>
void ZclOutputStream::write<ColorControlColorMode>(const ColorControlColorMode& value) {
    write<Enum8>(static_cast<uint8_t>(value));
}

template <>
ColorControlDriftCompensation ZclStream::read<ColorControlDriftCompensation>() {
    return static_cast<ColorControlDriftCompensation>(static_cast<uint8_t>(read<Enum8>()));
}

template <>
void ZclOutputStream::write<ColorControlDriftCompensation>(const ColorControlDriftCompensation& value) {
    write<Enum8>(static_cast<uint8_t>(value));
}

template <>
ColorControlEnhancedColorMode ZclStream::read<ColorControlEnhancedColorMode>() {
    return static_cast<ColorControlEnhancedColorMode>(static_cast<uint8_t>(read<Enum8>()));
}

template <>
void ZclOutputStream::write<ColorControlEnhancedColorMode>(const ColorControlEnhancedColorMode& value) {
    write<Enum8>(static_cast<uint8_t>(value));
}

template <>
ColorLoopSetAction ZclStream::read<ColorLoopSetAction>() {
    return static_cast<ColorLoopSetAction>(static_cast<uint8_t>(read<Enum8>()));
}

template <>
void ZclOutputStream::write<ColorLoopSetAction>(const ColorLoopSetAction& value) {
    write<Enum8>(static_cast<uint8_t>(value));
}

template <>
DoorLockDoorState ZclStream::read<DoorLockDoorState>() {
    return static_cast<DoorLockDoorState>(static_cast<uint8_t>(read<Enum8>()));
}

template <>
void ZclOutputStream::write<DoorLockDoorState>(const DoorLockDoorState& value) {
    write<Enum8>(static_cast<uint8_t>(value));
}

template <>
DoorLockLEDSettings ZclStream::read<DoorLockLEDSettings>() {
    return static_cast<DoorLockLEDSettings>(static_cast<uint8_t>(read<Uint8>()));
}

template <>
void ZclOutputStream::write<DoorLockLEDSettings>(const DoorLockLEDSettings& value) {
    write<Uint8>(static_cast<uint8_t>(value));
}

template <>
DoorLockLockState ZclStream::read<DoorLockLockState>() {
    return static_cast<DoorLockLockState>(static_cast<uint8_t>(read<Enum8>()));
}

template <>
void ZclOutputStream::write<DoorLockLockState>(const DoorLockLockState& value) {
    write<Enum8>(static_cast<uint8_t>(value));
}

template <>
DoorLockLockType ZclStream::read<DoorLockLockType>() {
    return static_cast<DoorLockLockType>(static_cast<uint8_t>(read<Enum8>()));
}

template <>
void ZclOutputStream::write<DoorLockLockType>(const DoorLockLockType& value) {
    write<Enum8>(static_cast<uint8_t>(value));
}

template <>
DoorLockSecurityLevel ZclStream::read<DoorLockSecurityLevel>() {
    return static_cast<DoorLockSecurityLevel>(static_cast<uint8_t>(read<Enum8>()));
}

template <>
void ZclOutputStream::write<DoorLockSecurityLevel>(const DoorLockSecurityLevel& value) {
    write<Enum8>(static_cast<uint8_t>(value));
}

template <>
DoorLockSoundVolume ZclStream::read<DoorLockSoundVolume>() {
    return static_cast<DoorLockSoundVolume>(static_cast<uint8_t>(read<Uint8>()));
}

template <>
void ZclOutputStream::write<DoorLockSoundVolume>(const DoorLockSoundVolume& value) {
    write<Uint8>(static_cast<uint8_t>(value));
}

template <>
DrlkOperEventSource ZclStream::read<DrlkOperEventSource>() {
    return static_cast<DrlkOperEventSource>(static_cast<uint8_t>(read<Uint8>()));
}

template <>
void ZclOutputStream::write<DrlkOperEventSource>(const DrlkOperEventSource& value) {
    write<Uint8>(static_cast<uint8_t>(value));
}

template <>
DrlkOperMode ZclStream::read<DrlkOperMode>() {
    return static_cast<DrlkOperMode>(static_cast<uint8_t>(read<Enum8>()));
}

template <>
void ZclOutputStream::write<DrlkOperMode>(const DrlkOperMode& value) {
    write<Enum8>(static_cast<uint8_t>(value));
}

template <>
DrlkPassFailStatus ZclStream::read<DrlkPassFailStatus>() {
    return static_cast<DrlkPassFailStatus>(static_cast<uint8_t>(read<Uint8>()));
}

template <>
void ZclOutputStream::write<DrlkPassFailStatus>(const DrlkPassFailStatus& value) {
    write<Uint8>(static_cast<uint8_t>(value));
}

template <>
DrlkSetCodeStatus ZclStream::read<DrlkSetCodeStatus>() {
    return static_cast<DrlkSetCodeStatus>(static_cast<uint8_t>(read<Uint8>()));
}

template <>
void ZclOutputStream::write<DrlkSetCodeStatus>(const DrlkSetCodeStatus& value) {
    write<Uint8>(static_cast<uint8_t>(value));
}

template <>
DrlkSettableUserStatus ZclStream::read<DrlkSettableUserStatus>() {
    return static_cast<DrlkSettableUserStatus>(static_cast<uint8_t>(read<Uint8>()));
}

template <>
void ZclOutputStream::write<DrlkSettableUserStatus>(const DrlkSettableUserStatus& value) {
    write<Uint8>(static_cast<uint8_t>(value));
}

template <>
DrlkUserStatus ZclStream::read<DrlkUserStatus>() {
    return static_cast<DrlkUserStatus>(static_cast<uint8_t>(read<Uint8>()));
}

template <>
void ZclOutputStream::write<DrlkUserStatus>(const DrlkUserStatus& value) {
    write<Uint8>(static_cast<uint8_t>(value));
}

template <>
DrlkUserType ZclStream::read<DrlkUserType>() {
    return static_cast<DrlkUserType>(static_cast<uint8_t>(read<Enum8>()));
}

template <>
void ZclOutputStream::write<DrlkUserType>(const DrlkUserType& value) {
    write<Enum8>(static_cast<uint8_t>(value));
}

template <>
GetLogRecordResponseEventType ZclStream::read<GetLogRecordResponseEventType>() {
    return static_cast<GetLogRecordResponseEventType>(static_cast<uint8_t>(read<Enum8>()));
}

template <>
void ZclOutputStream::write<GetLogRecordResponseEventType>(const GetLogRecordResponseEventType& value) {
    write<Enum8>(static_cast<uint8_t>(value));
}

template <>
IASZoneZoneState ZclStream::read<IASZoneZoneState>() {
    return static_cast<IASZoneZoneState>(static_cast<uint8_t>(read<Enum8>()));
}

template <>
void ZclOutputStream::write<IASZoneZoneState>(const IASZoneZoneState& value) {
    write<Enum8>(static_cast<uint8_t>(value));
}

template <>
IasZoneType ZclStream::read<IasZoneType>() {
    return static_cast<IasZoneType>(static_cast<uint16_t>(read<Enum16>()));
}

template <>
void ZclOutputStream::write<IasZoneType>(const IasZoneType& value) {
    write<Enum16>(static_cast<uint16_t>(value));
}

template <>
IlluminanceMeasurementLightSensorType ZclStream::read<IlluminanceMeasurementLightSensorType>() {
    return static_cast<IlluminanceMeasurementLightSensorType>(static_cast<uint8_t>(read<Enum8>()));
}

template <>
void ZclOutputStream::write<IlluminanceMeasurementLightSensorType>(const IlluminanceMeasurementLightSensorType& value) {
    write<Enum8>(static_cast<uint8_t>(value));
}

template <>
MoveStepMode ZclStream::read<MoveStepMode>() {
    return static_cast<MoveStepMode>(static_cast<uint8_t>(read<Enum8>()));
}

template <>
void ZclOutputStream::write<MoveStepMode>(const MoveStepMode& value) {
    write<Enum8>(static_cast<uint8_t>(value));
}

template <>
OccupancySensingOccupancySensorType ZclStream::read<OccupancySensingOccupancySensorType>() {
    return static_cast<OccupancySensingOccupancySensorType>(static_cast<uint8_t>(read<Enum8>()));
}

template <>
void ZclOutputStream::write<OccupancySensingOccupancySensorType>(const OccupancySensingOccupancySensorType& value) {
    write<Enum8>(static_cast<uint8_t>(value));
}

template <>
OffWithEffectEffectIdentifier ZclStream::read<OffWithEffectEffectIdentifier>() {
    return static_cast<OffWithEffectEffectIdentifier>(static_cast<uint8_t>(read<Uint8>()));
}

template <>
void ZclOutputStream::write<OffWithEffectEffectIdentifier>(const OffWithEffectEffectIdentifier& value) {
    write<Uint8>(static_cast<uint8_t>(value));
}

template <>
OnOffStartUpOnOff ZclStream::read<OnOffStartUpOnOff>() {
    return static_cast<OnOffStartUpOnOff>(static_cast<uint8_t>(read<Enum8>()));
}

template <>
void ZclOutputStream::write<OnOffStartUpOnOff>(const OnOffStartUpOnOff& value) {
    write<Enum8>(static_cast<uint8_t>(value));
}

template <>
OperatingEventNotificationOperationEventCode ZclStream::read<OperatingEventNotificationOperationEventCode>() {
    return static_cast<OperatingEventNotificationOperationEventCode>(static_cast<uint8_t>(read<Uint8>()));
}

template <>
void ZclOutputStream::write<OperatingEventNotificationOperationEventCode>(const OperatingEventNotificationOperationEventCode& value) {
    write<Uint8>(static_cast<uint8_t>(value));
}

template <>
ProgrammingEventNotificationProgramEventCode ZclStream::read<ProgrammingEventNotificationProgramEventCode>() {
    return static_cast<ProgrammingEventNotificationProgramEventCode>(static_cast<uint8_t>(read<Uint8>()));
}

template <>
void ZclOutputStream::write<ProgrammingEventNotificationProgramEventCode>(const ProgrammingEventNotificationProgramEventCode& value) {
    write<Uint8>(static_cast<uint8_t>(value));
}

template <>
ProgrammingEventNotificationProgramEventSource ZclStream::read<ProgrammingEventNotificationProgramEventSource>() {
    return static_cast<ProgrammingEventNotificationProgramEventSource>(static_cast<uint8_t>(read<Uint8>()));
}

template <>
void ZclOutputStream::write<ProgrammingEventNotificationProgramEventSource>(const ProgrammingEventNotificationProgramEventSource& value) {
    write<Uint8>(static_cast<uint8_t>(value));
}

template <>
ZoneEnrollResponseEnrollResponseCode ZclStream::read<ZoneEnrollResponseEnrollResponseCode>() {
    return static_cast<ZoneEnrollResponseEnrollResponseCode>(static_cast<uint8_t>(read<Enum8>()));
}

template <>
void ZclOutputStream::write<ZoneEnrollResponseEnrollResponseCode>(const ZoneEnrollResponseEnrollResponseCode& value) {
    write<Enum8>(static_cast<uint8_t>(value));
}

template <>
AttributeReportingStatus ZclStream::read<AttributeReportingStatus>() {
    return static_cast<AttributeReportingStatus>(static_cast<uint8_t>(read<Enum8>()));
}

template <>
void ZclOutputStream::write<AttributeReportingStatus>(const AttributeReportingStatus& value) {
    write<Enum8>(static_cast<uint8_t>(value));
}

template <>
ReportingRole ZclStream::read<ReportingRole>() {
    return static_cast<ReportingRole>(static_cast<uint8_t>(read<Enum8>()));
}

template <>
void ZclOutputStream::write<ReportingRole>(const ReportingRole& value) {
    write<Enum8>(static_cast<uint8_t>(value));
}

template <>
ZclStatus ZclStream::read<ZclStatus>() {
    return static_cast<ZclStatus>(static_cast<uint8_t>(read<Enum8>()));
}

template <>
void ZclOutputStream::write<ZclStatus>(const ZclStatus& value) {
    write<Enum8>(static_cast<uint8_t>(value));
}

template <>
ZclType ZclStream::read<ZclType>() {
    return static_cast<ZclType>(static_cast<uint8_t>(read<Enum8>()));
}

template <>
void ZclOutputStream::write<ZclType>(const ZclType& value) {
    write<Enum8>(static_cast<uint8_t>(value));
}


template <>
BasicAlarmMask ZclStream::read<BasicAlarmMask>() {
    return static_cast<BasicAlarmMask>(static_cast<uint8_t>(read<Map8>()));
}

template <>
void ZclOutputStream::write<BasicAlarmMask>(const BasicAlarmMask& value) {
    write<Map8>(static_cast<uint8_t>(value));
}

template <>
BasicDisableLocalConfig ZclStream::read<BasicDisableLocalConfig>() {
    return static_cast<BasicDisableLocalConfig>(static_cast<uint8_t>(read<Map8>()));
}

template <>
void ZclOutputStream::write<BasicDisableLocalConfig>(const BasicDisableLocalConfig& value) {
    write<Map8>(static_cast<uint8_t>(value));
}

template <>
CCColorOptions ZclStream::read<CCColorOptions>() {
    return static_cast<CCColorOptions>(static_cast<uint8_t>(read<Map8>()));
}

template <>
void ZclOutputStream::write<CCColorOptions>(const CCColorOptions& value) {
    write<Map8>(static_cast<uint8_t>(value));
}

template <>
ColorControlColorCapabilities ZclStream::read<ColorControlColorCapabilities>() {
    return static_cast<ColorControlColorCapabilities>(static_cast<uint16_t>(read<Map16>()));
}

template <>
void ZclOutputStream::write<ColorControlColorCapabilities>(const ColorControlColorCapabilities& value) {
    write<Map16>(static_cast<uint16_t>(value));
}

template <>
ColorLoopSetUpdateFlags ZclStream::read<ColorLoopSetUpdateFlags>() {
    return static_cast<ColorLoopSetUpdateFlags>(static_cast<uint8_t>(read<Map8>()));
}

template <>
void ZclOutputStream::write<ColorLoopSetUpdateFlags>(const ColorLoopSetUpdateFlags& value) {
    write<Map8>(static_cast<uint8_t>(value));
}

template <>
DoorLockAlarmMask ZclStream::read<DoorLockAlarmMask>() {
    return static_cast<DoorLockAlarmMask>(static_cast<uint16_t>(read<Map16>()));
}

template <>
void ZclOutputStream::write<DoorLockAlarmMask>(const DoorLockAlarmMask& value) {
    write<Map16>(static_cast<uint16_t>(value));
}

template <>
DoorLockDefaultConfigurationRegister ZclStream::read<DoorLockDefaultConfigurationRegister>() {
    return static_cast<DoorLockDefaultConfigurationRegister>(static_cast<uint16_t>(read<Map16>()));
}

template <>
void ZclOutputStream::write<DoorLockDefaultConfigurationRegister>(const DoorLockDefaultConfigurationRegister& value) {
    write<Map16>(static_cast<uint16_t>(value));
}

template <>
DoorLockKeypadOperationEventMask ZclStream::read<DoorLockKeypadOperationEventMask>() {
    return static_cast<DoorLockKeypadOperationEventMask>(static_cast<uint16_t>(read<Map16>()));
}

template <>
void ZclOutputStream::write<DoorLockKeypadOperationEventMask>(const DoorLockKeypadOperationEventMask& value) {
    write<Map16>(static_cast<uint16_t>(value));
}

template <>
DoorLockKeypadProgrammingEventMask ZclStream::read<DoorLockKeypadProgrammingEventMask>() {
    return static_cast<DoorLockKeypadProgrammingEventMask>(static_cast<uint16_t>(read<Map16>()));
}

template <>
void ZclOutputStream::write<DoorLockKeypadProgrammingEventMask>(const DoorLockKeypadProgrammingEventMask& value) {
    write<Map16>(static_cast<uint16_t>(value));
}

template <>
DoorLockManualOperationEventMask ZclStream::read<DoorLockManualOperationEventMask>() {
    return static_cast<DoorLockManualOperationEventMask>(static_cast<uint16_t>(read<Map16>()));
}

template <>
void ZclOutputStream::write<DoorLockManualOperationEventMask>(const DoorLockManualOperationEventMask& value) {
    write<Map16>(static_cast<uint16_t>(value));
}

template <>
DoorLockRFIDOperationEventMask ZclStream::read<DoorLockRFIDOperationEventMask>() {
    return static_cast<DoorLockRFIDOperationEventMask>(static_cast<uint16_t>(read<Map16>()));
}

template <>
void ZclOutputStream::write<DoorLockRFIDOperationEventMask>(const DoorLockRFIDOperationEventMask& value) {
    write<Map16>(static_cast<uint16_t>(value));
}

template <>
DoorLockRFIDProgrammingEventMask ZclStream::read<DoorLockRFIDProgrammingEventMask>() {
    return static_cast<DoorLockRFIDProgrammingEventMask>(static_cast<uint16_t>(read<Map16>()));
}

template <>
void ZclOutputStream::write<DoorLockRFIDProgrammingEventMask>(const DoorLockRFIDProgrammingEventMask& value) {
    write<Map16>(static_cast<uint16_t>(value));
}

template <>
DoorLockRFOperationEventMask ZclStream::read<DoorLockRFOperationEventMask>() {
    return static_cast<DoorLockRFOperationEventMask>(static_cast<uint16_t>(read<Map16>()));
}

template <>
void ZclOutputStream::write<DoorLockRFOperationEventMask>(const DoorLockRFOperationEventMask& value) {
    write<Map16>(static_cast<uint16_t>(value));
}

template <>
DoorLockRFProgrammingEventMask ZclStream::read<DoorLockRFProgrammingEventMask>() {
    return static_cast<DoorLockRFProgrammingEventMask>(static_cast<uint16_t>(read<Map16>()));
}

template <>
void ZclOutputStream::write<DoorLockRFProgrammingEventMask>(const DoorLockRFProgrammingEventMask& value) {
    write<Map16>(static_cast<uint16_t>(value));
}

template <>
DoorLockSupportedOperatingModes ZclStream::read<DoorLockSupportedOperatingModes>() {
    return static_cast<DoorLockSupportedOperatingModes>(static_cast<uint16_t>(read<Map16>()));
}

template <>
void ZclOutputStream::write<DoorLockSupportedOperatingModes>(const DoorLockSupportedOperatingModes& value) {
    write<Map16>(static_cast<uint16_t>(value));
}

template <>
DrlkDaysMask ZclStream::read<DrlkDaysMask>() {
    return static_cast<DrlkDaysMask>(static_cast<uint8_t>(read<Map8>()));
}

template <>
void ZclOutputStream::write<DrlkDaysMask>(const DrlkDaysMask& value) {
    write<Map8>(static_cast<uint8_t>(value));
}

template <>
IasZoneStatus ZclStream::read<IasZoneStatus>() {
    return static_cast<IasZoneStatus>(static_cast<uint16_t>(read<Map16>()));
}

template <>
void ZclOutputStream::write<IasZoneStatus>(const IasZoneStatus& value) {
    write<Map16>(static_cast<uint16_t>(value));
}

template <>
LevelOptions ZclStream::read<LevelOptions>() {
    return static_cast<LevelOptions>(static_cast<uint8_t>(read<Map8>()));
}

template <>
void ZclOutputStream::write<LevelOptions>(const LevelOptions& value) {
    write<Map8>(static_cast<uint8_t>(value));
}

template <>
OccupancySensingOccupancy ZclStream::read<OccupancySensingOccupancy>() {
    return static_cast<OccupancySensingOccupancy>(static_cast<uint8_t>(read<Map8>()));
}

template <>
void ZclOutputStream::write<OccupancySensingOccupancy>(const OccupancySensingOccupancy& value) {
    write<Map8>(static_cast<uint8_t>(value));
}

template <>
OccupancySensingOccupancySensorTypeBitmap ZclStream::read<OccupancySensingOccupancySensorTypeBitmap>() {
    return static_cast<OccupancySensingOccupancySensorTypeBitmap>(static_cast<uint8_t>(read<Map8>()));
}

template <>
void ZclOutputStream::write<OccupancySensingOccupancySensorTypeBitmap>(const OccupancySensingOccupancySensorTypeBitmap& value) {
    write<Map8>(static_cast<uint8_t>(value));
}

template <>
OnWithTimedOffOnOffControl ZclStream::read<OnWithTimedOffOnOffControl>() {
    return static_cast<OnWithTimedOffOnOffControl>(static_cast<uint8_t>(read<Uint8>()));
}

template <>
void ZclOutputStream::write<OnWithTimedOffOnOffControl>(const OnWithTimedOffOnOffControl& value) {
    write<Uint8>(static_cast<uint8_t>(value));
}

