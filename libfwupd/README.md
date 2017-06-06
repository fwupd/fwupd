Planned API Changes
===================

When we next bump soname the following changes are planned:

 * {sa{sv}} -> {a{sv}} -- we don't always want to send the device ID
 * Rename FU_DEVICE_FLAG -> FWUPD_DEVICE_FLAG
 * Remove all deprecated API
 * Rename GetDetailsLocal() -> GetDetails()
 * Rename UpdateMetadataWithId() -> UpdateMetadata()
 * Make DeviceAdded emit a FwupdDevice, not a FwupdResult
 * Unexport fwupd_result_to_data() and fwupd_result_new_from_data()
