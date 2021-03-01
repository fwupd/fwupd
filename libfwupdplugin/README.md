Planned API/ABI changes for next release
========================================

 * Remove fu_common_is_cpu_intel()
 * Remove fu_firmware_strparse_uintXX()
 * Remove fu_plugin_get_usb_context() and fu_plugin_set_usb_context()
 * Remove fu_plugin_runner_usb_device_added(), fu_plugin_runner_udev_device_added()
   and fu_plugin_runner_udev_device_changed()
 * Remove FuHidDevice->open() and FuHidDevice->close()
 * Remove FuUsbDevice->probe(), FuUsbDevice->open() and FuUsbDevice->close()
 * Remove FuUdevDevice->to_string(), FuUdevDevice->probe(), FuUdevDevice->open() and FuUdevDevice->close()
 * Remove fu_device_get_protocol() and fu_device_set_protocol()
