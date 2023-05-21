// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

enum CcgxDmcImgType {
    Invalid = 0,
    Image0,
    Image1,
}

#[derive(ToString)]
enum CcgxDmcImgStatus {
    Unknown = 0,
    Valid,
    Invalid,
    Recovery,
    RecoveredFromSecondary,
    NotSupported = 0x0F,
}

// flash architecture
#[derive(ToString)]
enum CcgxDmcImgMode {
    // indicates that the device has a single image
    SingleImg = 0,
    // the device supports symmetric boot. In symmetric mode the bootloader
    // boots the image with higher version, when they are valid
    DualImgSym,
    // the device supports Asymmetric boot. Image-1 & 2 can be different or
    // same. in this method Bootloader is hard coded to boot the primary
    // image. Secondary acts as recovery
    DualImgAsym,
    SingleImgWithRamImg,
    Last,
}

// dock status
#[derive(ToString)]
enum CcgxDmcDeviceStatus {
    // status code indicating DOCK IDLE state. SUCCESS: no malfunctioning
    // no outstanding request or event
    Idle = 0,
    // status code indicating dock FW update in progress
    UpdateInProgress,
    // status code indicating dock FW update is partially complete
    UpdatePartial,
    // status code indicating dock FW update SUCCESS - all m_images of all
    // devices are valid
    UpdateCompleteFull,
    // status code indicating dock FW update SUCCESS - not all m_images of all
    // devices are valid
    UpdateCompletePartial,
    // fw download status
    UpdatePhase1Complete,
    FwDownloadedUpdatePend,
    FwDownloadedPartialUpdatePend,
    Phase2UpdateInProgress = 0x81,
    Phase2UpdatePartial,
    Phase2UpdateFactoryBackup,
    Phase2UpdateCompletePartial,
    Phase2UpdateCompleteFull,
    Phase2UpdateFailInvalidFwct,
    Phase2UpdateFailInvalidDockIdentity,
    Phase2UpdateFailInvalidCompositeVer,
    Phase2UpdateFailAuthenticationFailed,
    Phase2UpdateFailInvalidAlgorithm,
    Phase2UpdateFailSpiReadFailed,
    Phase2UpdateFailNoValidKey,
    Phase2UpdateFailNoValidSpiPackage,
    Phase2UpdateFailRamInitFailed,
    Phase2UpdateFailFactoryBackupFailed,
    Phase2UpdateFailNoValidFactoryPackage,
    // status code indicating dock FW update FAILED
    UpdateFail = 0xff,
}

#[derive(ToString)]
enum CcgxDmcDevxDeviceType {
    Invalid = 0x00,
    Ccg3 = 0x01,
    Dmc = 0x02,
    Ccg4 = 0x03,
    Ccg5 = 0x04,
    Hx3 = 0x05,
    Hx3Pd = 0x0A,
    DmcPd = 0x0B,
    Spi = 0xFF,
}

// request codes for vendor interface
enum CcgxDmcRqtCode {
    UpgradeStart = 0xD0,
    Reserv0,
    FwctWrite,
    ImgWrite,
    Reserv1,
    Reserv2,
    DockStatus,
    DockIdentity,
    ResetStateMachine,  // command to reset dmc state machine of DMC
    SoftReset = 0xDC,   // command to reset for online enhanced mode (no reset during update)
    Trigger = 0xDA,     // Update Trigger command for offline mode
}

// opcode of interrupt read
#[derive(ToString)]
enum CcgxDmcIntOpcode {
    FwUpgradeRqt = 1,
    FwUpgradeStatus = 0x80,
    ImgWriteStatus,
    Reenum,
    FwctAnalysisStatus,
}

// fwct analysis status
#[derive(ToString)]
enum CcgxDmcFwctAnalysisStatus {
    InvalidFwct = 0,
    InvalidDockIdentity,
    InvalidCompositeVersion,
    AuthenticationFailed,
    InvalidAlgorithm,
}

#[derive(ToString)]
enum CcgxDmcUpdateModel {
    None = 0,
    DownloadTrigger, // need to trigger after updating FW
    PendingReset,    // need to set soft reset after updating FW
}

// fields of data returned when reading dock_identity for new firmware
#[derive(New, Getters)]
struct CcgxDmcDockIdentity {
    // this field indicates both validity and structure version
    // 0 : invalid
    // 1 : old structure
    // 2 : new structure
    structure_version: u8,
    cdtt_version: u8,
    vid: u16le,
    pid: u16le,
    device_id: u16le,
    vendor_string: [char; 32],
    product_string: [char; 32],
    custom_meta_data_flag: u8,
    // model field indicates the type of the firmware upgrade status
    // 0 - online/offline
    // 1 - Online model
    // 2 - ADICORA/Offline model
    // 3 - No reset
    // 4 - 0xFF - Reserved
    model: u8,
}

// fields of status of a specific device
#[derive(Parse)]
struct CcgxDmcDevxStatus {
    // device ID of the device
    device_type: u8,
    // component ID of the device
    component_id: u8,
    // image mode of the device - single image/ dual symmetric/ dual
    // asymmetric image >
    image_mode: u8,
    // current running image
    current_image: u8,
    // image status
    // b7:b4 => Image 2 status
    // b3:b0 => Image 1 status
    //  0 = Unknown
    //  1 = Valid
    //  2 = Invalid
    //  3-0xF = Reserved

    img_status: u8,
    // padding
    _reserved0: [u8; 3],
    // complete fw version 8 bytes for bootload, image1 and image2. 8 byte
    // for fw version and application version
    fw_version: [u8; 24],
}

// fields of data returned when reading dock_status
#[derive(New, Getters)]
struct CcgxDmcDockStatus {
    device_status: u8, // CcgxDmcDeviceStatus
    device_count: u8,
    status_length: u16le, // including dock_status, devx_status for each device
    composite_version: u32le, // dock composite version m_fwct_info
    // CcgxDmcDevxStatus devx_status[DMC_DOCK_MAX_DEV_COUNT],
}

// fields of data returned when reading an interrupt from DMC
#[derive(New, Getters)]
struct CcgxDmcIntRqt {
    opcode: u8,
    length: u8,
    data: [u8; 8],
}

// header structure of FWCT
#[derive(New, Parse, Validate)]
struct CcgxDmcFwctInfo {
    signature: u32le: const=0x54435746, // 'F' 'W' 'C' 'T'
    size: u16le,
    checksum: u8,
    version: u8,
    custom_meta_type: u8,
    cdtt_version: u8,
    vid: u16le,
    pid: u16le,
    device_id: u16le,
    _reserv0: [u8; 16],
    composite_version: u32le,
    image_count: u8,
    _reserv1: [u8; 3],
}

#[derive(New, Parse)]
struct CcgxDmcFwctImageInfo {
    device_type: u8,
    img_type: u8,
    comp_id: u8,
    row_size: u8,
    _reserv0: [u8; 4],
    fw_version: u32le,
    app_version: u32le,
    img_offset: u32le,
    img_size: u32le,
    img_digest: [u8; 32],
    num_img_segments: u8,
    _reserv1: [u8; 3],
}

#[derive(New, Parse)]
struct CcgxDmcFwctSegmentationInfo {
    img_id: u8,
    type: u8,
    start_row: u16le,
    num_rows: u16le, // size
    _reserv0: [u8; 2],
}
