/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Core components of fwupd

mod bitflags;

use thiserror::Error;

pub use crate::bitflags::{BitflagIter, Bitflags};

pub type Result<T> = std::result::Result<T, Error>;

#[derive(Error, Debug)]
pub enum Error {
    /// Error enum values matching the C `FwupdError` enum order from
    /// `fwupd-error.h`.
    #[error("Internal error.")]
    Internal = 0,
    #[error("Installed newer firmware version.")]
    VersionNewer = 1,
    #[error("Installed same firmware version.")]
    VersionSame = 2,
    #[error("Already set to be installed offline.")]
    AlreadyPending = 3,
    #[error("Failed to get authentication.")]
    AuthFailed = 4,
    #[error("Failed to read from device.")]
    Read = 5,
    #[error("Failed to write to the device.")]
    Write = 6,
    #[error("Invalid file format.")]
    InvalidFile = 7,
    #[error("No matching device exists.")]
    NotFound = 8,
    #[error("Nothing to do.")]
    NothingToDo = 9,
    #[error("Action was not possible.")]
    NotSupported = 10,
    #[error("Signature was invalid.")]
    SignatureInvalid = 11,
    #[error("AC power was required.")]
    AcPowerRequired = 12,
    #[error("Permission was denied.")]
    PermissionDenied = 13,
    #[error("User has configured their system in a broken way.")]
    BrokenSystem = 14,
    #[error("The system battery level is too low.")]
    BatteryLevelTooLow = 15,
    #[error("User needs to do an action to complete the update.")]
    NeedsUserAction = 16,
    #[error("Failed to get auth as credentials have expired.")]
    AuthExpired = 17,
    #[error("Invalid data.")]
    InvalidData = 18,
    #[error("The request timed out.")]
    TimedOut = 19,
    #[error("The device is busy.")]
    Busy = 20,
    #[error("The network is not reachable.")]
    NotReachable = 21,
}

impl From<Error> for u32 {
    fn from(e: Error) -> u32 {
        e as u32
    }
}
