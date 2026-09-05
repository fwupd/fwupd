/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! FFI wrappers for [`fwupd::json::JsonArray`].

use std::ffi::CString;
use std::os::raw::{c_char, c_uint};
use std::sync::Arc;

use fwupd::json::JsonArray;

use super::node::FwupdRsJsonNode;
use super::object::FwupdRsJsonObject;
use crate::glib::{GError, GString};
use crate::json::to_export_flags;

/// Opaque FFI handle for a JSON array.
///
/// Holds an `Arc<JsonArray>` so that cloning is cheap and multiple FFI
/// wrappers can share the same underlying data.
pub struct FwupdRsJsonArray {
    inner: Arc<JsonArray>,
}

impl FwupdRsJsonArray {
    pub(crate) fn new(arr: Arc<JsonArray>) -> Self {
        Self { inner: arr }
    }

    pub(crate) fn inner(&self) -> &Arc<JsonArray> {
        &self.inner
    }

    pub(crate) fn inner_mut(&mut self) -> &mut JsonArray {
        Arc::make_mut(&mut self.inner)
    }
}

/// Creates a new empty JSON array.
#[no_mangle]
pub extern "C" fn fwupd_rs_json_array_new() -> *mut FwupdRsJsonArray {
    Box::into_raw(Box::new(FwupdRsJsonArray::new(Arc::new(JsonArray::new()))))
}

/// Frees a JSON array.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_array_free(ptr: *mut FwupdRsJsonArray) {
    if !ptr.is_null() {
        drop(unsafe { Box::from_raw(ptr) });
    }
}

/// Returns the number of elements in the array.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_array_get_size(ptr: *const FwupdRsJsonArray) -> c_uint {
    match unsafe { ptr.as_ref() } {
        Some(a) => c_uint::try_from(a.inner.len()).unwrap_or(c_uint::MAX),
        None => 0,
    }
}

/// Gets a node from the array at the given index.
///
/// Returns a new `FwupdRsJsonNode` that the caller must free, or NULL.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_array_get_node(
    ptr: *const FwupdRsJsonArray,
    idx: c_uint,
    error: *mut *mut GError,
) -> *mut FwupdRsJsonNode {
    let Some(a) = (unsafe { ptr.as_ref() }) else {
        return std::ptr::null_mut();
    };
    if let Some(node) = a.inner.get_node(idx as usize) {
        Box::into_raw(Box::new(FwupdRsJsonNode::new(node)))
    } else {
        GError::convert(
            error,
            &notfound!("index {} is larger than array size {}", idx, a.inner.len()),
        );
        std::ptr::null_mut()
    }
}

/// Gets a string from the array at the given index.
///
/// Returns a newly allocated string, or NULL on error.
/// Sets `FWUPD_ERROR_NOT_FOUND` if the index is out of bounds.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_array_get_string(
    ptr: *mut FwupdRsJsonArray,
    idx: c_uint,
    error: *mut *mut GError,
) -> *mut c_char {
    let Some(a) = (unsafe { ptr.as_mut() }) else {
        return std::ptr::null_mut();
    };
    if (idx as usize) >= a.inner.len() {
        GError::convert(
            error,
            &notfound!("index {} is larger than array size {}", idx, a.inner.len()),
        );
        return std::ptr::null_mut();
    }
    match a.inner.get_string(idx as usize) {
        Ok(Some(s)) => CString::new(s).unwrap_or_default().into_raw(),
        Ok(None) => std::ptr::null_mut(),
        Err(e) => {
            GError::convert(error, &(&e).into());
            std::ptr::null_mut()
        }
    }
}

/// Gets a raw value from the array at the given index.
///
/// Returns a newly allocated string, or NULL on error.
/// Sets `FWUPD_ERROR_NOT_FOUND` if the index is out of bounds.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_array_get_raw(
    ptr: *mut FwupdRsJsonArray,
    idx: c_uint,
    error: *mut *mut GError,
) -> *mut c_char {
    let Some(a) = (unsafe { ptr.as_mut() }) else {
        return std::ptr::null_mut();
    };
    if (idx as usize) >= a.inner.len() {
        GError::convert(
            error,
            &notfound!("index {} is larger than array size {}", idx, a.inner.len()),
        );
        return std::ptr::null_mut();
    }
    match a.inner.get_raw(idx as usize) {
        Ok(Some(s)) => CString::new(s).unwrap_or_default().into_raw(),
        Ok(None) => std::ptr::null_mut(),
        Err(e) => {
            GError::convert(error, &(&e).into());
            std::ptr::null_mut()
        }
    }
}

/// Gets an object from the array at the given index.
///
/// Returns a new `FwupdRsJsonObject` that the caller must free, or NULL.
/// Sets `FWUPD_ERROR_NOT_FOUND` if the index is out of bounds.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_array_get_object(
    ptr: *const FwupdRsJsonArray,
    idx: c_uint,
    error: *mut *mut GError,
) -> *mut FwupdRsJsonObject {
    let Some(a) = (unsafe { ptr.as_ref() }) else {
        return std::ptr::null_mut();
    };
    if (idx as usize) >= a.inner.len() {
        GError::convert(
            error,
            &notfound!("index {} is larger than array size {}", idx, a.inner.len()),
        );
        return std::ptr::null_mut();
    }
    match a.inner.get_object(idx as usize) {
        Ok(Some(obj)) => Box::into_raw(Box::new(FwupdRsJsonObject::new(obj.clone()))),
        Ok(None) => std::ptr::null_mut(),
        Err(e) => {
            GError::convert(error, &(&e).into());
            std::ptr::null_mut()
        }
    }
}

/// Gets another array from the array at the given index.
///
/// Returns a new `FwupdRsJsonArray` that the caller must free, or NULL.
/// Sets `FWUPD_ERROR_NOT_FOUND` if the index is out of bounds.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_array_get_array(
    ptr: *const FwupdRsJsonArray,
    idx: c_uint,
    error: *mut *mut GError,
) -> *mut FwupdRsJsonArray {
    let Some(a) = (unsafe { ptr.as_ref() }) else {
        return std::ptr::null_mut();
    };
    if (idx as usize) >= a.inner.len() {
        GError::convert(
            error,
            &notfound!("index {} is larger than array size {}", idx, a.inner.len()),
        );
        return std::ptr::null_mut();
    }
    match a.inner.get_array(idx as usize) {
        Ok(Some(arr)) => Box::into_raw(Box::new(FwupdRsJsonArray::new(arr.clone()))),
        Ok(None) => std::ptr::null_mut(),
        Err(e) => {
            GError::convert(error, &(&e).into());
            std::ptr::null_mut()
        }
    }
}

/// Adds a string to the array.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_array_add_string(
    ptr: *mut FwupdRsJsonArray,
    value: *const c_char,
) {
    let Some(a) = (unsafe { ptr.as_mut() }) else {
        return;
    };
    if value.is_null() {
        return;
    }
    let s = unsafe { std::ffi::CStr::from_ptr(value) }
        .to_string_lossy()
        .into_owned();
    a.inner_mut().add_string(&s);
}

/// Adds a raw value to the array.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_array_add_raw(
    ptr: *mut FwupdRsJsonArray,
    value: *const c_char,
) {
    let Some(a) = (unsafe { ptr.as_mut() }) else {
        return;
    };
    if value.is_null() {
        return;
    }
    let s = unsafe { std::ffi::CStr::from_ptr(value) }
        .to_string_lossy()
        .into_owned();
    a.inner_mut().add_raw(&s);
}

/// Adds an object to the array. The object is cloned (not consumed).
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_array_add_object(
    ptr: *mut FwupdRsJsonArray,
    obj: *const FwupdRsJsonObject,
) {
    let Some(a) = (unsafe { ptr.as_mut() }) else {
        return;
    };
    if obj.is_null() {
        return;
    }
    let inner_obj = unsafe { &*obj }.inner().clone();
    a.inner_mut().add_object(inner_obj);
}

/// Adds another array to this array. The array is cloned (not consumed).
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_array_add_array(
    ptr: *mut FwupdRsJsonArray,
    arr: *const FwupdRsJsonArray,
) {
    let Some(a) = (unsafe { ptr.as_mut() }) else {
        return;
    };
    if arr.is_null() {
        return;
    }
    let inner_arr = unsafe { &*arr }.inner.clone();
    a.inner_mut().add_array(inner_arr);
}

/// Adds a node to the array. The node is cloned (not consumed).
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_array_add_node(
    ptr: *mut FwupdRsJsonArray,
    node: *const FwupdRsJsonNode,
) {
    let Some(a) = (unsafe { ptr.as_mut() }) else {
        return;
    };
    if node.is_null() {
        return;
    }
    let inner_node = unsafe { &*node }.inner().clone();
    a.inner_mut().add_node(inner_node);
}

/// Converts the array to a string representation.
///
/// Returns a newly allocated `GString *`. The caller must free it with
/// `g_string_free()`.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_array_to_string(
    ptr: *const FwupdRsJsonArray,
    flags: c_uint,
) -> *mut GString {
    let Some(a) = (unsafe { ptr.as_ref() }) else {
        return std::ptr::null_mut();
    };
    let s = a.inner.to_json_string(to_export_flags(flags));
    GString::from_rust_string(&s)
}
