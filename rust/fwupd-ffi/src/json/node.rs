/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! FFI wrappers for [`fwupd::json::JsonNode`].

use std::ffi::CString;
use std::os::raw::{c_char, c_uint};
use std::sync::Arc;

use fwupd::json::{JsonError, JsonNode, NodeKind};

use super::array::FwupdRsJsonArray;
use super::object::FwupdRsJsonObject;
use crate::glib::{GError, GString};
use crate::json::to_export_flags;

/// Opaque FFI handle for a JSON node.
///
/// Holds an `Arc<JsonNode>` so that cloning is cheap and multiple FFI
/// wrappers can share the same underlying data.
pub struct FwupdRsJsonNode {
    inner: Arc<JsonNode>,
}

impl FwupdRsJsonNode {
    pub(crate) fn new(node: Arc<JsonNode>) -> Self {
        Self { inner: node }
    }

    pub(crate) fn inner(&self) -> &Arc<JsonNode> {
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
    Box::into_raw(Box::new(FwupdRsJsonNode::new(Arc::new(JsonNode::Raw(s)))))
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
        Arc::new(JsonNode::Null)
    } else {
        let s = unsafe { std::ffi::CStr::from_ptr(value) }
            .to_string_lossy()
            .into_owned();
        Arc::new(JsonNode::Str(s))
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
    Box::into_raw(Box::new(FwupdRsJsonNode::new(Arc::new(JsonNode::Object(
        json_obj,
    )))))
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
    Box::into_raw(Box::new(FwupdRsJsonNode::new(Arc::new(JsonNode::Array(
        json_arr,
    )))))
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
) -> *mut c_char {
    let Some(n) = (unsafe { ptr.as_mut() }) else {
        return std::ptr::null_mut();
    };
    match n.inner.get_raw() {
        Ok(Some(s)) => CString::new(s).unwrap_or_default().into_raw(),
        Ok(None) => {
            let e = JsonError::InvalidData("json_node kind was null, not raw".to_owned());
            GError::convert(error, &(&e).into());
            std::ptr::null_mut()
        }
        Err(e) => {
            GError::convert(error, &(&e).into());
            std::ptr::null_mut()
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
) -> *mut c_char {
    let Some(n) = (unsafe { ptr.as_mut() }) else {
        return std::ptr::null_mut();
    };
    match n.inner.get_string() {
        Ok(Some(s)) => CString::new(s).unwrap_or_default().into_raw(),
        Ok(None) => {
            // The node is null-valued. C returns NOTHING_TO_DO for this case.
            let e = JsonError::NothingToDo("value was null".to_owned());
            GError::convert(error, &(&e).into());
            std::ptr::null_mut()
        }
        Err(e) => {
            GError::convert(error, &(&e).into());
            std::ptr::null_mut()
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
    let Some(n) = (unsafe { ptr.as_ref() }) else {
        return std::ptr::null_mut();
    };
    match n.inner.as_ref().get_object() {
        Ok(Some(obj)) => Box::into_raw(Box::new(FwupdRsJsonObject::new(obj))),
        Ok(None) => {
            let e = JsonError::InvalidData("json_node kind was null, not object".to_owned());
            GError::convert(error, &(&e).into());
            std::ptr::null_mut()
        }
        Err(e) => {
            GError::convert(error, &(&e).into());
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
    let Some(n) = (unsafe { ptr.as_ref() }) else {
        return std::ptr::null_mut();
    };
    match n.inner.as_ref().get_array() {
        Ok(Some(arr)) => Box::into_raw(Box::new(FwupdRsJsonArray::new(arr))),
        Ok(None) => {
            let e = JsonError::InvalidData("json_node kind was null, not array".to_owned());
            GError::convert(error, &(&e).into());
            std::ptr::null_mut()
        }
        Err(e) => {
            GError::convert(error, &(&e).into());
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
    let Some(n) = (unsafe { ptr.as_ref() }) else {
        return std::ptr::null_mut();
    };
    let export_flags = to_export_flags(flags);
    let s = n.inner.to_json_string(export_flags);
    GString::from_rust_string(&s)
}
