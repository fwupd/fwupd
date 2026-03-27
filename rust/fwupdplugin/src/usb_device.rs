/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! GObject wrapper for `FuUsbDevice`.

use crate::ffi;
use crate::udev_device::UdevDevice;
use crate::Device;
use glib::prelude::*;
use glib::subclass::prelude::*;
use glib::translate::*;

glib::wrapper! {
    /// A USB device that can be updated.
    ///
    /// Wraps the C `FuUsbDevice` type. Subclass this for USB devices.
    pub struct UsbDevice(Object<ffi::FuUsbDevice, ffi::FuUsbDeviceClass>)
        @extends UdevDevice, Device;

    match fn {
        type_ => || ffi::fu_usb_device_get_type(),
    }
}

/// Trait containing safe method wrappers for [`UsbDevice`] and its subclasses.
pub trait UsbDeviceExt: IsA<UsbDevice> + 'static {
    /// Sets the USB configuration number.
    #[doc(alias = "fu_usb_device_set_configuration")]
    fn set_usb_configuration(&self, configuration: i32) {
        unsafe {
            ffi::fu_usb_device_set_configuration(self.as_ref().to_glib_none().0, configuration);
        }
    }

    /// Adds a USB interface number to claim on open.
    #[doc(alias = "fu_usb_device_add_interface")]
    fn add_interface(&self, number: u8) {
        unsafe {
            ffi::fu_usb_device_add_interface(self.as_ref().to_glib_none().0, number);
        }
    }

    /// Performs a USB interrupt transfer.
    ///
    /// Returns the number of bytes actually transferred.
    #[doc(alias = "fu_usb_device_interrupt_transfer")]
    fn interrupt_transfer(
        &self,
        endpoint: u8,
        data: &mut [u8],
        timeout: u32,
    ) -> Result<usize, glib::Error> {
        unsafe {
            let mut actual_length: usize = 0;
            let mut error = std::ptr::null_mut();
            let ret = ffi::fu_usb_device_interrupt_transfer(
                self.as_ref().to_glib_none().0,
                endpoint,
                data.as_mut_ptr(),
                data.len(),
                &mut actual_length,
                timeout,
                std::ptr::null_mut(), // cancellable
                &mut error,
            );
            if ret != glib::ffi::GFALSE {
                Ok(actual_length)
            } else {
                Err(from_glib_full(error))
            }
        }
    }

    /// Gets the index for a custom USB descriptor.
    ///
    /// Returns the descriptor index for the given class/subclass/protocol,
    /// or an error if not found.
    #[doc(alias = "fu_usb_device_get_custom_index")]
    fn get_custom_index(
        &self,
        class_id: u8,
        subclass_id: u8,
        protocol_id: u8,
    ) -> Result<u8, glib::Error> {
        unsafe {
            let mut error = std::ptr::null_mut();
            let ret = ffi::fu_usb_device_get_custom_index(
                self.as_ref().to_glib_none().0,
                class_id,
                subclass_id,
                protocol_id,
                &mut error,
            );
            if error.is_null() {
                Ok(ret)
            } else {
                Err(from_glib_full(error))
            }
        }
    }

    /// Gets a USB string descriptor by index.
    #[doc(alias = "fu_usb_device_get_string_descriptor")]
    fn get_string_descriptor(&self, desc_index: u8) -> Result<glib::GString, glib::Error> {
        unsafe {
            let mut error = std::ptr::null_mut();
            let ret = ffi::fu_usb_device_get_string_descriptor(
                self.as_ref().to_glib_none().0,
                desc_index,
                &mut error,
            );
            if error.is_null() {
                Ok(from_glib_full(ret))
            } else {
                Err(from_glib_full(error))
            }
        }
    }
}

impl<O: IsA<UsbDevice>> UsbDeviceExt for O {}

/// Trait for implementing `FuUsbDevice` virtual methods.
///
/// `FuUsbDeviceClass` has no additional vfuncs beyond `FuDeviceClass`,
/// so this trait is currently empty. All device vfuncs are on
/// [`DeviceImpl`](crate::DeviceImpl).
pub trait UsbDeviceImpl: crate::udev_device::UdevDeviceImpl {}

unsafe impl<T: UsbDeviceImpl> IsSubclassable<T> for UsbDevice {
    fn class_init(class: &mut glib::Class<Self>) {
        Self::parent_class_init::<T>(class);
    }
}
