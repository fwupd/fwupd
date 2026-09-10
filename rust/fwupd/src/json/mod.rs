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
//! use fwupd::Bitflags;
//! use fwupd::json::{JsonParser, ExportFlags, LoadFlags};
//!
//! let parser = JsonParser::builder()
//!     .max_depth(NonZeroU32::new(10).unwrap())
//!     .max_items(NonZeroU32::new(100).unwrap())
//!     .max_quoted(NonZeroU32::new(1024).unwrap())
//!     .build();
//! let node = parser.load_from_str(r#"{"name": "fwupd", "version": 2}"#, LoadFlags::empty()).unwrap();
//! let s = node.to_json_string(ExportFlags::empty());
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
    /// [Bitflags](crate::Bitflags) of [`ExportFlag`] formatting options.
    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub struct ExportFlags / ExportFlag : u32 {
        /// Indent the output with 2 spaces per level.
        Indent = 1 << 0;
        /// Append a trailing newline.
        TrailingNewline = 1 << 1;
    }
}

declare_bitflags! {
    /// [Bitflags](crate::Bitflags) of [`LoadFlag`] load behavior options.
    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub struct LoadFlags / LoadFlag : u32 {
        /// Trust the input: skip duplicate key checks for faster parsing.
        Trusted = 1 << 0;
    }
}

/// Write `depth * 2` spaces to a [`String`].
fn push_indent(out: &mut String, depth: usize) {
    for _ in 0..depth * 2 {
        out.push(' ');
    }
}

/// Append a JSON-escaped quoted string to `out`.
fn append_quoted_string(out: &mut String, s: &str) {
    out.push('"');
    for ch in s.chars() {
        match ch {
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\t' => out.push_str("\\t"),
            '"' => out.push_str("\\\""),
            c => out.push(c),
        }
    }
    out.push('"');
}

#[cfg(test)]
mod tests;
