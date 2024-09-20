// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuLogitechTapTouchDirection {
    Out = 0x0,
    In = 0x0,
}

#[repr(u8)]
enum FuStructLogitechTapTouchHidCmd {
    GetFirmwareVersion = 0x40,
    GetProtocolVersion = 0x42,
    GetMcuVersion = 0x61,
    GetSysBusyStatus = 0x80,
    GetMcuMode = 0xC0,
    SetApMode = 0xC1,
    SetBlMode = 0xC2,
    WriteData = 0xC3,
    WriteEnable = 0xC4,
    GetApCrc = 0xC7,
    GetDfCrc = 0xCA,
    SetTdeTestMode = 0xF2,
}

#[derive(New)]
struct FuStructLogitechTapTouchHidReq {
    report_id: u8 == 0x03,
    // response buffer is always going to be less than 64 bytes for this hardware/plugin
    // if response buffer is more than 64 bytes (for some hardware/interfaces), then use A4 here 
    res_size_supported_id: u8 == 0xA3,
    payload_len: u8,
    // bytes to read from response 
    response_len: u8,
    cmd: FuStructLogitechTapTouchHidCmd,
    //payload goes here
}

#[repr(u8)]
enum FuLogitechTapSensorHidTdeMode {
    Disable = 0x0,
    Enable = 0x01,
    Selector = 0x02,
}

// device version
#[repr(u8)]
enum FuLogitechTapSensorHidColossusApp {
    GetVersion = 0x04,
}

// serial number of the device
#[repr(u8)]
enum FuLogitechTapSensorHidSerialNumber {
    SetReportByte1 = 0x0,
    SetReportByte4 = 0x0,
    SetReportByte3 = 0x0E,
    SetReportByte2 = 0x70,
}

#[repr(u8)]
enum FuLogitechTapSensorHidReboot {
    PinClr = 0x05,
    PinSet = 0x06,
    Pwr = 0x2D,
    Rst = 0x2E,
}

#[repr(u8)]
enum FuLogitechTapSensorHidSetCmd {
     // put device into suspend/operational mode
     Tde = 0x1A,
     Reboot = 0x1A,
     Version = 0x1B,
     SerialNumber = 0x1C,
}
 
#[repr(u8)]
enum FuLogitechTapSensorHidGetCmd {
     Version = 0x19,
     SerialNumber = 0x1D,
}
 
#[derive(New)]
struct FuStructLogitechTapSensorHidReq {
     cmd: FuLogitechTapSensorHidSetCmd,
      //payload goes here
      payload_byte1: u8,
      payload_byte2: u8,
      payload_byte3: u8 = 0x0,
      payload_byte4: u8 = 0x0,
}
 
#[derive(New)]
struct FuStructLogitechTapSensorHidRes {
     cmd: FuLogitechTapSensorHidGetCmd,
      //payload goes here
      payload_byte1: u8,
      payload_byte2: u8,
      payload_byte3: u8,
      payload_byte4: u8,
}

