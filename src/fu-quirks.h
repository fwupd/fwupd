/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Mario Limonciello <mario_limonciello@dell.com>
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
 * FU_QUIRKS_DFU_ALTERNATE_VIDPID:
 * @key: the USB device ID, e.g. `USB\VID_0763&PID_2806`
 * @value: the uint16_t VID and uint16_t PID values, encoded in base 16, e.g. `0B0E0411`
 *
 * Assigns the USB vendor ID and product ID when the device is in the alternate
 * mode. For instance this can be used to set the application VID/PID when in DFU
 * mode or the bootloader VID/PID when in application mode.
 *
 * Since: 1.0.1
 */
#define	FU_QUIRKS_DFU_ALTERNATE_VIDPID		"fwupd-dfu-alternate-vidpid"

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

G_END_DECLS

#endif /* __FU_QUIRKS_H */
