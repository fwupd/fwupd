# libfwupd

## Planned API/ABI changes for next release

* Typedef `FwupdFeatureFlags` to `guint64` so it's the same size on all platforms

## Migration from Version 1.9.x

* Migrate from `fwupd_build_machine_id()` to `fwupd_client_get_host_machine_id()`
* Migrate from `fwupd_build_history_report_json()` to `fwupd_client_build_report_history()`
* Migrate from `fwupd_get_os_release()` to `fwupd_client_get_report_metadata()`
* Convert `fwupd_client_emulation_load()` to take a readable file rather than taking bytes
* Convert `fwupd_client_emulation_save()` to take a writable file rather than returning bytes
* Drop use of FWUPD_DEVICE_FLAG_ONLY_OFFLINE
* Drop use of `fwupd_device_get_update_message()` and `fwupd_device_get_update_image()`

## Migration from Version 1.x.x

* Add section to `fwupd_client_modify_config()`
* Migrate from `fwupd_client_install_release2()` to `fwupd_client_install_release()`
* Migrate from `fwupd_client_install_release2_async()` to `fwupd_client_install_release_async()`
* Migrate from `fwupd_client_refresh_remote2()` to `fwupd_client_refresh_remote()`
* Migrate from `fwupd_client_refresh_remote2_async()` to `fwupd_client_refresh_remote_async()`
* Migrate from `fwupd_remote_get_enabled()` to `fwupd_remote_has_flag`
* Migrate from `fwupd_remote_get_approval_required()` to `fwupd_remote_has_flag`
* Migrate from `fwupd_remote_get_automatic_reports()` to `fwupd_remote_has_flag`
* Migrate from `fwupd_remote_get_automatic_security_reports()` to `fwupd_remote_has_flag`
* Migrate from `fwupd_device_get_protocol()` to `fwupd_device_get_protocols()`
* Migrate from `fwupd_device_get_vendor_id()` to `fwupd_device_get_vendor_ids()`
* Migrate from `fwupd_remote_set_checksum()` to `fwupd_remote_set_checksum_sig()`
* Migrate from `fwupd_device_set_protocol()` to `fwupd_device_add_protocol()`
* Migrate from `fwupd_device_set_vendor_id()` to `fwupd_device_add_vendor_id()`
* Migrate from `fwupd_release_get_trust_flags()` to `fwupd_release_get_flags()`
* Migrate from `fwupd_release_get_uri()` to `fwupd_release_get_locations()`
* Migrate from `fwupd_release_set_trust_flags()` to `fwupd_release_set_flags()`
* Migrate from `fwupd_release_set_uri()` to `fwupd_release_add_location()`
* Remove use of flags like `FWUPD_DEVICE_FLAG_MD_SET_ICON`
* Remove use of flags `FWUPD_INSTALL_FLAG_IGNORE_POWER`
* Remove the `soup-session` property in `FwupdClient`.
* Rename the flag `FWUPD_CLIENT_DOWNLOAD_FLAG_ONLY_IPFS` to `_ONLY_P2P`
* Rename the SMAP attribute from `FWUPD_SECURITY_ATTR_ID_INTEL_SMAP` to `FWUPD_SECURITY_ATTR_ID_SMAP`
* Rename the CET enabled attribute from `FWUPD_SECURITY_ATTR_ID_INTEL_CET_ENABLED` to `FWUPD_SECURITY_ATTR_ID_CET_ENABLED`
* Rename the CET active attribute `FWUPD_SECURITY_ATTR_ID_INTEL_CET_ACTIVE` to `FWUPD_SECURITY_ATTR_ID_CET_ACTIVE`

## Migration from Version 0.9.x

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
* Rename fwupd_device_**provider -> fwupd_device**_plugin
* Convert hash types sa{sv} -> a{sv}
* Convert fwupd_client_get_updates() -> fwupd_client_get_upgrades()
