// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

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
    Writeable = 1 << 1,
    Erasable = 1 << 2,
}
