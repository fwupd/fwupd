/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! A streaming tokenizer JSON parser resistant to malicious input.
//!
//! This module provides a JSON parser with configurable limits on nesting depth,
//! number of items, and quoted string length. It mirrors the behavior of the
//! fwupd C JSON parser.
//!
//! # Types
//!
//! - [`JsonNode`] -- a JSON value (null, raw, string, array, or object)
//! - [`JsonArray`] -- an ordered list of [`JsonNode`] values
//! - [`JsonObject`] -- an ordered list of key-value pairs
//! - [`JsonParser`] -- a streaming tokenizer parser with abuse-resistance limits
//!
//! # Example
//!
//! ```
//! use std::num::NonZeroU32;
//! use fwupd::json::{JsonParser, ExportFlags, LoadFlags};
//!
//! let parser = JsonParser::builder()
//!     .max_depth(NonZeroU32::new(10).unwrap())
//!     .max_items(NonZeroU32::new(100).unwrap())
//!     .max_quoted(NonZeroU32::new(1024).unwrap())
//!     .build();
//! let node = parser.load_from_str(r#"{"name": "fwupd", "version": 2}"#, LoadFlags::NONE).unwrap();
//! let s = node.to_json_string(ExportFlags::NONE);
//! assert_eq!(s, r#"{"name": "fwupd", "version": 2}"#);
//! ```

mod array;
mod error;
mod node;
mod object;
mod parser;

pub use array::JsonArray;
pub use error::JsonError;
pub use node::{JsonNode, NodeKind};
pub use object::JsonObject;
pub use parser::JsonParser;

use crate::declare_bitflags;

declare_bitflags! {
    /// Flags controlling JSON export formatting.
    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub struct ExportFlags: u32 {
        /// No formatting.
        const NONE = 0;
        /// Indent the output with 2 spaces per level.
        const INDENT = 1 << 0;
        /// Append a trailing newline.
        const TRAILING_NEWLINE = 1 << 1;
    }
}

declare_bitflags! {
    /// Flags controlling JSON load behavior.
    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub struct LoadFlags: u32 {
        /// No special behavior.
        const NONE = 0;
        /// Trust the input: skip duplicate key checks for faster parsing.
        const TRUSTED = 1 << 0;
    }
}

/// Write `depth * 2` spaces to a [`String`].
fn push_indent(out: &mut String, depth: usize) {
    for _ in 0..depth * 2 {
        out.push(' ');
    }
}

#[cfg(test)]
mod tests;
