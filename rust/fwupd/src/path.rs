/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

use crate::{declare_bitflags, Bitflags};

declare_bitflags! {
    /// Possible set of validations to perform on a string that *looks*
    /// like a path.
    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub struct PathValidationFlags / PathValidationFlag : u32 {
        DisallowAbsolutePaths = 1 << 0;
        /// Disallow paths containing backslashes
        DisallowMsDosPaths = 1 << 1;
        /// Disallow paths starting with drive letters
        DisallowDriveLetters = 1 << 2;
        /// Disallow `..`-style traversal
        DisallowTraversal = 1 << 3;
        /// Disallow any non-ascii characters
        DisallowNonAscii = 1 << 4;
        /// Disallow the empty path
        DisallowEmptyPath = 1 << 5;
        /// Whether special `.` and `..` are allowed
        DisallowDotAndDotDot = 1 << 6;
    }
}

/// A trait for various path-related functionality.
pub trait PathExt {
    type Basename: Sized;

    /// Validates the path, returns `true` if none of the
    /// [`PathValidationFlag`] checks fail or `false` otherwise.
    ///
    /// Unless [`PathValidationFlag::DisallowMsDosPaths`] is given
    /// any backslash in a path is treated as path separator. The
    /// paths ``/etc/passwd`` and `\etc\passwd` are considered the
    /// same path for validation purposes.
    ///
    /// # Examples
    ///
    /// ```
    /// use fwupd::path::{PathExt, PathValidationFlag, PathValidationFlags};
    /// use fwupd::Bitflags;
    ///
    /// // by default nothing is checked
    /// let no_checks = PathValidationFlags::empty();
    /// assert!("/etc/passwd".validate_path(no_checks));
    ///
    /// // reject anything that could escape the extraction directory
    /// let strict = PathValidationFlag::DisallowAbsolutePaths.as_mask()
    ///     | PathValidationFlag::DisallowTraversal
    ///     | PathValidationFlag::DisallowMsDosPaths
    ///     | PathValidationFlag::DisallowDriveLetters;
    /// assert!("payload/firmware.bin".validate_path(strict));
    /// assert!(!"../etc/passwd".validate_path(strict));
    /// assert!(!"C:\\evil.bin".validate_path(strict));
    ///
    /// // checks are opt-in individually, e.g. backslash-separated paths
    /// // are common in CAB files
    /// let msdos_ok = PathValidationFlag::DisallowAbsolutePaths.as_mask()
    ///     | PathValidationFlag::DisallowTraversal;
    /// assert!("dir\\file.bin".validate_path(msdos_ok));
    /// ```
    fn validate_path(&self, flags: PathValidationFlags) -> bool;

    /// Return self as a path that is safe to use as a basename (i.e. filename).
    ///
    /// # Examples
    ///
    /// ```
    /// use fwupd::path::PathExt;
    ///
    /// assert_eq!(".what/is/../that?".to_safe_basename().unwrap(),
    ///            "_what_is___that?");
    /// ```
    fn to_safe_basename(&self) -> Option<Self::Basename>;
}

impl PathExt for str {
    type Basename = String;

    fn validate_path(&self, flags: PathValidationFlags) -> bool {
        let flag = PathValidationFlag::DisallowEmptyPath;

        if flags.contains(flag) && self.is_empty() {
            return false;
        }

        let flag = PathValidationFlag::DisallowMsDosPaths;
        if flags.contains(flag) && self.contains('\\') {
            return false;
        }

        let unixy_path = self.replace('\\', "/");
        let flag = PathValidationFlag::DisallowNonAscii;
        if flags.contains(flag) && !unixy_path.is_ascii() {
            return false;
        }

        let flag = PathValidationFlag::DisallowDotAndDotDot;
        if flags.contains(flag) && (unixy_path == "." || self == "..") {
            return false;
        }

        let flag = PathValidationFlag::DisallowAbsolutePaths;
        if flags.contains(flag) && (unixy_path.starts_with('/') || self.starts_with('\\')) {
            return false;
        }

        let flag = PathValidationFlag::DisallowTraversal;
        if flags.contains(flag) && unixy_path.split('/').any(|p| p == "..") {
            return false;
        }

        let flag = PathValidationFlag::DisallowDriveLetters;
        if flags.contains(flag) {
            let drive_prefix: Vec<&str> = unixy_path.splitn(2, '/').collect();
            for drive in drive_prefix.iter().take(1) {
                if drive.contains(':') {
                    return false;
                }
            }
        }

        true
    }

    fn to_safe_basename(&self) -> Option<String> {
        let s = self.replace(['/', '\\'], "_").replace("..", "_");
        if s.starts_with('.') {
            Some(s.replacen('.', "_", 1))
        } else {
            Some(s)
        }
    }
}

impl PathExt for std::path::Path {
    type Basename = String;

    fn validate_path(&self, flags: PathValidationFlags) -> bool {
        // A non-UTF-8 path cannot be checked for separators, traversal,
        // etc. without a lossy conversion mangling the raw bytes. If the
        // caller asked for the non-ascii check it fails; otherwise we
        // cannot check anything else either way, so we let it pass.
        match self.to_str() {
            Some(s) => s.validate_path(flags),
            None => !flags.contains(PathValidationFlag::DisallowNonAscii),
        }
    }

    fn to_safe_basename(&self) -> Option<Self::Basename> {
        self.to_str()?.to_safe_basename()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Every `Disallow*` flag set: everything questionable is rejected.
    fn strict() -> PathValidationFlags {
        PathValidationFlag::DisallowAbsolutePaths.as_mask()
            | PathValidationFlag::DisallowMsDosPaths
            | PathValidationFlag::DisallowDriveLetters
            | PathValidationFlag::DisallowTraversal
            | PathValidationFlag::DisallowNonAscii
            | PathValidationFlag::DisallowEmptyPath
            | PathValidationFlag::DisallowDotAndDotDot
    }

    #[test]
    fn safe_paths() {
        for path in [
            "firmware.bin",
            "payload/firmware.bin",
            "a/b/c/firmware.cab",
            ".hidden",
            "dir.d/firmware.bin",
            "dir/foo:bar.bin",
        ] {
            assert!(path.validate_path(strict()), "rejected: {path}");
        }
    }

    #[test]
    fn no_flags_allows_everything() {
        let flags = PathValidationFlags::empty();
        assert!("/etc/../C:\\evil\\x".validate_path(flags));
        assert!("".validate_path(flags));
        assert!(".".validate_path(flags));
        assert!("f\u{e4}rmw\u{e4}re.bin".validate_path(flags));
    }

    #[test]
    fn empty_path() {
        let flags = PathValidationFlag::DisallowEmptyPath.as_mask();
        assert!(!"".validate_path(flags));
        assert!("firmware.bin".validate_path(flags));
    }

    #[test]
    fn non_ascii_path() {
        let flags = PathValidationFlag::DisallowNonAscii.as_mask();
        assert!(!"f\u{e4}rmw\u{e4}re.bin".validate_path(flags));
        assert!("firmware.bin".validate_path(flags));
    }

    #[test]
    fn special_paths() {
        let flags = PathValidationFlag::DisallowDotAndDotDot.as_mask();
        assert!(!".".validate_path(flags));
        assert!(!"..".validate_path(flags));
        assert!(".hidden".validate_path(flags));
    }

    #[test]
    fn absolute_paths() {
        let flags = PathValidationFlag::DisallowAbsolutePaths.as_mask();
        for path in ["/etc/fstab", "/tmp/evil", "/"] {
            assert!(!path.validate_path(flags), "accepted: {path}");
        }
        assert!("payload/firmware.bin".validate_path(flags));
    }

    #[test]
    fn path_traversal() {
        let flags = PathValidationFlag::DisallowTraversal.as_mask();
        for path in ["../etc/passwd", "foo/../../bar", "foo/..", "a/../b"] {
            assert!(!path.validate_path(flags), "accepted: {path}");
        }
        assert!("payload/firmware.bin".validate_path(flags));
    }

    #[test]
    fn msdos_paths() {
        let flags = PathValidationFlag::DisallowMsDosPaths.as_mask();
        for path in ["foo\\bar", "dir\\file.bin", "\\windows\\system32"] {
            assert!(!path.validate_path(flags), "accepted: {path}");
        }
        assert!("dir/file.bin".validate_path(flags));
    }

    #[test]
    fn drive_letters() {
        let flags = PathValidationFlag::DisallowDriveLetters.as_mask();
        for path in ["C:/windows/system32", "C:evil.bin", "Z:/file.txt"] {
            assert!(!path.validate_path(flags), "accepted: {path}");
        }

        // colon in a later component is fine
        assert!("dir/foo:bar.bin".validate_path(flags));

        // without the flag drive letters are allowed
        assert!("C:/windows/system32".validate_path(PathValidationFlags::empty()));
    }

    #[test]
    fn to_safe_basename() {
        // already a safe basename, unchanged
        assert_eq!(
            "firmware.bin".to_safe_basename().as_deref(),
            Some("firmware.bin")
        );
        assert_eq!(
            "file-1.2.bin".to_safe_basename().as_deref(),
            Some("file-1.2.bin")
        );

        // separators are replaced, so the result can no longer escape the
        // extraction directory
        assert_eq!(
            "dir/file.bin".to_safe_basename().as_deref(),
            Some("dir_file.bin")
        );
        assert_eq!(
            "dir\\file.bin".to_safe_basename().as_deref(),
            Some("dir_file.bin")
        );
        assert_eq!(
            "../etc/passwd".to_safe_basename().as_deref(),
            Some("__etc_passwd")
        );
        assert_eq!(
            "C:\\evil.bin".to_safe_basename().as_deref(),
            Some("C:_evil.bin")
        );

        // `..` components collapse into a single `_` each
        assert_eq!("a..b".to_safe_basename().as_deref(), Some("a_b"));
        assert_eq!("..".to_safe_basename().as_deref(), Some("_"));
        assert_eq!(".".to_safe_basename().as_deref(), Some("_"));

        // leading dots are hidden files, mangle them too
        assert_eq!(".hidden".to_safe_basename().as_deref(), Some("_hidden"));
        assert_eq!(
            ".ssh/authorized_keys".to_safe_basename().as_deref(),
            Some("_ssh_authorized_keys")
        );

        // colons are left alone, they are not special on unix
        assert_eq!(
            "dir/foo:bar.bin".to_safe_basename().as_deref(),
            Some("dir_foo:bar.bin")
        );

        // empty stays empty
        assert_eq!("".to_safe_basename().as_deref(), Some(""));
    }

    #[test]
    fn to_safe_basename_path_type() {
        use std::path::{Path, PathBuf};

        // delegates to the str impl
        assert_eq!(
            Path::new("dir/file.bin").to_safe_basename().as_deref(),
            Some("dir_file.bin")
        );
        assert_eq!(
            Path::new("../etc/passwd").to_safe_basename().as_deref(),
            Some("__etc_passwd")
        );

        // PathBuf works through Deref<Target = Path>
        let pathbuf = PathBuf::from(".hidden");
        assert_eq!(pathbuf.to_safe_basename().as_deref(), Some("_hidden"));

        // non-UTF-8 paths have no safe basename
        #[cfg(unix)]
        {
            use std::os::unix::ffi::OsStrExt;
            let path = Path::new(std::ffi::OsStr::from_bytes(b"f\xe4rware.bin"));
            assert_eq!(path.to_safe_basename(), None);
        }
    }

    #[test]
    fn path_type() {
        use std::path::{Path, PathBuf};

        // delegates to the str impl
        assert!(Path::new("payload/firmware.bin").validate_path(strict()));
        assert!(!Path::new("/etc/fstab").validate_path(strict()));
        assert!(!Path::new("../evil").validate_path(strict()));
        assert!(!Path::new("C:/evil.bin").validate_path(strict()));

        // PathBuf works through Deref<Target = Path>
        let pathbuf = PathBuf::from("firmware.bin");
        assert!(pathbuf.validate_path(strict()));

        // non-UTF-8 paths fail the non-ascii check...
        #[cfg(unix)]
        {
            use std::os::unix::ffi::OsStrExt;
            let path = Path::new(std::ffi::OsStr::from_bytes(b"f\xe4rware.bin"));
            assert!(!path.validate_path(strict()));
            assert!(!path.validate_path(PathValidationFlag::DisallowNonAscii.as_mask()));
            // ...but pass if no checks (or none that apply) are requested
            assert!(path.validate_path(PathValidationFlags::empty()));
        }
    }
}
