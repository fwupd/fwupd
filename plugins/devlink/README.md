---
title: Plugin: Devlink
---

## Introduction

This plugin provides firmware update support for network interface cards that support the Linux devlink interface.
This is a generic plugin that can work with any device that implements devlink functionality.

## Supported Devices

The plugin supports any device that implements the devlink interface, regardless the bus it resides on.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

This plugin supports the following protocol ID:

* `org.kernel.devlink`

## GUID Generation

These devices use custom instance IDs consisting of the component name.

* `PCI\VEN_15B3&DEV_1021&COMPONENT_fw`

Optionally, additional GUID might get generated as specified in the squirk file, see below.

### Device Identification

Devices are identified using their in the format:

```text
BUS_NAME/DEV_NAME
```

For PCI, this is for example:

```text
pci/0000:01:00.0
```

## Requirements

* Linux kernel with devlink support
* Device driver with devlink implementation
* Root privileges (required for devlink write commands)

## Update Behavior

The plugin uses the Linux devlink netlink interface to communicate with the kernel and perform firmware updates.
The process involves:

1. **Netlink Communication**: Opens Netlink socket to communicate with the devlink subsystem
2. **Device Detection**:  Gets all existing devlink devices
3. **Firmware Upload**: Writes the firmware file to `CACHE_DIRECTORY` and instructs devlink to flash it
4. **Progress Monitoring**: Monitors devlink status messages to provide real-time progress updates
5. **Firmware Activation**: Activates firmware using devlink reload activate action

### Devlink Protocol

The plugin implements the devlink generic netlink protocol:

1. **Device Enumeration**: Sends `DEVLINK_CMD_GET` with dump flag to discover all devlink devices
2. **Device Monitoring**: Receives `DEVLINK_CMD_NEW` notifications when devices are added
3. **Device Removal**: Receives `DEVLINK_CMD_DEL` notifications when devices are removed
4. **Device Information**: Sends `DEVLINK_CMD_INFO_GET` to retrieve device details and versions
5. **Flash Command**: Sends `DEVLINK_CMD_FLASH_UPDATE` with device and file information
6. **Status Monitoring**: Receives `DEVLINK_CMD_FLASH_UPDATE_STATUS` messages for progress
7. **Completion**: Waits for `DEVLINK_CMD_FLASH_UPDATE_END` to confirm completion
8. **Firmware Activation**: Sends `DEVLINK_CMD_RELOAD` with fw_activate action to activate updated firmware

## Quirk Use

This plugin uses the following plugin-specific quirks:

### DevlinkFixedVersions

Specifies a comma-separated list of "fixed version" names that should be used
to generate additional GUID instance IDs for component matching. This is useful to
target specific device according to device IDs, like ASIC ID, Board ID, etc.

**Example usage in quirk file:**

```ini
[PCI\VEN_15B3&DEV_1021]
DevlinkFixedVersions = fw.psid
```

Since: 2.0.15

## Private Flags

The plugin supports the following private flags:

### `Flags=omit-component-name`

When this flag is set, the plugin will not include the `DEVLINK_ATTR_FLASH_UPDATE_COMPONENT`
attribute in the flash command.
This is useful for devices that don't support component-specific updates. For such,
passing `DEVLINK_ATTR_FLASH_UPDATE_COMPONENT` in flash netlink message
would cause an error.

**Usage in metainfo XML:**

```xml
<custom>
  <value key="LVFS::DeviceFlags">omit-component-name</value>
</custom>
```

Since: 2.0.15

### Error Handling

The plugin handles various error conditions:

* Kernel without devlink support
* Device without devlink implementation
* Device without devlink flash implementation
* Firmware file access issues
* Flash operation failures

## Security Considerations

* Firmware files are temporarily stored in `/lib/firmware/`
* Root privileges are required for devlink write commands
* Temporary files are cleaned up after the operation

## Vendor ID Security

The vendor ID is set from the PCI vendor.

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `2.0.15`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Jiří Pírko: @jpirko

## References

* [Linux Devlink Documentation](https://www.kernel.org/doc/html/latest/networking/devlink/)
