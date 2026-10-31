#pragma once

#include <any>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace zigbee::zcl {

struct AnyType;

struct Nodata {
    /* TYPE WARNING: nodata defaults to */ uint8_t *  value_;
    Nodata() = default;
    Nodata(/* TYPE WARNING: nodata defaults to */ uint8_t *  value)
        : value_{value}
    {}
    operator /* TYPE WARNING: nodata defaults to */ uint8_t * & () {
        return value_;
    }
    operator /* TYPE WARNING: nodata defaults to */ uint8_t *  const & () const {
        return value_;
    }
};

struct Data8 {
    uint8_t value_;
    Data8() = default;
    Data8(uint8_t value)
        : value_{value}
    {}
    operator uint8_t& () {
        return value_;
    }
    operator uint8_t const & () const {
        return value_;
    }
};

struct Data16 {
    uint16_t value_;
    Data16() = default;
    Data16(uint16_t value)
        : value_{value}
    {}
    operator uint16_t& () {
        return value_;
    }
    operator uint16_t const & () const {
        return value_;
    }
};

struct Data24 {
    uint32_t value_;
    Data24() = default;
    Data24(uint32_t value)
        : value_{value}
    {}
    operator uint32_t& () {
        return value_;
    }
    operator uint32_t const & () const {
        return value_;
    }
};

struct Data32 {
    uint32_t value_;
    Data32() = default;
    Data32(uint32_t value)
        : value_{value}
    {}
    operator uint32_t& () {
        return value_;
    }
    operator uint32_t const & () const {
        return value_;
    }
};

struct Data40 {
    uint64_t value_;
    Data40() = default;
    Data40(uint64_t value)
        : value_{value}
    {}
    operator uint64_t& () {
        return value_;
    }
    operator uint64_t const & () const {
        return value_;
    }
};

struct Data48 {
    uint64_t value_;
    Data48() = default;
    Data48(uint64_t value)
        : value_{value}
    {}
    operator uint64_t& () {
        return value_;
    }
    operator uint64_t const & () const {
        return value_;
    }
};

struct Data56 {
    uint64_t value_;
    Data56() = default;
    Data56(uint64_t value)
        : value_{value}
    {}
    operator uint64_t& () {
        return value_;
    }
    operator uint64_t const & () const {
        return value_;
    }
};

struct Data64 {
    uint64_t value_;
    Data64() = default;
    Data64(uint64_t value)
        : value_{value}
    {}
    operator uint64_t& () {
        return value_;
    }
    operator uint64_t const & () const {
        return value_;
    }
};

struct Bool {
    bool value_;
    Bool() = default;
    Bool(bool value)
        : value_{value}
    {}
    operator bool& () {
        return value_;
    }
    operator bool const & () const {
        return value_;
    }
};

struct Map8 {
    uint8_t value_;
    Map8() = default;
    Map8(uint8_t value)
        : value_{value}
    {}
    operator uint8_t& () {
        return value_;
    }
    operator uint8_t const & () const {
        return value_;
    }
};

struct Map16 {
    uint16_t value_;
    Map16() = default;
    Map16(uint16_t value)
        : value_{value}
    {}
    operator uint16_t& () {
        return value_;
    }
    operator uint16_t const & () const {
        return value_;
    }
};

struct Map24 {
    uint32_t value_;
    Map24() = default;
    Map24(uint32_t value)
        : value_{value}
    {}
    operator uint32_t& () {
        return value_;
    }
    operator uint32_t const & () const {
        return value_;
    }
};

struct Map32 {
    uint32_t value_;
    Map32() = default;
    Map32(uint32_t value)
        : value_{value}
    {}
    operator uint32_t& () {
        return value_;
    }
    operator uint32_t const & () const {
        return value_;
    }
};

struct Map40 {
    uint64_t value_;
    Map40() = default;
    Map40(uint64_t value)
        : value_{value}
    {}
    operator uint64_t& () {
        return value_;
    }
    operator uint64_t const & () const {
        return value_;
    }
};

struct Map48 {
    uint64_t value_;
    Map48() = default;
    Map48(uint64_t value)
        : value_{value}
    {}
    operator uint64_t& () {
        return value_;
    }
    operator uint64_t const & () const {
        return value_;
    }
};

struct Map56 {
    uint64_t value_;
    Map56() = default;
    Map56(uint64_t value)
        : value_{value}
    {}
    operator uint64_t& () {
        return value_;
    }
    operator uint64_t const & () const {
        return value_;
    }
};

struct Map64 {
    uint64_t value_;
    Map64() = default;
    Map64(uint64_t value)
        : value_{value}
    {}
    operator uint64_t& () {
        return value_;
    }
    operator uint64_t const & () const {
        return value_;
    }
};

struct Uint8 {
    uint8_t value_;
    Uint8() = default;
    Uint8(uint8_t value)
        : value_{value}
    {}
    operator uint8_t& () {
        return value_;
    }
    operator uint8_t const & () const {
        return value_;
    }
};

struct Uint16 {
    uint16_t value_;
    Uint16() = default;
    Uint16(uint16_t value)
        : value_{value}
    {}
    operator uint16_t& () {
        return value_;
    }
    operator uint16_t const & () const {
        return value_;
    }
};

struct Uint24 {
    uint32_t value_;
    Uint24() = default;
    Uint24(uint32_t value)
        : value_{value}
    {}
    operator uint32_t& () {
        return value_;
    }
    operator uint32_t const & () const {
        return value_;
    }
};

struct Uint32 {
    uint32_t value_;
    Uint32() = default;
    Uint32(uint32_t value)
        : value_{value}
    {}
    operator uint32_t& () {
        return value_;
    }
    operator uint32_t const & () const {
        return value_;
    }
};

struct Uint40 {
    uint64_t value_;
    Uint40() = default;
    Uint40(uint64_t value)
        : value_{value}
    {}
    operator uint64_t& () {
        return value_;
    }
    operator uint64_t const & () const {
        return value_;
    }
};

struct Uint48 {
    uint64_t value_;
    Uint48() = default;
    Uint48(uint64_t value)
        : value_{value}
    {}
    operator uint64_t& () {
        return value_;
    }
    operator uint64_t const & () const {
        return value_;
    }
};

struct Uint56 {
    uint64_t value_;
    Uint56() = default;
    Uint56(uint64_t value)
        : value_{value}
    {}
    operator uint64_t& () {
        return value_;
    }
    operator uint64_t const & () const {
        return value_;
    }
};

struct Uint64 {
    uint64_t value_;
    Uint64() = default;
    Uint64(uint64_t value)
        : value_{value}
    {}
    operator uint64_t& () {
        return value_;
    }
    operator uint64_t const & () const {
        return value_;
    }
};

struct Int8 {
    int8_t value_;
    Int8() = default;
    Int8(int8_t value)
        : value_{value}
    {}
    operator int8_t& () {
        return value_;
    }
    operator int8_t const & () const {
        return value_;
    }
};

struct Int16 {
    int16_t value_;
    Int16() = default;
    Int16(int16_t value)
        : value_{value}
    {}
    operator int16_t& () {
        return value_;
    }
    operator int16_t const & () const {
        return value_;
    }
};

struct Int24 {
    int32_t value_;
    Int24() = default;
    Int24(int32_t value)
        : value_{value}
    {}
    operator int32_t& () {
        return value_;
    }
    operator int32_t const & () const {
        return value_;
    }
};

struct Int32 {
    int32_t value_;
    Int32() = default;
    Int32(int32_t value)
        : value_{value}
    {}
    operator int32_t& () {
        return value_;
    }
    operator int32_t const & () const {
        return value_;
    }
};

struct Int40 {
    int64_t value_;
    Int40() = default;
    Int40(int64_t value)
        : value_{value}
    {}
    operator int64_t& () {
        return value_;
    }
    operator int64_t const & () const {
        return value_;
    }
};

struct Int48 {
    int64_t value_;
    Int48() = default;
    Int48(int64_t value)
        : value_{value}
    {}
    operator int64_t& () {
        return value_;
    }
    operator int64_t const & () const {
        return value_;
    }
};

struct Int56 {
    int64_t value_;
    Int56() = default;
    Int56(int64_t value)
        : value_{value}
    {}
    operator int64_t& () {
        return value_;
    }
    operator int64_t const & () const {
        return value_;
    }
};

struct Int64 {
    int64_t value_;
    Int64() = default;
    Int64(int64_t value)
        : value_{value}
    {}
    operator int64_t& () {
        return value_;
    }
    operator int64_t const & () const {
        return value_;
    }
};

struct Enum8 {
    uint8_t value_;
    Enum8() = default;
    Enum8(uint8_t value)
        : value_{value}
    {}
    operator uint8_t& () {
        return value_;
    }
    operator uint8_t const & () const {
        return value_;
    }
};

struct Enum16 {
    uint16_t value_;
    Enum16() = default;
    Enum16(uint16_t value)
        : value_{value}
    {}
    operator uint16_t& () {
        return value_;
    }
    operator uint16_t const & () const {
        return value_;
    }
};

struct Semi {
    float value_;
    Semi() = default;
    Semi(float value)
        : value_{value}
    {}
    operator float& () {
        return value_;
    }
    operator float const & () const {
        return value_;
    }
};

struct Single {
    float value_;
    Single() = default;
    Single(float value)
        : value_{value}
    {}
    operator float& () {
        return value_;
    }
    operator float const & () const {
        return value_;
    }
};

struct Double {
    double value_;
    Double() = default;
    Double(double value)
        : value_{value}
    {}
    operator double& () {
        return value_;
    }
    operator double const & () const {
        return value_;
    }
};

struct Octstr {
    std::string value_;
    Octstr() = default;
    Octstr(std::string value)
        : value_{value}
    {}
    operator std::string& () {
        return value_;
    }
    operator std::string const & () const {
        return value_;
    }
};

struct String {
    std::string value_;
    String() = default;
    String(std::string value)
        : value_{value}
    {}
    operator std::string& () {
        return value_;
    }
    operator std::string const & () const {
        return value_;
    }
};

struct Octstr16 {
    std::string value_;
    Octstr16() = default;
    Octstr16(std::string value)
        : value_{value}
    {}
    operator std::string& () {
        return value_;
    }
    operator std::string const & () const {
        return value_;
    }
};

struct String16 {
    std::string value_;
    String16() = default;
    String16(std::string value)
        : value_{value}
    {}
    operator std::string& () {
        return value_;
    }
    operator std::string const & () const {
        return value_;
    }
};

struct Array {
    /* TYPE WARNING: array array defaults to */ uint8_t *  value_;
    Array() = default;
    Array(/* TYPE WARNING: array array defaults to */ uint8_t *  value)
        : value_{value}
    {}
    operator /* TYPE WARNING: array array defaults to */ uint8_t * & () {
        return value_;
    }
    operator /* TYPE WARNING: array array defaults to */ uint8_t *  const & () const {
        return value_;
    }
};

struct Struct {
    std::vector<AnyType> value_;
    Struct() = default;
    Struct(std::vector<AnyType> value)
        : value_{value}
    {}
    operator std::vector<AnyType>& () {
        return value_;
    }
    operator std::vector<AnyType> const & () const {
        return value_;
    }
};

struct Set {
    /* TYPE WARNING: set defaults to */ uint8_t *  value_;
    Set() = default;
    Set(/* TYPE WARNING: set defaults to */ uint8_t *  value)
        : value_{value}
    {}
    operator /* TYPE WARNING: set defaults to */ uint8_t * & () {
        return value_;
    }
    operator /* TYPE WARNING: set defaults to */ uint8_t *  const & () const {
        return value_;
    }
};

struct Bag {
    /* TYPE WARNING: bag defaults to */ uint8_t *  value_;
    Bag() = default;
    Bag(/* TYPE WARNING: bag defaults to */ uint8_t *  value)
        : value_{value}
    {}
    operator /* TYPE WARNING: bag defaults to */ uint8_t * & () {
        return value_;
    }
    operator /* TYPE WARNING: bag defaults to */ uint8_t *  const & () const {
        return value_;
    }
};

struct ToD {
    uint32_t value_;
    ToD() = default;
    ToD(uint32_t value)
        : value_{value}
    {}
    operator uint32_t& () {
        return value_;
    }
    operator uint32_t const & () const {
        return value_;
    }
};

struct Date {
    uint32_t value_;
    Date() = default;
    Date(uint32_t value)
        : value_{value}
    {}
    operator uint32_t& () {
        return value_;
    }
    operator uint32_t const & () const {
        return value_;
    }
};

struct Utc {
    uint32_t value_;
    Utc() = default;
    Utc(uint32_t value)
        : value_{value}
    {}
    operator uint32_t& () {
        return value_;
    }
    operator uint32_t const & () const {
        return value_;
    }
};

struct ClusterId {
    uint16_t value_;
    ClusterId() = default;
    ClusterId(uint16_t value)
        : value_{value}
    {}
    operator uint16_t& () {
        return value_;
    }
    operator uint16_t const & () const {
        return value_;
    }
};

struct AttribId {
    uint16_t value_;
    AttribId() = default;
    AttribId(uint16_t value)
        : value_{value}
    {}
    operator uint16_t& () {
        return value_;
    }
    operator uint16_t const & () const {
        return value_;
    }
};

struct BacOID {
    uint32_t value_;
    BacOID() = default;
    BacOID(uint32_t value)
        : value_{value}
    {}
    operator uint32_t& () {
        return value_;
    }
    operator uint32_t const & () const {
        return value_;
    }
};

struct Eui64 {
    std::array<uint8_t, 8> value_;
    Eui64() = default;
    Eui64(std::array<uint8_t, 8> value)
        : value_{value}
    {}
    operator std::array<uint8_t, 8>& () {
        return value_;
    }
    operator std::array<uint8_t, 8> const & () const {
        return value_;
    }
};

struct Key128 {
    std::array<uint8_t, 16> value_;
    Key128() = default;
    Key128(std::array<uint8_t, 16> value)
        : value_{value}
    {}
    operator std::array<uint8_t, 16>& () {
        return value_;
    }
    operator std::array<uint8_t, 16> const & () const {
        return value_;
    }
};

struct Unk {
    std::any value_;
    Unk() = default;
    template <typename T>
    Unk(T&& value)
        : value_{std::forward<T>(value)}
    {}
    operator std::any& () {
        return value_;
    }
    operator std::any const & () const {
        return value_;
    }
};


enum class BasicGenericDeviceClass : uint8_t {
    Lighting = 0x00,
};

enum class BasicGenericDeviceType : uint8_t {
    Incandescent = 0x00,
    SpotlightHalogen = 0x01,
    HalogenBulb = 0x02,
    Cfl = 0x03,
    LinearFluorescent = 0x04,
    LEDBulb = 0x05,
    SpotlightLED = 0x06,
    LEDStrip = 0x07,
    LEDTube = 0x08,
    GenericIndoorLuminaireOrLightFixture = 0x09,
    GenericOutdoorLuminaireOrLightFixture = 0x0A,
    PendantLuminaireOrLightFixture = 0x0B,
    FloorStandingLuminaireOrLightFixture = 0x0C,
    GenericController = 0xE0,
    WallSwitch = 0xE1,
    PortableRemoteController = 0xE2,
    MotionSensorOrLightSensor = 0xE3,
    GenericActuator = 0xF0,
    WallSocket = 0xF1,
    GatewayOrBridge = 0xF2,
    PlugInUnit = 0xF3,
    RetrofitActuator = 0xF4,
    Unspecified = 0xFF,
};

enum class BasicPhysicalEnvironment : uint8_t {
    UnspecifiedEnvironment = 0x00,
    Deprecated = 0x01,
    Bar = 0x02,
    Courtyard = 0x03,
    Bathroom = 0x04,
    Bedroom = 0x05,
    BilliardRoom = 0x06,
    UtilityRoom = 0x07,
    Cellar = 0x08,
    StorageCloset = 0x09,
    Theater = 0x0A,
    Office = 0x0B,
    Deck = 0x0C,
    Den = 0x0D,
    DiningRoom = 0x0E,
    ElectricalRoom = 0x0F,
    Elevator = 0x10,
    Entry = 0x11,
    FamilyRoom = 0x12,
    MainFloor = 0x13,
    Upstairs = 0x14,
    Downstairs = 0x15,
    BasementLowerLevel = 0x16,
    Gallery = 0x17,
    GameRoom = 0x18,
    Garage = 0x19,
    Gym = 0x1A,
    Hallway = 0x1B,
    House = 0x1C,
    Kitchen = 0x1D,
    LaundryRoom = 0x1E,
    Library = 0x1F,
    MasterBedroom = 0x20,
    MudRoom = 0x21,
    Nursery = 0x22,
    Pantry = 0x23,
    Office2 = 0x24,
    Outside = 0x25,
    Pool = 0x26,
    Porch = 0x27,
    SewingRoom = 0x28,
    SittingRoom = 0x29,
    Stairway = 0x2A,
    Yard = 0x2B,
    Attic = 0x2C,
    HotTub = 0x2D,
    LivingRoom = 0x2E,
    Sauna = 0x2F,
    ShopOrWorkshop = 0x30,
    GuestBedroom = 0x31,
    GuestBath = 0x32,
    PowderRoom = 0x33,
    BackYard = 0x34,
    FrontYard = 0x35,
    Patio = 0x36,
    Driveway = 0x37,
    SunRoom = 0x38,
    LivingRoom2 = 0x39,
    Spa = 0x3A,
    Whirlpool = 0x3B,
    Shed = 0x3C,
    EquipmentStorage = 0x3D,
    HobbyOrCraftRoom = 0x3E,
    Fountain = 0x3F,
    Pond = 0x40,
    ReceptionRoom = 0x41,
    BreakfastRoom = 0x42,
    Nook = 0x43,
    Garden = 0x44,
    Balcony = 0x45,
    PanicRoom = 0x46,
    Terrace = 0x47,
    Roof = 0x48,
    Toilet = 0x49,
    ToiletMain = 0x4A,
    OutsideToilet = 0x4B,
    ShowerRoom = 0x4C,
    Study = 0x4D,
    FrontGarden = 0x4E,
    BackGarden = 0x4F,
    Kettle = 0x50,
    Television = 0x51,
    Stove = 0x52,
    Microwave = 0x53,
    Toaster = 0x54,
    Vacuum = 0x55,
    Appliance = 0x56,
    FrontDoor = 0x57,
    BackDoor = 0x58,
    FridgeDoor = 0x59,
    MedicationCabinetDoor = 0x60,
    WardrobeDoor = 0x61,
    FrontCupboardDoor = 0x62,
    OtherDoor = 0x63,
    WaitingRoom = 0x64,
    TriageRoom = 0x65,
    DoctorsOffice = 0x66,
    PatientsPrivateRoom = 0x67,
    ConsultationRoom = 0x68,
    NurseStation = 0x69,
    Ward = 0x6A,
    Corridor = 0x6B,
    OperatingTheatre = 0x6C,
    DentalSurgeryRoom = 0x6D,
    MedicalImagingRoom = 0x6E,
    DecontaminationRoom = 0x6F,
    Atrium = 0x70,
    Mirror = 0x71,
    UnknownEnvironment = 0xFF,
};

enum class BasicPowerSource : uint8_t {
    Unknown = 0x00,
    SinglePhaseMains = 0x01,
    ThreePhaseMains = 0x02,
    Battery = 0x03,
    DCSource = 0x04,
    EmergencyMainsConstantlyPowered = 0x05,
    EmergencyMainsAndTransferSwitch = 0x06,
    UnknownWithBatteryBackup = 0x80,
    SinglePhaseMainsWithBatteryBackup = 0x81,
    ThreePhaseMainsWithBatteryBackup = 0x82,
    BatteryWithBatteryBackup = 0x83,
    DCSourceWithBatteryBackup = 0x84,
    EmergencyMainsConstantlyPoweredWithBatteryBackup = 0x85,
    EmergencyMainsAndTransferSwitchWithBatteryBackup = 0x86,
};

enum class CCColorLoopDirection : uint8_t {
    DecrementEnhancedCurrentHue = 0x00,
    IncrementEnhancedCurrentHue = 0x01,
};

enum class CCDirection : uint8_t {
    ShortestDistance = 0x00,
    LongestDistance = 0x01,
    Up = 0x02,
    Down = 0x03,
};

enum class CCMoveMode : uint8_t {
    Stop = 0x00,
    Up = 0x01,
    Down = 0x03,
};

enum class CCStepMode : uint8_t {
    Up = 0x01,
    Down = 0x03,
};

enum class ColorControlColorLoopActive : uint8_t {
    ColorLoopInactive = 0x00,
    ColorLoopActive = 0x01,
};

enum class ColorControlColorMode : uint8_t {
    CurrentHueAndCurrentSaturation = 0x00,
    CurrentXAndCurrentY = 0x01,
    ColorTemperatureMireds = 0x02,
};

enum class ColorControlDriftCompensation : uint8_t {
    None = 0x00,
    OtherOrUnknown = 0x01,
    TemperatureMonitoring = 0x02,
    OpticalLuminanceMonitoringAndFeedback = 0x03,
    OpticalColorMonitoringAndFeedback = 0x04,
};

enum class ColorControlEnhancedColorMode : uint8_t {
    CurrentHueAndCurrentSaturation = 0x00,
    CurrentXAndCurrentY = 0x01,
    ColorTemperatureMireds = 0x02,
    EnhancedCurrentHueAndCurrentSaturation = 0x03,
};

enum class ColorLoopSetAction : uint8_t {
    DeactivateColorLoop = 0x00,
    ActivateColorLoopFromColorLoopStartEnhancedHue = 0x01,
    ActivateColorLoopFromEnhancedCurrentHue = 0x02,
};

enum class DoorLockDoorState : uint8_t {
    Open = 0x00,
    Closed = 0x01,
    ErrorJammed = 0x02,
    ErrorForcedOpen = 0x03,
    ErrorUnspecified = 0x04,
    Undefined = 0xFF,
};

enum class DoorLockLEDSettings : uint8_t {
    NeverUseLED = 0x00,
    UseLEDExceptForAccessAllowed = 0x01,
    UseLEDForAllEvents = 0x02,
};

enum class DoorLockLockState : uint8_t {
    NotFullyLocked = 0x00,
    Locked = 0x01,
    Unlocked = 0x02,
    Undefined = 0xFF,
};

enum class DoorLockLockType : uint8_t {
    DeadBolt = 0x00,
    Magnetic = 0x01,
    Other = 0x02,
    Mortise = 0x03,
    Rim = 0x04,
    LatchBolt = 0x05,
    CylindricalLock = 0x06,
    TubularLock = 0x07,
    InterconnectedLock = 0x08,
    DeadLatch = 0x09,
    DoorFurniture = 0x0A,
};

enum class DoorLockSecurityLevel : uint8_t {
    Network = 0x00,
    Aps = 0x01,
};

enum class DoorLockSoundVolume : uint8_t {
    SilentMode = 0x00,
    LowVolume = 0x01,
    HighVolume = 0x02,
};

enum class DrlkOperEventSource : uint8_t {
    Keypad = 0x00,
    Rf = 0x01,
    Manual = 0x02,
    Rfid = 0x03,
    Indeterminate = 0xFF,
};

enum class DrlkOperMode : uint8_t {
    Normal = 0x00,
    Vacation = 0x01,
    Privacy = 0x02,
    NoRFLockOrUnlock = 0x03,
    Passage = 0x04,
};

enum class DrlkPassFailStatus : uint8_t {
    Pass = 0x00,
    Fail = 0x01,
};

enum class DrlkSetCodeStatus : uint8_t {
    Success = 0x00,
    GeneralFailure = 0x01,
    MemoryFull = 0x02,
    DuplicateCode = 0x03,
};

enum class DrlkSettableUserStatus : uint8_t {
    OccupiedEnabled = 0x01,
    OccupiedDisabled = 0x03,
};

enum class DrlkUserStatus : uint8_t {
    Available = 0x00,
    OccupiedEnabled = 0x01,
    OccupiedDisabled = 0x03,
    NotSupported = 0xFF,
};

enum class DrlkUserType : uint8_t {
    UnrestrictedUser = 0x00,
    YearDayScheduleUser = 0x01,
    WeekDayScheduleUser = 0x02,
    MasterUser = 0x03,
    NonAccessUser = 0x04,
    NotSupported = 0xFF,
};

enum class GetLogRecordResponseEventType : uint8_t {
    Operation = 0x00,
    Programming = 0x01,
    Alarm = 0x02,
};

enum class IASZoneZoneState : uint8_t {
    NotEnrolled = 0x00,
    Enrolled = 0x01,
};

enum class IasZoneType : uint16_t {
    StandardCIE = 0x00,
    MotionSensor = 0x0D,
    ContactSwitch = 0x15,
    DoorOrWindowHandle = 0x16,
    FireSensor = 0x28,
    WaterSensor = 0x2A,
    CarbonMonoxideSensor = 0x2B,
    PersonalEmergencyDevice = 0x2C,
    VibrationOrMovementSensor = 0x2D,
    RemoteControl = 0x10F,
    KeyFob = 0x115,
    Keypad = 0x21D,
    StandardWarningDevice = 0x225,
    GlassBreakSensor = 0x226,
    SecurityRepeater = 0x229,
    Invalid = 0xFFFF,
};

enum class IlluminanceMeasurementLightSensorType : uint8_t {
    Photodiode = 0x00,
    Cmos = 0x01,
    Unknown = 0xFF,
};

enum class MoveStepMode : uint8_t {
    Up = 0x00,
    Down = 0x01,
};

enum class OccupancySensingOccupancySensorType : uint8_t {
    Pir = 0x00,
    Ultrasonic = 0x01,
    PIRAndUltrasonic = 0x02,
    PhysicalContact = 0x03,
};

enum class OffWithEffectEffectIdentifier : uint8_t {
    DelayedAllOff = 0x00,
    DyingLight = 0x01,
};

enum class OnOffStartUpOnOff : uint8_t {
    SetOnOffTo0 = 0x00,
    SetOnOffTo1 = 0x01,
    TogglePreviousOnOff = 0x02,
    SetPreviousOnOff = 0xFF,
};

enum class OperatingEventNotificationOperationEventCode : uint8_t {
    UnknownOrMS = 0x00,
    Lock = 0x01,
    Unlock = 0x02,
    LockFailureInvalidPINOrID = 0x03,
    LockFailureInvalidSchedule = 0x04,
    UnlockFailureInvalidPINOrID = 0x05,
    UnlockFailureInvalidSchedule = 0x06,
    OneTouchLock = 0x07,
    KeyLock = 0x08,
    KeyUnlock = 0x09,
    AutoLock = 0x0A,
    ScheduleLock = 0x0B,
    ScheduleUnlock = 0x0C,
    ManualLock = 0x0D,
    ManualUnlock = 0x0E,
    NonAccessUserOperationalEvent = 0x0E,
};

enum class ProgrammingEventNotificationProgramEventCode : uint8_t {
    UnknownOrMS = 0x00,
    MasterCodeChanged = 0x01,
    PINCodeAdded = 0x02,
    PINCodeDeleted = 0x03,
    PINCodeChanged = 0x04,
    RFIDCodeAdded = 0x05,
    RFIDCodeDeleted = 0x06,
};

enum class ProgrammingEventNotificationProgramEventSource : uint8_t {
    Keypad = 0x00,
    Rf = 0x01,
    Rfid = 0x03,
    Indeterminate = 0xFF,
};

enum class ZoneEnrollResponseEnrollResponseCode : uint8_t {
    Success = 0x00,
    NotSupported = 0x01,
    NoEnrollPermit = 0x02,
    TooManyZones = 0x03,
};

enum class AttributeReportingStatus : uint8_t {
    Pending = 0x00,
    Complete = 0x01,
};

enum class ReportingRole : uint8_t {
    Generator = 0x00,
    Recipient = 0x01,
};

enum class ZclStatus : uint8_t {
    Success = 0x00,
    Failure = 0x01,
    NotAuthorized = 0x7E,
    MalformedCommand = 0x80,
    UnsupClusterCommand = 0x81,
    UnsupGeneralCommand = 0x82,
    UnsupManufClusterCommand = 0x83,
    UnsupManufGeneralCommand = 0x84,
    InvalidField = 0x85,
    UnsupportedAttribute = 0x86,
    InvalidValue = 0x87,
    ReadOnly = 0x88,
    InsufficientSpace = 0x89,
    DuplicateExists = 0x8A,
    NotFound = 0x8B,
    UnreportableAttribute = 0x8C,
    InvalidDataType = 0x8D,
    InvalidSelector = 0x8E,
    WriteOnly = 0x8F,
    InconsistentStartupState = 0x90,
    DefinedOutOfBand = 0x91,
    Inconsistent = 0x92,
    ActionDenied = 0x93,
    Timeout = 0x94,
    Abort = 0x95,
    InvalidImage = 0x96,
    WaitForData = 0x97,
    NoImageAvailable = 0x98,
    RequireMoreImage = 0x99,
    NotificationPending = 0x9A,
    HardwareFailure = 0xC0,
    SoftwareFailure = 0xC1,
    CalibrationError = 0xC2,
    UnsupportedCluster = 0xC3,
    LimitReached = 0xC4,
};

enum class ZclType : uint8_t {
    Nodata = 0x00,
    Data8 = 0x08,
    Data16 = 0x09,
    Data24 = 0x0A,
    Data32 = 0x0B,
    Data40 = 0x0C,
    Data48 = 0x0D,
    Data56 = 0x0E,
    Data64 = 0x0F,
    Bool = 0x10,
    Map8 = 0x18,
    Map16 = 0x19,
    Map24 = 0x1A,
    Map32 = 0x1B,
    Map40 = 0x1C,
    Map48 = 0x1D,
    Map56 = 0x1E,
    Map64 = 0x1F,
    Uint8 = 0x20,
    Uint16 = 0x21,
    Uint24 = 0x22,
    Uint32 = 0x23,
    Uint40 = 0x24,
    Uint48 = 0x25,
    Uint56 = 0x26,
    Uint64 = 0x27,
    Int8 = 0x28,
    Int16 = 0x29,
    Int24 = 0x2A,
    Int32 = 0x2B,
    Int40 = 0x2C,
    Int48 = 0x2D,
    Int56 = 0x2E,
    Int64 = 0x2F,
    Enum8 = 0x30,
    Enum16 = 0x31,
    Semi = 0x38,
    Single = 0x39,
    Double = 0x3A,
    Octstr = 0x41,
    String = 0x42,
    Octstr16 = 0x43,
    String16 = 0x44,
    Array = 0x48,
    Struct = 0x4C,
    Set = 0x50,
    Bag = 0x51,
    ToD = 0xE0,
    Date = 0xE1,
    Utc = 0xE2,
    ClusterId = 0xE8,
    AttribId = 0xE9,
    BacOID = 0xEA,
    Eui64 = 0xF0,
    Key128 = 0xF1,
    Unk = 0xFF,
};


enum class BasicAlarmMask : uint8_t {
    GeneralHardwareFault = 0x01,
    GeneralSoftwareFault = 0x02,
};

enum class BasicDisableLocalConfig : uint8_t {
    DisableResetToFactoryDefaults = 0x01,
    DisableDeviceConfiguration = 0x02,
};

enum class CCColorOptions : uint8_t {
    ExecuteIfOff = 0x01,
};

enum class ColorControlColorCapabilities : uint16_t {
    HueSaturationSupported = 0x0001,
    EnhancedHueSupported = 0x0002,
    ColorLoopSupported = 0x0004,
    XYSupported = 0x0008,
    ColorTemperatureSupported = 0x0010,
};

enum class ColorLoopSetUpdateFlags : uint8_t {
    UpdateAction = 0x01,
    UpdateDirection = 0x02,
    UpdateTime = 0x04,
    UpdateStartHue = 0x08,
};

enum class DoorLockAlarmMask : uint16_t {
    DeadboltJammed = 0x0001,
    LockResetToFactoryDefaults = 0x0002,
    RFPowerModuleCycled = 0x0008,
    TamperAlarmWrongCodeEntryLimit = 0x0010,
    TamperAlarmFrontEscutcheonRemovedFromMain = 0x0020,
    ForcedDoorOpenUnderDoorLockedCondition = 0x0040,
};

enum class DoorLockDefaultConfigurationRegister : uint16_t {
    DefaultEnableLocalProgrammingAttributeIsEnabled = 0x0001,
    DefaultKeypadInterfaceIsEnabled = 0x0002,
    DefaultRFInterfaceIsEnabled = 0x0004,
    DefaultSoundVolumeIsEnabled = 0x0020,
    DefaultAutoRelockTimeIsEnabled = 0x0040,
    DefaultLEDSettingsIsEnabled = 0x0080,
};

enum class DoorLockKeypadOperationEventMask : uint16_t {
    KeypadOpUnknownOrMS = 0x0001,
    KeypadOpLock = 0x0002,
    KeypadOpUnlock = 0x0004,
    KeypadOpLockErrorInvalidPIN = 0x0008,
    KeypadOpLockErrorInvalidSchedule = 0x0010,
    KeypadOpUnlockInvalidPIN = 0x0020,
    KeypadOpUnlockInvalidSchedule = 0x0040,
    KeypadOpNonAccessUser = 0x0080,
};

enum class DoorLockKeypadProgrammingEventMask : uint16_t {
    KeypadProgUnknownOrMS = 0x0001,
    KeypadProgMasterCodeChanged = 0x0002,
    KeypadProgPINAdded = 0x0004,
    KeypadProgPINDeleted = 0x0008,
    KeypadProgPINChanged = 0x0010,
};

enum class DoorLockManualOperationEventMask : uint16_t {
    ManualOpUnknownOrMS = 0x0001,
    ManualOpThumbturnLock = 0x0002,
    ManualOpThumbturnUnlock = 0x0004,
    ManualOpOneTouchLock = 0x0008,
    ManualOpKeyLock = 0x0010,
    ManualOpKeyUnlock = 0x0020,
    ManualOpAutoLock = 0x0040,
    ManualOpScheduleLock = 0x0080,
    ManualOpScheduleUnlock = 0x0100,
    ManualOpLock = 0x0200,
    ManualOpUnlock = 0x0400,
};

enum class DoorLockRFIDOperationEventMask : uint16_t {
    RFIDOpUnknownOrMS = 0x0001,
    RFIDOpLock = 0x0002,
    RFIDOpUnlock = 0x0004,
    RFIDOpLockErrorInvalidRFID = 0x0008,
    RFIDOpLockErrorInvalidSchedule = 0x0010,
    RFIDOpUnlockErrorInvalidRFID = 0x0020,
    RFIDOpUnlockErrorInvalidSchedule = 0x0040,
};

enum class DoorLockRFIDProgrammingEventMask : uint16_t {
    RFIDProgUnknownOrMS = 0x0001,
    RFIDProgRFIDAdded = 0x0020,
    RFIDProgRFIDDeleted = 0x0040,
};

enum class DoorLockRFOperationEventMask : uint16_t {
    RFOpUnknownOrMS = 0x0001,
    RFOpLock = 0x0002,
    RFOpUnlock = 0x0004,
    RFOpLockErrorInvalidCode = 0x0008,
    RFOpLockErrorInvalidSchedule = 0x0010,
    RFOpUnlockInvalidCode = 0x0020,
    RFOpUnlockInvalidSchedule = 0x0040,
};

enum class DoorLockRFProgrammingEventMask : uint16_t {
    RFProgUnknownOrMS = 0x0001,
    RFProgPINAdded = 0x0004,
    RFProgPINDeleted = 0x0008,
    RFProgPINChanged = 0x0010,
    RFProgRFIDAdded = 0x0020,
    RFProgRFIDDeleted = 0x0040,
};

enum class DoorLockSupportedOperatingModes : uint16_t {
    NormalModeSupported = 0x0001,
    VacationModeSupported = 0x0002,
    PrivacyModeSupported = 0x0004,
    NoRFLockOrUnlockModeSupported = 0x0008,
    PassageModeSupported = 0x0010,
};

enum class DrlkDaysMask : uint8_t {
    Sun = 0x01,
    Mon = 0x02,
    Tue = 0x04,
    Wed = 0x08,
    Thu = 0x10,
    Fri = 0x20,
    Sat = 0x40,
    Enable = 0x80,
};

enum class IasZoneStatus : uint16_t {
    Alarm1 = 0x0001,
    Alarm2 = 0x0002,
    Tamper = 0x0004,
    BatteryLow = 0x0008,
    SupervisionNotify = 0x0010,
    RestoreNotify = 0x0020,
    Trouble = 0x0040,
    ACMainsFault = 0x0080,
    Test = 0x0100,
    BatteryDefect = 0x0200,
};

enum class LevelOptions : uint8_t {
    ExecuteIfOff = 0x01,
    CoupleColorTempToLevel = 0x02,
};

enum class OccupancySensingOccupancy : uint8_t {
    SensedOccupancy = 0x01,
};

enum class OccupancySensingOccupancySensorTypeBitmap : uint8_t {
    Pir = 0x01,
    Ultrasonic = 0x02,
    PhysicalContact = 0x04,
};

enum class OnWithTimedOffOnOffControl : uint8_t {
    AcceptOnlyWhenOn = 0x01,
};


struct AnyType {
    ZclType type;
    Unk value;
};

struct WriteStructuredRecord {
    AttribId attributeIdentifier;
    std::vector<Uint16> selector;
    AnyType attribute;
};

struct WriteAttributeRecord {
    AttribId attributeIdentifier;
    AnyType attribute;
};

struct ReadAttributeResponseRecord {
    AttribId attributeIdentifier;
    ZclStatus status;
    AnyType attribute;
};

struct AttributeReportRecord {
    AttribId attributeIdentifier;
    AnyType attribute;
};

struct ConfigureReportingRecord {
    ReportingRole direction;
    AttribId attributeIdentifier;
    ZclType attributeType;
    Uint16 minimumReportingInterval;
    Uint16 maximumReportingInterval;
    Unk reportableChange;
    Uint16 timeout;
};

struct ConfigureReportingResponseRecord {
    ZclStatus status;
    ReportingRole direction;
    AttribId attributeIdentifier;
};

struct DiscoverAttributesExtendedResponseRecord {
    AttribId attributeIdentifier;
    ZclType attributeType;
    Map8 accessControl;
};

struct DiscoverAttributesResponseRecord {
    AttribId attributeIdentifier;
    ZclType attributeType;
};

struct ReadReportingConfigurationRecord {
    ReportingRole direction;
    AttribId attributeIdentifier;
};

struct ReadReportingConfigurationResponseRecord {
    ZclStatus status;
    ReportingRole direction;
    AttribId attributeIdentifier;
    ZclType attributeType;
    Uint16 minimumReportingInterval;
    Uint16 maximumReportingInterval;
    Unk reportableChange;
    Uint16 timeout;
};

struct ReadStructuredRecord {
    AttribId attributeIdentifier;
    std::vector<Uint16> index;
};

struct WriteAttributeResponseRecord {
    ZclStatus status;
    AttribId attributeIdentifier;
};

struct WriteStructuredResponseRecord {
    ZclStatus status;
    AttribId attributeIdentifier;
    std::vector<Uint16> selector;
};


} // namespace zigbee::zcl
