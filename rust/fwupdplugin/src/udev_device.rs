/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! GObject wrapper for `FuUdevDevice`.

use crate::ffi;
use crate::Device;
use glib::subclass::prelude::*;

glib::wrapper! {
    /// A device discovered via udev.
    ///
    /// Wraps the C `FuUdevDevice` type. Subclass this for devices
    /// discovered via udev subsystems.
    pub struct UdevDevice(Object<ffi::FuUdevDevice, ffi::FuUdevDeviceClass>)
        @extends Device;

    match fn {
        type_ => || ffi::fu_udev_device_get_type(),
    }
}

/// Trait for implementing `FuUdevDevice` virtual methods.
///
/// `FuUdevDeviceClass` has no additional vfuncs beyond `FuDeviceClass`,
/// so this trait is currently empty. All device vfuncs are on
/// [`DeviceImpl`](crate::DeviceImpl).
pub trait UdevDeviceImpl: crate::device_subclass::DeviceImpl {}

unsafe impl<T: UdevDeviceImpl> IsSubclassable<T> for UdevDevice {
    fn class_init(class: &mut glib::Class<Self>) {
        Self::parent_class_init::<T>(class);
    }
}
