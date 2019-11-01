/*
 * Copyright (C) 2015-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include <gio/gio.h>

#include "fu-dfu-firmware.h"

#include "dfu-common.h"
#include "dfu-image.h"

#include "fwupd-enums.h"

#define DFU_TYPE_FIRMWARE (dfu_firmware_get_type ())
G_DECLARE_DERIVABLE_TYPE (DfuFirmware, dfu_firmware, DFU, FIRMWARE, FuDfuFirmware)

struct _DfuFirmwareClass
{
	FuDfuFirmwareClass		 parent_class;
};

/**
 * DfuFirmwareFormat:
 * @DFU_FIRMWARE_FORMAT_UNKNOWN:			Format unknown
 * @DFU_FIRMWARE_FORMAT_RAW:				Raw format
 * @DFU_FIRMWARE_FORMAT_DFU:				DFU footer
 * @DFU_FIRMWARE_FORMAT_DFUSE:				DfuSe header
 *
 * The known versions of the DFU standard in BCD format.
 **/
typedef enum {
	DFU_FIRMWARE_FORMAT_UNKNOWN,
	DFU_FIRMWARE_FORMAT_RAW,
	DFU_FIRMWARE_FORMAT_DFU,
	DFU_FIRMWARE_FORMAT_DFUSE,
	/*< private >*/
	DFU_FIRMWARE_FORMAT_LAST
} DfuFirmwareFormat;

DfuFirmware	*dfu_firmware_new		(void);

const gchar	*dfu_firmware_format_to_string	(DfuFirmwareFormat format);
DfuFirmwareFormat dfu_firmware_format_from_string(const gchar	*format);

guint16		 dfu_firmware_get_format	(DfuFirmware	*firmware);
guint32		 dfu_firmware_get_size		(DfuFirmware	*firmware);

void		 dfu_firmware_set_format	(DfuFirmware	*firmware,
						 DfuFirmwareFormat format);

gboolean	 dfu_firmware_parse_data	(DfuFirmware	*firmware,
						 GBytes		*bytes,
						 FwupdInstallFlags flags,
						 GError		**error);
gboolean	 dfu_firmware_parse_file	(DfuFirmware	*firmware,
						 GFile		*file,
						 FwupdInstallFlags flags,
						 GError		**error);

GBytes		*dfu_firmware_write_data	(DfuFirmware	*firmware,
						 GError		**error);
gboolean	 dfu_firmware_write_file	(DfuFirmware	*firmware,
						 GFile		*file,
						 GError		**error);
