/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Example fwupd plugin written in Rust.
//!
//! This plugin demonstrates the full structure of a modular fwupd plugin
//! using the `fwupdplugin` crate, including device subclassing.

use glib::prelude::*;

use fwupdplugin::prelude::*;
use fwupdplugin::{fu_debug, fu_info};

// ---------------------------------------------------------------------------
// Device implementation
// ---------------------------------------------------------------------------

mod device_imp {
    use fwupdplugin::fu_debug;
    use fwupdplugin::prelude::*;
    use glib::subclass::prelude::*;

    /// Per-device private state.
    #[derive(Default)]
    pub struct ExampleDevice;

    #[glib::object_subclass]
    impl ObjectSubclass for ExampleDevice {
        const NAME: &'static str = "FuExampleDevice";
        type Type = super::ExampleDevice;
        type ParentType = fwupdplugin::Device;
    }

    impl ObjectImpl for ExampleDevice {}

    impl DeviceImpl for ExampleDevice {
        fn probe(&self) -> Result<(), glib::Error> {
            let device = self.obj();
            device.set_name("Example Rust Device");
            device.set_vendor("Rust Plugin Project");
            device.set_version("0.0.1");
            fu_debug!("FuExampleDevice", "probe() called");
            Ok(())
        }

        fn setup(&self) -> Result<(), glib::Error> {
            fu_debug!("FuExampleDevice", "setup() called");
            Ok(())
        }

        fn write_firmware(
            &self,
            _firmware: &Firmware,
            _progress: &Progress,
            _flags: fwupdplugin::fwupd_sys::FwupdInstallFlags,
        ) -> Result<(), glib::Error> {
            fu_debug!("FuExampleDevice", "write_firmware() called");
            Ok(())
        }

        fn set_progress(&self, _progress: &Progress) {
            fu_debug!("FuExampleDevice", "set_progress() called");
        }
    }
}

glib::wrapper! {
    pub struct ExampleDevice(ObjectSubclass<device_imp::ExampleDevice>)
        @extends fwupdplugin::Device;
}

// ---------------------------------------------------------------------------
// Plugin implementation
// ---------------------------------------------------------------------------

/// The example plugin implementation.
#[derive(Default)]
struct ExamplePlugin;

impl PluginImpl for ExamplePlugin {
    fn constructed(&self, plugin: &Plugin) {
        let name = plugin.name().unwrap_or_default();
        fu_info!("FuPluginExample", "Rust plugin '{}' was constructed", name);
        plugin.add_udev_subsystem("usb");
        plugin.add_device_gtype(ExampleDevice::static_type());
    }

    fn startup(&self, plugin: &Plugin, _progress: &Progress) -> Result<(), glib::Error> {
        let name = plugin.name().unwrap_or_default();
        fu_info!("FuPluginExample", "Rust plugin '{}' starting up", name);
        Ok(())
    }
}

fwupdplugin::export_plugin!(ExamplePlugin);
