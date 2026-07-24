/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! FFI wrappers for [`fwupd::json::JsonArray`].

use std::ffi::CString;
use std::os::raw::{c_char, c_uint};

use fwupd::json::{ExportFlags, JsonArray, JsonError};

use super::node::FwupdRsJsonNode;
use super::object::FwupdRsJsonObject;
use crate::glib::GString;
use crate::glib::{set_gerror, GError};

/// Opaque FFI handle for a JSON array.
pub struct FwupdRsJsonArray {
    inner: JsonArray,
    /// Cached C strings for string getters. Kept alive until the wrapper is freed
    /// so that returned `*const c_char` pointers remain valid.
    cached_cstrs: Vec<CString>,
}

impl FwupdRsJsonArray {
    pub(crate) fn new(arr: JsonArray) -> Self {
        Self {
            inner: arr,
            cached_cstrs: Vec::new(),
        }
    }

    pub(crate) fn inner(&self) -> &JsonArray {
        &self.inner
    }
}

/// Creates a new empty JSON array.
#[no_mangle]
pub extern "C" fn fwupd_rs_json_array_new() -> *mut FwupdRsJsonArray {
    Box::into_raw(Box::new(FwupdRsJsonArray::new(JsonArray::new())))
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
        Some(a) => a.inner.len() as c_uint,
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
    let a = match unsafe { ptr.as_ref() } {
        Some(a) => a,
        None => return std::ptr::null_mut(),
    };
    if (idx as usize) >= a.inner.len() {
        let e = JsonError::NotFound(format!(
            "index {} is larger than array size {}",
            idx,
            a.inner.len()
        ));
        unsafe { set_gerror(error, &e) };
        return std::ptr::null_mut();
    }
    match a.inner.get_node(idx as usize) {
        Ok(Some(node)) => Box::into_raw(Box::new(FwupdRsJsonNode::new(node.clone()))),
        Ok(None) | Err(_) => std::ptr::null_mut(),
    }
}

/// Gets a string from the array at the given index.
///
/// Returns a pointer valid until the array is freed, or NULL on error.
/// Sets `FWUPD_ERROR_NOT_FOUND` if the index is out of bounds.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_array_get_string(
    ptr: *mut FwupdRsJsonArray,
    idx: c_uint,
    error: *mut *mut GError,
) -> *const c_char {
    let a = match unsafe { ptr.as_mut() } {
        Some(a) => a,
        None => return std::ptr::null(),
    };
    if (idx as usize) >= a.inner.len() {
        let e = JsonError::NotFound(format!(
            "index {} is larger than array size {}",
            idx,
            a.inner.len()
        ));
        unsafe { set_gerror(error, &e) };
        return std::ptr::null();
    }
    match a.inner.get_string(idx as usize) {
        Ok(Some(s)) => {
            a.cached_cstrs.push(CString::new(s).unwrap_or_default());
            a.cached_cstrs.last().unwrap().as_ptr()
        }
        Ok(None) => std::ptr::null(),
        Err(e) => {
            unsafe { set_gerror(error, &e) };
            std::ptr::null()
        }
    }
}

/// Gets a raw value from the array at the given index.
///
/// Returns a pointer valid until the array is freed, or NULL on error.
/// Sets `FWUPD_ERROR_NOT_FOUND` if the index is out of bounds.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_array_get_raw(
    ptr: *mut FwupdRsJsonArray,
    idx: c_uint,
    error: *mut *mut GError,
) -> *const c_char {
    let a = match unsafe { ptr.as_mut() } {
        Some(a) => a,
        None => return std::ptr::null(),
    };
    if (idx as usize) >= a.inner.len() {
        let e = JsonError::NotFound(format!(
            "index {} is larger than array size {}",
            idx,
            a.inner.len()
        ));
        unsafe { set_gerror(error, &e) };
        return std::ptr::null();
    }
    match a.inner.get_raw(idx as usize) {
        Ok(Some(s)) => {
            a.cached_cstrs.push(CString::new(s).unwrap_or_default());
            a.cached_cstrs.last().unwrap().as_ptr()
        }
        Ok(None) => std::ptr::null(),
        Err(e) => {
            unsafe { set_gerror(error, &e) };
            std::ptr::null()
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
    let a = match unsafe { ptr.as_ref() } {
        Some(a) => a,
        None => return std::ptr::null_mut(),
    };
    if (idx as usize) >= a.inner.len() {
        let e = JsonError::NotFound(format!(
            "index {} is larger than array size {}",
            idx,
            a.inner.len()
        ));
        unsafe { set_gerror(error, &e) };
        return std::ptr::null_mut();
    }
    match a.inner.get_object(idx as usize) {
        Ok(Some(obj)) => Box::into_raw(Box::new(FwupdRsJsonObject::new(obj.clone()))),
        Ok(None) => std::ptr::null_mut(),
        Err(e) => {
            unsafe { set_gerror(error, &e) };
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
    let a = match unsafe { ptr.as_ref() } {
        Some(a) => a,
        None => return std::ptr::null_mut(),
    };
    if (idx as usize) >= a.inner.len() {
        let e = JsonError::NotFound(format!(
            "index {} is larger than array size {}",
            idx,
            a.inner.len()
        ));
        unsafe { set_gerror(error, &e) };
        return std::ptr::null_mut();
    }
    match a.inner.get_array(idx as usize) {
        Ok(Some(arr)) => Box::into_raw(Box::new(FwupdRsJsonArray::new(arr.clone()))),
        Ok(None) => std::ptr::null_mut(),
        Err(e) => {
            unsafe { set_gerror(error, &e) };
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
    let a = match unsafe { ptr.as_mut() } {
        Some(a) => a,
        None => return,
    };
    if value.is_null() {
        return;
    }
    let s = unsafe { std::ffi::CStr::from_ptr(value) }
        .to_string_lossy()
        .into_owned();
    a.inner.add_string(&s);
}

/// Adds a raw value to the array.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_array_add_raw(
    ptr: *mut FwupdRsJsonArray,
    value: *const c_char,
) {
    let a = match unsafe { ptr.as_mut() } {
        Some(a) => a,
        None => return,
    };
    if value.is_null() {
        return;
    }
    let s = unsafe { std::ffi::CStr::from_ptr(value) }
        .to_string_lossy()
        .into_owned();
    a.inner.add_raw(&s);
}

/// Adds an object to the array. The object is cloned (not consumed).
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_array_add_object(
    ptr: *mut FwupdRsJsonArray,
    obj: *const FwupdRsJsonObject,
) {
    let a = match unsafe { ptr.as_mut() } {
        Some(a) => a,
        None => return,
    };
    if obj.is_null() {
        return;
    }
    let inner_obj = unsafe { &*obj }.inner().clone();
    a.inner.add_object(inner_obj);
}

/// Adds another array to this array. The array is cloned (not consumed).
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_array_add_array(
    ptr: *mut FwupdRsJsonArray,
    arr: *const FwupdRsJsonArray,
) {
    let a = match unsafe { ptr.as_mut() } {
        Some(a) => a,
        None => return,
    };
    if arr.is_null() {
        return;
    }
    let inner_arr = unsafe { &*arr }.inner.clone();
    a.inner.add_array(inner_arr);
}

/// Adds a node to the array. The node is cloned (not consumed).
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_array_add_node(
    ptr: *mut FwupdRsJsonArray,
    node: *const FwupdRsJsonNode,
) {
    let a = match unsafe { ptr.as_mut() } {
        Some(a) => a,
        None => return,
    };
    if node.is_null() {
        return;
    }
    let inner_node = unsafe { &*node }.inner().clone();
    a.inner.add_node(inner_node);
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
    let a = match unsafe { ptr.as_ref() } {
        Some(a) => a,
        None => return std::ptr::null_mut(),
    };
    let mut ef = ExportFlags::NONE;
    if flags & 1 != 0 {
        ef |= ExportFlags::INDENT;
    }
    if flags & 2 != 0 {
        ef |= ExportFlags::TRAILING_NEWLINE;
    }
    let s = a.inner.to_json_string(ef);
    GString::from_rust_string(&s)
}
