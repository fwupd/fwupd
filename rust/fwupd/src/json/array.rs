/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

use std::sync::Arc;

use super::error::JsonError;
use super::node::JsonNode;
use super::object::JsonObject;
use super::{push_indent, ExportFlag, ExportFlags};
use crate::Bitflags;

/// A JSON array -- an ordered list of [`JsonNode`] values.
#[derive(Debug, Clone, PartialEq)]
pub struct JsonArray {
    nodes: Vec<Arc<JsonNode>>,
}

impl JsonArray {
    /// Creates a new empty JSON array.
    #[must_use]
    pub fn new() -> Self {
        Self { nodes: Vec::new() }
    }

    /// Returns the number of elements in the array.
    #[inline]
    #[must_use]
    pub fn len(&self) -> usize {
        self.nodes.len()
    }

    /// Returns `true` if the array is empty.
    #[inline]
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.nodes.is_empty()
    }

    /// Gets the node at the given index.
    ///
    /// Returns `None` if the index is out of bounds.
    #[must_use]
    pub fn get_node(&self, idx: usize) -> Option<Arc<JsonNode>> {
        self.nodes.get(idx).map(Arc::clone)
    }

    /// Gets the string value at the given index.
    ///
    /// Returns `Ok(None)` if the index is out of bounds or the node is null.
    ///
    /// # Errors
    ///
    /// Returns [`JsonError::WrongType`] if the node at `idx` is not a string.
    pub fn get_string(&self, idx: usize) -> Result<Option<&str>, JsonError> {
        match self.nodes.get(idx) {
            Some(node) => node.as_ref().get_string(),
            None => Ok(None),
        }
    }

    /// Gets the raw value at the given index.
    ///
    /// Returns `Ok(None)` if the index is out of bounds or the node is null.
    ///
    /// # Errors
    ///
    /// Returns [`JsonError::WrongType`] if the node at `idx` is not a raw value.
    pub fn get_raw(&self, idx: usize) -> Result<Option<&str>, JsonError> {
        match self.nodes.get(idx) {
            Some(node) => node.as_ref().get_raw(),
            None => Ok(None),
        }
    }

    /// Gets the object at the given index.
    ///
    /// Returns `Ok(None)` if the index is out of bounds or the node is null.
    ///
    /// # Errors
    ///
    /// Returns [`JsonError::WrongType`] if the node at `idx` is not an object.
    pub fn get_object(&self, idx: usize) -> Result<Option<Arc<JsonObject>>, JsonError> {
        match self.get_node(idx) {
            Some(node) => node.as_ref().get_object(),
            None => Ok(None),
        }
    }

    /// Gets the array at the given index.
    ///
    /// Returns `Ok(None)` if the index is out of bounds or the node is null.
    ///
    /// # Errors
    ///
    /// Returns [`JsonError::WrongType`] if the node at `idx` is not an array.
    pub fn get_array(&self, idx: usize) -> Result<Option<Arc<JsonArray>>, JsonError> {
        match self.get_node(idx) {
            Some(node) => node.as_ref().get_array(),
            None => Ok(None),
        }
    }

    /// Adds a node to the end of the array.
    pub fn add_node(&mut self, node: Arc<JsonNode>) {
        self.nodes.push(node);
    }

    /// Adds a string value to the end of the array.
    pub fn add_string(&mut self, value: &str) {
        self.nodes.push(Arc::new(JsonNode::Str(value.to_owned())));
    }

    /// Adds a raw value (number, boolean literal) to the end of the array.
    pub fn add_raw(&mut self, value: &str) {
        self.nodes.push(Arc::new(JsonNode::Raw(value.to_owned())));
    }

    /// Adds an object to the end of the array.
    pub fn add_object(&mut self, obj: Arc<JsonObject>) {
        self.nodes.push(Arc::new(JsonNode::Object(obj)));
    }

    /// Adds another array to the end of this array.
    pub fn add_array(&mut self, arr: Arc<JsonArray>) {
        self.nodes.push(Arc::new(JsonNode::Array(arr)));
    }

    /// Converts the array to a JSON string representation.
    #[must_use]
    pub fn to_json_string(&self, flags: ExportFlags) -> String {
        let mut out = String::new();
        self.append_to_string(&mut out, 0, flags);
        out
    }

    /// Appends this array's JSON representation to `out`.
    pub(crate) fn append_to_string(&self, out: &mut String, depth: usize, flags: ExportFlags) {
        out.push('[');
        if flags.contains(ExportFlag::Indent) {
            out.push('\n');
        }

        for (i, node) in self.nodes.iter().enumerate() {
            if flags.contains(ExportFlag::Indent) {
                push_indent(out, depth + 1);
            }
            node.append_to_string(out, depth + 1, flags);
            if flags.contains(ExportFlag::Indent) {
                if i != self.nodes.len() - 1 {
                    out.push(',');
                }
                out.push('\n');
            } else if i != self.nodes.len() - 1 {
                out.push_str(", ");
            }
        }

        if flags.contains(ExportFlag::Indent) {
            push_indent(out, depth);
        }
        out.push(']');
        if flags.contains(ExportFlag::TrailingNewline) {
            out.push('\n');
        }
    }

    /// Returns an iterator over the nodes in this array.
    pub fn iter(&self) -> std::slice::Iter<'_, Arc<JsonNode>> {
        self.nodes.iter()
    }
}

impl<'a> IntoIterator for &'a JsonArray {
    type Item = &'a Arc<JsonNode>;
    type IntoIter = std::slice::Iter<'a, Arc<JsonNode>>;
    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

impl Default for JsonArray {
    fn default() -> Self {
        Self::new()
    }
}

impl From<JsonArray> for String {
    fn from(array: JsonArray) -> Self {
        array.to_json_string(ExportFlag::Indent.into())
    }
}

#[cfg(test)]
mod array_tests {
    use super::*;

    #[test]
    fn get_typed_wrong_type() {
        let mut arr = JsonArray::new();
        arr.add_string("hello");
        arr.add_raw("42");

        assert!(matches!(arr.get_string(1), Err(JsonError::WrongType(_))));
        assert!(matches!(arr.get_raw(0), Err(JsonError::WrongType(_))));
        assert!(matches!(arr.get_object(0), Err(JsonError::WrongType(_))));
        assert!(matches!(arr.get_array(0), Err(JsonError::WrongType(_))));
    }

    #[test]
    fn get_out_of_bounds() {
        let arr = JsonArray::new();
        assert_eq!(arr.get_node(0), None);
        assert_eq!(arr.get_string(0).unwrap(), None);
        assert_eq!(arr.get_raw(0).unwrap(), None);
        assert_eq!(arr.get_object(0).unwrap(), None);
        assert_eq!(arr.get_array(0).unwrap(), None);
    }

    #[test]
    fn from_json_array_for_string() {
        let mut arr = JsonArray::new();
        arr.add_string("test");
        let s: String = arr.into();
        assert!(s.contains("\"test\""));
    }
}
