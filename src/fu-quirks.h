/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Mario Limonciello <mario_limonciello@dell.com>
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_QUIRKS_H
#define __FU_QUIRKS_H

G_BEGIN_DECLS

#include <glib-object.h>
#include <gusb.h>

#define FU_TYPE_QUIRKS (fu_quirks_get_type ())
G_DECLARE_FINAL_TYPE (FuQuirks, fu_quirks, FU, QUIRKS, GObject)

FuQuirks	*fu_quirks_new				(void);
gboolean	 fu_quirks_load				(FuQuirks	*self,
							 GError		**error);
const gchar	*fu_quirks_lookup_by_id			(FuQuirks	*self,
							 const gchar	*prefix,
							 const gchar	*id);
const gchar	*fu_quirks_lookup_by_glob		(FuQuirks	*self,
							 const gchar	*prefix,
							 const gchar	*glob);
const gchar	*fu_quirks_lookup_by_usb_device		(FuQuirks	*self,
							 const gchar	*prefix,
							 GUsbDevice	*dev);

/**
 * FU_QUIRKS_DFU:
 * @key: the USB device ID, e.g. `USB\VID_0763&PID_2806`
 * @value: a string, separated using `|`, e.g. `ignore-polltimeout|no-pid-change`
 *
 * Assigns optional quirks to use for a DFU device which does not follow the
 * DFU 1.0 or 1.1 specification. The list of supported quirks is thus:
 *
 * * `none`:			No device quirks
 * * `action-required`:		User has to do something manually, e.g. press a button
 * * `attach-extra-reset`:	Device needs resetting twice for attach
 * * `attach-upload-download`:	An upload or download is required for attach
 * * `force-dfu-mode`:		Force DFU mode
 * * `ignore-polltimeout`:	Ignore the device download timeout
 * * `ignore-runtime`:		Device has broken DFU runtime support
 * * `ignore-upload`:		Uploading from the device is broken
 * * `no-dfu-runtime`:		No DFU runtime interface is provided
 * * `no-get-status-upload`:	Do not do GetStatus when uploading
 * * `no-pid-change`:		Accept the same VID:PID when changing modes
 * * `use-any-interface`:	Use any interface for DFU
 * * `use-atmel-avr`:		Device uses the ATMEL bootloader
 * * `use-protocol-zero`:	Fix up the protocol number
 * * `legacy-protocol`:		Use a legacy protocol version
 *
 * Default value: `none`
 *
 * Since: 1.0.1
 */
#define	FU_QUIRKS_DFU				"fwupd-dfu"

/**
 * FU_QUIRKS_UEFI_VERSION_FORMAT:
 * @key: a %FU_HWIDS_KEY_MANUFACTURER, e.g. `Alienware`
 * @value: the version format, e.g. `none`
 *
 * Assigns the version format to use for a specific manufacturer.
 * A specific version format is sometimes chosen to match the appearance of
 * other systems or specifications.
 *
 * Default value: `use-triplet`
 *
 * Since: 1.0.1
 */
#define	FU_QUIRKS_UEFI_VERSION_FORMAT		"fwupd-uefi-version-format"

/**
 * FU_QUIRKS_DAEMON_VERSION_FORMAT:
 * @key: the optionally wildcarded AppStream ID e.g. `com.dell.uefi*.firmware`
 * @value: the version format, e.g. `none`
 *
 * Assigns the version format to use for a specific AppStream component.
 * A specific version format is sometimes chosen to match the appearance of
 * other systems or specifications.
 *
 * Default value: `use-triplet`
 *
 * Since: 1.0.1
 */
#define	FU_QUIRKS_DAEMON_VERSION_FORMAT		"fwupd-daemon-version-format"

/**
 * FU_QUIRKS_DFU_JABRA_DETACH:
 * @key: the USB device ID, e.g. `USB\VID_0763&PID_2806`
 * @value: the two uint8_t unlock values, encoded in base 16, e.g. `0201`
 *
 * Assigns the two magic bytes sent to the Jabra hardware when the device is
 * in runtime mode to make it switch into DFU mode.
 *
 * Since: 1.0.1
 */
#define	FU_QUIRKS_DFU_JABRA_DETACH		"fwupd-dfu-jabra-detach"

/**
 * FU_QUIRKS_DFU_AVR_CHIP_ID:
 * @key: the AVR chip ID, e.g. `0x58200204`
 * @value: the UM0424 sector description, e.g. `@Flash/0x2000/1*248Kg`
 *
 * Assigns a sector description for the chip ID. This is required so fwupd can
 * program the user firmware avoiding the bootloader and for checking the total
 * element size.
 *
 * The chip ID can be found from a datasheet or using `dfu-tool list` when the
 * hardware is connected and in bootloader mode.
 *
 * Since: 1.0.1
 */
#define	FU_QUIRKS_DFU_AVR_CHIP_ID		"fwupd-dfu-avr-chip-id"

/**
 * FU_QUIRKS_DFU_FORCE_VERSION:
 * @key: the USB device ID, e.g. `USB\VID_0763&PID_2806`
 * @value: the uint16_t DFU version, encoded in base 16, e.g. `0110`
 *
 * Forces a specific DFU version for the hardware device. This is required
 * if the device does not set, or sets incorrectly, items in the DFU functional
 * descriptor.
 *
 * Since: 1.0.1
 */
#define	FU_QUIRKS_DFU_FORCE_VERSION		"fwupd-dfu-force-version"

/**
 * FU_QUIRKS_USB_SUMMARY:
 * @key: the USB device ID, e.g. `USB\VID_0763&PID_2806`
 * @value: the USB device summary, e.g. `An open source display colorimeter`
 *
 * Sets a name for a specific hardware device.
 *
 * Since: 1.0.2
 */
#define	FU_QUIRKS_USB_SUMMARY			"FuUsbDevice:summary"

/**
 * FU_QUIRKS_USB_ICON:
 * @key: the USB device ID, e.g. `USB\VID_0763&PID_2806`
 * @value: the USB device icon name, e.g. `media-removable`
 *
 * Adds an icon name for a specific hardware device.
 *
 * Since: 1.0.2
 */
#define	FU_QUIRKS_USB_ICON			"FuUsbDevice:icon"

/**
 * FU_QUIRKS_USB_NAME:
 * @key: the USB device ID, e.g. `USB\VID_0763&PID_2806`
 * @value: the USB device name, e.g. `ColorHug`
 *
 * Sets a name for a specific hardware device.
 *
 * Since: 1.0.2
 */
#define	FU_QUIRKS_USB_NAME			"FuUsbDevice:name"

/**
 * FU_QUIRKS_USB_GUID:
 * @key: the USB device ID, e.g. `USB\VID_0763&PID_2806`
 * @value: the GUID, e.g. `537f7800-8529-5656-b2fa-b0901fe91696`
 *
 * Adds an extra GUID for a specific hardware device. If the value provided is
 * not already a suitable GUID, it will be converted to one.
 *
 * Since: 1.0.3
 */
#define	FU_QUIRKS_USB_GUID			"FuUsbDevice:guid"

/**
 * FU_QUIRKS_USB_VERSION:
 * @key: the USB device ID, e.g. `USB\VID_0763&PID_2806&REV_0001`
 * @value: the version number, e.g. `1.2`
 *
 * Sets a version for a specific hardware device.
 *
 * Since: 1.0.3
 */
#define	FU_QUIRKS_USB_VERSION			"FuUsbDevice:version"

/**
 * FU_QUIRKS_USB_VENDOR:
 * @key: the USB device ID, e.g. `USB\VID_0763&PID_2806`
 * @value: the vendor, e.g. `Hughski Limited`
 *
 * Sets a vendor name for a specific hardware device.
 *
 * Since: 1.0.3
 */
#define	FU_QUIRKS_USB_VENDOR			"FuUsbDevice:vendor"

/**
 * FU_QUIRKS_CSR:
 * @key: the USB device ID, e.g. `USB\VID_0763&PID_2806`
 * @value: the quirk, e.g. `require-delay`
 *
 * Assigns optional quirks to use for a CSR device which does not follow the
 * CSR specification. The list of supported quirks is thus:
 *
 * * `none`:			No device quirks
 * * `require-delay`:		Respect the write timeout value
 *
 * Since: 1.0.3
 */
#define	FU_QUIRKS_CSR_DEVICE			"FuCsrDevice"

/**
 * FU_QUIRKS_EBITDO:
 * @key: the USB device ID, e.g. `USB\VID_0763&PID_2806`
 * @value: the quirk, e.g. `bootloader`
 *
 * Assigns optional quirks to use for a 8Bitdo device. The list of supported
 * quirks is thus:
 *
 * * `none`:			No device quirks
 * * `bootloader`:		Device is in bootloader mode
 *
 * Since: 1.0.3
 */
#define	FU_QUIRKS_EBITDO_DEVICE			"FuEditdoDevice"

G_END_DECLS

#endif /* __FU_QUIRKS_H */
