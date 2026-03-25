/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! GObject wrapper for `FuHidDevice`.

use crate::ffi;
use crate::udev_device::UdevDevice;
use crate::usb_device::UsbDevice;
use crate::Device;
use glib::subclass::prelude::*;

glib::wrapper! {
    /// A HID (Human Interface Device) that can be updated.
    ///
    /// Wraps the C `FuHidDevice` type. Subclass this for HID devices.
    pub struct HidDevice(Object<ffi::FuHidDevice, ffi::FuHidDeviceClass>)
        @extends UsbDevice, UdevDevice, Device;

    match fn {
        type_ => || ffi::fu_hid_device_get_type(),
    }
}

/// Trait for implementing `FuHidDevice` virtual methods.
///
/// `FuHidDeviceClass` has no additional vfuncs beyond `FuDeviceClass`,
/// so this trait is currently empty. All device vfuncs are on
/// [`DeviceImpl`](crate::DeviceImpl).
pub trait HidDeviceImpl: crate::usb_device::UsbDeviceImpl {}

unsafe impl<T: HidDeviceImpl> IsSubclassable<T> for HidDevice {
    fn class_init(class: &mut glib::Class<Self>) {
        Self::parent_class_init::<T>(class);
    }
}
