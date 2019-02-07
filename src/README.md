Quirk use
---------
Quirks are defined by creating an INI style file in the compiled in quirk location (typically `/usr/share/fwupd/quirks.d`).

The quirk is declared by creating a group based upon the `DeviceInstanceId` or `GUID`
and then mapping out values to keys.

## All plugins
All fwupd devices support the following quirks:

### Plugin
Sets the plugin to use for a specific hardware device.
* Key: the device ID, e.g. `DeviceInstanceId=USB\VID_0763&PID_2806`
* Value: the plugin name, e.g. `csr`
* Minimum fwupd version: **1.1.0**
### UefiVersionFormat
Assigns the version format to use for a specific manufacturer. A specific version
format is sometimes chosen to match the appearance of other systems or
specifications.
* Key: a %FU_HWIDS_KEY_MANUFACTURER, e.g. `Alienware`
* Value: the version format, e.g. `none`
* Supported values: `none`, `use-triplet`
* Minimum fwupd version: **1.0.1**
### ComponentIDs
Assigns the version format to use for a specific AppStream component. A specific
version format is sometimes chosen to match the appearance of other systems or
specifications.
* Key: the optionally wildcarded AppStream ID e.g. `com.dell.uefi*.firmware`
* Value: the version format, e.g. `none`
* Minimum fwupd version: **1.0.1**
### Flags
Assigns optional quirks to use for a 8bitdo device
* Key: the device ID, e.g. `DeviceInstanceId=USB\VID_0763&PID_2806`
* Value: the quirk, e.g. `is-bootloader`
* Supported values:
  * `none`: no device quirks
  * `is-bootloader`: device is in bootloader mode
* Minimum fwupd version: **1.0.3**
### Summary
Sets a summary for a specific hardware device.
* Key: the device ID, e.g. `DeviceInstanceId=USB\VID_0763&PID_2806`
* the device summary, e.g. `An open source display colorimeter`
* Minimum fwupd version: **1.0.2**
### Icon
Adds an icon name for a specific hardware device.
* Key: the device ID, e.g. `DeviceInstanceId=USB\VID_0763&PID_2806`
* Value: the device icon name, e.g. `media-removable`
* Minimum fwupd version: **1.0.2**
### Name
Sets a name for a specific hardware device.
* Key: the device ID, e.g. `DeviceInstanceId=USB\VID_0763&PID_2806`
* Value: the device name, e.g. `ColorHug`
* Minimum fwupd version: **1.0.2**
### Guid
Adds an extra GUID for a specific hardware device. If the value provided is not
already a suitable GUID, it will be converted to one.
* Key: the device ID, e.g. `DeviceInstanceId=USB\VID_0763&PID_2806`
* Value: the GUID, e.g. `537f7800-8529-5656-b2fa-b0901fe91696`
* Minimum fwupd version: **1.0.3**
### CounterpartGuid
Adds an counterpart GUID for a specific hardware device. If the value provided
is not already a suitable GUID, it will be converted to one.   A counterpart
GUID is typically the GUID of the same device in bootloader or runtime mode,
if they have a different device PCI or USB ID. Adding this type of GUID does
not cause a "cascade" by matching using the quirk database.
* Key: the device ID, e.g. `DeviceInstanceId=USB\VID_0763&PID_2806`
* Value: the GUID, e.g. `537f7800-8529-5656-b2fa-b0901fe91696`
* Minimum fwupd version: **1.1.2**
### ParentGuid
Adds an extra GUID to mark as the parent device. If the value provided is not
already a suitable GUID, it will be converted to one.
* Key: the device ID, e.g. `DeviceInstanceId=USB\VID_0763&PID_2806`
* Value: the GUID, e.g. `537f7800-8529-5656-b2fa-b0901fe91696`
* Minimum fwupd version: **1.1.2**
### Children
Adds one or more virtual devices to a physical device. To set the object type
of the child device use a pipe before the object type, for instance:
`FuRts54xxDeviceUSB\VID_0763&PID_2806&I2C_01`  If the type of device is not
specified the parent device type is used.  If the values provided are not
already suitable GUIDs, they will be converted.
* Key: the device ID, e.g. `DeviceInstanceId=USB\VID_0763&PID_2806`
* Value: The virtual device, delimited by a comma
* Minimum fwupd version: **1.1.2**
### Vendor
Sets a vendor name for a specific hardware device.
* Key: the device ID, e.g. `DeviceInstanceId=USB\VID_0763&PID_2806`
* Value: the vendor, e.g. `Hughski Limited`
* Minimum fwupd version: **1.0.3**
### VendorId
Sets a vendor ID for a specific hardware device.
* Key: the device ID, e.g. `DeviceInstanceId=USB\VID_0763&PID_2806`
* Value: the vendor, e.g. `USB:0x123A`
* Minimum fwupd version: **1.1.2**
### Version
Sets a version for a specific hardware device.
* Key: the device ID, e.g. `DeviceInstanceId=USB\VID_0763&PID_2806`
* Value: Version number, e.g. `1.2`
* Minimum fwupd version: **1.0.3**
### FirmwareSizeMin
Sets the minimum allowed firmware size.
* Key: the device ID, e.g. `DeviceInstanceId=USB\VID_0763&PID_2806`
* Value: A number in bytes, e.g. `512`
* Minimum fwupd version: **1.1.2**
### FirmwareSizeMax
Sets the maximum allowed firmware size.
* Key: the device ID, e.g. `DeviceInstanceId=USB\VID_0763&PID_2806`
* Value: A number in bytes, e.g. `1024`
* Minimum fwupd version: **1.1.2**
### InstallDuration
Sets the estimated time to flash the device
* Key: the device ID, e.g. `DeviceInstanceId=USB\VID_0763&PID_2806`
* Value: A number in seconds, e.g. `60`
* Minimum fwupd version: **1.1.3**
### VersionFormat
Sets the version format the device should use for conversion.
* Key: the device ID, e.g. `DeviceInstanceId=USB\VID_0763&PID_2806`
* Value: The quirk format, e.g. `quad`
* Minimum fwupd version: **1.2.0**

## Plugin specific
Plugins may add support for additional quirks that are relevant only for
those plugins.  View them by looking at the `README.md` in plugin directories.

## Example
Here is an example as seen in the CSR plugin.

```
[DeviceInstanceId=USB\VID_0A12&PID_1337]
Plugin = csr
Name = H05
Summary = Bluetooth Headphones
Icon = audio-headphones
Vendor = AIAIAI
[DeviceInstanceId=USB\VID_0A12&PID_1337&REV_2520]
Version = 1.2
```
Additional samples can be found in other plugins.
