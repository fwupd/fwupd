Planned API Changes
===================

When we next bump soname the following changes are planned:

 * {sa{sv}} -> {a{sv}} -- we don't always want to send the device ID
 * Remove all deprecated API
 * Remove FWUPD_RESULT_KEY_DEVICE_CHECKSUM_KIND
 * Rename GetDetailsLocal() -> GetDetails()
 * Rename UpdateMetadataWithId() -> UpdateMetadata()
 * Make DeviceAdded emit a FwupdDevice, not a FwupdResult
 * Unexport fwupd_result_to_data() and fwupd_result_new_from_data()

Migration from Version 0.9.x
============================

 * Rename FU_DEVICE_FLAG -> FWUPD_DEVICE_FLAG
 * Rename FWUPD_DEVICE_FLAG_ALLOW_ONLINE -> FWUPD_DEVICE_FLAG_UPDATABLE
 * Rename FWUPD_DEVICE_FLAG_ALLOW_OFFLINE -> FWUPD_DEVICE_FLAG_ONLY_OFFLINE
