/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

use crate::ffi;
use glib::translate::*;

glib::wrapper! {
    /// A firmware image or container.
    ///
    /// Wraps the C `FuFirmware` type.
    pub struct Firmware(Object<ffi::FuFirmware, ffi::FuFirmwareClass>);

    match fn {
        type_ => || ffi::fu_firmware_get_type(),
    }
}

impl Firmware {
    /// Creates a new empty firmware object.
    #[doc(alias = "fu_firmware_new")]
    pub fn new() -> Firmware {
        unsafe { from_glib_full(ffi::fu_firmware_new()) }
    }

    /// Gets the address of the firmware, typically the offset in device memory.
    #[doc(alias = "fu_firmware_get_addr")]
    pub fn addr(&self) -> u64 {
        unsafe { ffi::fu_firmware_get_addr(self.to_glib_none().0) }
    }

    /// Gets the identifier string for this firmware.
    #[doc(alias = "fu_firmware_get_id")]
    pub fn id(&self) -> Option<glib::GString> {
        unsafe { from_glib_none(ffi::fu_firmware_get_id(self.to_glib_none().0)) }
    }

    /// Gets the index of this firmware image.
    #[doc(alias = "fu_firmware_get_idx")]
    pub fn idx(&self) -> u64 {
        unsafe { ffi::fu_firmware_get_idx(self.to_glib_none().0) }
    }

    /// Gets the size of the firmware in bytes.
    #[doc(alias = "fu_firmware_get_size")]
    pub fn size(&self) -> usize {
        unsafe { ffi::fu_firmware_get_size(self.to_glib_none().0) }
    }

    /// Gets the firmware contents as bytes.
    #[doc(alias = "fu_firmware_get_bytes")]
    pub fn bytes(&self) -> Result<glib::Bytes, glib::Error> {
        unsafe {
            let mut error = std::ptr::null_mut();
            let ret = ffi::fu_firmware_get_bytes(self.to_glib_none().0, &mut error);
            if error.is_null() {
                Ok(from_glib_full(ret))
            } else {
                Err(from_glib_full(error))
            }
        }
    }

    /// Sets the address of the firmware.
    #[doc(alias = "fu_firmware_set_addr")]
    pub fn set_addr(&self, addr: u64) {
        unsafe {
            ffi::fu_firmware_set_addr(self.to_glib_none().0, addr);
        }
    }

    /// Sets the firmware identifier.
    #[doc(alias = "fu_firmware_set_id")]
    pub fn set_id(&self, id: &str) {
        unsafe {
            ffi::fu_firmware_set_id(self.to_glib_none().0, id.to_glib_none().0);
        }
    }

    /// Sets the firmware index.
    #[doc(alias = "fu_firmware_set_idx")]
    pub fn set_idx(&self, idx: u64) {
        unsafe {
            ffi::fu_firmware_set_idx(self.to_glib_none().0, idx);
        }
    }

    /// Sets the firmware version string.
    #[doc(alias = "fu_firmware_set_version")]
    pub fn set_version(&self, version: &str) {
        unsafe {
            ffi::fu_firmware_set_version(self.to_glib_none().0, version.to_glib_none().0);
        }
    }

    /// Gets the firmware stream for reading raw data.
    #[doc(alias = "fu_firmware_get_stream")]
    pub fn stream(&self) -> Result<gio::InputStream, glib::Error> {
        unsafe {
            let mut error = std::ptr::null_mut();
            let ret = ffi::fu_firmware_get_stream(self.to_glib_none().0, &mut error);
            if error.is_null() {
                Ok(from_glib_full(ret))
            } else {
                Err(from_glib_full(error))
            }
        }
    }
}

impl Default for Firmware {
    fn default() -> Self {
        Self::new()
    }
}
