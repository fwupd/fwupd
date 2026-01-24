// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuLogitechBulkcontrollerFnumUsbMsg {
    Header = 1,
    Acknowledge = 2,
    Request = 3,
    Response = 4,
    Event = 5,
}

enum FuLogitechBulkcontrollerFnumHeader {
    Id = 1, // str, used in the ack message msgId field
    Timestamp = 2, // str, number of ms since the epoch
}

enum FuLogitechBulkcontrollerFnumRequest {
    GetDeviceInfo = 2,
    UpdateNow = 3,
    SendCrashDump = 4,
    TransitionToDeviceMode = 5,
    GetCertificateChain = 6,
    SetRightSightConfiguration = 7,
    GetManifestBody = 8,
    SendCrashDump2 = 9,
    SetDeviceTime = 10,
    SetAntiFlickerConfiguration = 11,
    SetBleCfg = 12,
    SetDeprovision = 13,
    RebootDevice = 14,
    SetSpeakerBoost = 15,
    SetNoiseReduction = 16,
    SetReverbMode = 17,
    GenerateCrashDump = 18,
    SendCertificateData = 19,
    SetMicEqMode = 20,
    SetSpeakerEqMode = 21,
    ForgetDevice = 22,
    SetRightSightConfiguration2 = 23,
    SendTestResult = 24,
    GetMemfaultManifest = 25,
    SendMemfaultSettings = 26,
}

enum FuLogitechBulkcontrollerFnumResponse {
    GetDeviceInfo = 2,
    UpdateNow = 3,
    SendCrashDump = 4,
    TransitionToDeviceMode = 5,
    GetCertificateChain = 6,
    SetRightSightConfiguration = 7,
    GetManifestBody = 8,
    SendCrashDumpResponse2 = 9,
    SetAntiFlickerConfiguration = 11,
    SetBleCfg = 12,
    SetDeprovision = 13,
    RebootDevice = 14,
    SetSpeakerBoost = 15,
    SetNoiseReduction = 16,
    SetReverbMode = 17,
    GenerateCrashDump = 18,
    SendCertificateData = 19,
    SetMicEqMode = 20,
    SetSpeakerEqMode = 21,
    ForgetDevice = 22,
    SendTestResult = 24,
    GetMemfaultManifest = 25,
    SendMemfaultSettings = 26,
}

enum FuLogitechBulkcontrollerFnumSetDeviceTimeRequest {
    Ts = 1, // u64 utc
    Timezone = 2, // str
}

enum FuLogitechBulkcontrollerFnumAcknowledge {
    MsgId = 1, // str, the same as UsbMsg.Header.id
    Success = 2, // bool
}

enum FuLogitechBulkcontrollerFnumTransitionToDeviceModeResponse {
    Success = 1, // bool, if Kong is not provisioned, should just respond with true value
    Error = 2, // int
    ErrorDescription = 3, //str
}

enum FuLogitechBulkcontrollerFnumGetDeviceInfoResponse {
    Payload = 1, // str, MQTT message
}

enum FuLogitechBulkcontrollerFnumEvent {
    Kong = 1,
    SendCrashDump = 2,
    CrashDumpAvailable = 3,
    Handshake = 4,
    InitiateMemfaultManifestRequest = 5,
}

enum FuLogitechBulkcontrollerFnumKongEvent {
    Payload = 1, // str, MQTT message
}

#[derive(ToString)]
enum FuLogitechBulkcontrollerDeviceState {
    Unknown = -1,
    Offline,
    Online,
    Idle,
    InUse,
    AudioOnly,
    Enumerating,
}

#[derive(ToString)]
enum FuLogitechBulkcontrollerUpdateState {
    Unknown = -1,
    Current,
    Available,
    Starting = 3,
    Downloading,
    Ready,
    Updating,
    Scheduled,
    Error,
}

#[repr(u32le)]
#[derive(ToString)]
enum FuLogitechBulkcontrollerCmd {
    CheckBuffersize = 0xCC00,
    Init = 0xCC01,
    StartTransfer = 0xCC02,
    DataTransfer = 0xCC03,
    EndTransfer = 0xCC04,
    Uninit = 0xCC05,
    BufferRead = 0xCC06,
    BufferWrite = 0xCC07,
    UninitBuffer = 0xCC08,
    Ack = 0xFF01,
    Timeout = 0xFF02,
    Nack = 0xFF03,
}

#[derive(New, ToString, Getters)]
#[repr(C, packed)]
struct FuStructLogitechBulkcontrollerSendSyncReq {
    cmd: FuLogitechBulkcontrollerCmd,
    payload_length: u32le,
    sequence_id: u32le,
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructLogitechBulkcontrollerSendSyncRes {
    cmd: FuLogitechBulkcontrollerCmd,
    payload_length: u32le,
    sequence_id: u32le,
}

#[derive(New)]
#[repr(C, packed)]
struct FuStructLogitechBulkcontrollerUpdateReq {
    cmd: FuLogitechBulkcontrollerCmd,
    payload_length: u32le,
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructLogitechBulkcontrollerUpdateRes {
    cmd: FuLogitechBulkcontrollerCmd,
    _payload_length: u32le,
    cmd_req: FuLogitechBulkcontrollerCmd,
}

enum FuLogitechBulkcontrollerChecksumType {
    Sha256,
    Sha512,
    Md5,
}
