/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Core components of fwupd

mod bitflags;

pub use crate::bitflags::{BitflagIter, Bitflags};

pub type Result<T> = std::result::Result<T, Error>;

/// Error enum values matching the C `FwupdError` enum order from
/// `fwupd-error.h`.
#[derive(Debug, Copy, Clone)]
#[repr(u32)]
pub enum ErrorKind {
    /// Internal error
    Internal = 0,
    /// Installed newer firmware version
    VersionNewer = 1,
    /// Installed same firmware version
    VersionSame = 2,
    /// Already set to be installed offline
    AlreadyPending = 3,
    /// Failed to get authentication
    AuthFailed = 4,
    /// Failed to read from device
    Read = 5,
    /// Failed to write to the device
    Write = 6,
    /// Invalid file format
    InvalidFile = 7,
    /// No matching device exists
    NotFound = 8,
    /// Nothing to do
    NothingToDo = 9,
    /// Action was not possible
    NotSupported = 10,
    /// Signature was invalid
    SignatureInvalid = 11,
    /// AC power was required
    AcPowerRequired = 12,
    /// Permission was denied
    PermissionDenied = 13,
    /// User has configured their system in a broken way
    BrokenSystem = 14,
    /// The system battery level is too low
    BatteryLevelTooLow = 15,
    /// User needs to do an action to complete the update
    NeedsUserAction = 16,
    /// Failed to get auth as credentials have expired
    AuthExpired = 17,
    /// Invalid data
    InvalidData = 18,
    /// The request timed out
    TimedOut = 19,
    /// The device is busy
    Busy = 20,
    /// The network is not reachable
    NotReachable = 21,
}

impl std::fmt::Display for ErrorKind {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ErrorKind::Internal => write!(f, "Internal error"),
            ErrorKind::VersionNewer => write!(f, "Installed newer firmware version"),
            ErrorKind::VersionSame => write!(f, "Installed same firmware version"),
            ErrorKind::AlreadyPending => write!(f, "Already set to be installed offline"),
            ErrorKind::AuthFailed => write!(f, "Failed to get authentication"),
            ErrorKind::Read => write!(f, "Failed to read from device"),
            ErrorKind::Write => write!(f, "Failed to write to the device"),
            ErrorKind::InvalidFile => write!(f, "Invalid file format"),
            ErrorKind::NotFound => write!(f, "No matching device exists"),
            ErrorKind::NothingToDo => write!(f, "Nothing to do"),
            ErrorKind::NotSupported => write!(f, "Action was not possible"),
            ErrorKind::SignatureInvalid => write!(f, "Signature was invalid"),
            ErrorKind::AcPowerRequired => write!(f, "AC power was required"),
            ErrorKind::PermissionDenied => write!(f, "Permission was denied"),
            ErrorKind::BrokenSystem => {
                write!(f, "User has configured their system in a broken way")
            }
            ErrorKind::BatteryLevelTooLow => write!(f, "The system battery level is too low"),
            ErrorKind::NeedsUserAction => {
                write!(f, "User needs to do an action to complete the update")
            }
            ErrorKind::AuthExpired => {
                write!(f, "Failed to get auth as credentials have expired")
            }
            ErrorKind::InvalidData => write!(f, "Invalid data"),
            ErrorKind::TimedOut => write!(f, "The request timed out"),
            ErrorKind::Busy => write!(f, "The device is busy"),
            ErrorKind::NotReachable => write!(f, "The network is not reachable"),
        }
    }
}

impl From<ErrorKind> for u32 {
    fn from(e: ErrorKind) -> u32 {
        e as u32
    }
}

impl From<ErrorKind> for i32 {
    fn from(e: ErrorKind) -> i32 {
        e as i32
    }
}

impl From<std::io::ErrorKind> for ErrorKind {
    fn from(kind: std::io::ErrorKind) -> Self {
        match kind {
            std::io::ErrorKind::NotFound => ErrorKind::NotFound,
            std::io::ErrorKind::PermissionDenied => ErrorKind::PermissionDenied,
            std::io::ErrorKind::TimedOut => ErrorKind::TimedOut,
            std::io::ErrorKind::InvalidInput | std::io::ErrorKind::InvalidData => {
                ErrorKind::InvalidData
            }
            std::io::ErrorKind::WriteZero => ErrorKind::Write,
            std::io::ErrorKind::UnexpectedEof => ErrorKind::Read,
            std::io::ErrorKind::Unsupported => ErrorKind::NotSupported,
            std::io::ErrorKind::ConnectionRefused
            | std::io::ErrorKind::ConnectionReset
            | std::io::ErrorKind::ConnectionAborted
            | std::io::ErrorKind::NotConnected
            | std::io::ErrorKind::AddrNotAvailable
            | std::io::ErrorKind::AddrInUse => ErrorKind::NotReachable,
            // HostUnreachable, NetworkUnreachable, NetworkDown and ResourceBusy require 1.83+
            _ => ErrorKind::Internal,
        }
    }
}

/// Main fwupd error type. This resembles the fwupd-specific
/// `GErrors` in the C code using an error code ([`ErrorKind`]) and
/// message. The `GError` error domain is encoded by our type.
#[derive(Debug)]
pub struct Error {
    kind: ErrorKind,
    message: String,
}

impl Error {
    #[must_use]
    pub fn new(kind: ErrorKind, message: &str) -> Self {
        Self {
            kind,
            message: String::from(message),
        }
    }
    #[must_use]
    pub fn kind(&self) -> ErrorKind {
        self.kind
    }
    #[must_use]
    pub fn message(&self) -> &str {
        &self.message
    }
}

impl std::error::Error for Error {}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}: {}", self.kind, self.message)
    }
}

impl From<std::io::Error> for Error {
    fn from(error: std::io::Error) -> Self {
        Self {
            kind: error.kind().into(),
            message: format!("{error}"),
        }
    }
}
