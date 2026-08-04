/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Core components of fwupd

mod bitflags;
#[macro_use]
mod errors;

pub use crate::bitflags::{BitflagIter, Bitflags};

pub type Result<T> = std::result::Result<T, Error>;

define_error! {
    /// Error enum values matching the C `FwupdError` enum order from
    /// `fwupd-error.h`.
    Error {
    Internal        = 0  => "Internal error.",
    VersionNewer    = 1  => "Installed newer firmware version.",
    VersionSame     = 2  => "Installed same firmware version.",
    AlreadyPending  = 3  => "Already set to be installed offline.",
    AuthFailed      = 4  => "Failed to get authentication.",
    Read            = 5  => "Failed to read from device.",
    Write           = 6  => "Failed to write to the device.",
    InvalidFile     = 7  => "Invalid file format.",
    NotFound        = 8  => "No matching device exists.",
    NothingToDo     = 9  => "Nothing to do.",
    NotSupported    = 10 => "Action was not possible.",
    SignatureInvalid = 11 => "Signature was invalid.",
    AcPowerRequired = 12 => "AC power was required.",
    PermissionDenied = 13 => "Permission was denied.",
    BrokenSystem    = 14 => "User has configured their system in a broken way.",
    BatteryLevelTooLow = 15 => "The system battery level is too low.",
    NeedsUserAction = 16 => "User needs to do an action to complete the update.",
    AuthExpired     = 17 => "Failed to get auth as credentials have expired.",
    InvalidData     = 18 => "Invalid data.",
    TimedOut        = 19 => "The request timed out.",
    Busy            = 20 => "The device is busy.",
    NotReachable    = 21 => "The network is not reachable.",
    }
}

impl From<Error> for u32 {
    fn from(e: Error) -> u32 {
        e as u32
    }
}
