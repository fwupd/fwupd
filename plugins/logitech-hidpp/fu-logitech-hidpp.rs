// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ToString)]
enum LogitechHidppFeature {
    Root                  = 0x0000,
    IFeatureSet           = 0x0001,
    IFirmwareInfo         = 0x0003,
    GetDeviceNameType     = 0x0005,
    DfuControl            = 0x00C1,
    DfuControlSigned      = 0x00C2,
    DfuControlBolt        = 0x00C3,
    Dfu                   = 0x00D0,
    BatteryLevelStatus    = 0x1000,
    UnifiedBattery        = 0x1004,
    KbdReprogrammableKeys = 0x1B00,
    SpecialKeysButtons    = 0x1B04,
    MousePointerBasic     = 0x2200,
    AdjustableDpi         = 0x2201,
    AdjustableReportRate  = 0x8060,
    ColorLedEffects       = 0x8070,
    OnboardProfiles       = 0x8100,
    MouseButtonSpy        = 0x8110,
}

#[derive(ToString)]
enum LogitechHidppDeviceIdx {
    Wired = 0x00,
    Receiver = 0xFF,
}

#[derive(ToString)]
enum LogitechHidppReportId {
    Short = 0x10,
    Long = 0x11,
    VeryLong = 0x12,
}

// HID++1.0 registers
#[derive(ToString)]
enum LogitechHidppRegister {
    HidppNotifications = 0x00,
    EnableIndividualFeatures = 0x01,
    BatteryStatus = 0x07,
    BatteryMileage = 0x0D,
    Profile = 0x0F,
    LedStatus = 0x51,
    LedIntensity = 0x54,
    LedColor = 0x57,
    OpticalSensorSettings = 0x61,
    CurrentResolution = 0x63,
    UsbRefreshRate = 0x64,
    GenericMemoryManagement = 0xA0,
    HotControl = 0xA1,
    ReadMemory = 0xA2,
    DeviceConnectionDisconnection = 0xB2,
    PairingInformation = 0xB5,
    DeviceFirmwareUpdateMode = 0xF0,
    DeviceFirmwareInformation = 0xF1,
}

#[derive(ToString)]
enum LogitechHidppSubid {
    VendorSpecificKeys = 0x03,
    PowerKeys = 0x04,
    Roller = 0x05,
    MouseExtraButtons = 0x06,
    BatteryChargingLevel = 0x07,
    UserInterfaceEvent = 0x08,
    FLockStatus = 0x09,
    CalculatorResult = 0x0A,
    MenuNavigate = 0x0B,
    FnKey = 0x0C,
    BatteryMileage = 0x0D,
    UartRx = 0x0E,
    BacklightDurationUpdate = 0x17,
    DeviceDisconnection = 0x40,
    DeviceConnection = 0x41,
    DeviceDiscovery = 0x42,
    PinCodeRequest = 0x43,
    ReceiverWorkingMode = 0x44,
    ErrorMessage = 0x45,
    RfLinkChange = 0x46,
    Hci = 0x48,
    LinkQuality = 0x49,
    DeviceLockingChanged = 0x4a,
    WirelessDeviceChange = 0x4B,
    Acl = 0x51,
    VoipTelephonyEvent = 0x5B,
    Led = 0x60,
    GestureAndAir = 0x65,
    TouchpadMultiTouch = 0x66,
    Traceability = 0x78,
    SetRegister = 0x80,
    GetRegister = 0x81,
    SetLongRegister = 0x82,
    GetLongRegister = 0x83,
    SetVeryLongRegister = 0x84,
    GetVeryLongRegister = 0x85,
    ErrorMsg = 0x8F,
    ErrorMsg_20 = 0xFF,
}

enum LogitechHidppBootloaderCmd {
    General_error = 0x01,
    Read = 0x10,
    Write = 0x20,
    WriteInvalidAddr = 0x21,
    WriteVerifyFail = 0x22,
    WriteNonzeroStart = 0x23,
    WriteInvalidCrc = 0x24,
    ErasePage = 0x30,
    ErasePageInvalidAddr = 0x31,
    ErasePageNonzeroStart = 0x33,
    GetHwPlatformId = 0x40,
    GetFwVersion = 0x50,
    GetChecksum = 0x60,
    Reboot = 0x70,
    GetMeminfo = 0x80,
    GetBlVersion = 0x90,
    GetInitFwVersion = 0xa0,
    ReadSignature = 0xb0,
    WriteRamBuffer = 0xc0,
    WriteRamBufferInvalidAddr = 0xc1,
    WriteRamBufferOverflow = 0xc2,
    FlashRam = 0xd0,
    FlashRamInvalidAddr = 0xd1,
    FlashRamWrongCrc = 0xd2,
    FlashRamPage0Invalid = 0xd3,
    FlashRamInvalidOrder = 0xd4,
    WriteSignature = 0xe0,
}
