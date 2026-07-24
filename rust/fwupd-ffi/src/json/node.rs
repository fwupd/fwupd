/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! FFI wrappers for [`fwupd::json::JsonNode`].

use std::ffi::CString;
use std::os::raw::{c_char, c_uint};

use fwupd::json::{ExportFlags, JsonError, JsonNode, NodeKind};

use super::array::FwupdRsJsonArray;
use super::object::FwupdRsJsonObject;
use crate::glib::GString;
use crate::glib::{set_gerror, GError};

/// Opaque FFI handle for a JSON node.
///
/// Owns both the [`JsonNode`] and a cached C string representation (lazily
/// populated by string/raw getters so the returned `*const c_char` remains
/// valid for the lifetime of the node).
pub struct FwupdRsJsonNode {
    inner: JsonNode,
    /// Cached C strings for string/raw getters. Kept alive until the wrapper is
    /// freed so that returned `*const c_char` pointers remain valid.
    cached_cstrs: Vec<CString>,
}

impl FwupdRsJsonNode {
    pub(crate) fn new(node: JsonNode) -> Self {
        Self {
            inner: node,
            cached_cstrs: Vec::new(),
        }
    }

    pub(crate) fn inner(&self) -> &JsonNode {
        &self.inner
    }
}

/// Creates a new raw-value JSON node.
///
/// The caller owns the returned pointer and must free it with
/// [`fwupd_rs_json_node_free`].
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_node_new_raw(value: *const c_char) -> *mut FwupdRsJsonNode {
    if value.is_null() {
        return std::ptr::null_mut();
    }
    let s = unsafe { std::ffi::CStr::from_ptr(value) }
        .to_string_lossy()
        .into_owned();
    Box::into_raw(Box::new(FwupdRsJsonNode::new(JsonNode::Raw(s))))
}

/// Creates a new string JSON node.
///
/// If `value` is NULL, creates a null node (matching the C behavior of
/// `fwupd_json_node_new_string(NULL)`).
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_node_new_string(
    value: *const c_char,
) -> *mut FwupdRsJsonNode {
    let node = if value.is_null() {
        JsonNode::Null
    } else {
        let s = unsafe { std::ffi::CStr::from_ptr(value) }
            .to_string_lossy()
            .into_owned();
        JsonNode::Str(s)
    };
    Box::into_raw(Box::new(FwupdRsJsonNode::new(node)))
}

/// Creates a new object JSON node.
///
/// The object is cloned; the caller retains ownership of `obj`.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_node_new_object(
    obj: *mut FwupdRsJsonObject,
) -> *mut FwupdRsJsonNode {
    if obj.is_null() {
        return std::ptr::null_mut();
    }
    let json_obj = unsafe { &*obj }.inner().clone();
    Box::into_raw(Box::new(FwupdRsJsonNode::new(JsonNode::Object(json_obj))))
}

/// Creates a new array JSON node.
///
/// The array is cloned; the caller retains ownership of `arr`.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_node_new_array(
    arr: *mut FwupdRsJsonArray,
) -> *mut FwupdRsJsonNode {
    if arr.is_null() {
        return std::ptr::null_mut();
    }
    let json_arr = unsafe { &*arr }.inner().clone();
    Box::into_raw(Box::new(FwupdRsJsonNode::new(JsonNode::Array(json_arr))))
}

/// Frees a JSON node.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_node_free(ptr: *mut FwupdRsJsonNode) {
    if !ptr.is_null() {
        drop(unsafe { Box::from_raw(ptr) });
    }
}

/// Returns the node kind as a C enum value.
///
/// Values match `FwupdJsonNodeKind`: Null=0, Raw=1, String=2, Array=3, Object=4.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_node_get_kind(ptr: *const FwupdRsJsonNode) -> c_uint {
    match unsafe { ptr.as_ref() } {
        Some(n) => match n.inner.kind() {
            NodeKind::Null => 0,
            NodeKind::Raw => 1,
            NodeKind::String => 2,
            NodeKind::Array => 3,
            NodeKind::Object => 4,
        },
        None => 0,
    }
}

/// Gets the raw value string from a JSON node.
///
/// Returns a pointer into the node's internal storage. The pointer is valid
/// until the node is freed. Returns NULL on error.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_node_get_raw(
    ptr: *mut FwupdRsJsonNode,
    error: *mut *mut GError,
) -> *const c_char {
    let n = match unsafe { ptr.as_mut() } {
        Some(n) => n,
        None => return std::ptr::null(),
    };
    match n.inner.get_raw() {
        Ok(Some(s)) => {
            n.cached_cstrs.push(CString::new(s).unwrap_or_default());
            n.cached_cstrs.last().unwrap().as_ptr()
        }
        Ok(None) => {
            let e = JsonError::InvalidData("json_node kind was null, not raw".to_owned());
            unsafe { set_gerror(error, &e) };
            std::ptr::null()
        }
        Err(e) => {
            unsafe { set_gerror(error, &e) };
            std::ptr::null()
        }
    }
}

/// Gets the string value from a JSON node.
///
/// Returns a pointer into the node's internal storage. The pointer is valid
/// until the node is freed. Returns NULL on error.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_node_get_string(
    ptr: *mut FwupdRsJsonNode,
    error: *mut *mut GError,
) -> *const c_char {
    let n = match unsafe { ptr.as_mut() } {
        Some(n) => n,
        None => return std::ptr::null(),
    };
    match n.inner.get_string() {
        Ok(Some(s)) => {
            n.cached_cstrs.push(CString::new(s).unwrap_or_default());
            n.cached_cstrs.last().unwrap().as_ptr()
        }
        Ok(None) => {
            // The node is null-valued. C returns NOTHING_TO_DO for this case.
            let e = JsonError::NothingToDo("value was null".to_owned());
            unsafe { set_gerror(error, &e) };
            std::ptr::null()
        }
        Err(e) => {
            unsafe { set_gerror(error, &e) };
            std::ptr::null()
        }
    }
}

/// Gets the object from a JSON node.
///
/// Returns a new `FwupdRsJsonObject` that the caller must free, or NULL on error.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_node_get_object(
    ptr: *const FwupdRsJsonNode,
    error: *mut *mut GError,
) -> *mut FwupdRsJsonObject {
    let n = match unsafe { ptr.as_ref() } {
        Some(n) => n,
        None => return std::ptr::null_mut(),
    };
    match n.inner.get_object() {
        Ok(Some(obj)) => Box::into_raw(Box::new(FwupdRsJsonObject::new(obj.clone()))),
        Ok(None) => {
            let e = JsonError::InvalidData("json_node kind was null, not object".to_owned());
            unsafe { set_gerror(error, &e) };
            std::ptr::null_mut()
        }
        Err(e) => {
            unsafe { set_gerror(error, &e) };
            std::ptr::null_mut()
        }
    }
}

/// Gets the array from a JSON node.
///
/// Returns a new `FwupdRsJsonArray` that the caller must free, or NULL on error.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_node_get_array(
    ptr: *const FwupdRsJsonNode,
    error: *mut *mut GError,
) -> *mut FwupdRsJsonArray {
    let n = match unsafe { ptr.as_ref() } {
        Some(n) => n,
        None => return std::ptr::null_mut(),
    };
    match n.inner.get_array() {
        Ok(Some(arr)) => Box::into_raw(Box::new(FwupdRsJsonArray::new(arr.clone()))),
        Ok(None) => {
            let e = JsonError::InvalidData("json_node kind was null, not array".to_owned());
            unsafe { set_gerror(error, &e) };
            std::ptr::null_mut()
        }
        Err(e) => {
            unsafe { set_gerror(error, &e) };
            std::ptr::null_mut()
        }
    }
}

/// Converts a JSON node to a string representation.
///
/// Returns a newly allocated `GString *`. The caller must free it with
/// `g_string_free()`.
///
/// The `flags` parameter maps to `FwupdJsonExportFlags`:
/// - 0 = none
/// - 1 = indent
/// - 2 = trailing newline
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_node_to_string(
    ptr: *const FwupdRsJsonNode,
    flags: c_uint,
) -> *mut GString {
    let n = match unsafe { ptr.as_ref() } {
        Some(n) => n,
        None => return std::ptr::null_mut(),
    };
    let export_flags = to_export_flags(flags);
    let s = n.inner.to_json_string(export_flags);
    GString::from_rust_string(&s)
}

fn to_export_flags(flags: c_uint) -> ExportFlags {
    let mut ef = ExportFlags::NONE;
    if flags & 1 != 0 {
        ef |= ExportFlags::INDENT;
    }
    if flags & 2 != 0 {
        ef |= ExportFlags::TRAILING_NEWLINE;
    }
    ef
}
