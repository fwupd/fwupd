/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

use crate::ffi;
use glib::prelude::*;
use glib::translate::*;

use crate::{Firmware, Progress};

glib::wrapper! {
    /// A physical or logical device that can be updated.
    ///
    /// Wraps the C `FuDevice` type.
    pub struct Device(Object<ffi::FuDevice, ffi::FuDeviceClass>);

    match fn {
        type_ => || ffi::fu_device_get_type(),
    }
}

/// Trait containing safe method wrappers for [`Device`] and its subclasses.
pub trait DeviceExt: IsA<Device> + 'static {
    /// Sets the device name (human-readable).
    #[doc(alias = "fu_device_set_name")]
    fn set_name(&self, name: &str) {
        unsafe {
            ffi::fu_device_set_name(self.as_ref().to_glib_none().0, name.to_glib_none().0);
        }
    }

    /// Sets the device vendor.
    #[doc(alias = "fu_device_set_vendor")]
    fn set_vendor(&self, vendor: &str) {
        unsafe {
            ffi::fu_device_set_vendor(self.as_ref().to_glib_none().0, vendor.to_glib_none().0);
        }
    }

    /// Sets the device version string.
    #[doc(alias = "fu_device_set_version")]
    fn set_version(&self, version: &str) {
        unsafe {
            ffi::fu_device_set_version(self.as_ref().to_glib_none().0, version.to_glib_none().0);
        }
    }

    /// Sets the raw version number.
    #[doc(alias = "fu_device_set_version_raw")]
    fn set_version_raw(&self, version_raw: u64) {
        unsafe {
            ffi::fu_device_set_version_raw(self.as_ref().to_glib_none().0, version_raw);
        }
    }

    /// Sets the version format (e.g. triplet, quad, etc.).
    #[doc(alias = "fu_device_set_version_format")]
    fn set_version_format(&self, fmt: crate::fwupd_sys::FwupdVersionFormat) {
        unsafe {
            ffi::fu_device_set_version_format(self.as_ref().to_glib_none().0, fmt);
        }
    }

    /// Adds a device flag (e.g. updatable, needs-reboot).
    #[doc(alias = "fu_device_add_flag")]
    fn add_flag(&self, flag: crate::fwupd_sys::FwupdDeviceFlags) {
        unsafe {
            ffi::fu_device_add_flag(self.as_ref().to_glib_none().0, flag);
        }
    }

    /// Removes a device flag.
    #[doc(alias = "fu_device_remove_flag")]
    fn remove_flag(&self, flag: crate::fwupd_sys::FwupdDeviceFlags) {
        unsafe {
            ffi::fu_device_remove_flag(self.as_ref().to_glib_none().0, flag);
        }
    }

    /// Adds an instance ID (used for GUID generation).
    #[doc(alias = "fu_device_add_instance_id")]
    fn add_instance_id(&self, instance_id: &str) {
        unsafe {
            ffi::fu_device_add_instance_id(
                self.as_ref().to_glib_none().0,
                instance_id.to_glib_none().0,
            );
        }
    }

    /// Returns whether the device has a specific GUID.
    #[doc(alias = "fu_device_has_guid")]
    fn has_guid(&self, guid: &str) -> bool {
        unsafe {
            from_glib(ffi::fu_device_has_guid(
                self.as_ref().to_glib_none().0,
                guid.to_glib_none().0,
            ))
        }
    }

    /// Adds a parent GUID to match parent devices.
    #[doc(alias = "fu_device_add_parent_guid")]
    fn add_parent_guid(&self, guid: &str) {
        unsafe {
            ffi::fu_device_add_parent_guid(self.as_ref().to_glib_none().0, guid.to_glib_none().0);
        }
    }

    /// Sets the device ID.
    #[doc(alias = "fu_device_set_id")]
    fn set_id(&self, id: &str) {
        unsafe {
            ffi::fu_device_set_id(self.as_ref().to_glib_none().0, id.to_glib_none().0);
        }
    }

    /// Sets the physical ID (e.g. USB bus address).
    #[doc(alias = "fu_device_set_physical_id")]
    fn set_physical_id(&self, physical_id: &str) {
        unsafe {
            ffi::fu_device_set_physical_id(
                self.as_ref().to_glib_none().0,
                physical_id.to_glib_none().0,
            );
        }
    }

    /// Sets the logical ID to distinguish devices at the same physical location.
    #[doc(alias = "fu_device_set_logical_id")]
    fn set_logical_id(&self, logical_id: &str) {
        unsafe {
            ffi::fu_device_set_logical_id(
                self.as_ref().to_glib_none().0,
                logical_id.to_glib_none().0,
            );
        }
    }

    /// Sets the firmware GType for this device.
    #[doc(alias = "fu_device_set_firmware_gtype")]
    fn set_firmware_gtype(&self, firmware_gtype: glib::Type) {
        unsafe {
            ffi::fu_device_set_firmware_gtype(
                self.as_ref().to_glib_none().0,
                firmware_gtype.into_glib(),
            );
        }
    }

    /// Sets the maximum firmware size in bytes.
    #[doc(alias = "fu_device_set_firmware_size_max")]
    fn set_firmware_size_max(&self, size_max: u64) {
        unsafe {
            ffi::fu_device_set_firmware_size_max(self.as_ref().to_glib_none().0, size_max);
        }
    }

    /// Sets the remove delay in milliseconds (time to wait for replug).
    #[doc(alias = "fu_device_set_remove_delay")]
    fn set_remove_delay(&self, remove_delay: u32) {
        unsafe {
            ffi::fu_device_set_remove_delay(self.as_ref().to_glib_none().0, remove_delay);
        }
    }

    /// Adds a string key-value pair to the device's instance data.
    #[doc(alias = "fu_device_add_instance_str")]
    fn add_instance_str(&self, key: &str, value: &str) {
        unsafe {
            ffi::fu_device_add_instance_str(
                self.as_ref().to_glib_none().0,
                key.to_glib_none().0,
                value.to_glib_none().0,
            );
        }
    }

    /// Adds a u16 value to the device's instance data.
    #[doc(alias = "fu_device_add_instance_u16")]
    fn add_instance_u16(&self, key: &str, value: u16) {
        unsafe {
            ffi::fu_device_add_instance_u16(
                self.as_ref().to_glib_none().0,
                key.to_glib_none().0,
                value,
            );
        }
    }

    /// Adds a u32 value to the device's instance data.
    #[doc(alias = "fu_device_add_instance_u32")]
    fn add_instance_u32(&self, key: &str, value: u32) {
        unsafe {
            ffi::fu_device_add_instance_u32(
                self.as_ref().to_glib_none().0,
                key.to_glib_none().0,
                value,
            );
        }
    }

    /// Opens the device for I/O.
    #[doc(alias = "fu_device_open")]
    fn open(&self) -> Result<(), glib::Error> {
        unsafe {
            let mut error = std::ptr::null_mut();
            let ret = ffi::fu_device_open(self.as_ref().to_glib_none().0, &mut error);
            if ret != glib::ffi::GFALSE {
                Ok(())
            } else {
                Err(from_glib_full(error))
            }
        }
    }

    /// Closes the device.
    #[doc(alias = "fu_device_close")]
    fn close(&self) -> Result<(), glib::Error> {
        unsafe {
            let mut error = std::ptr::null_mut();
            let ret = ffi::fu_device_close(self.as_ref().to_glib_none().0, &mut error);
            if ret != glib::ffi::GFALSE {
                Ok(())
            } else {
                Err(from_glib_full(error))
            }
        }
    }

    /// Probes the device to discover identity information.
    #[doc(alias = "fu_device_probe")]
    fn probe(&self) -> Result<(), glib::Error> {
        unsafe {
            let mut error = std::ptr::null_mut();
            let ret = ffi::fu_device_probe(self.as_ref().to_glib_none().0, &mut error);
            if ret != glib::ffi::GFALSE {
                Ok(())
            } else {
                Err(from_glib_full(error))
            }
        }
    }

    /// Sets up the device after probing.
    #[doc(alias = "fu_device_setup")]
    fn setup(&self) -> Result<(), glib::Error> {
        unsafe {
            let mut error = std::ptr::null_mut();
            let ret = ffi::fu_device_setup(self.as_ref().to_glib_none().0, &mut error);
            if ret != glib::ffi::GFALSE {
                Ok(())
            } else {
                Err(from_glib_full(error))
            }
        }
    }

    /// Writes firmware to the device.
    #[doc(alias = "fu_device_write_firmware")]
    fn write_firmware(
        &self,
        firmware: &Firmware,
        progress: &Progress,
        flags: crate::fwupd_sys::FwupdInstallFlags,
    ) -> Result<(), glib::Error> {
        unsafe {
            let mut error = std::ptr::null_mut();
            let ret = ffi::fu_device_write_firmware(
                self.as_ref().to_glib_none().0,
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
        }
    }
}

impl<O: IsA<Device>> DeviceExt for O {}
