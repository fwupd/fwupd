libfwupdplugin
==============

This library is only partially API and ABI stable. Keeping unused, unsafe and
deprecated functions around forever is a maintainance burden and so symbols are
removed when branching for new minor versions.

Remember: Plugins should be upstream!

Migrating from older API
========================

 * Migrate from fu_common_is_cpu_intel() to fu_common_get_cpu_vendor()
 * Migrate from fu_firmware_strparse_uintXX() to fu_firmware_strparse_uintXX_safe()
 * Remove calls to fu_plugin_get_usb_context() and fu_plugin_set_usb_context()
 * Migrate from fu_plugin_runner_usb_device_added(), fu_plugin_runner_udev_device_added()
   and fu_plugin_runner_udev_device_changed() to fu_plugin_runner_backend_device_added()
 * Migrate from FuHidDevice->open() and FuHidDevice->close() to using the superclass helpers
 * Migrate from FuUsbDevice->probe(), FuUsbDevice->open() and FuUsbDevice->close()
   to using the superclass helpers
 * Migrate from FuUdevDevice->to_string(), FuUdevDevice->probe(), FuUdevDevice->open()
   and FuUdevDevice->close() to using the superclass helpers
 * Migrate from fu_device_get_protocol() to fu_device_get_protocols() and
   fu_device_set_protocol() to fu_device_add_protocol()

Planned API/ABI changes for next release
========================================

 * Nothing yet.
