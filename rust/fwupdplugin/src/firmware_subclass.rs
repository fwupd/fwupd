/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! GObject subclassing support for [`Firmware`].
//!
//! Plugin authors implement [`FirmwareImpl`] on their firmware struct to
//! override virtual methods like `parse`, `write`, `build`, etc.

use crate::ffi;
use crate::Firmware;
use glib::prelude::*;
use glib::subclass::prelude::*;
use glib::translate::*;

/// Trait for implementing `FuFirmware` virtual methods in Rust.
///
/// All methods have default implementations that chain to the parent class.
pub trait FirmwareImpl: ObjectImpl + ObjectSubclass<Type: IsA<Firmware>> {
    /// Parse the firmware from an input stream.
    fn parse(&self, stream: &gio::InputStream, flags: u64) -> Result<(), glib::Error> {
        self.parent_parse(stream, flags)
    }

    /// Write the firmware contents out as a byte array.
    fn write(&self) -> Result<glib::ByteArray, glib::Error> {
        self.parent_write()
    }
}

/// Extension trait providing parent-class method calls for [`FirmwareImpl`].
pub trait FirmwareImplExt: FirmwareImpl {
    fn parent_parse(&self, stream: &gio::InputStream, flags: u64) -> Result<(), glib::Error> {
        unsafe {
            let klass = &*(self.obj().class().as_ref() as *const _ as *const ffi::FuFirmwareClass);
            if let Some(f) = klass.parse {
                let mut error = std::ptr::null_mut();
                let ret = f(
                    self.obj().unsafe_cast_ref::<Firmware>().to_glib_none().0,
                    stream.as_ptr() as *mut _,
                    flags,
                    &mut error,
                );
                if ret != glib::ffi::GFALSE {
                    Ok(())
                } else {
                    Err(from_glib_full(error))
                }
            } else {
                Ok(())
            }
        }
    }

    fn parent_write(&self) -> Result<glib::ByteArray, glib::Error> {
        unsafe {
            let klass = &*(self.obj().class().as_ref() as *const _ as *const ffi::FuFirmwareClass);
            if let Some(f) = klass.write {
                let mut error = std::ptr::null_mut();
                let ret = f(
                    self.obj().unsafe_cast_ref::<Firmware>().to_glib_none().0,
                    &mut error,
                );
                if error.is_null() {
                    Ok(from_glib_full(ret))
                } else {
                    Err(from_glib_full(error))
                }
            } else {
                Err(glib::Error::new(
                    glib::FileError::Failed,
                    "write not implemented",
                ))
            }
        }
    }
}

impl<T: FirmwareImpl> FirmwareImplExt for T {}

unsafe impl<T: FirmwareImpl> IsSubclassable<T> for Firmware {
    fn class_init(class: &mut glib::Class<Self>) {
        Self::parent_class_init::<T>(class);
        let klass = class.as_mut() as *mut _ as *mut ffi::FuFirmwareClass;
        unsafe {
            (*klass).parse = Some(parse_trampoline::<T>);
            (*klass).write = Some(write_trampoline::<T>);
        }
    }
}

// ---------------------------------------------------------------------------
// Trampoline functions
// ---------------------------------------------------------------------------

unsafe extern "C" fn parse_trampoline<T: FirmwareImpl>(
    firmware: *mut ffi::FuFirmware,
    stream: *mut gio::ffi::GInputStream,
    flags: u64,
    error: *mut *mut glib::ffi::GError,
) -> glib::ffi::gboolean {
    let instance = &*(firmware as *mut T::Instance);
    let imp = instance.imp();
    let stream = from_glib_borrow::<_, gio::InputStream>(stream);
    match imp.parse(&stream, flags) {
        Ok(()) => glib::ffi::GTRUE,
        Err(e) => {
            if !error.is_null() {
                *error = e.into_glib_ptr();
            }
            glib::ffi::GFALSE
        }
    }
}

unsafe extern "C" fn write_trampoline<T: FirmwareImpl>(
    firmware: *mut ffi::FuFirmware,
    error: *mut *mut glib::ffi::GError,
) -> *mut glib::ffi::GByteArray {
    let instance = &*(firmware as *mut T::Instance);
    let imp = instance.imp();
    match imp.write() {
        Ok(bytes) => bytes.to_glib_full(),
        Err(e) => {
            if !error.is_null() {
                *error = e.into_glib_ptr();
            }
            std::ptr::null_mut()
        }
    }
}
