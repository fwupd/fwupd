/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

use std::io::Read;
use std::num::NonZeroU32;

use super::array::JsonArray;
use super::error::JsonError;
use super::node::JsonNode;
use super::object::JsonObject;
use super::LoadFlags;

/// Maximum consecutive newlines allowed outside a quoted string.
const NEWLINE_MAX: u32 = 5;

/// Maximum whitespace chars per nesting depth level.
const INDENT_MAX: u32 = 8;

/// Token types produced by the tokenizer.
#[derive(Debug, Clone, PartialEq, Eq)]
enum Token {
    /// The `null` literal.
    Null,
    /// An unquoted raw value (number, boolean).
    Raw(String),
    /// A quoted string.
    String(String),
    /// `{`
    ObjectStart,
    /// `}`
    ObjectEnd,
    /// `:`
    ObjectDelim,
    /// `[`
    ArrayStart,
    /// `]`
    ArrayEnd,
}

/// Internal tokenizer state.
struct Helper {
    flags: LoadFlags,
    buf: Vec<u8>,
    buf_len: usize,
    buf_offset: usize,
    /// Byte accumulator. Kept as raw bytes so that multi-byte UTF-8 sequences
    /// pass through unmodified (matching the C parser's byte-level semantics).
    /// Converted to `String` only when a complete token is emitted.
    acc: Vec<u8>,
    max_quoted: u32,
    is_quoted: bool,
    is_escape: bool,
    line_count: u32,
    newline_count: u32,
    whitespace_count: u32,
    depth: u32,
}

impl Helper {
    fn new(max_quoted: u32) -> Self {
        Self {
            flags: LoadFlags::NONE,
            buf: vec![0u8; 32 * 1024],
            buf_len: 0,
            buf_offset: usize::MAX,
            acc: Vec::with_capacity(128),
            max_quoted,
            is_quoted: false,
            is_escape: false,
            line_count: 1,
            newline_count: 0,
            whitespace_count: 0,
            depth: 0,
        }
    }

    /// Returns up to 20 characters of context around the current buffer position.
    fn buffer_context(&self) -> String {
        let start = self.buf_offset.saturating_sub(10).min(self.buf_len);
        let end = (self.buf_offset + 10).min(self.buf_len);
        if start >= self.buf_len {
            return String::new();
        }
        String::from_utf8_lossy(&self.buf[start..end]).into_owned()
    }

    /// Read more data from the reader into the buffer.
    fn read_more_data(&mut self, reader: &mut dyn Read) -> Result<(), JsonError> {
        let n = reader.read(&mut self.buf)?;
        if n == 0 {
            return Err(JsonError::InvalidData(
                "incomplete data from stream".to_owned(),
            ));
        }
        self.buf_len = n;
        self.buf_offset = 0;
        Ok(())
    }

    /// Flush the accumulator into a token, if one is available.
    fn tokenize(&mut self) -> Option<Token> {
        if self.is_quoted {
            let bytes = std::mem::take(&mut self.acc);
            let s = String::from_utf8(bytes)
                .unwrap_or_else(|e| String::from_utf8_lossy(&e.into_bytes()).into_owned());
            return Some(Token::String(s));
        }
        if self.acc.is_empty() {
            return None;
        }
        if self.acc.eq_ignore_ascii_case(b"null") {
            self.acc.clear();
            Some(Token::Null)
        } else {
            let bytes = std::mem::take(&mut self.acc);
            // Raw values (numbers, booleans) are always ASCII.
            Some(Token::Raw(String::from_utf8(bytes).unwrap_or_default()))
        }
    }

    /// Process a single byte, potentially producing a token.
    fn process_byte(&mut self, data: u8) -> Result<(Option<Token>, bool), JsonError> {
        // Quotes
        if !self.is_escape && data == b'"' {
            if self.is_quoted {
                let token = self.tokenize();
                self.is_quoted = false;
                return Ok((token, true));
            }
            self.is_quoted = true;
            self.newline_count = 0;
            self.whitespace_count = 0;
            return Ok((None, true));
        }

        if self.is_quoted {
            // Escape char
            if !self.is_escape && data == b'\\' {
                self.is_escape = true;
                self.newline_count = 0;
                return Ok((None, true));
            }
            if self.is_escape {
                let unescaped = unescape_char(data).ok_or_else(|| {
                    JsonError::InvalidData(format!("invalid escape char '{}'", data as char))
                })?;
                self.is_escape = false;
                self.acc.push(unescaped);
            } else {
                self.acc.push(data);
            }
            self.newline_count = 0;
            self.whitespace_count = 0;
            if self.acc.len() as u32 > self.max_quoted {
                return Err(JsonError::InvalidData(format!(
                    "token too long, limit was {}",
                    self.max_quoted
                )));
            }
            return Ok((None, true));
        }

        // Newline
        if data == b'\n' {
            self.line_count += 1;
            // Check before incrementing to match C's post-increment semantics:
            // `if (helper->newlinecnt++ > MAX)` compares the old value.
            if self.newline_count > NEWLINE_MAX {
                return Err(JsonError::InvalidData(format!(
                    "too many newlines, limit was {NEWLINE_MAX}"
                )));
            }
            self.newline_count += 1;
            let token = self.tokenize();
            return Ok((token, true));
        }
        self.newline_count = 0;

        // Comma (split)
        if data == b',' {
            let token = self.tokenize();
            return Ok((token, true));
        }

        // Control tokens
        if matches!(data, b'{' | b'}' | b'[' | b']' | b':') {
            if let Some(token) = self.tokenize() {
                // We produced a token from the accumulator; don't advance so
                // we re-read this control char on the next call.
                return Ok((Some(token), false));
            }
            let ctrl_token = match data {
                b'{' => Token::ObjectStart,
                b'}' => Token::ObjectEnd,
                b'[' => Token::ArrayStart,
                b']' => Token::ArrayEnd,
                b':' => Token::ObjectDelim,
                _ => unreachable!(),
            };
            self.newline_count = 0;
            self.whitespace_count = 0;
            return Ok((Some(ctrl_token), true));
        }

        // Whitespace (include vertical tab 0x0B to match C's g_ascii_isspace)
        if (data as char).is_ascii_whitespace() || data == b'\x0b' {
            let whitespace_max = INDENT_MAX * (self.depth + 1);
            // Check before incrementing to match C's post-increment semantics:
            // `if (helper->whitespacecnt++ >= max)` compares the old value.
            if self.whitespace_count >= whitespace_max {
                return Err(JsonError::InvalidData(format!(
                    "too much whitespace, limit was {whitespace_max}"
                )));
            }
            self.whitespace_count += 1;
            return Ok((None, true));
        }

        // Control chars
        if (data as char).is_ascii_control() {
            return Err(JsonError::InvalidData(format!(
                "ASCII control character detected 0x{data:x}"
            )));
        }

        // Accumulate
        self.acc.push(data);
        self.whitespace_count = 0;
        Ok((None, true))
    }

    /// Get the next token from the stream.
    fn next_token(&mut self, reader: &mut dyn Read) -> Result<Token, JsonError> {
        loop {
            // Need more data?
            if self.buf_offset >= self.buf_len {
                self.read_more_data(reader)?;
            }
            let data = self.buf[self.buf_offset];
            let (token, do_advance) = self.process_byte(data)?;
            if do_advance {
                self.buf_offset += 1;
            }
            if let Some(token) = token {
                return Ok(token);
            }
        }
    }
}

/// Unescape a character after a backslash.
fn unescape_char(data: u8) -> Option<u8> {
    match data {
        b'n' => Some(b'\n'),
        b't' => Some(b'\t'),
        b'\\' => Some(b'\\'),
        b'"' => Some(b'"'),
        _ => None,
    }
}

/// A streaming tokenizer JSON parser resistant to malicious input.
///
/// Use [`JsonParser::builder()`] to construct a parser with custom limits.
pub struct JsonParser {
    max_depth: NonZeroU32,
    max_items: NonZeroU32,
    max_quoted: NonZeroU32,
}

impl JsonParser {
    /// Creates a builder for configuring parser limits.
    pub fn builder() -> JsonParserBuilder {
        let default = NonZeroU32::new(u16::MAX as u32).unwrap();
        JsonParserBuilder {
            max_depth: default,
            max_items: default,
            max_quoted: default,
        }
    }

    /// Creates a new parser with default (permissive) limits.
    pub fn new() -> Self {
        let default = NonZeroU32::new(u16::MAX as u32).unwrap();
        Self {
            max_depth: default,
            max_items: default,
            max_quoted: default,
        }
    }

    /// Parse JSON from a string.
    pub fn load_from_str(&self, text: &str, flags: LoadFlags) -> Result<JsonNode, JsonError> {
        self.load_from_reader(&mut text.as_bytes(), flags)
    }

    /// Parse JSON from a byte slice.
    pub fn load_from_bytes(&self, data: &[u8], flags: LoadFlags) -> Result<JsonNode, JsonError> {
        self.load_from_reader(&mut &data[..], flags)
    }

    /// Parse JSON from any reader.
    pub fn load_from_reader(
        &self,
        reader: &mut dyn Read,
        flags: LoadFlags,
    ) -> Result<JsonNode, JsonError> {
        let mut helper = Helper::new(self.max_quoted.get());
        helper.flags = flags;
        self.load(&mut helper, reader)
    }

    fn check_depth(&self, depth: u32) -> Result<(), JsonError> {
        if depth > self.max_depth.get() {
            return Err(JsonError::InvalidData(format!(
                "structure too deep, limit was {depth}"
            )));
        }
        Ok(())
    }

    fn load(&self, helper: &mut Helper, reader: &mut dyn Read) -> Result<JsonNode, JsonError> {
        let token = helper.next_token(reader)?;
        match token {
            Token::ObjectStart => {
                let obj = self.load_object(helper, reader)?;
                Ok(JsonNode::Object(obj))
            }
            Token::ArrayStart => {
                let arr = self.load_array(helper, reader)?;
                Ok(JsonNode::Array(arr))
            }
            Token::String(s) => Ok(JsonNode::Str(s)),
            Token::Raw(s) => Ok(JsonNode::Raw(s)),
            _ => Err(JsonError::InvalidData(
                "invalid JSON; token was not object, array, string or raw".to_owned(),
            )),
        }
    }

    fn load_array(
        &self,
        helper: &mut Helper,
        reader: &mut dyn Read,
    ) -> Result<JsonArray, JsonError> {
        let mut arr = JsonArray::new();

        helper.depth += 1;
        self.check_depth(helper.depth)?;

        loop {
            let token = helper.next_token(reader)?;
            match token {
                Token::ArrayEnd => break,
                Token::ObjectStart => {
                    let obj = self.load_object(helper, reader)?;
                    arr.add_object(obj);
                }
                Token::ArrayStart => {
                    let inner = self.load_array(helper, reader)?;
                    arr.add_array(inner);
                }
                Token::String(s) => {
                    arr.add_node(JsonNode::Str(s));
                }
                Token::Raw(s) => {
                    arr.add_node(JsonNode::Raw(s));
                }
                _ => {
                    return Err(JsonError::InvalidData(
                        "object delimiter not expected in array".to_owned(),
                    ));
                }
            }
            if arr.len() as u32 > self.max_items.get() {
                return Err(JsonError::InvalidData(format!(
                    "too many items in array, limit was {}",
                    self.max_items
                )));
            }
        }
        helper.depth -= 1;
        Ok(arr)
    }

    fn load_object(
        &self,
        helper: &mut Helper,
        reader: &mut dyn Read,
    ) -> Result<JsonObject, JsonError> {
        let mut obj = JsonObject::new();

        helper.depth += 1;
        self.check_depth(helper.depth)?;

        loop {
            // "key" : value
            let token1 = helper.next_token(reader)?;
            let key = match token1 {
                Token::ObjectEnd => break,
                Token::String(s) => s,
                _ => {
                    let context = helper.buffer_context();
                    return Err(JsonError::InvalidData(format!(
                        "expected quoted key, got '{context}' on line {}",
                        helper.line_count
                    )));
                }
            };

            let token2 = helper.next_token(reader)?;
            if token2 != Token::ObjectDelim {
                return Err(JsonError::InvalidData(format!(
                    "did not find object delimiter ':' on line {}",
                    helper.line_count
                )));
            }

            let token3 = helper.next_token(reader)?;
            let node = match token3 {
                Token::ObjectStart => {
                    let inner = self.load_object(helper, reader)?;
                    JsonNode::Object(inner)
                }
                Token::ArrayStart => {
                    let inner = self.load_array(helper, reader)?;
                    JsonNode::Array(inner)
                }
                Token::String(s) => JsonNode::Str(s),
                Token::Null => JsonNode::Null,
                Token::Raw(s) => JsonNode::Raw(s),
                _ => {
                    return Err(JsonError::InvalidData(format!(
                        "unexpected token for object value on line {}",
                        helper.line_count
                    )));
                }
            };

            obj.add_node_internal(key, node, helper.flags);

            if obj.len() as u32 > self.max_items.get() {
                return Err(JsonError::InvalidData(format!(
                    "too many items in object, limit was {}",
                    self.max_items
                )));
            }
        }
        helper.depth -= 1;
        Ok(obj)
    }
}

impl Default for JsonParser {
    fn default() -> Self {
        Self::new()
    }
}

/// Builder for [`JsonParser`].
pub struct JsonParserBuilder {
    max_depth: NonZeroU32,
    max_items: NonZeroU32,
    max_quoted: NonZeroU32,
}

impl JsonParserBuilder {
    /// Sets the maximum nesting depth.
    pub fn max_depth(mut self, max_depth: NonZeroU32) -> Self {
        self.max_depth = max_depth;
        self
    }

    /// Sets the maximum number of items in an array or object.
    pub fn max_items(mut self, max_items: NonZeroU32) -> Self {
        self.max_items = max_items;
        self
    }

    /// Sets the maximum length of a quoted string.
    pub fn max_quoted(mut self, max_quoted: NonZeroU32) -> Self {
        self.max_quoted = max_quoted;
        self
    }

    /// Builds the parser.
    pub fn build(self) -> JsonParser {
        JsonParser {
            max_depth: self.max_depth,
            max_items: self.max_items,
            max_quoted: self.max_quoted,
        }
    }
}

#[cfg(test)]
mod parser_tests {
    use super::*;

    #[test]
    fn unescape_char_valid() {
        assert_eq!(unescape_char(b'n'), Some(b'\n'));
        assert_eq!(unescape_char(b't'), Some(b'\t'));
        assert_eq!(unescape_char(b'\\'), Some(b'\\'));
        assert_eq!(unescape_char(b'"'), Some(b'"'));
    }

    #[test]
    fn unescape_char_invalid() {
        assert_eq!(unescape_char(b'p'), None);
        assert_eq!(unescape_char(b'r'), None);
        assert_eq!(unescape_char(b'a'), None);
        assert_eq!(unescape_char(b'/'), None);
        assert_eq!(unescape_char(b'0'), None);
    }

    fn parser_with_limits(depth: u32, items: u32, quoted: u32) -> JsonParser {
        JsonParser::builder()
            .max_depth(NonZeroU32::new(depth).unwrap())
            .max_items(NonZeroU32::new(items).unwrap())
            .max_quoted(NonZeroU32::new(quoted).unwrap())
            .build()
    }

    fn assert_err_contains(input: &str, parser: &JsonParser, expected_substr: &str) {
        let result = parser.load_from_str(input, LoadFlags::NONE);
        match result {
            Err(JsonError::InvalidData(msg)) => {
                assert!(
                    msg.contains(expected_substr),
                    "error message '{msg}' did not contain '{expected_substr}'"
                );
            }
            Err(other) => panic!("expected InvalidData, got {other:?}"),
            Ok(_) => panic!("expected error for input: {input:?}"),
        }
    }

    // -- Error: incomplete data from stream --
    #[test]
    fn err_incomplete_data() {
        let parser = parser_with_limits(10, 10, 100);
        for input in ["", "\"hello", "{\"key\": 1", "[1, 2"] {
            assert_err_contains(input, &parser, "incomplete data from stream");
        }
    }

    // -- Error: invalid escape char --
    #[test]
    fn err_invalid_escape_char() {
        let parser = parser_with_limits(10, 10, 100);
        for input in [
            "\"\\p\"",              // in a bare string
            "{\"ke\\y\": 1}",       // in an object key
            "{\"key\": \"va\\l\"}", // in an object value
        ] {
            assert_err_contains(input, &parser, "invalid escape char");
        }
    }

    // -- Error: token too long --
    #[test]
    fn err_token_too_long() {
        let parser = parser_with_limits(10, 100, 3);
        // "abc" (3 chars) should pass, "abcd" and "hello" (>3 chars) should fail
        let result = parser.load_from_str("\"abc\"", LoadFlags::NONE);
        assert!(result.is_ok(), "3-char string should be within limit of 3");
        for input in ["\"abcd\"", "\"hello\""] {
            assert_err_contains(input, &parser, "token too long");
        }
    }

    // -- Error: too many newlines --
    #[test]
    fn err_too_many_newlines() {
        let parser = parser_with_limits(10, 10, 10);
        // NEWLINE_MAX is 5; need more than 5+1=6 consecutive newlines to trigger
        assert_err_contains("\n\n\n\n\n\n\n[]", &parser, "too many newlines");
    }

    #[test]
    fn err_newlines_at_limit_passes() {
        let parser = parser_with_limits(10, 10, 10);
        // 6 newlines should still pass (count > 5 triggers on 7th)
        let result = parser.load_from_str("\n\n\n\n\n\n[]", LoadFlags::NONE);
        assert!(result.is_ok(), "6 newlines should be within limit");
    }

    // -- Error: too much whitespace --
    #[test]
    fn err_too_much_whitespace() {
        let parser = parser_with_limits(10, 10, 10);
        // At depth 0, whitespace_max = INDENT_MAX * (0+1) = 8
        for input in [
            "         []",          // 9 spaces
            "\t\t\t\t\t\t\t\t\t[]", // 9 tabs
        ] {
            assert_err_contains(input, &parser, "too much whitespace");
        }
    }

    // -- Error: ASCII control character --
    #[test]
    fn err_control_char() {
        let parser = parser_with_limits(10, 10, 100);
        for input in ["\x01", "\x02", "\x7f"] {
            assert_err_contains(input, &parser, "ASCII control character detected");
        }
    }

    // -- Error: structure too deep --
    #[test]
    fn err_depth_nested_objects() {
        let parser = parser_with_limits(3, 10, 10);
        assert_err_contains(
            "{\"one\": {\"two\": {\"three\": []}}}",
            &parser,
            "structure too deep",
        );
    }

    #[test]
    fn err_depth_nested_arrays() {
        let parser = parser_with_limits(2, 10, 10);
        assert_err_contains("[[[1]]]", &parser, "structure too deep");
    }

    #[test]
    fn err_depth_at_limit_passes() {
        let parser = parser_with_limits(2, 10, 10);
        // 2 levels of nesting should pass
        let result = parser.load_from_str("[[1]]", LoadFlags::NONE);
        assert!(result.is_ok(), "2 levels should be within depth limit of 2");
    }

    // -- Error: invalid top-level token --
    #[test]
    fn err_top_level_invalid_token() {
        let parser = parser_with_limits(10, 10, 10);
        for input in [":1", "}", "]"] {
            assert_err_contains(
                input,
                &parser,
                "invalid JSON; token was not object, array, string or raw",
            );
        }
        // Top-level null without any terminator causes the tokenizer to run
        // out of data before emitting a token (since null is accumulated as
        // raw bytes until a delimiter is encountered).
        assert_err_contains("null", &parser, "incomplete data from stream");
        // Null is valid as an object value, not as a top-level token. Wrapping
        // in an array confirms that null hits the 'not expected in array' path
        // since arrays don't accept Null tokens.
        let parser2 = parser_with_limits(10, 10, 100);
        let result = parser2.load_from_str("[null]", LoadFlags::NONE);
        assert!(result.is_err());
    }

    // -- Error: object delimiter in array --
    #[test]
    fn err_delimiter_in_array() {
        let parser = parser_with_limits(10, 10, 100);
        for input in ["[\"one\": true]", "[\n\"one\":]"] {
            assert_err_contains(input, &parser, "object delimiter not expected in array");
        }
    }

    // -- Error: too many items in array --
    #[test]
    fn err_too_many_items_array() {
        let parser = parser_with_limits(10, 3, 10);
        assert_err_contains("[1,2,3,4]", &parser, "too many items in array");
    }

    #[test]
    fn err_too_many_items_array_at_limit_passes() {
        let parser = parser_with_limits(10, 3, 10);
        let result = parser.load_from_str("[1,2,3]", LoadFlags::NONE);
        assert!(result.is_ok(), "3 items should be within limit of 3");
    }

    // -- Error: expected quoted key (unquoted key) --
    #[test]
    fn err_unquoted_key() {
        let parser = parser_with_limits(10, 10, 100);
        assert_err_contains("{one: true}", &parser, "expected quoted key");
    }

    #[test]
    fn err_comma_as_object_key_delimiter() {
        let parser = parser_with_limits(10, 10, 100);
        // {"one", true} -- comma after key instead of colon
        assert_err_contains(
            "{\"one\", true}",
            &parser,
            "did not find object delimiter ':'",
        );
    }

    // -- Error: missing object delimiter ':' --
    #[test]
    fn err_missing_object_delimiter() {
        let parser = parser_with_limits(10, 10, 100);
        // {"key" "value"} -- missing : between key and value
        assert_err_contains(
            "{\"key\" \"value\"}",
            &parser,
            "did not find object delimiter",
        );
    }

    // -- Error: unexpected token for object value --
    #[test]
    fn err_unexpected_object_value_close_bracket() {
        let parser = parser_with_limits(10, 10, 100);
        // {"key": ] -- ] is not a valid value
        assert_err_contains("{\"key\": ]", &parser, "unexpected token for object value");
    }

    #[test]
    fn err_unexpected_object_value_colon() {
        let parser = parser_with_limits(10, 10, 100);
        // {"key": : -- : is not a valid value
        assert_err_contains("{\"key\": :}", &parser, "unexpected token for object value");
    }

    // -- Error: too many items in object --
    #[test]
    fn err_too_many_items_object() {
        let parser = parser_with_limits(10, 2, 10);
        assert_err_contains(
            "{\"a\": 1, \"b\": 2, \"c\": 3}",
            &parser,
            "too many items in object",
        );
    }

    #[test]
    fn err_too_many_items_object_at_limit_passes() {
        let parser = parser_with_limits(10, 2, 10);
        let result = parser.load_from_str("{\"a\": 1, \"b\": 2}", LoadFlags::NONE);
        assert!(result.is_ok(), "2 items should be within limit of 2");
    }
}
