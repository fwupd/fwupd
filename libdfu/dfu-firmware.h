/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef __DFU_FIRMWARE_H
#define __DFU_FIRMWARE_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define DFU_TYPE_FIRMWARE (dfu_firmware_get_type ())
G_DECLARE_DERIVABLE_TYPE (DfuFirmware, dfu_firmware, DFU, FIRMWARE, GObject)

struct _DfuFirmwareClass
{
	GObjectClass		 parent_class;
};

/**
 * DfuFirmwareParseFlags:
 * @DFU_FIRMWARE_PARSE_FLAG_NONE:			No flags set
 * @DFU_FIRMWARE_PARSE_FLAG_NO_CRC_TEST:		Do not verify the CRC
 * @DFU_FIRMWARE_PARSE_FLAG_NO_VERSION_TEST:		Do not verify the DFU version
 *
 * The optional flags used for parsing.
 **/
typedef enum {
	DFU_FIRMWARE_PARSE_FLAG_NONE			= 0,
	DFU_FIRMWARE_PARSE_FLAG_NO_CRC_TEST		= (1 << 0),
	DFU_FIRMWARE_PARSE_FLAG_NO_VERSION_TEST		= (1 << 1),
	/* private */
	DFU_FIRMWARE_PARSE_FLAG_LAST,
} DfuFirmwareParseFlags;

/**
 * DfuFormat:
 * @DFU_FORMAT_UNKNOWN:			Version number unknown
 * @DFU_FORMAT_DFU_1_0:			DFU 1.0
 * @DFU_FORMAT_DEFUSE:			DfuSe extension detected
 *
 * The known versions of the DFU standard in BCD format.
 **/
typedef enum {
	DFU_FORMAT_UNKNOWN		= 0,
	DFU_FORMAT_DFU_1_0		= 0x0100,
	DFU_FORMAT_DEFUSE		= 0x011a,
	/* private */
	DFU_FORMAT_LAST,
} DfuFormat;

DfuFirmware	*dfu_firmware_new		(void);

GBytes		*dfu_firmware_get_contents	(DfuFirmware	*firmware);
guint16		 dfu_firmware_get_vid		(DfuFirmware	*firmware);
guint16		 dfu_firmware_get_pid		(DfuFirmware	*firmware);
guint16		 dfu_firmware_get_release	(DfuFirmware	*firmware);
guint16		 dfu_firmware_get_format	(DfuFirmware	*firmware);
void		 dfu_firmware_set_contents	(DfuFirmware	*firmware,
						 GBytes		*contents);
void		 dfu_firmware_set_vid		(DfuFirmware	*firmware,
						 guint16	 vid);
void		 dfu_firmware_set_pid		(DfuFirmware	*firmware,
						 guint16	 pid);
void		 dfu_firmware_set_release	(DfuFirmware	*firmware,
						 guint16	 release);
void		 dfu_firmware_set_format	(DfuFirmware	*firmware,
						 guint16	 format);
void		 dfu_firmware_set_target_size	(DfuFirmware	*firmware,
						 guint32	 target_size);

gboolean	 dfu_firmware_parse_data	(DfuFirmware	*firmware,
						 GBytes		*bytes,
						 DfuFirmwareParseFlags flags,
						 GError		**error);
gboolean	 dfu_firmware_parse_file	(DfuFirmware	*firmware,
						 GFile		*file,
						 DfuFirmwareParseFlags flags,
						 GCancellable	*cancellable,
						 GError		**error);

GBytes		*dfu_firmware_write_data	(DfuFirmware	*firmware,
						 GError		**error);
gboolean	 dfu_firmware_write_file	(DfuFirmware	*firmware,
						 GFile		*file,
						 GCancellable	*cancellable,
						 GError		**error);
gchar		*dfu_firmware_to_string		(DfuFirmware	*firmware);

G_END_DECLS

#endif /* __DFU_FIRMWARE_H */
