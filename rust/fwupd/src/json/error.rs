/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

use std::fmt;

/// Errors that can occur during JSON parsing or access.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum JsonError {
    /// The input data is structurally invalid.
    InvalidData(String),
    /// The caller requested a type that does not match the node's actual type.
    WrongType(String),
    /// An I/O error occurred while reading the input stream.
    IoError(String),
    /// The requested key or index was not found.
    NotFound(String),
    /// The value exists but is null (nothing to do).
    NothingToDo(String),
}

impl fmt::Display for JsonError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidData(msg) => write!(f, "{msg}"),
            Self::WrongType(msg) => write!(f, "{msg}"),
            Self::IoError(msg) => write!(f, "{msg}"),
            Self::NotFound(msg) => write!(f, "{msg}"),
            Self::NothingToDo(msg) => write!(f, "{msg}"),
        }
    }
}

impl std::error::Error for JsonError {}

impl From<std::io::Error> for JsonError {
    fn from(e: std::io::Error) -> Self {
        Self::IoError(e.to_string())
    }
}

#[cfg(test)]
mod error_tests {
    use super::*;

    #[test]
    fn display_all_variants() {
        assert_eq!(
            JsonError::InvalidData("bad data".into()).to_string(),
            "bad data"
        );
        assert_eq!(
            JsonError::WrongType("wrong type".into()).to_string(),
            "wrong type"
        );
        assert_eq!(
            JsonError::IoError("read failed".into()).to_string(),
            "read failed"
        );
        assert_eq!(
            JsonError::NotFound("missing key".into()).to_string(),
            "missing key"
        );
        assert_eq!(
            JsonError::NothingToDo("null value".into()).to_string(),
            "null value"
        );
    }

    #[test]
    fn from_io_error() {
        let io_err = std::io::Error::new(std::io::ErrorKind::BrokenPipe, "pipe broke");
        let json_err = JsonError::from(io_err);
        assert!(matches!(json_err, JsonError::IoError(_)));
        assert!(json_err.to_string().contains("pipe broke"));
    }
}
