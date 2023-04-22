struct EfiUxCapsuleHeader {
    version: u8: const=0x01
    checksum: u8
    image_type: u8
    _reserved: u8
    mode: u32le
    x_offset: u32le
    y_offset: u32le
}
struct EfiCapsuleHeader {
    guid: guid
    header_size: u32le: default=$struct_size
    flags: u32le
    image_size: u32le
}
struct EfiUpdateInfo {
    version: u32le: default=0x7
    guid: guid
    flags: u32le
    hw_inst: u64le
    time_attempted: 16u8 // a EFI_TIME_T
    status: u32le
    // EFI_DEVICE_PATH goes here
}
struct AcpiInsydeQuirk {
    signature: 6s
    size: u32le
    flags: u32le
}
enum UefiUpdateInfoStatus {
    Unknown,
    AttemptUpdate,
    Attempted,
}
enum UefiDeviceKind {
    Unknown,
    SystemFirmware,
    DeviceFirmware,
    UefiDriver,
    Fmp,
    DellTpmFirmware,
    Last,
}
enum UefiDeviceStatus {
    Success = 0x00,
    ErrorUnsuccessful = 0x01,
    ErrorInsufficientResources = 0x02,
    ErrorIncorrectVersion = 0x03,
    ErrorInvalidFormat = 0x04,
    ErrorAuthError = 0x05,
    ErrorPwrEvtAc = 0x06,
    ErrorPwrEvtBatt = 0x07,
    Last,
}
