/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! GObject subclassing support for [`Device`].
//!
//! Plugin authors implement [`DeviceImpl`] on their device struct to override
//! virtual methods like `probe`, `setup`, `write_firmware`, etc.

use crate::ffi;
use crate::{Device, Firmware, Progress};
use glib::prelude::*;
use glib::subclass::prelude::*;
use glib::translate::*;

/// Trait for implementing `FuDevice` virtual methods in Rust.
///
/// All methods have default implementations that chain to the parent class,
/// so you only need to override the methods your device requires.
///
/// # Example
///
/// ```ignore
/// mod imp {
///     use glib::subclass::prelude::*;
///     use fwupdplugin::device_subclass::DeviceImpl;
///
///     #[derive(Default)]
///     pub struct MyDevice;
///
///     #[glib::object_subclass]
///     impl ObjectSubclass for MyDevice {
///         const NAME: &'static str = "FuMyDevice";
///         type Type = super::MyDevice;
///         type ParentType = fwupdplugin::Device;
///     }
///
///     impl ObjectImpl for MyDevice {}
///     impl DeviceImpl for MyDevice {
///         fn probe(&self) -> Result<(), glib::Error> {
///             self.obj().set_name("My Device");
///             Ok(())
///         }
///     }
/// }
/// ```
pub trait DeviceImpl: ObjectImpl + ObjectSubclass<Type: IsA<Device>> {
    fn probe(&self) -> Result<(), glib::Error> {
        self.parent_probe()
    }
    fn setup(&self) -> Result<(), glib::Error> {
        self.parent_setup()
    }
    fn open(&self) -> Result<(), glib::Error> {
        self.parent_open()
    }
    fn close(&self) -> Result<(), glib::Error> {
        self.parent_close()
    }
    fn write_firmware(
        &self,
        firmware: &Firmware,
        progress: &Progress,
        flags: fwupd_sys::FwupdInstallFlags,
    ) -> Result<(), glib::Error> {
        self.parent_write_firmware(firmware, progress, flags)
    }
    fn detach(&self, progress: &Progress) -> Result<(), glib::Error> {
        self.parent_detach(progress)
    }
    fn attach(&self, progress: &Progress) -> Result<(), glib::Error> {
        self.parent_attach(progress)
    }
    fn activate(&self, progress: &Progress) -> Result<(), glib::Error> {
        self.parent_activate(progress)
    }
    fn reload(&self) -> Result<(), glib::Error> {
        self.parent_reload()
    }
    fn set_progress(&self, _progress: &Progress) {}
}

/// Extension trait providing parent-class method calls for [`DeviceImpl`].
pub trait DeviceImplExt: DeviceImpl {
    fn parent_probe(&self) -> Result<(), glib::Error> {
        unsafe {
            let klass = &*(self.obj().class().as_ref() as *const _ as *const ffi::FuDeviceClass);
            if let Some(f) = klass.probe {
                let mut error = std::ptr::null_mut();
                let ret = f(
                    self.obj().unsafe_cast_ref::<Device>().to_glib_none().0,
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

    fn parent_setup(&self) -> Result<(), glib::Error> {
        unsafe {
            let klass = &*(self.obj().class().as_ref() as *const _ as *const ffi::FuDeviceClass);
            if let Some(f) = klass.setup {
                let mut error = std::ptr::null_mut();
                let ret = f(
                    self.obj().unsafe_cast_ref::<Device>().to_glib_none().0,
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

    fn parent_open(&self) -> Result<(), glib::Error> {
        unsafe {
            let klass = &*(self.obj().class().as_ref() as *const _ as *const ffi::FuDeviceClass);
            if let Some(f) = klass.open {
                let mut error = std::ptr::null_mut();
                let ret = f(
                    self.obj().unsafe_cast_ref::<Device>().to_glib_none().0,
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

    fn parent_close(&self) -> Result<(), glib::Error> {
        unsafe {
            let klass = &*(self.obj().class().as_ref() as *const _ as *const ffi::FuDeviceClass);
            if let Some(f) = klass.close {
                let mut error = std::ptr::null_mut();
                let ret = f(
                    self.obj().unsafe_cast_ref::<Device>().to_glib_none().0,
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

    fn parent_write_firmware(
        &self,
        firmware: &Firmware,
        progress: &Progress,
        flags: fwupd_sys::FwupdInstallFlags,
    ) -> Result<(), glib::Error> {
        unsafe {
            let klass = &*(self.obj().class().as_ref() as *const _ as *const ffi::FuDeviceClass);
            if let Some(f) = klass.write_firmware {
                let mut error = std::ptr::null_mut();
                let ret = f(
                    self.obj().unsafe_cast_ref::<Device>().to_glib_none().0,
                    firmware.to_glib_none().0,
                    progress.to_glib_none().0,
                    flags,
                    &mut error,
                );
                if ret != glib::ffi::GFALSE {
                    Ok(())
                } else {
                    Err(from_glib_full(error))
                }
            } else {
                Err(glib::Error::new(
                    glib::FileError::Failed,
                    "write_firmware not implemented",
                ))
            }
        }
    }

    fn parent_detach(&self, progress: &Progress) -> Result<(), glib::Error> {
        unsafe {
            let klass = &*(self.obj().class().as_ref() as *const _ as *const ffi::FuDeviceClass);
            if let Some(f) = klass.detach {
                let mut error = std::ptr::null_mut();
                let ret = f(
                    self.obj().unsafe_cast_ref::<Device>().to_glib_none().0,
                    progress.to_glib_none().0,
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

    fn parent_attach(&self, progress: &Progress) -> Result<(), glib::Error> {
        unsafe {
            let klass = &*(self.obj().class().as_ref() as *const _ as *const ffi::FuDeviceClass);
            if let Some(f) = klass.attach {
                let mut error = std::ptr::null_mut();
                let ret = f(
                    self.obj().unsafe_cast_ref::<Device>().to_glib_none().0,
                    progress.to_glib_none().0,
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

    fn parent_activate(&self, progress: &Progress) -> Result<(), glib::Error> {
        unsafe {
            let klass = &*(self.obj().class().as_ref() as *const _ as *const ffi::FuDeviceClass);
            if let Some(f) = klass.activate {
                let mut error = std::ptr::null_mut();
                let ret = f(
                    self.obj().unsafe_cast_ref::<Device>().to_glib_none().0,
                    progress.to_glib_none().0,
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

    fn parent_reload(&self) -> Result<(), glib::Error> {
        unsafe {
            let klass = &*(self.obj().class().as_ref() as *const _ as *const ffi::FuDeviceClass);
            if let Some(f) = klass.reload {
                let mut error = std::ptr::null_mut();
                let ret = f(
                    self.obj().unsafe_cast_ref::<Device>().to_glib_none().0,
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
}

impl<T: DeviceImpl> DeviceImplExt for T {}

// GObject class_init hook: install the vfunc trampolines into FuDeviceClass
unsafe impl<T: DeviceImpl> IsSubclassable<T> for Device {
    fn class_init(class: &mut glib::Class<Self>) {
        Self::parent_class_init::<T>(class);
        let klass = class.as_mut() as *mut _ as *mut ffi::FuDeviceClass;
        unsafe {
            (*klass).probe = Some(probe_trampoline::<T>);
            (*klass).setup = Some(setup_trampoline::<T>);
            (*klass).open = Some(open_trampoline::<T>);
            (*klass).close = Some(close_trampoline::<T>);
            (*klass).write_firmware = Some(write_firmware_trampoline::<T>);
            (*klass).detach = Some(detach_trampoline::<T>);
            (*klass).attach = Some(attach_trampoline::<T>);
            (*klass).activate = Some(activate_trampoline::<T>);
            (*klass).reload = Some(reload_trampoline::<T>);
            (*klass).set_progress = Some(set_progress_trampoline::<T>);
        }
    }
}

// ---------------------------------------------------------------------------
// Trampoline functions for FuDeviceClass vfuncs
// ---------------------------------------------------------------------------

unsafe extern "C" fn probe_trampoline<T: DeviceImpl>(
    device: *mut ffi::FuDevice,
    error: *mut *mut glib::ffi::GError,
) -> glib::ffi::gboolean {
    let instance = &*(device as *mut T::Instance);
    let imp = instance.imp();
    match imp.probe() {
        Ok(()) => glib::ffi::GTRUE,
        Err(e) => {
            if !error.is_null() {
                *error = e.into_glib_ptr();
            }
            glib::ffi::GFALSE
        }
    }
}

unsafe extern "C" fn setup_trampoline<T: DeviceImpl>(
    device: *mut ffi::FuDevice,
    error: *mut *mut glib::ffi::GError,
) -> glib::ffi::gboolean {
    let instance = &*(device as *mut T::Instance);
    let imp = instance.imp();
    match imp.setup() {
        Ok(()) => glib::ffi::GTRUE,
        Err(e) => {
            if !error.is_null() {
                *error = e.into_glib_ptr();
            }
            glib::ffi::GFALSE
        }
    }
}

unsafe extern "C" fn open_trampoline<T: DeviceImpl>(
    device: *mut ffi::FuDevice,
    error: *mut *mut glib::ffi::GError,
) -> glib::ffi::gboolean {
    let instance = &*(device as *mut T::Instance);
    let imp = instance.imp();
    match imp.open() {
        Ok(()) => glib::ffi::GTRUE,
        Err(e) => {
            if !error.is_null() {
                *error = e.into_glib_ptr();
            }
            glib::ffi::GFALSE
        }
    }
}

unsafe extern "C" fn close_trampoline<T: DeviceImpl>(
    device: *mut ffi::FuDevice,
    error: *mut *mut glib::ffi::GError,
) -> glib::ffi::gboolean {
    let instance = &*(device as *mut T::Instance);
    let imp = instance.imp();
    match imp.close() {
        Ok(()) => glib::ffi::GTRUE,
        Err(e) => {
            if !error.is_null() {
                *error = e.into_glib_ptr();
            }
            glib::ffi::GFALSE
        }
    }
}

unsafe extern "C" fn write_firmware_trampoline<T: DeviceImpl>(
    device: *mut ffi::FuDevice,
    firmware: *mut ffi::FuFirmware,
    progress: *mut ffi::FuProgress,
    flags: fwupd_sys::FwupdInstallFlags,
    error: *mut *mut glib::ffi::GError,
) -> glib::ffi::gboolean {
    let instance = &*(device as *mut T::Instance);
    let imp = instance.imp();
    let firmware = from_glib_borrow::<_, Firmware>(firmware);
    let progress = from_glib_borrow::<_, Progress>(progress);
    match imp.write_firmware(&firmware, &progress, flags) {
        Ok(()) => glib::ffi::GTRUE,
        Err(e) => {
            if !error.is_null() {
                *error = e.into_glib_ptr();
            }
            glib::ffi::GFALSE
        }
    }
}

unsafe extern "C" fn detach_trampoline<T: DeviceImpl>(
    device: *mut ffi::FuDevice,
    progress: *mut ffi::FuProgress,
    error: *mut *mut glib::ffi::GError,
) -> glib::ffi::gboolean {
    let instance = &*(device as *mut T::Instance);
    let imp = instance.imp();
    let progress = from_glib_borrow::<_, Progress>(progress);
    match imp.detach(&progress) {
        Ok(()) => glib::ffi::GTRUE,
        Err(e) => {
            if !error.is_null() {
                *error = e.into_glib_ptr();
            }
            glib::ffi::GFALSE
        }
    }
}

unsafe extern "C" fn attach_trampoline<T: DeviceImpl>(
    device: *mut ffi::FuDevice,
    progress: *mut ffi::FuProgress,
    error: *mut *mut glib::ffi::GError,
) -> glib::ffi::gboolean {
    let instance = &*(device as *mut T::Instance);
    let imp = instance.imp();
    let progress = from_glib_borrow::<_, Progress>(progress);
    match imp.attach(&progress) {
        Ok(()) => glib::ffi::GTRUE,
        Err(e) => {
            if !error.is_null() {
                *error = e.into_glib_ptr();
            }
            glib::ffi::GFALSE
        }
    }
}

unsafe extern "C" fn activate_trampoline<T: DeviceImpl>(
    device: *mut ffi::FuDevice,
    progress: *mut ffi::FuProgress,
    error: *mut *mut glib::ffi::GError,
) -> glib::ffi::gboolean {
    let instance = &*(device as *mut T::Instance);
    let imp = instance.imp();
    let progress = from_glib_borrow::<_, Progress>(progress);
    match imp.activate(&progress) {
        Ok(()) => glib::ffi::GTRUE,
        Err(e) => {
            if !error.is_null() {
                *error = e.into_glib_ptr();
            }
            glib::ffi::GFALSE
        }
    }
}

unsafe extern "C" fn reload_trampoline<T: DeviceImpl>(
    device: *mut ffi::FuDevice,
    error: *mut *mut glib::ffi::GError,
) -> glib::ffi::gboolean {
    let instance = &*(device as *mut T::Instance);
    let imp = instance.imp();
    match imp.reload() {
        Ok(()) => glib::ffi::GTRUE,
        Err(e) => {
            if !error.is_null() {
                *error = e.into_glib_ptr();
            }
            glib::ffi::GFALSE
        }
    }
}

unsafe extern "C" fn set_progress_trampoline<T: DeviceImpl>(
    device: *mut ffi::FuDevice,
    progress: *mut ffi::FuProgress,
) {
    let instance = &*(device as *mut T::Instance);
    let imp = instance.imp();
    let progress = from_glib_borrow::<_, Progress>(progress);
    imp.set_progress(&progress);
}
