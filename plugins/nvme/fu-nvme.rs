// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString)]
enum FuNvmeStatus {
    // Generic Command Status:
    Success = 0x0,
    InvalidOpcode = 0x1,
    InvalidField = 0x2,
    CmdidConflict = 0x3,
    DataXferError = 0x4,
    PowerLoss = 0x5,
    Internal = 0x6,
    AbortReq = 0x7,
    AbortQueue = 0x8,
    FusedFail = 0x9,
    FusedMissing = 0xa,
    InvalidNs = 0xb,
    CmdSeqError = 0xc,
    SglInvalidLast = 0xd,
    SglInvalidCount = 0xe,
    SglInvalidData = 0xf,
    SglInvalidMetadata = 0x10,
    SglInvalidType = 0x11,

    SglInvalidOffset = 0x16,
    SglInvalidSubtype = 0x17,

    SanitizeFailed = 0x1c,
    SanitizeInProgress = 0x1d,

    NsWriteProtected = 0x20,

    LbaRange = 0x80,
    CapExceeded = 0x81,
    NsNotReady = 0x82,
    ReservationConflict = 0x83,

    // command specific status:
    CqInvalid = 0x100,
    QidInvalid = 0x101,
    QueueSize = 0x102,
    AbortLimit = 0x103,
    AbortMissing = 0x104,
    AsyncLimit = 0x105,
    FirmwareSlot = 0x106,
    FirmwareImage = 0x107,
    InvalidVector = 0x108,
    InvalidLogPage = 0x109,
    InvalidFormat = 0x10a,
    FwNeedsConvReset = 0x10b,
    InvalidQueue = 0x10c,
    FeatureNotSaveable = 0x10d,
    FeatureNotChangeable = 0x10e,
    FeatureNotPerNs = 0x10f,
    FwNeedsSubsysReset = 0x110,
    FwNeedsReset = 0x111,
    FwNeedsMaxTime = 0x112,
    FwActivateProhibited = 0x113,
    OverlappingRange = 0x114,
    NsInsufficentCap = 0x115,
    NsIdUnavailable = 0x116,
    NsAlreadyAttached = 0x118,
    NsIsPrivate = 0x119,
    NsNotAttached = 0x11a,
    ThinProvNotSupp = 0x11b,
    CtrlListInvalid = 0x11c,
    BpWriteProhibited = 0x11e,

    // i/o command set specific - nvm commands:
    BadAttributes = 0x180,
    InvalidPi = 0x181,
    ReadOnly = 0x182,
    OncsNotSupported = 0x183,

    DiscoveryRestart = 0x190,
    AuthRequired = 0x191,

    // media and data integrity errors:
    WriteFault = 0x280,
    ReadError = 0x281,
    GuardCheck = 0x282,
    ApptagCheck = 0x283,
    ReftagCheck = 0x284,
    CompareFailed = 0x285,
    AccessDenied = 0x286,
    UnwrittenBlock = 0x287,

    // path-related errors:
    AnaPersistentLoss = 0x301,
    AnaInaccessible = 0x302,
    AnaTransition = 0x303,

    Dnr = 0x4000,
}
