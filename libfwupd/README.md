Planned API/ABI changes for next release
========================================

 * Typedef `FwupdFeatureFlags` to `guint64` so it's the same size on all platforms
 * Remove the `soup-session` fallback property in `FwupdClient`.
 * Remove fwupd_device_set_vendor_id() and fwupd_device_get_vendor_id()
 * Remove the deprecated flags like `FWUPD_DEVICE_FLAG_MD_SET_ICON`
 * Remove `fwupd_release_get_uri()` and `fwupd_release_set_uri()`
 * Rename `fwupd_client_install_release2_async()` to `fwupd_client_install_release_async()`

Migration from Version 0.9.x
============================

 * Rename FU_DEVICE_FLAG -> FWUPD_DEVICE_FLAG
 * Rename FWUPD_DEVICE_FLAG_ALLOW_ONLINE -> FWUPD_DEVICE_FLAG_UPDATABLE
 * Rename FWUPD_DEVICE_FLAG_ALLOW_OFFLINE -> FWUPD_DEVICE_FLAG_ONLY_OFFLINE
 * Rename fwupd_client_get_devices_simple -> fwupd_client_get_devices
 * Rename fwupd_client_get_details_local -> fwupd_client_get_details
 * Rename fwupd_client_update_metadata_with_id -> fwupd_client_update_metadata
 * Rename fwupd_remote_get_uri -> fwupd_remote_get_metadata_uri
 * Rename fwupd_remote_get_uri_asc -> fwupd_remote_get_metadata_uri_sig
 * Rename fwupd_remote_build_uri -> fwupd_remote_build_firmware_uri
 * Switch FWUPD_RESULT_KEY_DEVICE_CHECKSUM_KIND to fwupd_checksum_guess_kind()
 * Rename fwupd_result_update_*() to fwupd_release_*()
 * Rename fwupd_result_*() to fwupd_device_*()
 * Convert FwupdResult to FwupdDevice in all callbacks
 * Rename fwupd_device_*_provider -> fwupd_device_*_plugin
 * Convert hash types sa{sv} -> a{sv}
 * Convert fwupd_client_get_updates() -> fwupd_client_get_upgrades()
