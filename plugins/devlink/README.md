# Devlink Plugin

This plugin provides firmware update support for network interface cards that support the Linux devlink interface. This is a generic plugin that can work with any device that implements devlink functionality.

## Supported Devices

The plugin supports any device that implements the devlink interface, regardless the bus it resides on.

## How It Works

The plugin uses the Linux devlink netlink interface to communicate with the kernel and perform firmware updates. The process involves:

1. **Netlink Communication**: Opens Netlink socket to communicate with the devlink subsystem
2. **Device Detection**:  Gets all existing devlink devices
3. **Firmware Upload**: Writes the firmware file to `/lib/firmware/` and instructs devlink to flash it
4. **Progress Monitoring**: Monitors devlink status messages to provide real-time progress updates
5. **Firmware Activation**: Activates firmware using devlink reload activate action

## Requirements

- Linux kernel with devlink support
- Device driver with devlink implementation
- Root privileges (required for devlink write commands)

## Usage

The plugin integrates seamlessly with fwupd. Once installed, supported devices will appear in the device list:

```bash
# List devices
fwupdmgr get-devices

# Update firmware
fwupdmgr update
```

## Technical Details

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

### Private Flags

The plugin supports the following private flags:

#### `omit-component-name`

When this flag is set, the plugin will not include the `DEVLINK_ATTR_FLASH_UPDATE_COMPONENT` attribute in the flash command. This is useful for devices that don't require or support component-specific updates.

**Usage in metainfo XML:**

```xml
<custom>
  <value key="LVFS::DeviceFlags">omit-component-name</value>
</custom>
```

### Device Identification

Devices are identified using their in the format:

```text
BUS_NAME/DEV_NAME
```

For PCI, this is for example:

```text
pci/0000:01:00.0
```

### Error Handling

The plugin handles various error conditions:

- Kernel without devlink support
- Device without devlink implementation
- Device without devlink flash implementation
- Firmware file access issues
- Flash operation failures

## Debugging

Enable debug logging to troubleshoot issues:

```bash
fwupdmgr --verbose update
```

This will show detailed netlink communication and device detection information.

## Security Considerations

- Firmware files are temporarily stored in `/lib/firmware/`
- Root privileges are required for devlink write commands
- Temporary files are cleaned up after the operation

## References

- [Linux Devlink Documentation](https://www.kernel.org/doc/html/latest/networking/devlink/)
