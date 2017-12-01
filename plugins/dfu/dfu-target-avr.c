/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
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

#include "config.h"

#include <string.h>
#include <stdio.h>

#include "dfu-chunked.h"
#include "dfu-common.h"
#include "dfu-sector.h"
#include "dfu-target-avr.h"
#include "dfu-target-private.h"
#include "dfu-device-private.h"

#include "fwupd-error.h"

typedef struct {
	guint32			 device_id;
} DfuTargetAvrPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (DfuTargetAvr, dfu_target_avr, DFU_TYPE_TARGET)
#define GET_PRIVATE(o) (dfu_target_avr_get_instance_private (o))

/* ATMEL AVR version of DFU:
 * http://www.atmel.com/Images/doc7618.pdf */
#define DFU_AVR_CMD_PROG_START			0x01
#define DFU_AVR_CMD_DISPLAY_DATA		0x03
#define DFU_AVR_CMD_WRITE_COMMAND		0x04
#define DFU_AVR_CMD_READ_COMMAND		0x05
#define DFU_AVR_CMD_CHANGE_BASE_ADDR		0x06

/* Atmel AVR32 version of DFU:
 * http://www.atmel.com/images/doc32131.pdf */
#define DFU_AVR32_GROUP_SELECT			0x06	/** SELECT */
#define DFU_AVR32_CMD_SELECT_MEMORY		0x03
#define DFU_AVR32_MEMORY_UNIT			0x00
#define DFU_AVR32_MEMORY_PAGE			0x01
#define DFU_AVR32_MEMORY_UNIT_FLASH		0x00
#define DFU_AVR32_MEMORY_UNIT_EEPROM		0x01
#define DFU_AVR32_MEMORY_UNIT_SECURITY		0x02
#define DFU_AVR32_MEMORY_UNIT_CONFIGURATION	0x03
#define DFU_AVR32_MEMORY_UNIT_BOOTLOADER	0x04
#define DFU_AVR32_MEMORY_UNIT_SIGNATURE		0x05
#define DFU_AVR32_MEMORY_UNIT_USER		0x06
#define DFU_AVR32_GROUP_DOWNLOAD		0x01	/** DOWNLOAD */
#define DFU_AVR32_CMD_PROGRAM_START		0x00
#define DFU_AVR32_GROUP_UPLOAD			0x03	/** UPLOAD */
#define DFU_AVR32_CMD_READ_MEMORY		0x00
#define DFU_AVR32_CMD_BLANK_CHECK		0x01
#define DFU_AVR32_GROUP_EXEC			0x04	/** EXEC */
#define DFU_AVR32_CMD_ERASE			0x00
#define DFU_AVR32_ERASE_EVERYTHING		0xff
#define DFU_AVR32_CMD_START_APPLI		0x03
#define DFU_AVR32_START_APPLI_RESET		0x00
#define DFU_AVR32_START_APPLI_NO_RESET		0x01

#define ATMEL_64KB_PAGE				0x10000
#define ATMEL_MAX_TRANSFER_SIZE			0x0400
#define ATMEL_AVR_CONTROL_BLOCK_SIZE		32
#define ATMEL_AVR32_CONTROL_BLOCK_SIZE		64

#define ATMEL_MANUFACTURER_CODE1		0x58
#define ATMEL_MANUFACTURER_CODE2		0x1e

static gboolean
dfu_target_avr_mass_erase (DfuTarget *target, GError **error)
{
	g_autoptr(GBytes) data_in = NULL;
	guint8 buf[3];

	/* this takes a long time on some devices */
	dfu_device_set_timeout (dfu_target_get_device (target), 5000);

	/* format buffer */
	buf[0] = DFU_AVR32_GROUP_EXEC;
	buf[1] = DFU_AVR32_CMD_ERASE;
	buf[2] = 0xff;
	data_in = g_bytes_new_static (buf, sizeof(buf));
	g_debug ("mass erasing");
	dfu_target_set_action (target, FWUPD_STATUS_DEVICE_ERASE);
	if (!dfu_target_download_chunk (target, 0, data_in, error)) {
		g_prefix_error (error, "cannot mass-erase: ");
		return FALSE;
	}
	dfu_target_set_action (target, FWUPD_STATUS_IDLE);
	return TRUE;
}

static gboolean
dfu_target_avr_attach (DfuTarget *target, GError **error)
{
	guint8 buf[3];
	g_autoptr(GBytes) data_empty = NULL;
	g_autoptr(GBytes) data_in = NULL;
	g_autoptr(GError) error_local = NULL;

	/* format buffer */
	buf[0] = DFU_AVR32_GROUP_EXEC;
	buf[1] = DFU_AVR32_CMD_START_APPLI;
	buf[2] = DFU_AVR32_START_APPLI_RESET;
	data_in = g_bytes_new_static (buf, sizeof(buf));
	if (!dfu_target_download_chunk (target, 0, data_in, &error_local)) {
		if (g_error_matches (error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
			g_debug ("ignoring as device rebooting: %s", error_local->message);
			return TRUE;
		}
		g_prefix_error (error, "cannot start application reset attach: ");
		return FALSE;
	}

	/* do zero-sized download to initiate the reset */
	data_empty = g_bytes_new (NULL, 0);
	if (!dfu_target_download_chunk (target, 0, data_empty, &error_local)) {
		if (g_error_matches (error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
			g_debug ("ignoring as device rebooting: %s", error_local->message);
			return TRUE;
		}
		g_prefix_error (error, "cannot initiate reset for attach: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

/**
 * dfu_target_avr_select_memory_unit:
 * @target: a #DfuTarget
 * @memory_unit: a unit, e.g. %DFU_AVR32_MEMORY_UNIT_FLASH
 * @error: a #GError, or %NULL
 *
 * Selects the memory unit for the device.
 *
 * Return value: %TRUE for success
 **/
static gboolean
dfu_target_avr_select_memory_unit (DfuTarget *target,
				   guint8 memory_unit,
				   GError **error)
{
	g_autoptr(GBytes) data_in = NULL;
	guint8 buf[4];

	/* check legacy protocol quirk */
	if (dfu_device_has_quirk (dfu_target_get_device (target),
				  DFU_DEVICE_QUIRK_LEGACY_PROTOCOL)) {
		g_debug ("ignoring select memory unit as legacy protocol");
		return TRUE;
	}

	/* format buffer */
	buf[0] = DFU_AVR32_GROUP_SELECT;
	buf[1] = DFU_AVR32_CMD_SELECT_MEMORY;
	buf[2] = DFU_AVR32_MEMORY_UNIT;
	buf[3] = memory_unit;
	data_in = g_bytes_new_static (buf, sizeof(buf));
	g_debug ("selecting memory unit 0x%02x", (guint) memory_unit);
	if (!dfu_target_download_chunk (target, 0, data_in, error)) {
		g_prefix_error (error, "cannot select memory unit: ");
		return FALSE;
	}
	return TRUE;
}

/**
 * dfu_target_avr_select_memory_page:
 * @target: a #DfuTarget
 * @memory_page: an address
 * @error: a #GError, or %NULL
 *
 * Selects the memory page for the AVR device.
 *
 * Return value: %TRUE for success
 **/
static gboolean
dfu_target_avr_select_memory_page (DfuTarget *target,
				   guint16 memory_page,
				   GError **error)
{
	g_autoptr(GBytes) data_in = NULL;
	guint8 buf[4];

	/* check page not too large for protocol */
	if (memory_page > 0xff) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "cannot select memory page:0x%02x "
			     "with FLIP protocol version 1",
			     memory_page);
		return FALSE;
	}

	/* format buffer */
	buf[0] = DFU_AVR_CMD_CHANGE_BASE_ADDR;
	buf[1] = 0x03;
	buf[2] = 0x00;
	buf[3] = memory_page & 0xff;
	data_in = g_bytes_new_static (buf, sizeof(buf));
	g_debug ("selecting memory page 0x%01x", (guint) memory_page);
	if (!dfu_target_download_chunk (target, 0, data_in, error)) {
		g_prefix_error (error, "cannot select memory page: ");
		return FALSE;
	}
	return TRUE;
}

/**
 * dfu_target_avr32_select_memory_page:
 * @target: a #DfuTarget
 * @memory_page: an address
 * @error: a #GError, or %NULL
 *
 * Selects the memory page for the AVR32 device.
 *
 * Return value: %TRUE for success
 **/
static gboolean
dfu_target_avr32_select_memory_page (DfuTarget *target,
				     guint16 memory_page,
				     GError **error)
{
	g_autoptr(GBytes) data_in = NULL;
	guint16 memory_page_le = GUINT16_TO_BE (memory_page);
	guint8 buf[5];

	/* format buffer */
	buf[0] = DFU_AVR32_GROUP_SELECT;
	buf[1] = DFU_AVR32_CMD_SELECT_MEMORY;
	buf[2] = DFU_AVR32_MEMORY_PAGE;
	memcpy (&buf[3], &memory_page_le, 2);
	data_in = g_bytes_new_static (buf, sizeof(buf));
	g_debug ("selecting memory page 0x%02x", (guint) memory_page);
	if (!dfu_target_download_chunk (target, 0, data_in, error)) {
		g_prefix_error (error, "cannot select memory page: ");
		return FALSE;
	}
	return TRUE;
}

/**
 * dfu_target_avr_read_memory
 * @target: a #DfuTarget
 * @addr_start: an address
 * @addr_end: an address
 * @error: a #GError, or %NULL
 *
 * Reads flash data from the device.
 *
 * Return value: %TRUE for success
 **/
static gboolean
dfu_target_avr_read_memory (DfuTarget *target,
			    guint16 addr_start,
			    guint16 addr_end,
			    GError **error)
{
	g_autoptr(GBytes) data_in = NULL;
	guint16 addr_start_le = GUINT16_TO_BE (addr_start);
	guint16 addr_end_le = GUINT16_TO_BE (addr_end);
	guint8 buf[6];

	/* format buffer */
	buf[0] = DFU_AVR32_GROUP_UPLOAD;
	buf[1] = DFU_AVR32_CMD_READ_MEMORY;
	memcpy (&buf[2], &addr_start_le, 2);
	memcpy (&buf[4], &addr_end_le, 2);
	data_in = g_bytes_new_static (buf, sizeof(buf));
	g_debug ("reading memory from 0x%04x to 0x%04x",
		 (guint) addr_start, (guint) addr_end);
	if (!dfu_target_download_chunk (target, 0, data_in, error)) {
		g_prefix_error (error, "cannot read memory 0x%04x to 0x%04x: ",
				(guint) addr_start, (guint) addr_end);
		return FALSE;
	}
	return TRUE;
}

/**
 * dfu_target_avr_read_command:
 * @target: a #DfuTarget
 * @memory_unit: a unit, e.g. %DFU_AVR32_MEMORY_UNIT_FLASH
 * @error: a #GError, or %NULL
 *
 * Performs a read operation on the device.
 *
 * Return value: %TRUE for success
 **/
static gboolean
dfu_target_avr_read_command (DfuTarget *target, guint8 page, guint8 addr, GError **error)
{
	g_autoptr(GBytes) data_in = NULL;
	guint8 buf[3];

	/* format buffer */
	buf[0] = DFU_AVR_CMD_READ_COMMAND;
	buf[1] = page;
	buf[2] = addr;
	data_in = g_bytes_new_static (buf, sizeof(buf));
	g_debug ("read command page:0x%02x addr:0x%02x", (guint) page, (guint) addr);
	if (!dfu_target_download_chunk (target, 0, data_in, error)) {
		g_prefix_error (error, "cannot read command page: ");
		return FALSE;
	}
	return TRUE;
}

/**
 * dfu_target_avr32_get_chip_signature:
 * @target: a #DfuTarget
 * @error: a #GError, or %NULL
 *
 * Gets the chip signature for the AVR32 device.
 *
 * Return value: a 4-byte %GBytes object for success, else %NULL
 **/
static GBytes *
dfu_target_avr32_get_chip_signature (DfuTarget *target, GError **error)
{
	/* select unit, and request 4 bytes */
	if (!dfu_target_avr_select_memory_unit (target,
						DFU_AVR32_MEMORY_UNIT_SIGNATURE,
						error))
		return NULL;
	if (!dfu_target_avr32_select_memory_page (target, 0x00, error))
		return NULL;
	if (!dfu_target_avr_read_memory (target, 0x00, 0x03, error))
		return NULL;

	/* get data back */
	return dfu_target_upload_chunk (target, 0x00, 0, error);
}

/**
 * dfu_target_avr_get_chip_signature:
 * @target: a #DfuTarget
 * @error: a #GError, or %NULL
 *
 * Gets the chip signature for the AVR device.
 *
 * Return value: a 4-byte %GBytes object for success, else %NULL
 **/
static GBytes *
dfu_target_avr_get_chip_signature (DfuTarget *target, GError **error)
{
	struct {
		guint8 page;
		guint addr;
	} signature_locations[] = {
		{ 0x01, 0x30 },
		{ 0x01, 0x31 },
		{ 0x01, 0x60 },
		{ 0x01, 0x61 },
		{ 0xff, 0xff }
	};
	g_autoptr(GPtrArray) chunks = NULL;

	/* we have to request this one byte at a time */
	chunks = g_ptr_array_new_with_free_func ((GDestroyNotify) g_bytes_unref);
	for (guint i = 0; signature_locations[i].page != 0xff; i++) {
		g_autoptr(GBytes) chunk_byte = NULL;

		/* request a single byte */
		if (!dfu_target_avr_read_command (target,
						  signature_locations[i].page,
						  signature_locations[i].addr,
						  error))
			return NULL;

		/* get data back */
		chunk_byte = dfu_target_upload_chunk (target, 0x00, 0x01, error);
		if (chunk_byte == NULL)
			return NULL;
		if (g_bytes_get_size (chunk_byte) != 1) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "cannot read signature memory page:0x%02x "
				     "addr:0x%02x, got 0x%02x bytes",
				     (guint) signature_locations[i].page,
				     (guint) signature_locations[i].addr,
				     (guint) g_bytes_get_size (chunk_byte));
			return NULL;
		}
		g_ptr_array_add (chunks, g_steal_pointer (&chunk_byte));
	}
	return dfu_utils_bytes_join_array (chunks);
}

static gboolean
dfu_target_avr_setup (DfuTarget *target, GError **error)
{
	DfuDevice *device;
	DfuTargetAvr *target_avr = DFU_TARGET_AVR (target);
	DfuTargetAvrPrivate *priv = GET_PRIVATE (target_avr);
	const gchar *quirk_str;
	const guint8 *buf;
	gsize sz;
	guint32 device_id_be;
	g_autofree gchar *chip_id = NULL;
	g_autoptr(GBytes) chunk_sig = NULL;

	/* already done */
	if (priv->device_id > 0x0)
		return TRUE;

	/* different methods for AVR vs. AVR32 */
	if (dfu_device_has_quirk (dfu_target_get_device (target),
				  DFU_DEVICE_QUIRK_LEGACY_PROTOCOL)) {
		chunk_sig = dfu_target_avr_get_chip_signature (target, error);
		if (chunk_sig == NULL)
			return FALSE;
	} else {
		chunk_sig = dfu_target_avr32_get_chip_signature (target, error);
		if (chunk_sig == NULL)
			return FALSE;
	}

	/* get data back */
	buf = g_bytes_get_data (chunk_sig, &sz);
	if (sz != 4) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "cannot read config memory, got 0x%02x bytes",
			     (guint) sz);
		return FALSE;
	}
	memcpy (&device_id_be, buf, 4);
	priv->device_id = GINT32_FROM_BE (device_id_be);
	if (buf[0] == ATMEL_MANUFACTURER_CODE1) {
		chip_id = g_strdup_printf ("0x%08x", (guint) priv->device_id);
	} else if (buf[0] == ATMEL_MANUFACTURER_CODE2) {
		chip_id = g_strdup_printf ("0x%06x", (guint) priv->device_id >> 8);
	} else {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "cannot read config vendor, got 0x%08x, "
			     "expected 0x%02x or 0x%02x",
			     (guint) priv->device_id,
			     (guint) ATMEL_MANUFACTURER_CODE1,
			     (guint) ATMEL_MANUFACTURER_CODE2);
		return FALSE;
	}

	/* set the alt-name using the device ID */
	dfu_device_set_chip_id (dfu_target_get_device (target), chip_id);
	device = dfu_target_get_device (target);
	quirk_str = fu_quirks_lookup_by_id (dfu_device_get_system_quirks (device),
					    FU_QUIRKS_DFU_AVR_CHIP_ID,
					    chip_id);
	if (quirk_str == NULL) {
		dfu_device_remove_attribute (dfu_target_get_device (target),
					     DFU_DEVICE_ATTRIBUTE_CAN_DOWNLOAD);
		dfu_device_remove_attribute (dfu_target_get_device (target),
					     DFU_DEVICE_ATTRIBUTE_CAN_UPLOAD);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "DeviceID %s is not supported",
			     chip_id);
		return FALSE;
	}
	dfu_target_set_alt_name (target, quirk_str);

	return TRUE;
}

static gboolean
dfu_target_avr_download_element (DfuTarget *target,
				 DfuElement *element,
				 DfuTargetTransferFlags flags,
				 GError **error)
{
	DfuSector *sector;
	GBytes *blob;
	const guint8 *data;
	gsize header_sz = ATMEL_AVR32_CONTROL_BLOCK_SIZE;
	guint16 page_last = G_MAXUINT16;
	guint32 address;
	guint32 address_offset = 0x0;
	g_autoptr(GPtrArray) packets = NULL;
	const guint8 footer[] = { 0x00, 0x00, 0x00, 0x00,	/* CRC */
				  16,				/* len */
				  'D', 'F', 'U',		/* signature */
				  0x01, 0x10,			/* version */
				  0xff, 0xff,			/* vendor ID */
				  0xff, 0xff,			/* product ID */
				  0xff, 0xff };			/* release */

	/* select a memory and erase everything */
	if (!dfu_target_avr_select_memory_unit (target,
						dfu_target_get_alt_setting (target),
						error))
		return FALSE;
	if (!dfu_target_avr_mass_erase (target, error))
		return FALSE;

	/* verify the element isn't larger than the target size */
	blob = dfu_element_get_contents (element);
	sector = dfu_target_get_sector_default (target);
	if (sector == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "no sector defined for target");
		return FALSE;
	}
	address = dfu_element_get_address (element) & ~0x80000000;
	if (address < dfu_sector_get_address (sector)) {
		address_offset = dfu_sector_get_address (sector) - address;
		g_warning ("firmware element starts at 0x%x but sector "
			   "starts at 0x%x, so offsetting by 0x%x (bootloader?)",
			   (guint) address,
			   (guint) dfu_sector_get_address (sector),
			   (guint) address_offset);
	}
	if (g_bytes_get_size (blob) + address_offset > dfu_sector_get_size (sector)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "element was larger than sector size: 0x%x",
			     (guint) dfu_sector_get_size (sector));
		return FALSE;
	}

	/* the original AVR protocol uses a half-size control block */
	if (dfu_device_has_quirk (dfu_target_get_device (target),
				  DFU_DEVICE_QUIRK_LEGACY_PROTOCOL)) {
		header_sz = ATMEL_AVR_CONTROL_BLOCK_SIZE;
	}

	/* chunk up the memory space into pages */
	data = g_bytes_get_data (blob, NULL);
	packets = dfu_chunked_new (data + address_offset,
				   g_bytes_get_size (blob) - address_offset,
				   dfu_sector_get_address (sector),
				   ATMEL_64KB_PAGE,
				   ATMEL_MAX_TRANSFER_SIZE);

	/* update UI */
	dfu_target_set_action (target, FWUPD_STATUS_DEVICE_WRITE);

	/* process each chunk */
	for (guint i = 0; i < packets->len; i++) {
		const DfuChunkedPacket *packet = g_ptr_array_index (packets, i);
		guint16 addr_start_le;
		guint16 addr_end_le;
		g_autofree guint8 *buf = NULL;
		g_autoptr(GBytes) chunk_tmp = NULL;

		/* select page if required */
		if (packet->page != page_last) {
			if (dfu_device_has_quirk (dfu_target_get_device (target),
						  DFU_DEVICE_QUIRK_LEGACY_PROTOCOL)) {
				if (!dfu_target_avr_select_memory_page (target,
									packet->page,
									error))
					return FALSE;
			} else {
				if (!dfu_target_avr32_select_memory_page (target,
									  packet->page,
									  error))
					return FALSE;
			}
			page_last = packet->page;
		}

		/* create packet with header and footer */
		addr_start_le = GUINT16_TO_BE (packet->address);
		addr_end_le = GUINT16_TO_BE (packet->address + packet->data_sz - 1);
		buf = g_malloc0 (packet->data_sz + header_sz + sizeof(footer));
		buf[0] = DFU_AVR32_GROUP_DOWNLOAD;
		buf[1] = DFU_AVR32_CMD_PROGRAM_START;
		memcpy (&buf[2], &addr_start_le, 2);
		memcpy (&buf[4], &addr_end_le, 2);
		memcpy (&buf[header_sz], packet->data, packet->data_sz);
		memcpy (&buf[header_sz + packet->data_sz], footer, sizeof(footer));

		/* download data */
		chunk_tmp = g_bytes_new_static (buf, packet->data_sz + header_sz + sizeof(footer));
		g_debug ("sending %" G_GSIZE_FORMAT " bytes to the hardware",
			 g_bytes_get_size (chunk_tmp));
		if (!dfu_target_download_chunk (target, i, chunk_tmp, error))
			return FALSE;

		/* update UI */
		dfu_target_set_percentage (target, i + 1, packets->len);
	}

	/* done */
	dfu_target_set_percentage_raw (target, 100);
	dfu_target_set_action (target, FWUPD_STATUS_IDLE);
	return TRUE;
}

static DfuElement *
dfu_target_avr_upload_element (DfuTarget *target,
			       guint32 address,
			       gsize expected_size,
			       gsize maximum_size,
			       GError **error)
{
	guint16 page_last = G_MAXUINT16;
	guint chunk_valid = G_MAXUINT;
	g_autoptr(DfuElement) element = NULL;
	g_autoptr(GBytes) contents = NULL;
	g_autoptr(GBytes) contents_truncated = NULL;
	g_autoptr(GPtrArray) packets = NULL;
	g_autoptr(GPtrArray) chunks = NULL;
	DfuSector *sector;

	/* select unit */
	if (!dfu_target_avr_select_memory_unit (target,
						dfu_target_get_alt_setting (target),
						error))
		return NULL;

	/* verify the element isn't lower than the flash area */
	sector = dfu_target_get_sector_default (target);
	if (sector == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "no sector defined for target");
		return NULL;
	}
	if (address < dfu_sector_get_address (sector)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "cannot read from below sector start");
		return NULL;
	}

	/* the flash starts at 0x80000000, but is indexed from zero */
	address &= ~0x80000000;

	/* chunk up the memory space into pages */
	packets = dfu_chunked_new (NULL, maximum_size, address,
				   ATMEL_64KB_PAGE, ATMEL_MAX_TRANSFER_SIZE);

	/* update UI */
	dfu_target_set_action (target, FWUPD_STATUS_DEVICE_READ);

	/* process each chunk */
	chunks = g_ptr_array_new_with_free_func ((GDestroyNotify) g_bytes_unref);
	for (guint i = 0; i < packets->len; i++) {
		GBytes *chunk_tmp = NULL;
		const DfuChunkedPacket *packet = g_ptr_array_index (packets, i);

		/* select page if required */
		if (packet->page != page_last) {
			if (dfu_device_has_quirk (dfu_target_get_device (target),
						  DFU_DEVICE_QUIRK_LEGACY_PROTOCOL)) {
				if (!dfu_target_avr_select_memory_page (target,
									packet->page,
									error))
					return NULL;
			} else {
				if (!dfu_target_avr32_select_memory_page (target,
									  packet->page,
									  error))
					return NULL;
			}
			page_last = packet->page;
		}

		/* prepare to read */
		if (!dfu_target_avr_read_memory (target,
						 packet->address,
						 packet->address + packet->data_sz - 1,
						 error))
			return NULL;

		/* upload data */
		g_debug ("requesting %i bytes from the hardware",
			 ATMEL_MAX_TRANSFER_SIZE);
		chunk_tmp = dfu_target_upload_chunk (target, i,
						     ATMEL_MAX_TRANSFER_SIZE,
						     error);
		if (chunk_tmp == NULL)
			return NULL;
		g_ptr_array_add (chunks, chunk_tmp);

		/* this page has valid data */
		if (!dfu_utils_bytes_is_empty (chunk_tmp)) {
			g_debug ("chunk %u has data (page %" G_GUINT32_FORMAT ")",
				 i, packet->page);
			chunk_valid = i;
		} else {
			g_debug ("chunk %u is empty", i);
		}

		/* update UI */
		dfu_target_set_percentage (target, i + 1, packets->len);
	}

	/* done */
	dfu_target_set_percentage_raw (target, 100);
	dfu_target_set_action (target, FWUPD_STATUS_IDLE);

	/* truncate the image if any sectors are empty, i.e. all 0xff */
	if (chunk_valid == G_MAXUINT) {
		g_debug ("all %u chunks are empty", chunks->len);
		g_ptr_array_set_size (chunks, 0);
	} else if (chunks->len != chunk_valid + 1) {
		g_debug ("truncating chunks from %u to %u",
			 chunks->len, chunk_valid + 1);
		g_ptr_array_set_size (chunks, chunk_valid + 1);
	}

	/* create element of required size */
	contents = dfu_utils_bytes_join_array (chunks);
	if (expected_size > 0 && g_bytes_get_size (contents) > expected_size) {
		contents_truncated = g_bytes_new_from_bytes (contents, 0x0, expected_size);
	} else {
		contents_truncated = g_bytes_ref (contents);
	}

	element = dfu_element_new ();
	dfu_element_set_address (element, address | 0x80000000); /* flash */
	dfu_element_set_contents (element, contents_truncated);
	return g_steal_pointer (&element);
}

static void
dfu_target_avr_init (DfuTargetAvr *target_avr)
{
}

static void
dfu_target_avr_class_init (DfuTargetAvrClass *klass)
{
	DfuTargetClass *klass_target = DFU_TARGET_CLASS (klass);
	klass_target->setup = dfu_target_avr_setup;
	klass_target->attach = dfu_target_avr_attach;
	klass_target->mass_erase = dfu_target_avr_mass_erase;
	klass_target->upload_element = dfu_target_avr_upload_element;
	klass_target->download_element = dfu_target_avr_download_element;
}

DfuTarget *
dfu_target_avr_new (void)
{
	DfuTarget *target;
	target = g_object_new (DFU_TYPE_TARGET_AVR, NULL);
	return target;
}
