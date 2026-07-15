/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

use super::array::JsonArray;
use super::error::JsonError;
use super::node::JsonNode;
use super::{push_indent, ExportFlags, LoadFlags};
use crate::Bitflags;

/// A JSON object -- an ordered list of key-value pairs.
///
/// Keys are stored in insertion order. Lookup is linear, matching the C
/// implementation's behavior. When a duplicate key is inserted, the existing
/// entry is updated in-place (unless [`LoadFlags::TRUSTED`] is set, in which
/// case duplicates are appended without checking).
#[derive(Debug, Clone, PartialEq)]
pub struct JsonObject {
    entries: Vec<(String, JsonNode)>,
}

impl JsonObject {
    /// Creates a new empty JSON object.
    pub fn new() -> Self {
        Self {
            entries: Vec::new(),
        }
    }

    /// Returns the number of key-value pairs.
    #[inline]
    pub fn len(&self) -> usize {
        self.entries.len()
    }

    /// Returns `true` if the object has no entries.
    #[inline]
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }

    /// Clears all entries.
    pub fn clear(&mut self) {
        self.entries.clear();
    }

    /// Gets the node for the given key.
    ///
    /// Returns `Ok(None)` if the key is not found.
    pub fn get_node(&self, key: &str) -> Option<&JsonNode> {
        self.entries.iter().find(|(k, _)| k == key).map(|(_, v)| v)
    }

    /// Gets a string value for the given key.
    ///
    /// Returns `Ok(None)` if the key is not found or the value is null.
    /// Returns `Err` if the key exists but has the wrong type.
    pub fn get_string(&self, key: &str) -> Result<Option<&str>, JsonError> {
        match self.get_node(key) {
            Some(node) => node.get_string(),
            None => Ok(None),
        }
    }

    /// Gets an integer value for the given key.
    ///
    /// Returns `Ok(None)` if the key is not found or the value is null.
    /// Returns `Err` if the key exists but cannot be parsed as an integer.
    pub fn get_integer(&self, key: &str) -> Result<Option<i64>, JsonError> {
        match self.get_node(key) {
            Some(node) => match node.get_raw()? {
                Some(raw) => parse_integer(raw).map(Some),
                None => Ok(None),
            },
            None => Ok(None),
        }
    }

    /// Gets a boolean value for the given key.
    ///
    /// Returns `Ok(None)` if the key is not found or the value is null.
    /// Returns `Err` if the key exists but cannot be parsed as a boolean.
    pub fn get_boolean(&self, key: &str) -> Result<Option<bool>, JsonError> {
        match self.get_node(key) {
            Some(node) => match node.get_raw()? {
                Some(raw) => parse_boolean(raw).map(Some),
                None => Ok(None),
            },
            None => Ok(None),
        }
    }

    /// Gets an object value for the given key.
    ///
    /// Returns `Ok(None)` if the key is not found or the value is null.
    /// Returns `Err` if the key exists but has the wrong type.
    pub fn get_object(&self, key: &str) -> Result<Option<&JsonObject>, JsonError> {
        match self.get_node(key) {
            Some(node) => node.get_object(),
            None => Ok(None),
        }
    }

    /// Gets an array value for the given key.
    ///
    /// Returns `Ok(None)` if the key is not found or the value is null.
    /// Returns `Err` if the key exists but has the wrong type.
    pub fn get_array(&self, key: &str) -> Result<Option<&JsonArray>, JsonError> {
        match self.get_node(key) {
            Some(node) => node.get_array(),
            None => Ok(None),
        }
    }

    /// Adds or replaces a node. If a node with the same key already exists, it
    /// is replaced in-place.
    pub fn add_node(&mut self, key: &str, node: JsonNode) {
        if let Some(entry) = self.entries.iter_mut().find(|(k, _)| k == key) {
            entry.1 = node;
        } else {
            self.entries.push((key.to_owned(), node));
        }
    }

    /// Adds or replaces a string value.
    pub fn add_string(&mut self, key: &str, value: &str) {
        self.add_node(key, JsonNode::Str(value.to_owned()));
    }

    /// Adds or replaces a null-valued string node (matching the C behavior of
    /// `fwupd_json_object_add_string(obj, key, NULL)`).
    pub fn add_null(&mut self, key: &str) {
        self.add_node(key, JsonNode::Null);
    }

    /// Adds or replaces a raw value.
    pub fn add_raw(&mut self, key: &str, value: &str) {
        self.add_node(key, JsonNode::Raw(value.to_owned()));
    }

    /// Adds or replaces an integer value (stored as a raw node).
    pub fn add_integer(&mut self, key: &str, value: i64) {
        self.add_node(key, JsonNode::Raw(value.to_string()));
    }

    /// Adds or replaces a boolean value (stored as a raw node).
    pub fn add_boolean(&mut self, key: &str, value: bool) {
        self.add_node(
            key,
            JsonNode::Raw(if value { "true" } else { "false" }.to_owned()),
        );
    }

    /// Adds or replaces an object value.
    pub fn add_object(&mut self, key: &str, obj: JsonObject) {
        self.add_node(key, JsonNode::Object(obj));
    }

    /// Adds or replaces an array value.
    pub fn add_array(&mut self, key: &str, arr: JsonArray) {
        self.add_node(key, JsonNode::Array(arr));
    }

    /// Internal: add a node, optionally skipping duplicate-key checks (trusted mode).
    pub(crate) fn add_node_internal(&mut self, key: String, node: JsonNode, flags: LoadFlags) {
        if !flags.all(LoadFlags::TRUSTED) {
            if let Some(entry) = self.entries.iter_mut().find(|(k, _)| k == &key) {
                entry.1 = node;
                return;
            }
        }
        self.entries.push((key, node));
    }

    /// Converts the object to a JSON string representation.
    pub fn to_json_string(&self, flags: ExportFlags) -> String {
        let mut out = String::new();
        self.append_to_string(&mut out, 0, flags);
        if flags.all(ExportFlags::TRAILING_NEWLINE) {
            out.push('\n');
        }
        out
    }

    /// Appends this object's JSON representation to `out`.
    pub(crate) fn append_to_string(&self, out: &mut String, depth: usize, flags: ExportFlags) {
        out.push('{');
        if flags.all(ExportFlags::INDENT) {
            out.push('\n');
        }

        for (i, (key, node)) in self.entries.iter().enumerate() {
            if flags.all(ExportFlags::INDENT) {
                push_indent(out, depth + 1);
            }
            out.push('"');
            out.push_str(key);
            out.push_str("\": ");
            node.append_to_string(out, depth + 1, flags);
            if flags.all(ExportFlags::INDENT) {
                if i != self.entries.len() - 1 {
                    out.push(',');
                }
                out.push('\n');
            } else if i != self.entries.len() - 1 {
                out.push_str(", ");
            }
        }

        if flags.all(ExportFlags::INDENT) {
            push_indent(out, depth);
        }
        out.push('}');
    }

    /// Returns an iterator over `(key, node)` pairs.
    pub fn iter(&self) -> std::slice::Iter<'_, (String, JsonNode)> {
        self.entries.iter()
    }
}

impl Default for JsonObject {
    fn default() -> Self {
        Self::new()
    }
}

impl From<JsonObject> for String {
    fn from(object: JsonObject) -> Self {
        object.to_json_string(ExportFlags::INDENT)
    }
}

/// Parse an integer from a raw JSON string.
fn parse_integer(s: &str) -> Result<i64, JsonError> {
    let value: i64 = s
        .parse()
        .map_err(|_| JsonError::InvalidData(format!("cannot parse {s}")))?;
    if value == i64::MAX {
        return Err(JsonError::InvalidData(format!(
            "cannot parse {s} due to overflow"
        )));
    }
    Ok(value)
}

/// Parse a boolean from a raw JSON string.
fn parse_boolean(s: &str) -> Result<bool, JsonError> {
    if s.eq_ignore_ascii_case("true") {
        Ok(true)
    } else if s.eq_ignore_ascii_case("false") {
        Ok(false)
    } else {
        Err(JsonError::InvalidData(format!("cannot parse {s}")))
    }
}

#[cfg(test)]
mod object_tests {
    use super::*;

    #[test]
    fn parse_integer() {
        // valid
        assert_eq!(super::parse_integer("0").unwrap(), 0);
        assert_eq!(super::parse_integer("42").unwrap(), 42);
        assert_eq!(super::parse_integer("-1").unwrap(), -1);
        assert_eq!(super::parse_integer("1000000").unwrap(), 1_000_000);

        // invalid strings
        assert!(matches!(
            super::parse_integer("abc"),
            Err(JsonError::InvalidData(_))
        ));
        assert!(matches!(
            super::parse_integer("12.5"),
            Err(JsonError::InvalidData(_))
        ));
        assert!(matches!(
            super::parse_integer(""),
            Err(JsonError::InvalidData(_))
        ));

        // overflow: i64::MAX is rejected, as are values beyond range
        let max_str = i64::MAX.to_string();
        assert!(matches!(
            super::parse_integer(&max_str),
            Err(JsonError::InvalidData(_))
        ));
        assert!(matches!(
            super::parse_integer("99999999999999999999"),
            Err(JsonError::InvalidData(_))
        ));
    }

    #[test]
    fn parse_boolean() {
        // valid (case-insensitive)
        assert_eq!(super::parse_boolean("true").unwrap(), true);
        assert_eq!(super::parse_boolean("false").unwrap(), false);
        assert_eq!(super::parse_boolean("TRUE").unwrap(), true);
        assert_eq!(super::parse_boolean("False").unwrap(), false);

        // invalid
        assert!(matches!(
            super::parse_boolean("yes"),
            Err(JsonError::InvalidData(_))
        ));
        assert!(matches!(
            super::parse_boolean("1"),
            Err(JsonError::InvalidData(_))
        ));
        assert!(matches!(
            super::parse_boolean(""),
            Err(JsonError::InvalidData(_))
        ));
    }

    #[test]
    fn get_integer_errors() {
        let mut obj = JsonObject::new();
        obj.add_string("str", "alice");
        obj.add_raw("bad", "not_a_number");
        obj.add_null("null_key");

        // wrong type (string, not raw)
        assert!(matches!(
            obj.get_integer("str"),
            Err(JsonError::WrongType(_))
        ));
        // unparsable raw value
        assert!(matches!(
            obj.get_integer("bad"),
            Err(JsonError::InvalidData(_))
        ));
        // missing key returns None
        assert_eq!(obj.get_integer("nope").unwrap(), None);
        // null value returns None
        assert_eq!(obj.get_integer("null_key").unwrap(), None);
    }

    #[test]
    fn get_boolean_errors() {
        let mut obj = JsonObject::new();
        obj.add_string("str", "alice");
        obj.add_raw("bad", "maybe");
        obj.add_null("null_key");

        assert!(matches!(
            obj.get_boolean("str"),
            Err(JsonError::WrongType(_))
        ));
        assert!(matches!(
            obj.get_boolean("bad"),
            Err(JsonError::InvalidData(_))
        ));
        assert_eq!(obj.get_boolean("nope").unwrap(), None);
        assert_eq!(obj.get_boolean("null_key").unwrap(), None);
    }

    #[test]
    fn get_string_errors() {
        let mut obj = JsonObject::new();
        obj.add_integer("num", 42);
        obj.add_null("null_key");

        assert!(matches!(
            obj.get_string("num"),
            Err(JsonError::WrongType(_))
        ));
        assert_eq!(obj.get_string("nope").unwrap(), None);
        assert_eq!(obj.get_string("null_key").unwrap(), None);
    }
}
