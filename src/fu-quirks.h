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
 * * `ignore-invalid-version`:	Ignore invalid version numbers
 * * `ignore-polltimeout`:	Ignore the device download timeout
 * * `ignore-runtime`:		Device has broken DFU runtime support
 * * `ignore-upload`:		Uploading from the device is broken
 * * `no-dfu-runtime`:		No DFU runtime interface is provided
 * * `no-get-status-upload`:	Do not do GetStatus when uploading
 * * `no-pid-change`:		Accept the same VID:PID when changing modes
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

G_END_DECLS

#endif /* __FU_QUIRKS_H */
