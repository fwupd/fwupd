/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! FFI wrappers for [`fwupd::json::JsonArray`].

use std::os::raw::{c_char, c_uint};
use std::sync::{Arc, Mutex};

use fwupd::json::{JsonArray, JsonError};

use super::node::FwupdRsJsonNode;
use super::object::FwupdRsJsonObject;
use crate::glib::{GError, GRefString, GString};
use crate::json::to_export_flags;

/// Opaque FFI handle for a JSON array.
///
/// Holds an `Arc<Mutex<JsonArray>>` to allow shared mutable access across
/// the FFI boundary, preserving the legacy C API's child mutation semantics.
pub struct FwupdRsJsonArray {
    inner: Arc<Mutex<JsonArray>>,
}

impl FwupdRsJsonArray {
    pub(crate) fn new(arr: Arc<Mutex<JsonArray>>) -> Self {
        Self { inner: arr }
    }

    pub(crate) fn inner(&self) -> &Arc<Mutex<JsonArray>> {
        &self.inner
    }
}

/// Creates a new empty JSON array.
#[no_mangle]
pub extern "C" fn fwupd_rs_json_array_new() -> *mut FwupdRsJsonArray {
    Box::into_raw(Box::new(FwupdRsJsonArray::new(Arc::new(Mutex::new(
        JsonArray::new(),
    )))))
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
        Some(a) => c_uint::try_from(a.inner.lock().unwrap().len()).unwrap_or(c_uint::MAX),
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
    let a = unwrap_or_panic!(unsafe { ptr.as_ref() });
    let guard = a.inner.lock().unwrap();

    if let Some(node) = guard.get_node(idx as usize) {
        Box::into_raw(Box::new(FwupdRsJsonNode::new(node)))
    } else {
        GError::convert(
            error,
            &notfound!("index {} is larger than array size {}", idx, guard.len()),
        );
        std::ptr::null_mut()
    }
}

/// Gets a string from the array at the given index.
///
/// Returns a new `GRefString`, or NULL on error.
/// Sets `FWUPD_ERROR_NOT_FOUND` if the index is out of bounds.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_array_get_string(
    ptr: *mut FwupdRsJsonArray,
    idx: c_uint,
    error: *mut *mut GError,
) -> *mut GRefString {
    let a = unwrap_or_panic!(unsafe { ptr.as_ref() });
    let guard = a.inner.lock().unwrap();

    if (idx as usize) >= guard.len() {
        GError::convert(
            error,
            &notfound!("index {} is larger than array size {}", idx, guard.len()),
        );
        return std::ptr::null_mut();
    }
    match guard.get_string(idx as usize) {
        Ok(Some(s)) => GRefString::from_rust_string(s),
        Ok(None) => {
            let e =
                JsonError::InvalidData("null in arrays is not supported by this parser".to_owned());
            GError::convert(error, &(&e).into());
            std::ptr::null_mut()
        }
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
) -> *mut GRefString {
    let a = unwrap_or_panic!(unsafe { ptr.as_ref() });
    let guard = a.inner.lock().unwrap();

    if (idx as usize) >= guard.len() {
        GError::convert(
            error,
            &notfound!("index {} is larger than array size {}", idx, guard.len()),
        );
        return std::ptr::null_mut();
    }
    match guard.get_raw(idx as usize) {
        Ok(Some(s)) => GRefString::from_rust_string(s),
        Ok(None) => {
            let e =
                JsonError::InvalidData("null in arrays is not supported by this parser".to_owned());
            GError::convert(error, &(&e).into());
            std::ptr::null_mut()
        }
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
    let a = unwrap_or_panic!(unsafe { ptr.as_ref() });
    let guard = a.inner.lock().unwrap();

    if (idx as usize) >= guard.len() {
        GError::convert(
            error,
            &notfound!("index {} is larger than array size {}", idx, guard.len()),
        );
        return std::ptr::null_mut();
    }
    match guard.get_object(idx as usize) {
        Ok(Some(obj)) => Box::into_raw(Box::new(FwupdRsJsonObject::new(obj.clone()))),
        Ok(None) => {
            let e =
                JsonError::InvalidData("null in arrays is not supported by this parser".to_owned());
            GError::convert(error, &(&e).into());
            std::ptr::null_mut()
        }
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
    let a = unwrap_or_panic!(unsafe { ptr.as_ref() });
    let guard = a.inner.lock().unwrap();

    if (idx as usize) >= guard.len() {
        GError::convert(
            error,
            &notfound!("index {} is larger than array size {}", idx, guard.len()),
        );
        return std::ptr::null_mut();
    }
    match guard.get_array(idx as usize) {
        Ok(Some(arr)) => Box::into_raw(Box::new(FwupdRsJsonArray::new(arr.clone()))),
        Ok(None) => {
            let e =
                JsonError::InvalidData("null in arrays is not supported by this parser".to_owned());
            GError::convert(error, &(&e).into());
            std::ptr::null_mut()
        }
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
    let a = unwrap_or_panic!(unsafe { ptr.as_mut() });
    panic_if!(value.is_null());

    let s = unsafe { std::ffi::CStr::from_ptr(value) }
        .to_string_lossy()
        .into_owned();
    a.inner.lock().unwrap().add_string(&s);
}

/// Adds a raw value to the array.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_array_add_raw(
    ptr: *mut FwupdRsJsonArray,
    value: *const c_char,
) {
    let a = unwrap_or_panic!(unsafe { ptr.as_mut() });
    panic_if!(value.is_null());

    let s = unsafe { std::ffi::CStr::from_ptr(value) }
        .to_string_lossy()
        .into_owned();
    a.inner.lock().unwrap().add_raw(&s);
}

/// Adds an object to the array. The object is shared (not consumed).
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_array_add_object(
    ptr: *mut FwupdRsJsonArray,
    obj: *const FwupdRsJsonObject,
) {
    let a = unwrap_or_panic!(unsafe { ptr.as_mut() });
    panic_if!(obj.is_null());

    // Share the Arc<Mutex<JsonObject>> so mutations via either handle affect
    // the same underlying data.
    let inner_obj = unsafe { &*obj }.inner().clone();
    a.inner.lock().unwrap().add_object(inner_obj);
}

/// Adds another array to this array. The array is shared (not consumed).
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_array_add_array(
    ptr: *mut FwupdRsJsonArray,
    arr: *const FwupdRsJsonArray,
) {
    let a = unwrap_or_panic!(unsafe { ptr.as_mut() });
    panic_if!(arr.is_null());

    // Share the Arc<Mutex<JsonArray>> so mutations via either handle affect
    // the same underlying data.
    let inner_arr = unsafe { &*arr }.inner.clone();
    a.inner.lock().unwrap().add_array(inner_arr);
}

/// Adds a node to the array. The node is cloned (not consumed).
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_array_add_node(
    ptr: *mut FwupdRsJsonArray,
    node: *const FwupdRsJsonNode,
) {
    let a = unwrap_or_panic!(unsafe { ptr.as_mut() });
    panic_if!(node.is_null());

    let inner_node = unsafe { &*node }.inner().clone();
    a.inner.lock().unwrap().add_node(inner_node);
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
    let a = unwrap_or_panic!(unsafe { ptr.as_ref() });

    let guard = a.inner.lock().unwrap();
    let s = guard.to_json_string(to_export_flags(flags));
    GString::from_rust_string(&s)
}
