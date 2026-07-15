/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/// Errors that can occur during JSON parsing or access.
#[derive(Debug)]
pub enum JsonError {
    /// The input data is structurally invalid.
    InvalidData(String),
    /// The caller requested a type that does not match the node's actual type.
    WrongType(String),
    /// An I/O error occurred while reading the input stream.
    IoError(String),
    /// The requested key or index was not found.
    NothingToDo(String),
}

impl std::fmt::Display for JsonError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            JsonError::InvalidData(s) => write!(f, "Invalid data: {s}"),
            JsonError::WrongType(s) => write!(f, "Wrong type: {s}"),
            JsonError::IoError(s) => write!(f, "I/O error: {s}"),
            JsonError::NothingToDo(s) => write!(f, "Nothing to do: {s}"),
        }
    }
}

impl From<std::io::Error> for JsonError {
    fn from(e: std::io::Error) -> Self {
        Self::IoError(e.to_string())
    }
}

impl From<&JsonError> for crate::Error {
    fn from(e: &JsonError) -> Self {
        let (code, msg) = match e {
            JsonError::InvalidData(m) | JsonError::WrongType(m) | JsonError::IoError(m) => {
                (crate::ErrorKind::InvalidData, m.as_str())
            }
            JsonError::NothingToDo(m) => (crate::ErrorKind::NothingToDo, m.as_str()),
        };
        crate::Error::new(code, msg)
    }
}

impl std::error::Error for JsonError {}

#[cfg(test)]
mod error_tests {
    use super::*;

    #[test]
    fn display_all_variants() {
        assert_eq!(
            JsonError::InvalidData("bad data".into()).to_string(),
            "Invalid data: bad data"
        );
        assert_eq!(
            JsonError::WrongType("wrong type".into()).to_string(),
            "Wrong type: wrong type"
        );
        assert_eq!(
            JsonError::IoError("read failed".into()).to_string(),
            "I/O error: read failed"
        );
        assert_eq!(
            JsonError::NothingToDo("null value".into()).to_string(),
            "Nothing to do: null value"
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
