/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! GObject wrapper for `FuUsbDevice`.

use crate::ffi;
use crate::udev_device::UdevDevice;
use crate::Device;
use glib::subclass::prelude::*;

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
