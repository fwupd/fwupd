/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

use super::array::JsonArray;
use super::error::JsonError;
use super::object::JsonObject;
use super::ExportFlags;

/// The kind of a JSON node, for reporting in error messages.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum NodeKind {
    Null,
    Raw,
    String,
    Array,
    Object,
}

impl std::fmt::Display for NodeKind {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Null => f.write_str("null"),
            Self::Raw => f.write_str("raw"),
            Self::String => f.write_str("string"),
            Self::Array => f.write_str("array"),
            Self::Object => f.write_str("object"),
        }
    }
}

/// A JSON value.
///
/// This is the central type of the JSON model. Each node is one of:
/// - `Null` -- the JSON `null` literal
/// - `Raw` -- an unquoted value (number, boolean)
/// - `Str` -- a quoted string
/// - `Array` -- an ordered list of nodes
/// - `Object` -- an ordered list of key-value pairs
#[derive(Debug, Clone, PartialEq)]
pub enum JsonNode {
    /// The JSON `null` literal.
    Null,
    /// An unquoted raw value (number or boolean literal).
    Raw(String),
    /// A quoted JSON string (with escapes already resolved).
    Str(String),
    /// A JSON array.
    Array(JsonArray),
    /// A JSON object.
    Object(JsonObject),
}

impl JsonNode {
    /// Returns the kind of this node.
    pub fn kind(&self) -> NodeKind {
        match self {
            Self::Null => NodeKind::Null,
            Self::Raw(_) => NodeKind::Raw,
            Self::Str(_) => NodeKind::String,
            Self::Array(_) => NodeKind::Array,
            Self::Object(_) => NodeKind::Object,
        }
    }

    /// Gets the raw value string.
    ///
    /// Returns `Ok(None)` if this is a `Null` node. Returns `Err` if this
    /// node is not a raw value or null.
    pub fn get_raw(&self) -> Result<Option<&str>, JsonError> {
        match self {
            Self::Raw(s) => Ok(Some(s)),
            Self::Null => Ok(None),
            other => Err(JsonError::WrongType(format!(
                "json_node kind was {}, not raw",
                other.kind()
            ))),
        }
    }

    /// Gets the string value.
    ///
    /// Returns `Ok(None)` if this is a `Null` node. Returns `Err` if this
    /// node is not a string or null.
    pub fn get_string(&self) -> Result<Option<&str>, JsonError> {
        match self {
            Self::Str(s) => Ok(Some(s)),
            Self::Null => Ok(None),
            other => Err(JsonError::WrongType(format!(
                "json_node kind was {}, not string",
                other.kind()
            ))),
        }
    }

    /// Gets the contained object.
    ///
    /// Returns `Ok(None)` if this is a `Null` node. Returns `Err` if this
    /// node is not an object or null.
    pub fn get_object(&self) -> Result<Option<&JsonObject>, JsonError> {
        match self {
            Self::Object(obj) => Ok(Some(obj)),
            Self::Null => Ok(None),
            other => Err(JsonError::WrongType(format!(
                "json_node kind was {}, not object",
                other.kind()
            ))),
        }
    }

    /// Gets the contained array.
    ///
    /// Returns `Ok(None)` if this is a `Null` node. Returns `Err` if this
    /// node is not an array or null.
    pub fn get_array(&self) -> Result<Option<&JsonArray>, JsonError> {
        match self {
            Self::Array(arr) => Ok(Some(arr)),
            Self::Null => Ok(None),
            other => Err(JsonError::WrongType(format!(
                "json_node kind was {}, not array",
                other.kind()
            ))),
        }
    }

    /// Converts this node to a JSON string representation.
    ///
    /// Note: `TRAILING_NEWLINE` is not handled here (matching the C behavior
    /// of `fwupd_json_node_to_string`). Only [`JsonObject::to_json_string`]
    /// applies trailing newlines.
    pub fn to_json_string(&self, flags: ExportFlags) -> String {
        let mut out = String::new();
        self.append_to_string(&mut out, 0, flags);
        out
    }

    /// Append this node's JSON representation to `out`.
    pub(crate) fn append_to_string(&self, out: &mut String, depth: usize, flags: ExportFlags) {
        match self {
            Self::Null => out.push_str("null"),
            Self::Raw(s) => out.push_str(s),
            Self::Str(s) => append_quoted_string(out, s),
            Self::Array(arr) => arr.append_to_string(out, depth, flags),
            Self::Object(obj) => obj.append_to_string(out, depth, flags),
        }
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

/// Helper used by the [`std::fmt::Display`] impl to write a node with formatting.
pub(crate) fn fmt_node(
    node: &JsonNode,
    f: &mut std::fmt::Formatter<'_>,
    depth: usize,
    flags: ExportFlags,
) -> std::fmt::Result {
    // We reuse the string-building path for Display.
    let mut buf = String::new();
    node.append_to_string(&mut buf, depth, flags);
    f.write_str(&buf)
}

impl std::fmt::Display for JsonNode {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        fmt_node(self, f, 0, ExportFlags::NONE)
    }
}

impl From<JsonNode> for String {
    fn from(node: JsonNode) -> Self {
        node.to_json_string(ExportFlags::INDENT)
    }
}

#[cfg(test)]
mod node_tests {
    use super::*;

    #[test]
    fn get_typed_wrong_type() {
        // get_raw on non-raw types
        assert!(matches!(
            JsonNode::Str("hello".into()).get_raw(),
            Err(JsonError::WrongType(_))
        ));
        assert!(matches!(
            JsonNode::Array(JsonArray::new()).get_raw(),
            Err(JsonError::WrongType(_))
        ));
        assert!(matches!(
            JsonNode::Object(JsonObject::new()).get_raw(),
            Err(JsonError::WrongType(_))
        ));

        // get_string on non-string types
        assert!(matches!(
            JsonNode::Raw("42".into()).get_string(),
            Err(JsonError::WrongType(_))
        ));
        assert!(matches!(
            JsonNode::Array(JsonArray::new()).get_string(),
            Err(JsonError::WrongType(_))
        ));
        assert!(matches!(
            JsonNode::Object(JsonObject::new()).get_string(),
            Err(JsonError::WrongType(_))
        ));

        // get_object on non-object types
        assert!(matches!(
            JsonNode::Raw("42".into()).get_object(),
            Err(JsonError::WrongType(_))
        ));
        assert!(matches!(
            JsonNode::Str("hello".into()).get_object(),
            Err(JsonError::WrongType(_))
        ));
        assert!(matches!(
            JsonNode::Array(JsonArray::new()).get_object(),
            Err(JsonError::WrongType(_))
        ));

        // get_array on non-array types
        assert!(matches!(
            JsonNode::Raw("42".into()).get_array(),
            Err(JsonError::WrongType(_))
        ));
        assert!(matches!(
            JsonNode::Str("hello".into()).get_array(),
            Err(JsonError::WrongType(_))
        ));
        assert!(matches!(
            JsonNode::Object(JsonObject::new()).get_array(),
            Err(JsonError::WrongType(_))
        ));
    }

    #[test]
    fn display_and_conversion() {
        assert_eq!(format!("{}", JsonNode::Raw("42".into())), "42");
        assert_eq!(format!("{}", JsonNode::Str("hello".into())), "\"hello\"");
        assert_eq!(format!("{}", JsonNode::Null), "null");

        let s: String = JsonNode::Str("test".into()).into();
        assert_eq!(s, "\"test\"");
    }

    #[test]
    fn kind_and_display() {
        assert_eq!(JsonNode::Null.kind(), NodeKind::Null);
        assert_eq!(JsonNode::Raw("1".into()).kind(), NodeKind::Raw);
        assert_eq!(JsonNode::Str("s".into()).kind(), NodeKind::String);
        assert_eq!(JsonNode::Array(JsonArray::new()).kind(), NodeKind::Array);
        assert_eq!(JsonNode::Object(JsonObject::new()).kind(), NodeKind::Object);

        assert_eq!(format!("{}", NodeKind::Null), "null");
        assert_eq!(format!("{}", NodeKind::Raw), "raw");
        assert_eq!(format!("{}", NodeKind::String), "string");
        assert_eq!(format!("{}", NodeKind::Array), "array");
        assert_eq!(format!("{}", NodeKind::Object), "object");
    }
}
