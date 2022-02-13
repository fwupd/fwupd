# GPIO

## Introduction

This plugin sets GPIO outputs either high or low before and/or after an
update has been deployed.

## GUID Generation

These device use GPIO `gpiochip_info.label` values, e.g.

* `GPIO\ID_INT3450:00`

## Quirk Use

This plugin uses the following plugin-specific quirks:

### GpioForUpdate

The GPIO bit to set before the update is deployed e.g. `INT3450:00,SPI_MUX,high`.
After the update has finished, the bits are returned to the default state.

For example, to set GPIO pin 2 low for the duration of the ColorHug device update
this could be added to the quirk file:

    [USB\VID_273F&PID_1001]
    GpioForUpdate=fake-gpio-chip,2,low

Since: 1.7.6

## External Interface Access

This plugin requires ioctl `GPIO_GET_CHIPINFO_IOCTL` and `GPIO_V2_GET_LINE_IOCTL`
access on `/dev/gpiochip*` devices.
