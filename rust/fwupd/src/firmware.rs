/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Firmware types and flags.

use bitflags::bitflags;

bitflags! {
    /// Flags controlling firmware parsing, matching the C `FuFirmwareParseFlags`.
    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub struct FuFirmwareParseFlags: u64 {
        /// No flags set.
        const NONE = 0;
        /// Skip checksum verification.
        const IGNORE_CHECKSUM = 1 << 6;
        /// Ignore VID/PID mismatches.
        const IGNORE_VID_PID = 1 << 7;
        /// Do not use heuristic searching.
        const NO_SEARCH = 1 << 8;
        /// Cache the stream for later use.
        const CACHE_STREAM = 1 << 10;
        /// Cache the blob for later use.
        const CACHE_BLOB = 1 << 11;
        /// Only trust post-quantum signatures.
        const ONLY_TRUST_PQ_SIGNATURES = 1 << 12;
        /// Only parse the partition layout.
        const ONLY_PARTITION_LAYOUT = 1 << 13;
        /// Strip directory components from filenames, keeping only the basename.
        const ONLY_BASENAME = 1 << 14;
    }
}
