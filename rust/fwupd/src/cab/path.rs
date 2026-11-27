/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

use super::archive::{CFString, Utf8CFString};
use super::CabError;
use std::ffi::CStr;
use std::fmt;
use std::path::PathBuf;

/// A null-terminated string inside a CAB archive that represents a
/// (sanitized and validated) UTF-8 compatible file path.
///
/// The raw [`path`](CabPath::path) keeps the original path
/// used during construction and may contain backslashes
/// as path separators.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CabPath(pub(super) Utf8CFString);

impl CabPath {
    /// We might get windows paths in CAB files so we need
    /// to do everything by hand, e.g. `Path` on Unix doesn'tt
    /// check for drive letters.
    fn path_is_valid(path: &str) -> Result<(), CabError> {
        let unixy_path = path.replace('\\', "/");

        if unixy_path.starts_with('/') {
            return Err(CabError::Format(format!(
                "absolute path not allowed: {path}"
            )));
        }

        let drive_prefix: Vec<&str> = unixy_path.splitn(2, '/').collect();
        for drive in drive_prefix.iter().take(1) {
            if drive.contains(':') {
                return Err(CabError::Format(format!(
                    "drive letters in paths are not allowed: {path}"
                )));
            }
        }

        if unixy_path.split('/').any(|p| p == "..") {
            return Err(CabError::Format(format!(
                "path traversal not allowed: {path}"
            )));
        }

        Ok(())
    }

    /// Construct a new `CabPath` from the given string, sanitizing
    /// and validating the path in the process.
    ///
    /// The input string
    pub(super) fn new(path: &CFString) -> Result<Self, CabError> {
        if path.cstr.is_empty() {
            return Err(CabError::Format("empty filename".into()));
        }

        // Note: the old C code was more restrictive, allowing ascii noly
        let utf8 = Utf8CFString::new(path.clone())?;
        Self::path_is_valid(utf8.as_ref())?;
        Ok(Self(utf8))
    }

    /// Return the full path as a `CStr`.
    ///
    /// This value keeps the original path used during construction and
    /// may contain backslashes as separators.
    ///
    /// Use [`as_pathbuf()`](Self::as_pathbuf) for conversion to
    /// unix-y paths.
    #[must_use]
    pub fn path(&self) -> &CStr {
        &self.0.cstr
    }

    /// Return the full path as [`PathBuf`], using `\\` and `/` as
    /// folder separators in the source path.
    ///
    /// Technically a valid unix path could contain a valid `\\`
    /// but without a drive prefix or an absolute path we cannot
    /// know if the path is unix-y or windows-y. So we simply don't care about
    /// the niche case of `\\` in a file path.
    #[must_use]
    pub fn as_pathbuf(&self) -> PathBuf {
        let utf8 = String::from(&self.0);
        let mut pathbuf = PathBuf::new();
        for component in utf8.split(['\\', '/']) {
            pathbuf = pathbuf.join(component);
        }
        pathbuf
    }

    /// Return true if the path is an ascii-only string.
    #[must_use]
    pub fn is_ascii(&self) -> bool {
        self.0.cstr.as_bytes().is_ascii()
    }

    /// The basename of the path (i.e. the component after the last `/` or `\\`).
    #[must_use]
    #[allow(clippy::missing_panics_doc)]
    pub fn basename(&self) -> &CStr {
        let bytes = self.0.cstr.as_bytes_with_nul();
        let data = &bytes[..bytes.len() - 1];
        let last_sep = data.iter().rposition(|&b| b == b'/' || b == b'\\');
        match last_sep {
            Some(pos) => CStr::from_bytes_with_nul(&bytes[pos + 1..]).expect("valid CStr slice"),
            None => &self.0.cstr,
        }
    }
}

impl std::str::FromStr for CabPath {
    type Err = CabError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        Self::new(&CFString::from(s))
    }
}

impl From<&CabPath> for CFString {
    fn from(path: &CabPath) -> Self {
        path.0 .0.clone()
    }
}

impl fmt::Display for CabPath {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.0)
    }
}

#[cfg(test)]
impl PartialEq<&str> for CabPath {
    fn eq(&self, other: &&str) -> bool {
        self.0.cstr.to_bytes() == other.as_bytes()
    }
}

#[cfg(test)]
impl PartialEq<CabPath> for &str {
    fn eq(&self, other: &CabPath) -> bool {
        other == self
    }
}

#[cfg(test)]
impl From<&str> for CabPath {
    fn from(value: &str) -> Self {
        CabPath::new(&value.into()).unwrap()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn validate_path_ok() {
        assert!(CabPath::new(&CFString::from("test.txt")).is_ok());
        assert!(CabPath::new(&CFString::from("dir/test.txt")).is_ok());
        assert!(CabPath::new(&CFString::from("a/b/c.bin")).is_ok());
        assert!(CabPath::new(&CFString::from(".hidden")).is_ok());
    }

    #[test]
    fn validate_path_empty() {
        assert!(CabPath::new(&CFString::from("")).is_err());
    }

    #[test]
    fn validate_path_absolute() {
        assert!(CabPath::new(&CFString::from("/etc/passwd")).is_err());
        assert!(CabPath::new(&CFString::from("\\windows\\system32")).is_err());
    }

    #[test]
    fn validate_path_traversal() {
        assert!(CabPath::new(&CFString::from("../etc/passwd")).is_err());
        assert!(CabPath::new(&CFString::from("foo/../../bar")).is_err());
        assert!(CabPath::new(&CFString::from("..\\windows")).is_err());
    }

    #[test]
    fn validate_path_drive_letter() {
        assert!(CabPath::new(&CFString::from("C:\\windows\\system32")).is_err());
        assert!(CabPath::new(&CFString::from("C:/foo/bar")).is_err());
        assert!(CabPath::new(&CFString::from("Z:\\file.txt")).is_err());
    }

    #[test]
    fn path_backslash_accepted() {
        assert!(CabPath::new(&CFString::from("dir\\file.txt")).is_ok());
        let path = CabPath::new(&CFString::from("a\\b\\c")).unwrap();
        assert_eq!(path.basename().to_string_lossy(), "c");
    }

    #[test]
    fn path_forward_slash() {
        assert!(CabPath::new(&CFString::from("dir/file.txt")).is_ok());
    }

    #[test]
    fn basename_simple() {
        let path = CabPath::new(&CFString::from("file.txt")).unwrap();
        assert_eq!(path.basename().to_string_lossy(), "file.txt");
    }

    #[test]
    fn basename_with_dir() {
        let path = CabPath::new(&CFString::from("dir/file.txt")).unwrap();
        assert_eq!(path.basename().to_string_lossy(), "file.txt");
        let path = CabPath::new(&CFString::from("a/b/c/file.bin")).unwrap();
        assert_eq!(path.basename().to_string_lossy(), "file.bin");
    }

    #[test]
    fn basename_with_backslash() {
        let path = CabPath::new(&CFString::from("dir\\file.txt")).unwrap();
        assert_eq!(path.basename().to_string_lossy(), "file.txt");
    }
}
