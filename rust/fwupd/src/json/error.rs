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
    IoError(std::io::Error),
    /// The requested key or index was not found.
    NothingToDo(String),
}

impl std::fmt::Display for JsonError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            JsonError::InvalidData(s) => write!(f, "invalid data: {s}"),
            JsonError::WrongType(s) => write!(f, "wrong type: {s}"),
            JsonError::IoError(e) => write!(f, "I/O error: {e}"),
            JsonError::NothingToDo(s) => write!(f, "nothing to do: {s}"),
        }
    }
}

impl From<std::io::Error> for JsonError {
    fn from(e: std::io::Error) -> Self {
        Self::IoError(e)
    }
}

impl From<&JsonError> for crate::Error {
    fn from(e: &JsonError) -> Self {
        let (code, msg) = match e {
            JsonError::InvalidData(m) | JsonError::WrongType(m) => {
                (crate::ErrorKind::InvalidData, m.as_str())
            }
            JsonError::IoError(e) => return e.into(),
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
            "invalid data: bad data"
        );
        assert_eq!(
            JsonError::WrongType("wrong type".into()).to_string(),
            "wrong type: wrong type"
        );
        assert_eq!(
            {
                let e = std::io::Error::new(std::io::ErrorKind::Interrupted, "interrupted!");
                JsonError::IoError(e).to_string()
            },
            "I/O error: interrupted!"
        );
        assert_eq!(
            JsonError::NothingToDo("null value".into()).to_string(),
            "nothing to do: null value"
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
