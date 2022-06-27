# libfwupdplugin

This library is only partially API and ABI stable. Keeping unused, unsafe and
deprecated functions around forever is a maintenance burden and so symbols are
removed when branching for new minor versions.

Use `./contrib/migrate.py` to migrate up out-of-tree plugins to the new API.

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
* `fu_udev_device_ioctl_full()`: Use `fu_udev_device_ioctl()` instead -- as the latter now always specifies the timeout.
* `fu_udev_device_new_full()`: Use `fu_udev_device_new()` instead -- as the latter always specifies the context.
* `fu_usb_device_new_full()`: Use `fu_usb_device_new()` instead -- as the latter always specifies the context.
* `fu_device_new_with_context()`: Use `fu_device_new()` instead -- as the latter always specifies the context.
* `fu_plugin_has_custom_flag()`: Use `fu_plugin_has_private_flag()` instead.
* `fu_efivar_secure_boot_enabled_full()`: Use `fu_efivar_secure_boot_enabled()` instead -- as the latter always specifies the error.
* `fu_progress_add_step()`: Add a 4th parameter to the function to specify the nice name for the step, or NULL.
* `fu_backend_setup()`: Now requires a `FuProgress`, although it can be ignored.
* `fu_backend_coldplug`: Now requires a `FuProgress`, although it can be ignored.
* `FuPluginVfuncs->setup`: Now requires a `FuProgress`, although it can be ignored.
* `FuPluginVfuncs->coldplug`: Now requires a `FuProgress`, although it can be ignored.
* `fu_common_crc*`: Use `fu_crc` prefix, i.e. remove the `_common`
* `fu_common_sum*`: Use `fu_sum` prefix, i.e. remove the `_common`
* `fu_byte_array_set_size_full()`: Use `fu_byte_array_set_size` instead -- as the latter now always specifies the fill char.
* `fu_common_string*`: Use `fu_string` prefix, i.e. remove the `_common`
* `fu_common_bytes*`: Use `fu_bytes` prefix, i.e. remove the `_common`
* `fu_common_set_contents_bytes()`: Use `fu_bytes_set_contents()` instead
* `fu_common_get_contents_bytes()`: Use `fu_bytes_get_contents()` instead
* `fu_common_read*`: Use `fu_memread` prefix, i.e. replace the `_common` with `_mem`
* `fu_common_write*`: Use `fu_memwrite` prefix, i.e. replace the `_common` with `_mem`
* `fu_common_bytes_compare_raw()`: Use `fu_memcmp_safe()` instead
* `fu_common_spawn_sync()`: Use `g_spawn_sync()` instead, or ideally not at all!
* `fu_common_extract_archive()`: Use `FuArchiveFirmware()` instead.
* `fu_common_instance_id_strsafe()`: Use `fu_device_add_instance_strsafe()` instead.
* `fu_common_kernel_locked_down()`: Use `fu_kernel_locked_down` instead.
* `fu_common_check_kernel_version()`: Use `fu_kernel_check_version` instead.
* `fu_common_get_firmware_search_path()`: Use `fu_kernel_get_firmware_search_path` instead.
* `fu_common_set_firmware_search_path()`: Use `fu_kernel_set_firmware_search_path` instead.
* `fu_common_reset_firmware_search_path()`: Use `fu_kernel_reset_firmware_search_path` instead.
* `fu_common_firmware_builder()`: You should not be using this.
* `fu_common_realpath()`: You should not be using this.
* `fu_common_uri_get_scheme()`: You should not be using this.
* `fu_common_dump*`: Use `fu_dump` prefix, i.e. remove the `_common`
* `fu_common_error_array_get_best()`: You should not be using this.
* `fu_common_cpuid()`: Use `fu_cpuid` instead.
* `fu_common_get_cpu_vendor()`: Use `fu_cpu_get_vendor` instead.
* `fu_common_vercmp_full()`: Use `fu_version_compare()` instead.
* `fu_common_version_ensure_semver()`: Use `fu_version_ensure_semver()` instead.
* `fu_common_version_from_uint*()`: Use `fu_version_from_uint*()` instead.
* `fu_common_strtoull()`: Use `fu_strtoull()` instead -- as the latter always specifies the error.
* `fu_smbios_to_string()`: Use `fu_firmware_to_string()` instead -- as `FuSmbios` is a `FuFirmware` superclass.
* `fu_common_cab_build_silo()`: You should not be using this.
* `fu_i2c_device_read_full()`: Use `fu_i2c_device_read` instead.
* `fu_i2c_device_write_full()`: Use `fu_i2c_device_write` instead.
* `fu_firmware_parse_full()`: Remove the `addr_end` parameter, and ensure that `offset` is a `gsize`.
