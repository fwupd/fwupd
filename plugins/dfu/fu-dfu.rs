// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuDfuDeviceAttr {
    CanDownload = 1 << 0,
    CanUpload = 1 << 1,
    ManifestTol = 1 << 2,
    WillDetach = 1 << 3,
    CanAccelerate = 1 << 7,
}

enum FuDfuTargetTransferFlags {
    None = 0,
    Verify = 1 << 0,
    WildcardVid = 1 << 4,
    WildcardPid = 1 << 5,
    AddrHeuristic = 1 << 7, // automatically detect
}

enum FuDfuRequest {
    Detach,
    Dnload,
    Upload,
    Getstatus,
    Clrstatus,
    Getstate,
    Abort,
}

#[derive(ToString)]
enum FuDfuStatus {
    Ok,
    ErrTarget,
    ErrFile,
    ErrWrite,
    ErrErase,
    ErrCheckErased,
    ErrProg,
    ErrVerify,
    ErrAddress,
    ErrNotdone,
    ErrFirmware,
    ErrVendor,
    ErrUsbr,
    ErrPor,
    ErrUnknown,
    ErrStalldpkt,
}

#[derive(ToString)]
enum FuDfuState {
    AppIdle,
    AppDetach,
    DfuIdle,
    DfuDnloadSync,
    DfuDnbusy,
    DfuDnloadIdle,
    DfuManifestSync,
    DfuManifest,
    DfuManifestWaitReset,
    DfuUploadIdle,
    DfuError,
}

#[derive(ToBitString)]
enum FuDfuSectorCap {
    None = 0, // No operations possible
    Readable = 1 << 0,
    Writable = 1 << 1,
    Erasable = 1 << 2,
}

// ATMEL AVR version of DFU: http://www.atmel.com/Images/doc7618.pdf
enum FuDfuAvrCmd {
    ProgStart = 0x01,
    DisplayData = 0x03,
    WriteCommand = 0x04,
    ReadCommand = 0x05,
    ChangeBaseAddr = 0x06,
}

enum FuDfuAvr32MemoryUnit {
    Flash,
    Eeprom,
    Security,
    Configuration,
    Bootloader,
    Signature,
    User,
}
