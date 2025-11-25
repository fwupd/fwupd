// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u16le)]
enum FuSynapticsCapeCmd {
    FwUpdateStart           = 0xC8,
    FwUpdateWrite           = 0xC9,
    FwUpdateEnd             = 0xCA,
    McuSoftReset            = 0xAF,
    FwGetActivePartition    = 0x1CF,
    GetVersion              = 0x103,
}

#[derive(ToString)]
enum FuSynapticsCapeError {
    Eagain                                  = -11,
    SfuFail                                 = -200,
    SfuWriteFail                            = -201,
    SfuReadFail                             = -202,
    SfuCrcError                             = -203,
    SfuUsbIdNotMatch                        = -204,
    SfuVersionDowngrade                     = -205,
    SfuHeaderCorruption                     = -206,
    SfuImageCorruption                      = -207,
    SfuAlreadyActive                        = -208,
    SfuNotReady                             = -209,
    SfuSignTransferCorruption               = -210,
    SfuSignatureVerification                = -211,
    SfuTaskNotRunning                       = -212,
    GenericFailure                          = -1025,
    AlreadyExists                           = -1026,
    NullAppPointer                          = -1027,
    NullModulePointer                       = -1028,
    NullStreamPointer                       = -1029,
    NullPointer                             = -1030,
    BadAppId                                = -1031,
    ModuleTypeHasNoApi                      = -1034,
    BadMagicNumber                          = -1052,
    CmdModeUnsupported                      = -1056,
}

#[repr(u32le)]
enum FuSynapticsCapeModuleId {
    AppCtrl                                 = 0xb32d2300,
}

#[derive(New, Getters, Default)]
#[repr(C, packed)]
struct FuSynapticsCapeMsg {
    data_len: u16le,            // data length in DWORDs
    cmd_id: FuSynapticsCapeCmd, // bit 15 set when the host want a reply from device
    module_id: FuSynapticsCapeModuleId = AppCtrl,
    data: [u32le; 13],
}

#[derive(New, Getters, Default)]
#[repr(C, packed)]
struct FuSynapticsCapeCmdHidReport {
    report_id: u16le == 1,
    msg: FuSynapticsCapeMsg,
}

#[derive(New, ParseStream, Default)]
#[repr(C, packed)]
struct FuStructSynapticsCapeHidHdr {
    vid: u32le,
    pid: u32le,
    update_type: u32le,
    signature: u32le == 0x43534645, // "EFSC"
    crc: u32le,
    ver_w: u16le,
    ver_x: u16le,
    ver_y: u16le,
    ver_z: u16le,
    reserved: u32le,
}

#[derive(New, ParseStream, Default)]
#[repr(C, packed)]
struct FuStructSynapticsCapeSnglHdr {
    magic: u32le == 0x4C474E53, // "SNGL"
    file_crc: u32le,
    file_size: u32le,
    magic2: u32le,
    img_type: u32le,
    fw_version: u32le,
    vid: u16le,
    pid: u16le,
    fw_file_num: u32le,
    version: u32le,
    fw_crc: u32le,
    _reserved: [u8; 8],
    machine_name: [char; 16],
    time_stamp: [char; 16],
}

#[repr(C, packed)]
struct FuStructSynapticsCapeSnglFile {
    Id: u32le,
    Crc: u32le,
    File: u32le,
    Size: u32le,
}

enum FuSynapticsCapeSnglImgTypeId {
    Hid0 = 0x30444948, // hid file for partition 0
    Hid1 = 0x31444948, // hid file for partition 1
    Hof0 = 0x30464F48, // hid + offer file for partition 0
    Hof1 = 0x31464F48, // hid + offer file for partition 1
    Sfsx = 0x58534653, // sfs file
    Sofx = 0x58464F53, // sfs + offer file
    Sign = 0x4e474953, // digital signature file
}

enum FuSynapticsCapeFirmwarePartition {
    Auto,
    1,
    2,
}
