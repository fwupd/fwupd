/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! FFI wrappers for [`fwupd::json::JsonObject`].

use std::ffi::CString;
use std::os::raw::{c_char, c_int, c_uint};

use fwupd::json::{ExportFlags, JsonError, JsonObject};

use super::array::FwupdRsJsonArray;
use super::node::FwupdRsJsonNode;
use crate::glib::GString;
use crate::glib::{set_gerror, GError};

/// Opaque FFI handle for a JSON object.
pub struct FwupdRsJsonObject {
    inner: JsonObject,
    /// Cached C strings for string getters. Kept alive until the wrapper is freed
    /// so that returned `*const c_char` pointers remain valid.
    cached_cstrs: Vec<CString>,
}

impl FwupdRsJsonObject {
    pub(crate) fn new(obj: JsonObject) -> Self {
        Self {
            inner: obj,
            cached_cstrs: Vec::new(),
        }
    }

    pub(crate) fn inner(&self) -> &JsonObject {
        &self.inner
    }
}

/// Creates a new empty JSON object.
#[no_mangle]
pub extern "C" fn fwupd_rs_json_object_new() -> *mut FwupdRsJsonObject {
    Box::into_raw(Box::new(FwupdRsJsonObject::new(JsonObject::new())))
}

/// Frees a JSON object.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_object_free(ptr: *mut FwupdRsJsonObject) {
    if !ptr.is_null() {
        drop(unsafe { Box::from_raw(ptr) });
    }
}

/// Returns the number of key-value pairs.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_object_get_size(ptr: *const FwupdRsJsonObject) -> c_uint {
    match unsafe { ptr.as_ref() } {
        Some(o) => o.inner.len() as c_uint,
        None => 0,
    }
}

/// Clears all entries from the object.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_object_clear(ptr: *mut FwupdRsJsonObject) {
    if let Some(o) = unsafe { ptr.as_mut() } {
        o.inner.clear();
    }
}

/// Gets the key at the given index position.
///
/// Returns a pointer valid until the object is freed, or NULL if out of bounds.
/// Sets `FWUPD_ERROR_NOT_FOUND` if the index is out of bounds.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_object_get_key_for_index(
    ptr: *mut FwupdRsJsonObject,
    idx: c_uint,
    error: *mut *mut GError,
) -> *const c_char {
    let o = match unsafe { ptr.as_mut() } {
        Some(o) => o,
        None => return std::ptr::null(),
    };
    match o.inner.iter().nth(idx as usize) {
        Some((key, _)) => {
            o.cached_cstrs
                .push(CString::new(key.as_str()).unwrap_or_default());
            o.cached_cstrs.last().unwrap().as_ptr()
        }
        None => {
            let e = JsonError::NotFound(format!(
                "index {} is larger than object size {}",
                idx,
                o.inner.len()
            ));
            unsafe { set_gerror(error, &e) };
            std::ptr::null()
        }
    }
}

/// Gets the node at the given index position.
///
/// Returns a new `FwupdRsJsonNode` that the caller must free, or NULL.
/// Sets `FWUPD_ERROR_NOT_FOUND` if the index is out of bounds.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_object_get_node_for_index(
    ptr: *const FwupdRsJsonObject,
    idx: c_uint,
    error: *mut *mut GError,
) -> *mut FwupdRsJsonNode {
    let o = match unsafe { ptr.as_ref() } {
        Some(o) => o,
        None => return std::ptr::null_mut(),
    };
    match o.inner.iter().nth(idx as usize) {
        Some((_, node)) => Box::into_raw(Box::new(FwupdRsJsonNode::new(node.clone()))),
        None => {
            let e = JsonError::NotFound(format!(
                "index {} is larger than object size {}",
                idx,
                o.inner.len()
            ));
            unsafe { set_gerror(error, &e) };
            std::ptr::null_mut()
        }
    }
}

/// Returns `true` (non-zero) if a node with the given key exists.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_object_has_node(
    ptr: *const FwupdRsJsonObject,
    key: *const c_char,
) -> c_int {
    let o = match unsafe { ptr.as_ref() } {
        Some(o) => o,
        None => return 0,
    };
    if key.is_null() {
        return 0;
    }
    let key_str = unsafe { std::ffi::CStr::from_ptr(key) }.to_string_lossy();
    o.inner.get_node(&key_str).is_some() as c_int
}

/// Gets a string value for the given key.
///
/// Returns a pointer valid until the object is freed, or NULL on error.
/// Sets `FWUPD_ERROR_NOT_FOUND` if the key does not exist.
/// Sets `FWUPD_ERROR_NOTHING_TO_DO` if the value is null.
/// Sets `FWUPD_ERROR_INVALID_DATA` if the value has the wrong type.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_object_get_string(
    ptr: *mut FwupdRsJsonObject,
    key: *const c_char,
    error: *mut *mut GError,
) -> *const c_char {
    let o = match unsafe { ptr.as_mut() } {
        Some(o) => o,
        None => return std::ptr::null(),
    };
    if key.is_null() {
        return std::ptr::null();
    }
    let key_str = unsafe { std::ffi::CStr::from_ptr(key) }.to_string_lossy();
    match o.inner.get_node(&key_str) {
        None => {
            let e = JsonError::NotFound(format!("no json_node for key {key_str}"));
            unsafe { set_gerror(error, &e) };
            std::ptr::null()
        }
        Some(node) => match node.get_string() {
            Ok(Some(s)) => {
                o.cached_cstrs.push(CString::new(s).unwrap_or_default());
                o.cached_cstrs.last().unwrap().as_ptr()
            }
            Ok(None) => {
                // The node exists but is null-valued. C returns NOTHING_TO_DO.
                let e = JsonError::NothingToDo("value was null".to_owned());
                unsafe { set_gerror(error, &e) };
                std::ptr::null()
            }
            Err(e) => {
                unsafe { set_gerror(error, &e) };
                std::ptr::null()
            }
        },
    }
}

/// Gets an integer value for the given key.
///
/// Returns 1 (TRUE) on success, 0 (FALSE) on error.
/// Sets `FWUPD_ERROR_NOT_FOUND` if the key does not exist.
/// Sets `FWUPD_ERROR_INVALID_DATA` if the value cannot be parsed.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_object_get_integer(
    ptr: *const FwupdRsJsonObject,
    key: *const c_char,
    value: *mut i64,
    error: *mut *mut GError,
) -> c_int {
    let o = match unsafe { ptr.as_ref() } {
        Some(o) => o,
        None => return 0,
    };
    if key.is_null() {
        return 0;
    }
    let key_str = unsafe { std::ffi::CStr::from_ptr(key) }.to_string_lossy();
    match o.inner.get_node(&key_str) {
        None => {
            let e = JsonError::NotFound(format!("no json_node for key {key_str}"));
            unsafe { set_gerror(error, &e) };
            0
        }
        Some(_) => match o.inner.get_integer(&key_str) {
            Ok(Some(v)) => {
                if !value.is_null() {
                    unsafe { *value = v };
                }
                1
            }
            Ok(None) => {
                // The node exists but is null-valued. C returns NOTHING_TO_DO.
                let e = JsonError::NothingToDo("value was null".to_owned());
                unsafe { set_gerror(error, &e) };
                0
            }
            Err(e) => {
                unsafe { set_gerror(error, &e) };
                0
            }
        },
    }
}

/// Gets a boolean value for the given key.
///
/// Returns 1 (TRUE) on success, 0 (FALSE) on error.
/// Sets `FWUPD_ERROR_NOT_FOUND` if the key does not exist.
/// Sets `FWUPD_ERROR_INVALID_DATA` if the value cannot be parsed.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_object_get_boolean(
    ptr: *const FwupdRsJsonObject,
    key: *const c_char,
    value: *mut c_int,
    error: *mut *mut GError,
) -> c_int {
    let o = match unsafe { ptr.as_ref() } {
        Some(o) => o,
        None => return 0,
    };
    if key.is_null() {
        return 0;
    }
    let key_str = unsafe { std::ffi::CStr::from_ptr(key) }.to_string_lossy();
    match o.inner.get_node(&key_str) {
        None => {
            let e = JsonError::NotFound(format!("no json_node for key {key_str}"));
            unsafe { set_gerror(error, &e) };
            0
        }
        Some(_) => match o.inner.get_boolean(&key_str) {
            Ok(Some(v)) => {
                if !value.is_null() {
                    unsafe { *value = v as c_int };
                }
                1
            }
            Ok(None) => {
                // The node exists but is null-valued. C returns NOTHING_TO_DO.
                let e = JsonError::NothingToDo("value was null".to_owned());
                unsafe { set_gerror(error, &e) };
                0
            }
            Err(e) => {
                unsafe { set_gerror(error, &e) };
                0
            }
        },
    }
}

/// Gets a node for the given key.
///
/// Returns a new `FwupdRsJsonNode` that the caller must free, or NULL.
/// Sets `FWUPD_ERROR_NOT_FOUND` if the key does not exist.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_object_get_node(
    ptr: *const FwupdRsJsonObject,
    key: *const c_char,
    error: *mut *mut GError,
) -> *mut FwupdRsJsonNode {
    let o = match unsafe { ptr.as_ref() } {
        Some(o) => o,
        None => return std::ptr::null_mut(),
    };
    if key.is_null() {
        return std::ptr::null_mut();
    }
    let key_str = unsafe { std::ffi::CStr::from_ptr(key) }.to_string_lossy();
    match o.inner.get_node(&key_str) {
        Some(node) => Box::into_raw(Box::new(FwupdRsJsonNode::new(node.clone()))),
        None => {
            let e = JsonError::NotFound(format!("no json_node for key {key_str}"));
            unsafe { set_gerror(error, &e) };
            std::ptr::null_mut()
        }
    }
}

/// Gets an object for the given key.
///
/// Returns a new `FwupdRsJsonObject` that the caller must free, or NULL.
/// Sets `FWUPD_ERROR_NOT_FOUND` if the key does not exist.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_object_get_object(
    ptr: *const FwupdRsJsonObject,
    key: *const c_char,
    error: *mut *mut GError,
) -> *mut FwupdRsJsonObject {
    let o = match unsafe { ptr.as_ref() } {
        Some(o) => o,
        None => return std::ptr::null_mut(),
    };
    if key.is_null() {
        return std::ptr::null_mut();
    }
    let key_str = unsafe { std::ffi::CStr::from_ptr(key) }.to_string_lossy();
    match o.inner.get_node(&key_str) {
        None => {
            let e = JsonError::NotFound(format!("no json_node for key {key_str}"));
            unsafe { set_gerror(error, &e) };
            std::ptr::null_mut()
        }
        Some(_) => match o.inner.get_object(&key_str) {
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
        },
    }
}

/// Gets an array for the given key.
///
/// Returns a new `FwupdRsJsonArray` that the caller must free, or NULL.
/// Sets `FWUPD_ERROR_NOT_FOUND` if the key does not exist.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_object_get_array(
    ptr: *const FwupdRsJsonObject,
    key: *const c_char,
    error: *mut *mut GError,
) -> *mut FwupdRsJsonArray {
    let o = match unsafe { ptr.as_ref() } {
        Some(o) => o,
        None => return std::ptr::null_mut(),
    };
    if key.is_null() {
        return std::ptr::null_mut();
    }
    let key_str = unsafe { std::ffi::CStr::from_ptr(key) }.to_string_lossy();
    match o.inner.get_node(&key_str) {
        None => {
            let e = JsonError::NotFound(format!("no json_node for key {key_str}"));
            unsafe { set_gerror(error, &e) };
            std::ptr::null_mut()
        }
        Some(_) => match o.inner.get_array(&key_str) {
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
        },
    }
}

/// Adds a string value to the object.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_object_add_string(
    ptr: *mut FwupdRsJsonObject,
    key: *const c_char,
    value: *const c_char,
) {
    let o = match unsafe { ptr.as_mut() } {
        Some(o) => o,
        None => return,
    };
    if key.is_null() {
        return;
    }
    let key_str = unsafe { std::ffi::CStr::from_ptr(key) }
        .to_string_lossy()
        .into_owned();
    if value.is_null() {
        o.inner.add_null(&key_str);
    } else {
        let val_str = unsafe { std::ffi::CStr::from_ptr(value) }
            .to_string_lossy()
            .into_owned();
        o.inner.add_string(&key_str, &val_str);
    }
}

/// Adds a raw value to the object.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_object_add_raw(
    ptr: *mut FwupdRsJsonObject,
    key: *const c_char,
    value: *const c_char,
) {
    let o = match unsafe { ptr.as_mut() } {
        Some(o) => o,
        None => return,
    };
    if key.is_null() || value.is_null() {
        return;
    }
    let key_str = unsafe { std::ffi::CStr::from_ptr(key) }
        .to_string_lossy()
        .into_owned();
    let val_str = unsafe { std::ffi::CStr::from_ptr(value) }
        .to_string_lossy()
        .into_owned();
    o.inner.add_raw(&key_str, &val_str);
}

/// Adds an integer value to the object.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_object_add_integer(
    ptr: *mut FwupdRsJsonObject,
    key: *const c_char,
    value: i64,
) {
    let o = match unsafe { ptr.as_mut() } {
        Some(o) => o,
        None => return,
    };
    if key.is_null() {
        return;
    }
    let key_str = unsafe { std::ffi::CStr::from_ptr(key) }
        .to_string_lossy()
        .into_owned();
    o.inner.add_integer(&key_str, value);
}

/// Adds a boolean value to the object.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_object_add_boolean(
    ptr: *mut FwupdRsJsonObject,
    key: *const c_char,
    value: c_int,
) {
    let o = match unsafe { ptr.as_mut() } {
        Some(o) => o,
        None => return,
    };
    if key.is_null() {
        return;
    }
    let key_str = unsafe { std::ffi::CStr::from_ptr(key) }
        .to_string_lossy()
        .into_owned();
    o.inner.add_boolean(&key_str, value != 0);
}

/// Adds an object value to the object. Takes ownership of `obj`.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_object_add_object(
    ptr: *mut FwupdRsJsonObject,
    key: *const c_char,
    obj: *mut FwupdRsJsonObject,
) {
    let o = match unsafe { ptr.as_mut() } {
        Some(o) => o,
        None => return,
    };
    if key.is_null() || obj.is_null() {
        return;
    }
    let key_str = unsafe { std::ffi::CStr::from_ptr(key) }
        .to_string_lossy()
        .into_owned();
    // Clone the inner object (don't take ownership of the FFI wrapper since
    // the C side may still hold a reference).
    let inner_obj = unsafe { &*obj }.inner().clone();
    o.inner.add_object(&key_str, inner_obj);
}

/// Adds an array value to the object.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_object_add_array(
    ptr: *mut FwupdRsJsonObject,
    key: *const c_char,
    arr: *mut FwupdRsJsonArray,
) {
    let o = match unsafe { ptr.as_mut() } {
        Some(o) => o,
        None => return,
    };
    if key.is_null() || arr.is_null() {
        return;
    }
    let key_str = unsafe { std::ffi::CStr::from_ptr(key) }
        .to_string_lossy()
        .into_owned();
    let inner_arr = unsafe { &*arr }.inner().clone();
    o.inner.add_array(&key_str, inner_arr);
}

/// Adds a node value to the object. The node is cloned (not consumed).
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_object_add_node(
    ptr: *mut FwupdRsJsonObject,
    key: *const c_char,
    node: *const FwupdRsJsonNode,
) {
    let o = match unsafe { ptr.as_mut() } {
        Some(o) => o,
        None => return,
    };
    if key.is_null() || node.is_null() {
        return;
    }
    let key_str = unsafe { std::ffi::CStr::from_ptr(key) }
        .to_string_lossy()
        .into_owned();
    let inner_node = unsafe { &*node }.inner().clone();
    o.inner.add_node(&key_str, inner_node);
}

/// Converts the object to a string representation.
///
/// Returns a newly allocated `GString *`. The caller must free it with
/// `g_string_free()`.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_object_to_string(
    ptr: *const FwupdRsJsonObject,
    flags: c_uint,
) -> *mut GString {
    let o = match unsafe { ptr.as_ref() } {
        Some(o) => o,
        None => return std::ptr::null_mut(),
    };
    let export_flags = to_export_flags(flags);
    let s = o.inner.to_json_string(export_flags);
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
