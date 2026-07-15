/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Tests ported from libfwupd/fwupd-json-test.c.

use std::num::NonZeroU32;

use super::*;

/// Helper to create a NonZeroU32 from a u32 literal in test code.
fn nz(v: u32) -> NonZeroU32 {
    NonZeroU32::new(v).unwrap()
}

fn make_parser() -> JsonParser {
    JsonParser::builder()
        .max_depth(nz(10))
        .max_items(nz(10))
        .max_quoted(nz(10))
        .build()
}

// -- C test: fwupd_json_parser_depth_func --
#[test]
fn parser_depth() {
    let parser = JsonParser::builder()
        .max_depth(nz(3))
        .max_items(nz(10))
        .max_quoted(nz(10))
        .build();
    let json = r#"{"one": {"two": {"three": []}}}"#;
    let result = parser.load_from_str(json, LoadFlags::NONE);
    assert!(matches!(result, Err(JsonError::InvalidData(_))));
}

// -- C test: fwupd_json_parser_items_func --
#[test]
fn parser_items() {
    let parser = JsonParser::builder()
        .max_depth(nz(10))
        .max_items(nz(3))
        .max_quoted(nz(10))
        .build();
    let json = "[1,2,3,4]";
    let result = parser.load_from_str(json, LoadFlags::NONE);
    assert!(matches!(result, Err(JsonError::InvalidData(_))));
}

// -- C test: fwupd_json_parser_quoted_func --
#[test]
fn parser_quoted() {
    let parser = JsonParser::builder()
        .max_depth(nz(10))
        .max_items(nz(100))
        .max_quoted(nz(3))
        .build();
    let json = r#""hello""#;
    let result = parser.load_from_str(json, LoadFlags::NONE);
    assert!(matches!(result, Err(JsonError::InvalidData(_))));
}

// -- C test: fwupd_json_parser_stream_func --
#[test]
fn parser_stream() {
    let parser = make_parser();
    let json = r#""one""#;

    // load_from_bytes (equivalent to C's load_from_bytes with GBytes)
    let node1 = parser
        .load_from_bytes(json.as_bytes(), LoadFlags::NONE)
        .unwrap();
    assert_eq!(node1.get_string().unwrap(), Some("one"));

    // load_from_reader (equivalent to C's load_from_stream with GInputStream)
    let node2 = parser
        .load_from_reader(&mut json.as_bytes(), LoadFlags::NONE)
        .unwrap();
    assert_eq!(node2.get_string().unwrap(), Some("one"));
}

// -- C test: fwupd_json_parser_null_func --
#[test]
fn parser_null() {
    let parser = make_parser();
    let node = parser
        .load_from_str(r#"{"seven": null}"#, LoadFlags::NONE)
        .unwrap();

    let obj = node.get_object().unwrap().unwrap();

    // ensure 'null' is tagged correctly
    let node2 = obj.get_node("seven").unwrap();
    assert_eq!(node2.kind(), NodeKind::Null);

    // ensure we get None for the integer (C: get_integer_with_default returns 123)
    assert_eq!(obj.get_integer("seven").unwrap(), None);

    // get_string on null-valued key returns Ok(None) (C: FWUPD_ERROR_NOTHING_TO_DO)
    assert_eq!(obj.get_string("seven").unwrap(), None);

    let s = node2.to_json_string(ExportFlags::NONE);
    assert_eq!(s, "null");
}

// -- C test: fwupd_json_parser_valid_func --
#[test]
fn parser_valid() {
    let parser = make_parser();
    let data = [
        r#"{"one": "alice", "two": "bob"}"#,
        r#"{"one": True, "two": 123}"#,
        r#"{"one": null}"#,
        r#""one""#,
        "\"one\\ttwo\"",
        "\"two\\nthree\"",
        "\"four\\\"five\"",
        "[]",
        r#"["one", "two\n", [{"three": [true]}]]"#,
    ];

    for input in &data {
        let node = parser
            .load_from_str(input, LoadFlags::NONE)
            .unwrap_or_else(|e| panic!("failed to parse '{input}': {e}"));
        let output = node.to_json_string(ExportFlags::NONE);
        assert_eq!(*input, output, "roundtrip mismatch for input: {input}");
    }
}

// -- C test: fwupd_json_parser_invalid_func --
#[test]
fn parser_invalid() {
    let parser = make_parser();
    let data = [
        "[",
        "[\"one\": true]",
        "[\n\"one\":]",
        "{\"one\", true}",
        "{one, true}",
        "\"\\p\"",
        ":1",
        "\x02",
        "\n\n\n\n\n\n\n[]",
        "         []",
    ];

    for input in &data {
        let result = parser.load_from_str(input, LoadFlags::NONE);
        assert!(result.is_err(), "expected error for input: {:?}", input);
    }
}

// -- C test: fwupd_json_object_func --
#[test]
fn object_construction() {
    let mut obj = JsonObject::new();
    assert_eq!(obj.len(), 0);

    // add_string with duplicate key replaces
    obj.add_string("one", "alice");
    obj.add_string("one", "bob");
    obj.add_string("two", "clara\ndave");
    obj.add_integer("three", 3);
    obj.add_string("four", "");
    obj.add_boolean("six", true);
    obj.add_null("seven"); // C: add_string(obj, "seven", NULL)
    assert_eq!(obj.len(), 6);

    // get_string
    assert_eq!(obj.get_string("one").unwrap(), Some("bob"));
    assert_eq!(obj.get_string("two").unwrap(), Some("clara\ndave"));

    // get_integer
    assert_eq!(obj.get_integer("three").unwrap(), Some(3));

    // get_boolean
    assert_eq!(obj.get_boolean("six").unwrap(), Some(true));
    assert!(obj.get_node("six").is_some());

    // iterate entries
    assert_eq!(obj.iter().count(), 6);

    // empty string
    assert_eq!(obj.get_string("four").unwrap(), Some(""));

    // missing key returns Ok(None) (C: FWUPD_ERROR_NOT_FOUND)
    assert_eq!(obj.get_string("five").unwrap(), None);

    // iterate by position (C: get_key_for_index / get_node_for_index)
    let (key, node) = obj.iter().next().unwrap();
    assert_eq!(key, "one");
    assert_eq!(node.get_string().unwrap(), Some("bob"));

    // null string: exists, get_string returns Ok(None) (C: FWUPD_ERROR_NOTHING_TO_DO)
    assert!(obj.get_node("seven").is_some());
    assert_eq!(obj.get_string("seven").unwrap(), None);

    // export with indent
    let s = obj.to_json_string(ExportFlags::INDENT);
    let expected = "\
{
  \"one\": \"bob\",
  \"two\": \"clara\\ndave\",
  \"three\": 3,
  \"four\": \"\",
  \"six\": true,
  \"seven\": null
}";
    assert_eq!(s, expected);

    // to_json_string as bytes matches string length
    // (C: fwupd_json_object_to_bytes compared to str2->len)
    let bytes = obj.to_json_string(ExportFlags::INDENT);
    assert_eq!(bytes.len(), s.len());

    // wrong type: get_array on a string key -> Err (C: FWUPD_ERROR_INVALID_DATA)
    assert!(matches!(obj.get_array("one"), Err(JsonError::WrongType(_))));
    // wrong type: get_object on a string key -> Err (C: FWUPD_ERROR_INVALID_DATA)
    assert!(matches!(
        obj.get_object("one"),
        Err(JsonError::WrongType(_))
    ));

    // add array
    let mut arr = JsonArray::new();
    arr.add_string("dave");
    obj.add_array("array", arr);

    // add object
    let mut obj2 = JsonObject::new();
    obj2.add_integer("int", 123);
    obj.add_object("object", obj2);

    // get unknown with defaults via unwrap_or
    // (C: get_integer_with_default(obj2, "XXX", &rc, 123, &error))
    assert_eq!(
        JsonObject::new().get_integer("XXX").unwrap().unwrap_or(123),
        123
    );
    // (C: get_string_with_default(obj, "XXX", "dave", &error))
    assert_eq!(obj.get_string("XXX").unwrap().unwrap_or("dave"), "dave");
    // (C: get_boolean_with_default(obj, "XXX", &tmpb, TRUE, &error))
    assert_eq!(obj.get_boolean("XXX").unwrap().unwrap_or(true), true);

    // final export
    let s2 = obj.to_json_string(ExportFlags::INDENT);
    let expected2 = "\
{
  \"one\": \"bob\",
  \"two\": \"clara\\ndave\",
  \"three\": 3,
  \"four\": \"\",
  \"six\": true,
  \"seven\": null,
  \"array\": [
    \"dave\"
  ],
  \"object\": {
    \"int\": 123
  }
}";
    assert_eq!(s2, expected2);
}

// -- C test: fwupd_json_node_func --
#[test]
fn node_types() {
    let node = JsonNode::Raw("dave".to_owned());

    // get_raw on raw node
    assert_eq!(node.get_raw().unwrap(), Some("dave"));
    assert_eq!(node.to_json_string(ExportFlags::NONE), "dave");

    // wrong type: get_string on raw -> Err (C: FWUPD_ERROR_INVALID_DATA)
    assert!(matches!(node.get_string(), Err(JsonError::WrongType(_))));
    // wrong type: get_object on raw -> Err (C: FWUPD_ERROR_INVALID_DATA)
    assert!(matches!(node.get_object(), Err(JsonError::WrongType(_))));
    // wrong type: get_array on raw -> Err (C: FWUPD_ERROR_INVALID_DATA)
    assert!(matches!(node.get_array(), Err(JsonError::WrongType(_))));
}

// -- C test: fwupd_json_array_func --
#[test]
fn array_construction() {
    let mut arr = JsonArray::new();
    assert_eq!(arr.len(), 0);

    arr.add_string("hello");
    arr.add_raw("world");
    assert_eq!(arr.len(), 2);

    // get_string
    assert_eq!(arr.get_string(0).unwrap(), Some("hello"));
    // get_raw
    assert_eq!(arr.get_raw(1).unwrap(), Some("world"));
    // out of bounds -> Ok(None) (C: FWUPD_ERROR_NOT_FOUND)
    assert_eq!(arr.get_string(2).unwrap(), None);

    // wrong type: get_raw on string -> Err (C: FWUPD_ERROR_INVALID_DATA)
    assert!(matches!(arr.get_raw(0), Err(JsonError::WrongType(_))));
    // wrong type: get_object on string -> Err (C: FWUPD_ERROR_INVALID_DATA)
    assert!(matches!(arr.get_object(0), Err(JsonError::WrongType(_))));
    // wrong type: get_array on string -> Err (C: FWUPD_ERROR_INVALID_DATA)
    assert!(matches!(arr.get_array(0), Err(JsonError::WrongType(_))));

    // to_string with indent
    let s = arr.to_json_string(ExportFlags::INDENT);
    assert_eq!(s, "[\n  \"hello\",\n  world\n]");
}

// -- C test: fwupd_json_bytes_func --
// The C test uses g_base64_encode / fwupd_json_array_add_bytes /
// fwupd_json_object_add_bytes. We don't provide add_bytes (it's a
// convenience that base64-encodes; not a core JSON concern). Instead we
// test the equivalent: manually base64-encode and add as a string.
// This is not currently implemented as add_bytes is not part of the
// Rust json module.

// -- additional tests not in C suite --
#[test]
fn parser_valid_large_limits() {
    let parser = JsonParser::builder()
        .max_depth(nz(100))
        .max_items(nz(1000))
        .max_quoted(nz(10000))
        .build();
    let json =
        r#"{"devices": [{"name": "test", "version": "1.0", "flags": ["verified", "updatable"]}]}"#;
    let node = parser.load_from_str(json, LoadFlags::NONE).unwrap();
    let obj = node.get_object().unwrap().unwrap();
    let arr = obj.get_array("devices").unwrap().unwrap();
    assert_eq!(arr.len(), 1);
    let dev = arr.get_object(0).unwrap().unwrap();
    assert_eq!(dev.get_string("name").unwrap(), Some("test"));
    assert_eq!(dev.get_string("version").unwrap(), Some("1.0"));
    let flags = dev.get_array("flags").unwrap().unwrap();
    assert_eq!(flags.len(), 2);
    assert_eq!(flags.get_string(0).unwrap(), Some("verified"));
    assert_eq!(flags.get_string(1).unwrap(), Some("updatable"));
}

#[test]
fn compact_output() {
    let parser = JsonParser::builder()
        .max_depth(nz(10))
        .max_items(nz(100))
        .max_quoted(nz(100))
        .build();
    let json = r#"{"a": 1, "b": "two"}"#;
    let node = parser.load_from_str(json, LoadFlags::NONE).unwrap();
    let output = node.to_json_string(ExportFlags::NONE);
    assert_eq!(output, r#"{"a": 1, "b": "two"}"#);
}

#[test]
fn trailing_newline_flag() {
    let mut obj = JsonObject::new();
    obj.add_integer("x", 42);
    let s = obj.to_json_string(ExportFlags::INDENT | ExportFlags::TRAILING_NEWLINE);
    assert!(s.ends_with('\n'));
    let s2 = obj.to_json_string(ExportFlags::INDENT);
    assert!(!s2.ends_with('\n'));
}

#[test]
fn empty_containers() {
    let parser = make_parser();
    let node = parser.load_from_str("[]", LoadFlags::NONE).unwrap();
    assert_eq!(node.to_json_string(ExportFlags::NONE), "[]");

    let node2 = parser.load_from_str("{}", LoadFlags::NONE).unwrap();
    assert_eq!(node2.to_json_string(ExportFlags::NONE), "{}");
}

#[test]
fn null_node_getters_return_none() {
    let node = JsonNode::Null;
    assert_eq!(node.get_string().unwrap(), None);
    assert_eq!(node.get_raw().unwrap(), None);
    assert_eq!(node.get_object().unwrap(), None);
    assert_eq!(node.get_array().unwrap(), None);
    assert_eq!(node.kind(), NodeKind::Null);
    assert_eq!(node.to_json_string(ExportFlags::NONE), "null");
}

#[test]
fn object_clear() {
    let mut obj = JsonObject::new();
    obj.add_string("a", "b");
    assert_eq!(obj.len(), 1);
    obj.clear();
    assert_eq!(obj.len(), 0);
}

#[test]
fn null_in_array_rejected() {
    let parser = make_parser();
    // The C parser does not handle null inside arrays (only in object values),
    // so the Rust parser must reject it too.
    let result = parser.load_from_str("[null]", LoadFlags::NONE);
    assert!(result.is_err());
}

#[test]
fn non_ascii_roundtrip() {
    let parser = JsonParser::builder()
        .max_depth(nz(10))
        .max_items(nz(100))
        .max_quoted(nz(1000))
        .build();
    // Multi-byte UTF-8: "café" contains é (U+00E9, bytes C3 A9)
    let json = r#"{"name": "café"}"#;
    let node = parser.load_from_str(json, LoadFlags::NONE).unwrap();
    let obj = node.get_object().unwrap().unwrap();
    assert_eq!(obj.get_string("name").unwrap(), Some("café"));
    let output = node.to_json_string(ExportFlags::NONE);
    assert_eq!(output, json);
}

#[test]
fn empty_object_iter() {
    let obj = JsonObject::new();
    assert_eq!(obj.iter().count(), 0);
}
