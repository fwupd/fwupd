---
title: Plugin: Genesys Logic
---

## Introduction

This plugin allows updating the Genesys Logic USB Hub devices.

* GL3521
* GL3523
* GL3525
* GL3590

The Lenovo P34WD-40 Hubs are also supported. These Hubs are updated through a vendor HID
that hangs below each Hub, see [HID Proxy](#hid-proxy) below.

* Lenovo P34WD-40: `USB\VID_17EF&PID_1121`, `USB\VID_17EF&PID_1122` and `USB\VID_17EF&PID_1123`

Additionally, this plugin allows updating the MStar Semiconductor Scaler connected via an I²C bus.

* TSUM G

## Firmware Format

The daemon will decompress the cabinet archives and extract the firmware blob in an unspecified binary file format.

This plugin supports the following protocol IDs:

* `com.genesys.usbhub`
* `com.mstarsemi.scaler`

## GUID Generation

These devices use the standard USB DeviceInstanceId values for the USB Hub, e.g.

* `USB\VID_05E3&PID_0610` (quirk-only)

These devices use the standard USB DeviceInstanceId values for the HID under USB hub, e.g.

* `USB\VID_05E3&PID_0102` (quirk-only)

Additionally, some customized instance IDs are added. e.g.

* `USB\VID_03F0&PID_0610&IC_352330&BONDING_0F`
* `USB\VID_03F0&PID_0610&IC_352330&BONDING_0F&RUNMODE_M`
* `USB\VID_03F0&PID_0610&VENDOR_GENESYSLOGIC&IC_352330&BONDING_0F&PORTNUM_23&VENDORSUP_C09B5DD3-1A23-51D2-995A-F7366AAB3CA4`
* `USB\VID_05E3&PID_0630&PROJECT_1885D34D-0418-5EF8-8E69-4CEF77B6B6E8`
* `USB\VID_03F0&PID_0610&PUBKEY_AB859399-95B8-5817-B521-9AD8CC7F5BD6`

These devices also use custom GUID values for the Scaler, e.g.

* `GENESYS_SCALER\MSTAR_TSUM_G&PUBKEY_B335BDCE-7073-5D0E-9BD3-9B69C1A6899F&PANELREV_RIM101`

The Public Key is product-specific and is required to identify the product.

## Quirk Use

This plugin uses the following plugin-specific quirks:

### `Flags=has-mstar-scaler`

USB Hub has a MStar Semiconductor Scaler attached via I²C.

Since 1.7.6.

### `Flags=has-public-key`

Device has a public-key appended to firmware.

Since 1.8.0

### `Flags=pause-r2-cpu`

Pause R2 CPU.

Since 1.7.6

### `Flags=use-i2c-ch0`

Use I2C ch0.

Since 1.7.6

### `Flags=hid-transport`

The Hub only answers Genesys vendor commands through its HID, so the flash part of the setup
(chip detection and bank versions) is finished once the HID proxy is bound.
Combining this flag with a codesign firmware is not supported.

### `Flags=require-hid-proxy`

The Hub is only updatable when a HID of the model given by `GenesysHidProxyPid` hangs directly below it.
Without such a verified HID the Hub is inhibited with `hid-proxy` and no entry that changes the device
state (detach, prepare, write or dump) falls back to USB control transfers.

### `Flags=single-hid-proxy`

A second live HID of the proxy model below the same Hub makes the proxy ambiguous: the proxy is
unbound and the Hub is inhibited with `hid-proxy-ambiguous` until it is replugged.
A HID that was removed is replaced normally.

### `Flags=strict-project-check`

The `PROJECT` and IC type of the firmware must match the device exactly.

### GenesysHidProxyPid

USB product ID of the HID that must hang directly below the Hub to act as its proxy,
for example `0xF00F`, `0x4C41` or `0x4C42` for the Lenovo P34WD-40 dock.
A wrong value only keeps the Hub inhibited.

### GenesysParentHubPid

USB product ID of the Hub that must be the direct upstream parent of this Hub, for example `0x1121`
for the Lenovo P34WD-40 `1122` Hub. The check is done before the first read in setup and before every
entry that changes the device state. A Hub without this quirk has no fixed upstream parent.

### GenesysUsbhubSwitchRequest

USB Hub Switch Request value.

* HP Mxfd FHD Monitors: `0xA1`

Since 1.7.6.

### GenesysUsbhubReadRequest

USB Hub Read Request value.

* HP Mxfd FHD Monitors: `0xA2`

Since 1.7.6.

### GenesysUsbhubWriteRequest

USB Hub Write Request value.

* HP Mxfd FHD Monitors: `0xA3`

Since 1.7.6.

### GenesysScalerDeviceTransferSize

Scaler Block size to use for transfers.

* MStar Semiconductor TSUM G: `0x40`

Since 1.7.6.

### GenesysScalerGpioOutputRegister

Scaler GPIO Output Register value.

* MStar Semiconductor TSUM G: `0x0426`

Since 1.7.6.

### GenesysScalerGpioEnableRegister

Scaler GPIO Enable Register value.

* MStar Semiconductor TSUM G: `0x0428`

Since 1.7.6.

### GenesysScalerGpioValue

Scaler GPIO value.

* MStar Semiconductor TSUM G: `0x01`

Since 1.7.6.

### GenesysScalerCfiFlashId

CFI Flash Id.

* HP M24fd USB-C Monitor: `0xC22016`
* HP M27fd USB-C Monitor: `0xC84016`

Since 1.8.2.

## Firmware Bank Quirk Use

These parameters are used to configure and manage Genesys firmware banks.

This plugin uses the following plugin-specific quirks to indicate firmware bank detail:

### GenesysSupportDualBank

Indicates if dual-bank support is enabled.

Since 2.0.17

### GenesysSupportCodeSize

Indicatess if code size support is enabled.

Since 2.0.17

### GenesysHubBank1Address / GenesysHubBank2Address

Memory addresses for Hub bank 1 and 2.

Since 2.0.17

### GenesysHubBankCapacity

Capacity of the Hub bank.

Since 2.0.17

### GenesysDevBank1Address / GenesysDevBank2Address

Memory addresses for Device bank 1 and 2.

Since 2.0.17

### GenesysDevBankCapacity

Capacity of the Device bank.

Since 2.0.17

### GenesysPdBank1Address / GenesysPdBank2Address

Memory addresses for PD bank 1 and 2.

Since 2.0.17

### GenesysPdBankCapacity

Capacity of the PD bank.

Since 2.0.17

### GenesysCodesignBank1Address / GenesysCodesignBank2Address

Memory addresses for Codesign bank 1 and 2.

Since 2.0.17

### GenesysCodesignBankCapacity

Capacity of the Codesign bank.

Since 2.0.17

### GenesysFwDataMaxCount

Maximum count of firmware data.

Since 2.0.17

## HID Proxy

The Lenovo P34WD-40 Hubs are not written with USB control transfers. Each Hub is written
through the vendor HID that hangs below it (`FuGenesysHubhidDevice`, matched by the `GenesysHidProxyPid`
quirk) and the two devices are bound as proxy and Hub in the plugin.

The binding is checked, not assumed:

* the HID must be a direct child of the Hub, not of a neighbouring Hub or a foreign model;
* with `single-hid-proxy` a second HID below the same Hub blocks the Hub instead of guessing;
* with `GenesysParentHubPid` the Hub must hang directly below the expected Genesys Hub, which keeps the
  three cascaded Hubs (`1121` > `1122` > `1123`) from being mixed up;
* a Hub that does not pass these checks is inhibited and its detach, prepare, write and dump entries
  refuse before any command is sent.

## Update Behavior

The devices are independently updated at runtime using USB control transfers.

The Lenovo P34WD-40 Hubs are updated through their HID proxy instead, see [HID Proxy](#hid-proxy).
The quirk file sets a `RemoveDelay` of 40 seconds for these Hubs, because the default of 5 seconds is too
short for the Hub to reappear.

With `dual-image` the bank to write is chosen from the bank versions that are read before the update:
bank 1 is written unless it is newer than bank 2. On a Hub that runs its newest bank (the GL3525 `1121`),
an image that is older than the other bank is refused before any write, because it would not become the
running version, and the write is also refused when the bank versions could not be read.
Every transfer to the flash is cut at the 64 KB window of the flash page.

The firmware is deployed when the device is in normal runtime mode, and the device will reset when the new firmware has been written.

The HP Mxfd FHD Monitors must be connected to host via the USB-C cable to apply an update. The devices remain functional during the update; the Scaler update is ~10 minute long.

## Vendor ID Security

The vendor ID is set from the USB vendor, for example set to `USB:0x03F0` for HP.

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `1.7.6`.
