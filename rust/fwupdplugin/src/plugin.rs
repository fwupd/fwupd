/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

use std::sync::OnceLock;

use crate::ffi;
use glib::prelude::*;
use glib::translate::*;

use crate::{Context, Device, Progress};

glib::wrapper! {
    /// An fwupd plugin that manages firmware update devices.
    ///
    /// Wraps the C `FuPlugin` type. For modular plugins (loaded as `.so`
    /// files), use the [`export_plugin!`] macro together with a type
    /// implementing [`PluginImpl`].
    pub struct Plugin(Object<ffi::FuPlugin, ffi::FuPluginClass>);

    match fn {
        type_ => || ffi::fu_plugin_get_type(),
    }
}

/// Trait containing safe method wrappers for [`Plugin`].
pub trait PluginExt: IsA<Plugin> + 'static {
    /// Gets the plugin name.
    #[doc(alias = "fu_plugin_get_name")]
    fn name(&self) -> Option<glib::GString> {
        unsafe { from_glib_none(ffi::fu_plugin_get_name(self.as_ref().to_glib_none().0)) }
    }

    /// Gets the plugin context.
    #[doc(alias = "fu_plugin_get_context")]
    fn context(&self) -> Context {
        unsafe { from_glib_none(ffi::fu_plugin_get_context(self.as_ref().to_glib_none().0)) }
    }

    /// Registers a udev subsystem that this plugin handles.
    #[doc(alias = "fu_plugin_add_udev_subsystem")]
    fn add_udev_subsystem(&self, subsystem: &str) {
        unsafe {
            ffi::fu_plugin_add_udev_subsystem(
                self.as_ref().to_glib_none().0,
                subsystem.to_glib_none().0,
            );
        }
    }

    /// Registers a device GType that this plugin creates.
    #[doc(alias = "fu_plugin_add_device_gtype")]
    fn add_device_gtype(&self, device_gtype: glib::Type) {
        unsafe {
            ffi::fu_plugin_add_device_gtype(
                self.as_ref().to_glib_none().0,
                device_gtype.into_glib(),
            );
        }
    }

    /// Sets the default device GType for this plugin.
    #[doc(alias = "fu_plugin_set_device_gtype_default")]
    fn set_device_gtype_default(&self, device_gtype: glib::Type) {
        unsafe {
            ffi::fu_plugin_set_device_gtype_default(
                self.as_ref().to_glib_none().0,
                device_gtype.into_glib(),
            );
        }
    }

    /// Registers a firmware GType that this plugin uses.
    #[doc(alias = "fu_plugin_add_firmware_gtype")]
    fn add_firmware_gtype(&self, gtype: glib::Type) {
        unsafe {
            ffi::fu_plugin_add_firmware_gtype(self.as_ref().to_glib_none().0, gtype.into_glib());
        }
    }

    /// Adds a plugin ordering/conflict rule.
    #[doc(alias = "fu_plugin_add_rule")]
    fn add_rule(&self, rule: ffi::FuPluginRule, name: &str) {
        unsafe {
            ffi::fu_plugin_add_rule(self.as_ref().to_glib_none().0, rule, name.to_glib_none().0);
        }
    }

    /// Adds a device to the plugin's device list.
    #[doc(alias = "fu_plugin_add_device")]
    fn add_device(&self, device: &impl IsA<Device>) {
        unsafe {
            ffi::fu_plugin_add_device(
                self.as_ref().to_glib_none().0,
                device.as_ref().to_glib_none().0,
            );
        }
    }

    /// Removes a device from the plugin's device list.
    #[doc(alias = "fu_plugin_remove_device")]
    fn remove_device(&self, device: &impl IsA<Device>) {
        unsafe {
            ffi::fu_plugin_remove_device(
                self.as_ref().to_glib_none().0,
                device.as_ref().to_glib_none().0,
            );
        }
    }

    /// Gets a configuration value.
    #[doc(alias = "fu_plugin_get_config_value")]
    fn config_value(&self, key: &str) -> Option<glib::GString> {
        unsafe {
            from_glib_full(ffi::fu_plugin_get_config_value(
                self.as_ref().to_glib_none().0,
                key.to_glib_none().0,
            ))
        }
    }

    /// Gets a boolean configuration value.
    #[doc(alias = "fu_plugin_get_config_value_boolean")]
    fn config_value_boolean(&self, key: &str) -> bool {
        unsafe {
            from_glib(ffi::fu_plugin_get_config_value_boolean(
                self.as_ref().to_glib_none().0,
                key.to_glib_none().0,
            ))
        }
    }

    /// Sets a default configuration value.
    #[doc(alias = "fu_plugin_set_config_default")]
    fn set_config_default(&self, key: &str, value: &str) {
        unsafe {
            ffi::fu_plugin_set_config_default(
                self.as_ref().to_glib_none().0,
                key.to_glib_none().0,
                value.to_glib_none().0,
            );
        }
    }

    /// Adds report metadata key-value pair.
    #[doc(alias = "fu_plugin_add_report_metadata")]
    fn add_report_metadata(&self, key: &str, value: &str) {
        unsafe {
            ffi::fu_plugin_add_report_metadata(
                self.as_ref().to_glib_none().0,
                key.to_glib_none().0,
                value.to_glib_none().0,
            );
        }
    }
}

impl<O: IsA<Plugin>> PluginExt for O {}

// ---------------------------------------------------------------------------
// PluginImpl trait -- the Rust interface for modular plugin vfuncs
// ---------------------------------------------------------------------------

/// Trait for implementing an fwupd modular plugin in Rust.
///
/// All methods have default implementations that do nothing (or return `Ok(())`),
/// so you only need to override the methods your plugin requires.
///
/// Use [`export_plugin!`] to generate the `fu_plugin_init_vfuncs` C entry point.
pub trait PluginImpl: Default + Send + Sync + 'static {
    /// Called during plugin construction. Register device GTypes, udev
    /// subsystems, and plugin rules here.
    fn constructed(&self, _plugin: &Plugin) {}

    /// Called very early during loading, before the plugin is fully
    /// initialized. Receives the shared context.
    fn load(&self, _ctx: &Context) {}

    /// Called during daemon startup. Return an error if the plugin
    /// cannot operate on this system.
    fn startup(&self, _plugin: &Plugin, _progress: &Progress) -> Result<(), glib::Error> {
        Ok(())
    }

    /// Called after all plugins have started and devices are coldplugged.
    fn ready(&self, _plugin: &Plugin, _progress: &Progress) -> Result<(), glib::Error> {
        Ok(())
    }

    /// Enumerate devices at startup.
    fn coldplug(&self, _plugin: &Plugin, _progress: &Progress) -> Result<(), glib::Error> {
        Ok(())
    }

    /// Called when a backend device (USB, udev) is added.
    fn backend_device_added(
        &self,
        _plugin: &Plugin,
        _device: &Device,
        _progress: &Progress,
    ) -> Result<(), glib::Error> {
        Ok(())
    }

    /// Called when a backend device changes.
    fn backend_device_changed(
        &self,
        _plugin: &Plugin,
        _device: &Device,
    ) -> Result<(), glib::Error> {
        Ok(())
    }

    /// Called when a backend device is removed.
    fn backend_device_removed(
        &self,
        _plugin: &Plugin,
        _device: &Device,
    ) -> Result<(), glib::Error> {
        Ok(())
    }

    /// Called when a device is created by the engine.
    fn device_created(&self, _plugin: &Plugin, _device: &Device) -> Result<(), glib::Error> {
        Ok(())
    }

    /// Called when a device is registered by any plugin.
    fn device_registered(&self, _plugin: &Plugin, _device: &Device) {}

    /// Called when a device is added to the daemon.
    fn device_added(&self, _plugin: &Plugin, _device: &Device) {}

    /// Called during plugin finalization / cleanup.
    fn finalize(&self, _plugin: &Plugin) {}
}

// ---------------------------------------------------------------------------
// Trampoline functions -- bridge C vfunc calls to Rust PluginImpl methods
//
// These are only used through init_vfuncs<T> which is called from the
// export_plugin! macro in downstream crates, so they appear unused here.
// ---------------------------------------------------------------------------

#[allow(dead_code)]
/// Retrieves or initializes the Rust plugin implementation for a given type.
///
/// The implementation is stored in a `OnceLock` so it is created once and
/// shared for the lifetime of the plugin.
fn get_impl<T: PluginImpl>() -> &'static T {
    static IMPL: OnceLock<Box<dyn std::any::Any + Send + Sync>> = OnceLock::new();
    IMPL.get_or_init(|| Box::new(T::default()))
        .downcast_ref::<T>()
        .expect("PluginImpl type mismatch")
}

#[allow(dead_code)]
unsafe extern "C" fn constructed_trampoline<T: PluginImpl>(obj: *mut glib::gobject_ffi::GObject) {
    let plugin = from_glib_borrow::<_, Plugin>(obj as *mut ffi::FuPlugin);
    get_impl::<T>().constructed(&plugin);
}

#[allow(dead_code)]
unsafe extern "C" fn finalize_trampoline<T: PluginImpl>(obj: *mut glib::gobject_ffi::GObject) {
    let plugin = from_glib_borrow::<_, Plugin>(obj as *mut ffi::FuPlugin);
    get_impl::<T>().finalize(&plugin);
}

#[allow(dead_code)]
unsafe extern "C" fn load_trampoline<T: PluginImpl>(ctx: *mut ffi::FuContext) {
    let ctx = from_glib_borrow::<_, Context>(ctx);
    get_impl::<T>().load(&ctx);
}

#[allow(dead_code)]
unsafe extern "C" fn startup_trampoline<T: PluginImpl>(
    plugin: *mut ffi::FuPlugin,
    progress: *mut ffi::FuProgress,
    error: *mut *mut glib::ffi::GError,
) -> glib::ffi::gboolean {
    let plugin = from_glib_borrow::<_, Plugin>(plugin);
    let progress = from_glib_borrow::<_, Progress>(progress);
    match get_impl::<T>().startup(&plugin, &progress) {
        Ok(()) => glib::ffi::GTRUE,
        Err(e) => {
            if !error.is_null() {
                *error = e.into_glib_ptr();
            }
            glib::ffi::GFALSE
        }
    }
}

#[allow(dead_code)]
unsafe extern "C" fn ready_trampoline<T: PluginImpl>(
    plugin: *mut ffi::FuPlugin,
    progress: *mut ffi::FuProgress,
    error: *mut *mut glib::ffi::GError,
) -> glib::ffi::gboolean {
    let plugin = from_glib_borrow::<_, Plugin>(plugin);
    let progress = from_glib_borrow::<_, Progress>(progress);
    match get_impl::<T>().ready(&plugin, &progress) {
        Ok(()) => glib::ffi::GTRUE,
        Err(e) => {
            if !error.is_null() {
                *error = e.into_glib_ptr();
            }
            glib::ffi::GFALSE
        }
    }
}

#[allow(dead_code)]
unsafe extern "C" fn coldplug_trampoline<T: PluginImpl>(
    plugin: *mut ffi::FuPlugin,
    progress: *mut ffi::FuProgress,
    error: *mut *mut glib::ffi::GError,
) -> glib::ffi::gboolean {
    let plugin = from_glib_borrow::<_, Plugin>(plugin);
    let progress = from_glib_borrow::<_, Progress>(progress);
    match get_impl::<T>().coldplug(&plugin, &progress) {
        Ok(()) => glib::ffi::GTRUE,
        Err(e) => {
            if !error.is_null() {
                *error = e.into_glib_ptr();
            }
            glib::ffi::GFALSE
        }
    }
}

#[allow(dead_code)]
unsafe extern "C" fn backend_device_added_trampoline<T: PluginImpl>(
    plugin: *mut ffi::FuPlugin,
    device: *mut ffi::FuDevice,
    progress: *mut ffi::FuProgress,
    error: *mut *mut glib::ffi::GError,
) -> glib::ffi::gboolean {
    let plugin = from_glib_borrow::<_, Plugin>(plugin);
    let device = from_glib_borrow::<_, Device>(device);
    let progress = from_glib_borrow::<_, Progress>(progress);
    match get_impl::<T>().backend_device_added(&plugin, &device, &progress) {
        Ok(()) => glib::ffi::GTRUE,
        Err(e) => {
            if !error.is_null() {
                *error = e.into_glib_ptr();
            }
            glib::ffi::GFALSE
        }
    }
}

#[allow(dead_code)]
unsafe extern "C" fn backend_device_changed_trampoline<T: PluginImpl>(
    plugin: *mut ffi::FuPlugin,
    device: *mut ffi::FuDevice,
    error: *mut *mut glib::ffi::GError,
) -> glib::ffi::gboolean {
    let plugin = from_glib_borrow::<_, Plugin>(plugin);
    let device = from_glib_borrow::<_, Device>(device);
    match get_impl::<T>().backend_device_changed(&plugin, &device) {
        Ok(()) => glib::ffi::GTRUE,
        Err(e) => {
            if !error.is_null() {
                *error = e.into_glib_ptr();
            }
            glib::ffi::GFALSE
        }
    }
}

#[allow(dead_code)]
unsafe extern "C" fn backend_device_removed_trampoline<T: PluginImpl>(
    plugin: *mut ffi::FuPlugin,
    device: *mut ffi::FuDevice,
    error: *mut *mut glib::ffi::GError,
) -> glib::ffi::gboolean {
    let plugin = from_glib_borrow::<_, Plugin>(plugin);
    let device = from_glib_borrow::<_, Device>(device);
    match get_impl::<T>().backend_device_removed(&plugin, &device) {
        Ok(()) => glib::ffi::GTRUE,
        Err(e) => {
            if !error.is_null() {
                *error = e.into_glib_ptr();
            }
            glib::ffi::GFALSE
        }
    }
}

#[allow(dead_code)]
unsafe extern "C" fn device_created_trampoline<T: PluginImpl>(
    plugin: *mut ffi::FuPlugin,
    device: *mut ffi::FuDevice,
    error: *mut *mut glib::ffi::GError,
) -> glib::ffi::gboolean {
    let plugin = from_glib_borrow::<_, Plugin>(plugin);
    let device = from_glib_borrow::<_, Device>(device);
    match get_impl::<T>().device_created(&plugin, &device) {
        Ok(()) => glib::ffi::GTRUE,
        Err(e) => {
            if !error.is_null() {
                *error = e.into_glib_ptr();
            }
            glib::ffi::GFALSE
        }
    }
}

#[allow(dead_code)]
unsafe extern "C" fn device_registered_trampoline<T: PluginImpl>(
    plugin: *mut ffi::FuPlugin,
    device: *mut ffi::FuDevice,
) {
    let plugin = from_glib_borrow::<_, Plugin>(plugin);
    let device = from_glib_borrow::<_, Device>(device);
    get_impl::<T>().device_registered(&plugin, &device);
}

#[allow(dead_code)]
unsafe extern "C" fn device_added_trampoline<T: PluginImpl>(
    plugin: *mut ffi::FuPlugin,
    device: *mut ffi::FuDevice,
) {
    let plugin = from_glib_borrow::<_, Plugin>(plugin);
    let device = from_glib_borrow::<_, Device>(device);
    get_impl::<T>().device_added(&plugin, &device);
}

/// Populates the `FuPluginVfuncs` struct with trampoline functions that
/// dispatch to the Rust [`PluginImpl`] implementation.
///
/// # Safety
///
/// `vfuncs` must be a valid, writable pointer to an `FuPluginClass` struct.
#[allow(dead_code)]
pub unsafe fn init_vfuncs<T: PluginImpl>(vfuncs: *mut ffi::FuPluginClass) {
    (*vfuncs).constructed = Some(constructed_trampoline::<T>);
    (*vfuncs).finalize = Some(finalize_trampoline::<T>);
    (*vfuncs).load = Some(load_trampoline::<T>);
    (*vfuncs).startup = Some(startup_trampoline::<T>);
    (*vfuncs).ready = Some(ready_trampoline::<T>);
    (*vfuncs).coldplug = Some(coldplug_trampoline::<T>);
    (*vfuncs).backend_device_added = Some(backend_device_added_trampoline::<T>);
    (*vfuncs).backend_device_changed = Some(backend_device_changed_trampoline::<T>);
    (*vfuncs).backend_device_removed = Some(backend_device_removed_trampoline::<T>);
    (*vfuncs).device_created = Some(device_created_trampoline::<T>);
    (*vfuncs).device_registered = Some(device_registered_trampoline::<T>);
    (*vfuncs).device_added = Some(device_added_trampoline::<T>);
}

/// Export a Rust type as an fwupd modular plugin.
///
/// This macro generates the `fu_plugin_init_vfuncs` C symbol that the fwupd
/// daemon looks up via `g_module_symbol()` when loading a plugin `.so` file.
///
/// The type must implement [`PluginImpl`], [`Default`], [`Send`], and [`Sync`].
///
/// # Example
///
/// ```ignore
/// use fwupdplugin::prelude::*;
///
/// #[derive(Default)]
/// struct MyPlugin;
///
/// impl PluginImpl for MyPlugin {
///     fn constructed(&self, plugin: &Plugin) {
///         plugin.add_udev_subsystem("usb");
///     }
///
///     fn startup(
///         &self,
///         _plugin: &Plugin,
///         _progress: &Progress,
///     ) -> Result<(), glib::Error> {
///         Ok(())
///     }
/// }
///
/// fwupdplugin::export_plugin!(MyPlugin);
/// ```
///
/// The resulting `.so` must be named `libfu_plugin_<name>.so` and placed in
/// the fwupd plugin directory.
#[macro_export]
macro_rules! export_plugin {
    ($plugin_type:ty) => {
        /// Entry point called by fwupd when loading this modular plugin.
        ///
        /// # Safety
        ///
        /// Called by the fwupd daemon via `g_module_symbol`. The `vfuncs`
        /// pointer is guaranteed valid by the daemon.
        #[no_mangle]
        pub unsafe extern "C" fn fu_plugin_init_vfuncs(vfuncs: *mut $crate::ffi::FuPluginClass) {
            $crate::plugin::init_vfuncs::<$plugin_type>(vfuncs);
        }
    };
}
