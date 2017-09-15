Planned API Changes
===================

When we next bump soname the following changes are planned:

 * {sa{sv}} -> {a{sv}} -- we don't always want to send the device ID
 * Unexport fwupd_result_to_data() and fwupd_result_new_from_data()

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
