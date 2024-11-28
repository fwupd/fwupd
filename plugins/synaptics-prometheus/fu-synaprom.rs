// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuSynapromCmd {
    GetVersion      = 0x01,
    BootldrPatch    = 0x7d,
    IotaFind        = 0x8e,
}

#[derive(New)]
#[repr(C, packed)]
struct FuStructSynapromRequest {
    cmd: FuSynapromCmd,
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructSynapromReplyGeneric {
    status: u16le,
}

#[repr(u8)]
enum FuSynapromProduct {
    Prometheus      = 65, // b1422
    Prometheuspbl   = 66,
    Prometheusmsbl  = 67,
    Triton          = 69,
    Tritonpbl       = 70,
    Tritonmsbl      = 71,
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructSynapromReplyGetVersion {
    status: u16le,
    buildtime: u32le,
    buildnum: u32le,
    vmajor: u8,
    vminor: u8,
    target: u8,             // e.g. VCSFW_TARGET_ROM
    product: FuSynapromProduct,
    siliconrev: u8,
    formalrel: u8,          // boolean: non-zero -> formal release
    platform: u8,           // PCB revision
    patch: u8,
    serial_number: [u8; 6], // 48-bit */
    security0: u8,          // byte of OTP */
    security1: u8,          // byte of OTP */
    patchsig: u32le,        // opaque patch signature */
    iface: u8,              // interface type
    otpsig: [u8; 3],        // OTP Patch Signature
    otpspare1: u16le,       // OTP spare space
    reserved: u8,
    device_type: u8,
}

enum FuSynapromResult {
    Ok                      = 0,
    GenOperationCanceled    = 103,
    GenInvalid              = 110,
    GenBadParam             = 111,
    GenNullPointer          = 112,
    GenUnexpectedFormat     = 114,
    GenTimeout              = 117,
    GenObjectDoesntExist    = 118,
    GenError                = 119,
    SensorMalfunctioned     = 202,
    SysOutOfMemory          = 602,
}

enum FuSynapromProductType {
    Denali      = 0,
    Hayes       = 1,
    Shasta      = 2,
    Steller     = 3,
    Whitney     = 4,
    Prometheus  = 5,
    PacificPeak = 6,
    Morgan      = 7,
    Ox6101      = 8,
    Triton      = 9,
}

#[derive(New, ParseStream, Default)]
#[repr(C, packed)]
struct FuStructSynapromMfwHdr {
    product: u32le,
    id: u32le = 0xFF,		// MFW unique id used for compat verification
    buildtime: u32le = 0xFF,	// unix-style
    buildnum: u32le = 0xFF,
    vmajor: u8 = 10,			// major version
    vminor: u8 = 1,			// minor version
    unused: [u8; 6],
}

#[derive(ToString)]
#[repr(u16le)]
enum FuSynapromFirmwareTag {
    MfwUpdateHeader  = 0x0001,
    MfwUpdatePayload = 0x0002,
    CfgUpdateHeader  = 0x0003,
    CfgUpdatePayload = 0x0004,
}

#[derive(New, ParseStream)]
#[repr(C, packed)]
struct FuStructSynapromHdr {
    tag: FuSynapromFirmwareTag,
    bufsz: u32le,
}

#[derive(ParseStream, Default)]
#[repr(C, packed)]
struct FuStructSynapromCfgHdr {
    product: u32le = 65, // Prometheus (b1422)
    id1: u32le,
    id2: u32le,
    version: u16le,
    _unused: [u8; 2],
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructSynapromIotaConfigVersion {
    config_id1: u32le, // YYMMDD
    config_id2: u32le, // HHMMSS
    version: u16le,
    _unused: [u16; 3],
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructSynapromReplyIotaFindHdr {
    status: u16le,
    fullsize: u32le,
    nbytes: u16le,
    itype: u16le,
}

// Iotas can exceed the size of available RAM in the part: to allow the host to read them the
// IOTA_FIND command supports transferring iotas with multiple commands
#[derive(New, Getters)]
#[repr(C, packed)]
struct FuStructSynapromCmdIotaFind {
    itype: u16le,    // type of iotas to find
    flags: u16le,
    maxniotas: u8,   // maximum number of iotas to return, 0 = unlimited
    firstidx: u8,    // first index of iotas to return
    _dummy: [u8; 2],
    offset: u32le,   // byte offset of data to return
    nbytes: u32le,   // maximum number of bytes to return
}
