# libfwupdplugin

This library is only partially API and ABI stable. Keeping unused, unsafe and
deprecated functions around forever is a maintenance burden and so symbols are
removed when branching for new minor versions.

Remember: Plugins should be upstream!

## 1.5.5

* `fu_common_is_cpu_intel()`: Use `fu_common_get_cpu_vendor()` instead.
* `fu_firmware_strparse_uintXX()`: Use `fu_firmware_strparse_uintXX_safe()` instead.
* `fu_plugin_get_usb_context()`: Remove, as no longer required.
* `fu_plugin_set_usb_context()`: Remove, as no longer required.
* `fu_plugin_runner_usb_device_added()`: Use `fu_plugin_runner_backend_device_added()` instead.
* `fu_plugin_runner_udev_device_added()`: Use `fu_plugin_runner_backend_device_added()` instead.
* `fu_plugin_runner_udev_device_changed()`: Use `fu_plugin_runner_backend_device_added()` instead.
* `FuHidDevice->open()`: Use the `FuDevice` superclass instead.
* `FuHidDevice->close()`: Use the `FuDevice` superclass instead.
* `FuUsbDevice->probe()`: Use the `FuDevice` superclass instead.
* `FuUsbDevice->open()`: Use the `FuDevice` superclass instead.
* `FuUsbDevice->close()`: Use the `FuDevice` superclass instead.
* `FuUdevDevice->to_string()`: Use the `FuDevice` superclass instead.
* `FuUdevDevice->probe()`: Use the `FuDevice` superclass instead.
* `FuUdevDevice->open()`: Use the `FuDevice` superclass instead.
* `FuUdevDevice->close()`: Use the `FuDevice` superclass instead.

## 1.5.6

* `fu_device_get_protocol()`: Use `fu_device_get_protocols()` instead.
* `fu_device_set_protocol()`: Use `fu_device_add_protocol()` instead.

## 1.6.2

* `fu_device_has_custom_flag()`: Use `fu_device_has_private_flag()` instead.

## 1.6.3

* `fu_device_sleep_with_progress()`: Use `fu_progress_sleep()` instead -- but be aware the unit of time has changed from *seconds* to *milliseconds*.
* `fu_device_get_status()`: Use `fu_progress_get_status()` instead.
* `fu_device_set_status()`: Use `fu_progress_set_status()` instead.
* `fu_device_get_progress()`: Use `fu_progress_get_percentage()` instead.
* `fu_device_set_progress_full()`: Use `fu_progress_set_percentage_full()` instead.
* `fu_device_set_progress()`: Use `fu_progress_set_steps()`, `fu_progress_add_step()` and `fu_progress_done()` -- see the `FuProgress` docs for more details!

## 1.8.2

* `fu_udev_device_pread_full()`: Use `fu_udev_device_pread()` instead -- as the latter now specifies the buffer length.
* `fu_udev_device_pread_full()`: Use `fu_udev_device_pwrite()` instead -- as the latter now specifies the buffer length.
