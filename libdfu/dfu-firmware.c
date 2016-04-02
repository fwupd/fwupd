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

/**
 * SECTION:dfu-firmware
 * @short_description: Object representing a DFU or DfuSe firmware file
 *
 * This object allows reading and writing firmware files either in
 * raw, DFU or DfuSe formats.
 *
 * A #DfuFirmware can be made up of several #DfuImages, although
 * typically there is only one.
 *
 * See also: #DfuImage
 */

#include "config.h"

#include <string.h>
#include <stdio.h>

#include "dfu-common.h"
#include "dfu-error.h"
#include "dfu-firmware.h"
#include "dfu-image-private.h"

static void dfu_firmware_finalize			 (GObject *object);

/**
 * DfuFirmwarePrivate:
 *
 * Private #DfuFirmware data
 **/
typedef struct {
	GHashTable		*metadata;
	GPtrArray		*images;
	guint16			 vid;
	guint16			 pid;
	guint16			 release;
	guint32			 crc;
	DfuCipherKind		 cipher_kind;
	DfuFirmwareFormat	 format;
} DfuFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (DfuFirmware, dfu_firmware, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (dfu_firmware_get_instance_private (o))

/**
 * dfu_firmware_class_init:
 **/
static void
dfu_firmware_class_init (DfuFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = dfu_firmware_finalize;
}

/**
 * dfu_firmware_init:
 **/
static void
dfu_firmware_init (DfuFirmware *firmware)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	priv->vid = 0xffff;
	priv->pid = 0xffff;
	priv->release = 0xffff;
	priv->images = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	priv->metadata = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

/**
 * dfu_firmware_finalize:
 **/
static void
dfu_firmware_finalize (GObject *object)
{
	DfuFirmware *firmware = DFU_FIRMWARE (object);
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);

	g_ptr_array_unref (priv->images);
	g_hash_table_destroy (priv->metadata);

	G_OBJECT_CLASS (dfu_firmware_parent_class)->finalize (object);
}

/**
 * dfu_firmware_new:
 *
 * Creates a new DFU firmware object.
 *
 * Return value: a new #DfuFirmware
 *
 * Since: 0.5.4
 **/
DfuFirmware *
dfu_firmware_new (void)
{
	DfuFirmware *firmware;
	firmware = g_object_new (DFU_TYPE_FIRMWARE, NULL);
	return firmware;
}

/**
 * dfu_firmware_get_image:
 * @firmware: a #DfuFirmware
 * @alt_setting: an alternative setting, typically 0x00
 *
 * Gets an image from the firmware file.
 *
 * Return value: (transfer none): a #DfuImage, or %NULL for not found
 *
 * Since: 0.5.4
 **/
DfuImage *
dfu_firmware_get_image (DfuFirmware *firmware, guint8 alt_setting)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	DfuImage *im;
	guint i;

	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), NULL);

	/* find correct image */
	for (i = 0; i < priv->images->len; i++) {
		im = g_ptr_array_index (priv->images, i);
		if (dfu_image_get_alt_setting (im) == alt_setting)
			return im;
	}
	return NULL;
}

/**
 * dfu_firmware_get_image_by_name:
 * @firmware: a #DfuFirmware
 * @name: an alternative setting name
 *
 * Gets an image from the firmware file.
 *
 * Return value: (transfer none): a #DfuImage, or %NULL for not found
 *
 * Since: 0.5.4
 **/
DfuImage *
dfu_firmware_get_image_by_name (DfuFirmware *firmware, const gchar *name)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	DfuImage *im;
	guint i;

	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), NULL);

	/* find correct image */
	for (i = 0; i < priv->images->len; i++) {
		im = g_ptr_array_index (priv->images, i);
		if (g_strcmp0 (dfu_image_get_name (im), name) == 0)
			return im;
	}
	return NULL;
}

/**
 * dfu_firmware_get_image_default:
 * @firmware: a #DfuFirmware
 *
 * Gets the default image from the firmware file.
 *
 * Return value: (transfer none): a #DfuImage, or %NULL for not found
 *
 * Since: 0.5.4
 **/
DfuImage *
dfu_firmware_get_image_default (DfuFirmware *firmware)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), NULL);
	if (priv->images->len == 0)
		return NULL;
	return g_ptr_array_index (priv->images, 0);
}

/**
 * dfu_firmware_get_images:
 * @firmware: a #DfuFirmware
 *
 * Gets all the images contained in this firmware file.
 *
 * Return value: (transfer none) (element-type DfuImage): list of images
 *
 * Since: 0.5.4
 **/
GPtrArray *
dfu_firmware_get_images (DfuFirmware *firmware)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), NULL);
	return priv->images;
}

/**
 * dfu_firmware_get_size:
 * @firmware: a #DfuFirmware
 *
 * Gets the size of all the images in the firmware.
 *
 * This only returns actual data that would be sent to the device and
 * does not include any padding.
 *
 * Return value: a integer value, or 0 if there are no images.
 *
 * Since: 0.5.4
 **/
guint32
dfu_firmware_get_size (DfuFirmware *firmware)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	guint32 length = 0;
	guint i;
	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), 0);
	for (i = 0; i < priv->images->len; i++) {
		DfuImage *image = g_ptr_array_index (priv->images, i);
		length += dfu_image_get_size (image);
	}
	return length;
}

/**
 * dfu_firmware_add_image:
 * @firmware: a #DfuFirmware
 * @image: a #DfuImage
 *
 * Adds an image to the list of images.
 *
 * Since: 0.5.4
 **/
void
dfu_firmware_add_image (DfuFirmware *firmware, DfuImage *image)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_return_if_fail (DFU_IS_FIRMWARE (firmware));
	g_return_if_fail (DFU_IS_IMAGE (image));
	g_ptr_array_add (priv->images, g_object_ref (image));
}

/**
 * dfu_firmware_get_vid:
 * @firmware: a #DfuFirmware
 *
 * Gets the vendor ID.
 *
 * Return value: a vendor ID, or 0xffff for unset
 *
 * Since: 0.5.4
 **/
guint16
dfu_firmware_get_vid (DfuFirmware *firmware)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), 0xffff);
	return priv->vid;
}

/**
 * dfu_firmware_get_pid:
 * @firmware: a #DfuFirmware
 *
 * Gets the product ID.
 *
 * Return value: a product ID, or 0xffff for unset
 *
 * Since: 0.5.4
 **/
guint16
dfu_firmware_get_pid (DfuFirmware *firmware)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), 0xffff);
	return priv->pid;
}

/**
 * dfu_firmware_get_release:
 * @firmware: a #DfuFirmware
 *
 * Gets the device ID.
 *
 * Return value: a device ID, or 0xffff for unset
 *
 * Since: 0.5.4
 **/
guint16
dfu_firmware_get_release (DfuFirmware *firmware)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), 0xffff);
	return priv->release;
}

/**
 * dfu_firmware_get_format:
 * @firmware: a #DfuFirmware
 *
 * Gets the DFU version.
 *
 * Return value: a version, or 0x0 for unset
 *
 * Since: 0.5.4
 **/
guint16
dfu_firmware_get_format (DfuFirmware *firmware)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), 0xffff);
	return priv->format;
}

/**
 * dfu_firmware_set_vid:
 * @firmware: a #DfuFirmware
 * @vid: vendor ID, or 0xffff for unset
 *
 * Sets the vendor ID.
 *
 * Since: 0.5.4
 **/
void
dfu_firmware_set_vid (DfuFirmware *firmware, guint16 vid)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_return_if_fail (DFU_IS_FIRMWARE (firmware));
	priv->vid = vid;
}

/**
 * dfu_firmware_set_pid:
 * @firmware: a #DfuFirmware
 * @pid: product ID, or 0xffff for unset
 *
 * Sets the product ID.
 *
 * Since: 0.5.4
 **/
void
dfu_firmware_set_pid (DfuFirmware *firmware, guint16 pid)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_return_if_fail (DFU_IS_FIRMWARE (firmware));
	priv->pid = pid;
}

/**
 * dfu_firmware_set_release:
 * @firmware: a #DfuFirmware
 * @release: device ID, or 0xffff for unset
 *
 * Sets the device ID.
 *
 * Since: 0.5.4
 **/
void
dfu_firmware_set_release (DfuFirmware *firmware, guint16 release)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_return_if_fail (DFU_IS_FIRMWARE (firmware));
	priv->release = release;
}

/**
 * dfu_firmware_set_format:
 * @firmware: a #DfuFirmware
 * @format: a #DfuFirmwareFormat, e.g. %DFU_FIRMWARE_FORMAT_DFUSE
 *
 * Sets the DFU version in BCD format.
 *
 * Since: 0.5.4
 **/
void
dfu_firmware_set_format (DfuFirmware *firmware, DfuFirmwareFormat format)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_return_if_fail (DFU_IS_FIRMWARE (firmware));
	priv->format = format;
}

typedef struct __attribute__((packed)) {
	guint16		release;
	guint16		pid;
	guint16		vid;
	guint16		ver;
	guint8		sig[3];
	guint8		len;
	guint32		crc;
} DfuFirmwareFooter;

static guint32 _crctbl[] = {
	0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
	0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
	0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
	0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
	0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
	0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
	0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
	0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
	0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
	0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
	0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
	0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
	0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
	0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
	0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
	0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
	0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
	0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
	0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
	0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
	0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
	0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
	0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
	0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
	0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
	0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
	0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
	0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
	0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
	0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
	0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
	0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
	0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
	0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
	0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
	0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
	0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
	0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
	0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
	0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
	0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
	0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
	0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d };

/**
 * dfu_firmware_generate_crc32:
 **/
static guint32
dfu_firmware_generate_crc32 (const guint8 *data, gsize length)
{
	guint i;
	guint32 accum = 0xffffffff;
	for (i = 0; i < length; i++)
		accum = _crctbl[(accum^data[i]) & 0xff] ^ (accum >> 8);
	return accum;
}

/**
 * dfu_firmware_ihex_parse_uint8:
 **/
static guint8
dfu_firmware_ihex_parse_uint8 (const gchar *data, guint pos)
{
	gchar buffer[3];
	memcpy (buffer, data + pos, 2);
	buffer[2] = '\0';
	return g_ascii_strtoull (buffer, NULL, 16);
}

/**
 * dfu_firmware_ihex_parse_uint16:
 **/
static guint16
dfu_firmware_ihex_parse_uint16 (const gchar *data, guint pos)
{
	gchar buffer[5];
	memcpy (buffer, data + pos, 4);
	buffer[4] = '\0';
	return g_ascii_strtoull (buffer, NULL, 16);
}

#define	DFU_INHX32_RECORD_TYPE_DATA		0
#define	DFU_INHX32_RECORD_TYPE_EOF		1
#define	DFU_INHX32_RECORD_TYPE_EXTENDED		4

/**
 * dfu_firmware_add_ihex:
 **/
static gboolean
dfu_firmware_add_ihex (DfuFirmware *firmware, GBytes *bytes,
		       DfuFirmwareParseFlags flags, GError **error)
{
	const gchar *in_buffer;
	gsize len_in;
	guint16 addr_high = 0;
	guint16 addr_low = 0;
	guint32 addr32 = 0;
	guint32 addr32_last = 0;
	guint8 checksum;
	guint8 data_tmp;
	guint8 len_tmp;
	guint8 type;
	guint end;
	guint i;
	guint j;
	guint offset = 0;
	g_autoptr(DfuElement) element = NULL;
	g_autoptr(DfuImage) image = NULL;
	g_autoptr(GBytes) contents = NULL;
	g_autoptr(GString) string = NULL;

	g_return_val_if_fail (bytes != NULL, FALSE);

	/* create element */
	image = dfu_image_new ();
	dfu_image_set_name (image, "ihex");
	element = dfu_element_new ();

	/* parse records */
	in_buffer = g_bytes_get_data (bytes, &len_in);
	string = g_string_new ("");
	while (offset < len_in) {

		/* check starting token */
		if (in_buffer[offset] != ':') {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_INVALID_FILE,
				     "invalid starting token, got %c at %x",
				     in_buffer[offset], offset);
			return FALSE;
		}

		/* check there's enough data for the smallest possible record */
		if (offset + 12 > (guint) len_in) {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_INVALID_FILE,
				     "record incomplete at %i, length %i",
				     offset, (guint) len_in);
			return FALSE;
		}

		/* length, 16-bit address, type */
		len_tmp = dfu_firmware_ihex_parse_uint8 (in_buffer, offset+1);
		addr_low = dfu_firmware_ihex_parse_uint16 (in_buffer, offset+3);
		type = dfu_firmware_ihex_parse_uint8 (in_buffer, offset+7);

		/* position of checksum */
		end = offset + 9 + len_tmp * 2;
		if (end > (guint) len_in) {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_INVALID_FILE,
				     "checksum > file length: %u",
				     end);
			return FALSE;
		}

		/* verify checksum */
		if ((flags & DFU_FIRMWARE_PARSE_FLAG_NO_CRC_TEST) == 0) {
			checksum = 0;
			for (i = offset + 1; i < end + 2; i += 2) {
				data_tmp = dfu_firmware_ihex_parse_uint8 (in_buffer, i);
				checksum += data_tmp;
			}
			if (checksum != 0)  {
				g_set_error_literal (error,
						     DFU_ERROR,
						     DFU_ERROR_INVALID_FILE,
						     "invalid record checksum");
				return FALSE;
			}
		}

		/* process different record types */
		switch (type) {
		case DFU_INHX32_RECORD_TYPE_DATA:
			/* if not contiguous with previous record */
			if ((addr_high + addr_low) != addr32) {
				if (addr32 == 0x0) {
					g_debug ("base address %04x", addr_low);
					dfu_element_set_address (element, addr_low);
				}
				addr32 = addr_high + addr_low;
			}

			/* parse bytes from line */
			for (i = offset + 9; i < end; i += 2) {
				/* any holes in the hex record */
				len_tmp = addr32 - addr32_last;
				if (addr32_last > 0x0 && len_tmp > 1) {
					for (j = 1; j < len_tmp; j++) {
						g_debug ("filling address 0x%04x",
							 addr32_last + j);
						/* although 0xff might be clearer,
						 * we can't write 0xffff to pic14 */
						g_string_append_c (string, 0x00);
					}
				}
				/* write into buf */
				data_tmp = dfu_firmware_ihex_parse_uint8 (in_buffer, i);
				g_string_append_c (string, data_tmp);
				g_debug ("writing address 0x%04x", addr32);
				addr32_last = addr32++;
			}
			break;
		case DFU_INHX32_RECORD_TYPE_EOF:
			break;
		case DFU_INHX32_RECORD_TYPE_EXTENDED:
			addr_high = dfu_firmware_ihex_parse_uint16 (in_buffer, offset+9);
			g_error ("set base address %x", addr_high);
			addr_high <<= 16;
			addr32 = addr_high + addr_low;
			break;
		default:
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_INVALID_FILE,
				     "invalid ihex record type %i",
				     type);
			return FALSE;
		}

		/* ignore any line return */
		offset = end + 2;
		for (; offset < len_in; offset++) {
			if (in_buffer[offset] != '\n' &&
			    in_buffer[offset] != '\r')
				break;
		}
	}

	/* add single image */
	contents = g_bytes_new (string->str, string->len);
	dfu_element_set_contents (element, contents);
	dfu_image_add_element (image, element);
	dfu_firmware_add_image (firmware, image);
	dfu_firmware_set_format (firmware, DFU_FIRMWARE_FORMAT_INTEL_HEX);
	return TRUE;
}

/**
 * dfu_firmware_write_data_ihex_element:
 **/
static gboolean
dfu_firmware_write_data_ihex_element (DfuElement *element,
				      GString *str,
				      GError **error)
{
	GBytes *contents;
	const guint8 *data;
	const guint chunk_size = 16;
	gsize len;
	guint chunk_len;
	guint i;
	guint j;

	/* get number of chunks */
	contents = dfu_element_get_contents (element);
	data = g_bytes_get_data (contents, &len);
	for (i = 0; i < len; i += chunk_size) {
		guint8 checksum = 0;

		/* length, 16-bit address, type */
		chunk_len = MIN (len - i, 16);
		g_string_append_printf (str, ":%02X%04X%02X",
					chunk_len,
					dfu_element_get_address (element) + i,
					DFU_INHX32_RECORD_TYPE_DATA);
		for (j = 0; j < chunk_len; j++)
			g_string_append_printf (str, "%02X", data[i+j]);

		/* add checksum */
		for (j = 0; j < (chunk_len * 2) + 8; j++)
			checksum += str->str[str->len - (j + 1)];
		g_string_append_printf (str, "%02X\n", checksum);
	}
	return TRUE;
}

/**
 * dfu_firmware_write_data_ihex:
 **/
static GBytes *
dfu_firmware_write_data_ihex (DfuFirmware *firmware, GError **error)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	DfuElement *element;
	DfuImage *image;
	GPtrArray *elements;
	guint i;
	guint j;
	g_autoptr(GString) str = NULL;

	/* write all the element data */
	str = g_string_new ("");
	for (i = 0; i < priv->images->len; i++) {
		image = g_ptr_array_index (priv->images, i);
		elements = dfu_image_get_elements (image);
		for (j = 0; j < elements->len; j++) {
			element = g_ptr_array_index (elements, j);
			if (!dfu_firmware_write_data_ihex_element (element,
								   str,
								   error))
				return NULL;
		}
	}

	/* add EOF */
	g_string_append_printf (str, ":000000%02XFF\n", DFU_INHX32_RECORD_TYPE_EOF);
	return g_bytes_new (str->str, str->len);
}

/**
 * dfu_firmware_add_binary:
 **/
static gboolean
dfu_firmware_add_binary (DfuFirmware *firmware, GBytes *bytes, GError **error)
{
	g_autoptr(DfuElement) element = NULL;
	g_autoptr(DfuImage) image = NULL;
	image = dfu_image_new ();
	element = dfu_element_new ();
	dfu_element_set_contents (element, bytes);
	dfu_image_add_element (image, element);
	dfu_firmware_add_image (firmware, image);
	return TRUE;
}

/* DfuSe header */
typedef struct __attribute__((packed)) {
	guint8		 sig[5];
	guint8		 ver;
	guint32		 image_size;
	guint8		 targets;
} DfuSePrefix;

/**
 * dfu_firmware_add_dfuse:
 **/
static gboolean
dfu_firmware_add_dfuse (DfuFirmware *firmware, GBytes *bytes, GError **error)
{
	DfuSePrefix *prefix;
	gsize len;
	guint32 offset = sizeof(DfuSePrefix);
	guint8 *data;
	guint i;

	/* check the prefix (BE) */
	data = (guint8 *) g_bytes_get_data (bytes, &len);
	prefix = (DfuSePrefix *) data;
	if (memcmp (prefix->sig, "DfuSe", 5) != 0) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "invalid DfuSe prefix");
		return FALSE;
	}

	/* check the version */
	if (prefix->ver != 0x01) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "invalid DfuSe version, got %02x",
			     prefix->ver);
		return FALSE;
	}

	/* check image size */
	if (GUINT32_FROM_LE (prefix->image_size) != len) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "invalid DfuSe image size, "
			     "got %" G_GUINT32_FORMAT ", "
			     "expected %" G_GSIZE_FORMAT,
			     GUINT32_FROM_LE (prefix->image_size),
			     len);
		return FALSE;
	}

	/* parse the image targets */
	len -= sizeof(DfuSePrefix);
	for (i = 0; i < prefix->targets; i++) {
		guint consumed;
		g_autoptr(DfuImage) image = NULL;
		image = dfu_image_from_dfuse (data + offset, len,
					      &consumed, error);
		if (image == NULL)
			return FALSE;
		dfu_firmware_add_image (firmware, image);
		offset += consumed;
		len -= consumed;
	}
	return TRUE;
}

/**
 * dfu_firmware_write_data_dfuse:
 **/
static GBytes *
dfu_firmware_write_data_dfuse (DfuFirmware *firmware, GError **error)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	DfuSePrefix *prefix;
	guint i;
	guint32 image_size_total = 0;
	guint32 offset = sizeof (DfuSePrefix);
	guint8 *buf;
	g_autoptr(GPtrArray) dfuse_images = NULL;

	/* get all the image data */
	dfuse_images = g_ptr_array_new_with_free_func ((GDestroyNotify) g_bytes_unref);
	for (i = 0; i < priv->images->len; i++) {
		DfuImage *im = g_ptr_array_index (priv->images, i);
		GBytes *contents;
		contents = dfu_image_to_dfuse (im);
		image_size_total += g_bytes_get_size (contents);
		g_ptr_array_add (dfuse_images, contents);
	}
	g_debug ("image_size_total: %i", image_size_total);

	buf = g_malloc0 (sizeof (DfuSePrefix) + image_size_total);

	/* DfuSe header */
	prefix = (DfuSePrefix *) buf;
	memcpy (prefix->sig, "DfuSe", 5);
	prefix->ver = 0x01;
	prefix->image_size = offset + image_size_total;
	prefix->targets = priv->images->len;

	/* copy images */
	for (i = 0; i < dfuse_images->len; i++) {
		GBytes *contents = g_ptr_array_index (dfuse_images, i);
		gsize length;
		const guint8 *data;
		data = g_bytes_get_data (contents, &length);
		memcpy (buf + offset, data, length);
		offset += length;
	}

	/* return blob */
	return g_bytes_new_take (buf, sizeof (DfuSePrefix) + image_size_total);
}

/**
 * dfu_firmware_parse_metadata:
 *
 * The representation in memory is as follows:
 *
 * uint16      signature='MD'
 * uint8       number_of_keys
 * uint8       number_of_keys
 * uint8       key(n)_length
 * ...         key(n) (no NUL)
 * uint8       value(n)_length
 * ...         value(n) (no NUL)
 * <existing DFU footer>
 **/
static gboolean
dfu_firmware_parse_metadata (DfuFirmware *firmware,
			     const guint8 *data,
			     guint data_length,
			     guint32 footer_size,
			     GError **error)
{
	guint i;
	guint idx = data_length - footer_size + 2;
	guint kvlen;
	guint number_keys;

	/* not big enough */
	if (footer_size <= 0x10)
		return TRUE;

	/* signature invalid */
	if (memcmp (&data[data_length - footer_size], "MD", 2) != 0)
		return TRUE;

	/* parse key=value store */
	number_keys = data[idx++];
	for (i = 0; i < number_keys; i++) {
		g_autofree gchar *key = NULL;
		g_autofree gchar *value = NULL;

		/* parse key */
		kvlen = data[idx++];
		if (kvlen > 233) {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "metadata table corrupt, key=%i",
				     kvlen);
			return FALSE;
		}
		if (idx + kvlen + 0x10 > data_length) {
			g_set_error_literal (error,
					     DFU_ERROR,
					     DFU_ERROR_INTERNAL,
					     "metadata table corrupt");
			return FALSE;
		}
		key = g_strndup ((const gchar *) data + idx, kvlen);
		idx += kvlen;

		/* parse value */
		kvlen = data[idx++];
		if (kvlen > 233) {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "metadata table corrupt, value=%i",
				     kvlen);
			return FALSE;
		}
		if (idx + kvlen + 0x10 > data_length) {
			g_set_error_literal (error,
					     DFU_ERROR,
					     DFU_ERROR_INTERNAL,
					     "metadata table corrupt");
			return FALSE;
		}
		value = g_strndup ((const gchar *) data + idx, kvlen);
		idx += kvlen;
		dfu_firmware_set_metadata (firmware, key, value);
	}
	return TRUE;
}

/**
 * dfu_firmware_parse_data:
 * @firmware: a #DfuFirmware
 * @bytes: raw firmware data
 * @flags: optional flags, e.g. %DFU_FIRMWARE_PARSE_FLAG_NO_CRC_TEST
 * @error: a #GError, or %NULL
 *
 * Parses firmware data which may have an optional DFU suffix.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
gboolean
dfu_firmware_parse_data (DfuFirmware *firmware, GBytes *bytes,
			 DfuFirmwareParseFlags flags, GError **error)
{
	DfuFirmwareFooter *ftr;
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	const gchar *cipher_str;
	gsize len;
	guint32 crc_new;
	guint32 size;
	guint8 *data;
	g_autoptr(GBytes) contents = NULL;

	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), FALSE);
	g_return_val_if_fail (bytes != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* sanity check */
	g_assert_cmpint(sizeof(DfuSePrefix), ==, 11);

	/* set defaults */
	priv->vid = 0xffff;
	priv->pid = 0xffff;
	priv->release = 0xffff;

	/* this is ihex */
	data = (guint8 *) g_bytes_get_data (bytes, &len);
	if (data[0] == ':')
		return dfu_firmware_add_ihex (firmware, bytes, flags, error);

	/* too small to be a DFU file */
	if (len < 16) {
		priv->format = DFU_FIRMWARE_FORMAT_RAW;
		return dfu_firmware_add_binary (firmware, bytes, error);
	}

	/* check for DFU signature */
	ftr = (DfuFirmwareFooter *) &data[len-sizeof(DfuFirmwareFooter)];
	if (memcmp (ftr->sig, "UFD", 3) != 0) {
		priv->format = DFU_FIRMWARE_FORMAT_RAW;
		return dfu_firmware_add_binary (firmware, bytes, error);
	}

	/* check version */
	priv->format = GUINT16_FROM_LE (ftr->ver);
	if ((flags & DFU_FIRMWARE_PARSE_FLAG_NO_VERSION_TEST) == 0) {
		if (priv->format != DFU_FIRMWARE_FORMAT_DFU_1_0 &&
		    priv->format != DFU_FIRMWARE_FORMAT_DFUSE) {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "version check failed, got %04x",
				     priv->format);
			return FALSE;
		}
	}

	/* verify the checksum */
	priv->crc = GUINT32_FROM_LE (ftr->crc);
	if ((flags & DFU_FIRMWARE_PARSE_FLAG_NO_CRC_TEST) == 0) {
		crc_new = GUINT32_FROM_LE (dfu_firmware_generate_crc32 (data, len - 4));
		if (priv->crc != crc_new) {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "CRC failed, expected %04x, got %04x",
				     crc_new, GUINT32_FROM_LE (ftr->crc));
			return FALSE;
		}
	}

	/* set from footer */
	dfu_firmware_set_vid (firmware, GUINT16_FROM_LE (ftr->vid));
	dfu_firmware_set_pid (firmware, GUINT16_FROM_LE (ftr->pid));
	dfu_firmware_set_release (firmware, GUINT16_FROM_LE (ftr->release));

	/* check reported length */
	size = GUINT16_FROM_LE (ftr->len);
	if (size > len) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "reported firmware size %04x larger than file %04x",
			     (guint) size, (guint) len);
		return FALSE;
	}

	/* parse the optional metadata segment */
	if ((flags & DFU_FIRMWARE_PARSE_FLAG_NO_METADATA) == 0) {
		if (!dfu_firmware_parse_metadata (firmware, data, len, size, error))
			return FALSE;
	}

	/* set this automatically */
	cipher_str = dfu_firmware_get_metadata (firmware, DFU_METADATA_KEY_CIPHER_KIND);
	if (cipher_str != NULL) {
		if (g_strcmp0 (cipher_str, "XTEA") == 0)
			priv->cipher_kind = DFU_CIPHER_KIND_XTEA;
		else
			g_warning ("Unknown CipherKind: %s", cipher_str);
	}

	/* parse DfuSe prefix */
	contents = g_bytes_new_from_bytes (bytes, 0, len - size);
	if (priv->format == DFU_FIRMWARE_FORMAT_DFUSE)
		return dfu_firmware_add_dfuse (firmware, contents, error);

	/* just copy old-plain DFU file */
	return dfu_firmware_add_binary (firmware, contents, error);
}

/**
 * dfu_firmware_parse_file:
 * @firmware: a #DfuFirmware
 * @file: a #GFile to load and parse
 * @flags: optional flags, e.g. %DFU_FIRMWARE_PARSE_FLAG_NO_CRC_TEST
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Parses a DFU firmware, which may contain an optional footer.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
gboolean
dfu_firmware_parse_file (DfuFirmware *firmware, GFile *file,
			 DfuFirmwareParseFlags flags,
			 GCancellable *cancellable, GError **error)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	gchar *contents = NULL;
	gsize length = 0;
	g_autofree gchar *basename = NULL;
	g_autoptr(GBytes) bytes = NULL;

	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), FALSE);
	g_return_val_if_fail (G_IS_FILE (file), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* guess cipher kind based on file extension */
	basename = g_file_get_basename (file);
	if (g_str_has_suffix (basename, ".xdfu"))
		priv->cipher_kind = DFU_CIPHER_KIND_XTEA;

	if (!g_file_load_contents (file, cancellable, &contents,
				   &length, NULL, error))
		return FALSE;
	bytes = g_bytes_new_take (contents, length);
	return dfu_firmware_parse_data (firmware, bytes, flags, error);
}

/**
 * dfu_firmware_get_metadata:
 * @firmware: a #DfuFirmware
 * @key: metadata string key
 *
 * Gets metadata from the store with a specific key.
 *
 * Return value: the metadata value, or %NULL for unset
 *
 * Since: 0.5.4
 **/
const gchar *
dfu_firmware_get_metadata (DfuFirmware *firmware, const gchar *key)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	return g_hash_table_lookup (priv->metadata, key);
}

/**
 * dfu_firmware_set_metadata:
 * @firmware: a #DfuFirmware
 * @key: metadata string key
 * @value: metadata string value
 *
 * Sets a metadata value with a specific key.
 *
 * Since: 0.5.4
 **/
void
dfu_firmware_set_metadata (DfuFirmware *firmware, const gchar *key, const gchar *value)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_debug ("adding metadata %s=%s", key, value);
	g_hash_table_insert (priv->metadata, g_strdup (key), g_strdup (value));
}

/**
 * dfu_firmware_remove_metadata:
 * @firmware: a #DfuFirmware
 * @key: metadata string key
 *
 * Removes a metadata item from the store
 *
 * Since: 0.5.4
 **/
void
dfu_firmware_remove_metadata (DfuFirmware *firmware, const gchar *key)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_debug ("removing metadata %s", key);
	g_hash_table_remove (priv->metadata, key);
}

/**
 * dfu_firmware_build_metadata_table:
 **/
static GBytes *
dfu_firmware_build_metadata_table (DfuFirmware *firmware, GError **error)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	GList *l;
	guint8 mdbuf[239];
	guint idx = 0;
	guint number_keys;
	g_autoptr(GList) keys = NULL;

	/* no metadata */
	if (g_hash_table_size (priv->metadata) == 0)
		return g_bytes_new (NULL, 0);

	/* check the number of keys */
	keys = g_hash_table_get_keys (priv->metadata);
	number_keys = g_list_length (keys);
	if (number_keys > 59) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_NOT_SUPPORTED,
			     "too many metadata keys (%i)",
			     number_keys);
		return NULL;
	}

	/* write the signature */
	mdbuf[idx++] = 'M';
	mdbuf[idx++] = 'D';
	mdbuf[idx++] = number_keys;
	for (l = keys; l != NULL; l = l->next) {
		const gchar *key;
		const gchar *value;
		guint key_len;
		guint value_len;

		/* check key and value length */
		key = l->data;
		key_len = strlen (key);
		if (key_len > 233) {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_SUPPORTED,
				     "metdata key too long: %s",
				     key);
			return NULL;
		}
		value = g_hash_table_lookup (priv->metadata, key);
		value_len = strlen (value);
		if (value_len > 233) {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_SUPPORTED,
				     "value too long: %s",
				     value);
			return NULL;
		}

		/* do we still have space? */
		if (idx + key_len + value_len + 2 > sizeof(mdbuf)) {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_SUPPORTED,
				     "not enough space in metadata table, "
				     "already used %i bytes", idx);
			return NULL;
		}

		/* write the key */
		mdbuf[idx++] = key_len;
		memcpy(mdbuf + idx, key, key_len);
		idx += key_len;

		/* write the value */
		mdbuf[idx++] = value_len;
		memcpy(mdbuf + idx, value, value_len);
		idx += value_len;
	}
	g_debug ("metadata table was %i/%i bytes", idx, (guint) sizeof(mdbuf));
	return g_bytes_new (mdbuf, idx);
}

/**
 * dfu_firmware_add_footer:
 **/
static GBytes *
dfu_firmware_add_footer (DfuFirmware *firmware, GBytes *contents, GError **error)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	DfuFirmwareFooter *ftr;
	const guint8 *data_bin;
	const guint8 *data_md;
	gsize length_bin = 0;
	gsize length_md = 0;
	guint8 *buf;
	g_autoptr(GBytes) metadata_table = NULL;

	/* get any file metadata */
	metadata_table = dfu_firmware_build_metadata_table (firmware, error);
	if (metadata_table == NULL)
		return NULL;
	data_md = g_bytes_get_data (metadata_table, &length_md);

	/* add the raw firmware data */
	data_bin = g_bytes_get_data (contents, &length_bin);
	buf = g_malloc0 (length_bin + length_md + 0x10);
	memcpy (buf + 0, data_bin, length_bin);

	/* add the metadata table */
	memcpy (buf + length_bin, data_md, length_md);

	/* set up LE footer */
	ftr = (DfuFirmwareFooter *) (buf + length_bin + length_md);
	ftr->release = GUINT16_TO_LE (priv->release);
	ftr->pid = GUINT16_TO_LE (priv->pid);
	ftr->vid = GUINT16_TO_LE (priv->vid);
	ftr->ver = GUINT16_TO_LE (priv->format);
	ftr->len = GUINT16_TO_LE (0x10 + length_md);
	memcpy(ftr->sig, "UFD", 3);
	ftr->crc = dfu_firmware_generate_crc32 (buf, length_bin + length_md + 12);

	/* return all data */
	return g_bytes_new_take (buf, length_bin + length_md + 0x10);
}

/**
 * dfu_firmware_write_data:
 * @firmware: a #DfuFirmware
 * @error: a #GError, or %NULL
 *
 * Writes DFU data to a data blob with a DFU-specific footer.
 *
 * Return value: (transfer none): firmware data
 *
 * Since: 0.5.4
 **/
GBytes *
dfu_firmware_write_data (DfuFirmware *firmware, GError **error)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	DfuImage *image;

	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* at least one image */
	if (priv->images == 0) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "no image data to write");
		return NULL;
	}

	/* DFU only supports one image */
	if (priv->images->len > 1 &&
	    priv->format != DFU_FIRMWARE_FORMAT_DFUSE) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "only DfuSe format supports multiple images (%i)",
			     priv->images->len);
		return NULL;
	}

	/* raw */
	if (priv->format == DFU_FIRMWARE_FORMAT_RAW) {
		GBytes *contents;
		DfuElement *element;
		image = dfu_firmware_get_image_default (firmware);
		g_assert (image != NULL);
		element = dfu_image_get_element (image, 0);
		if (element == NULL) {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_FOUND,
				     "no firmware element data to write");
			return NULL;
		}
		contents = dfu_element_get_contents (element);
		return g_bytes_ref (contents);
	}

	/* plain-old DFU */
	if (priv->format == DFU_FIRMWARE_FORMAT_DFU_1_0) {
		GBytes *contents;
		DfuElement *element;
		image = dfu_firmware_get_image_default (firmware);
		g_assert (image != NULL);
		element = dfu_image_get_element (image, 0);
		if (element == NULL) {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_FOUND,
				     "no firmware element data to write");
			return NULL;
		}
		contents = dfu_element_get_contents (element);
		g_assert (contents != NULL);
		return dfu_firmware_add_footer (firmware, contents, error);
	}

	/* DfuSe */
	if (priv->format == DFU_FIRMWARE_FORMAT_DFUSE) {
		g_autoptr(GBytes) contents = NULL;
		contents = dfu_firmware_write_data_dfuse (firmware, error);
		if (contents == NULL)
			return NULL;
		return dfu_firmware_add_footer (firmware, contents, error);
	}

	/* Intel HEX */
	if (priv->format == DFU_FIRMWARE_FORMAT_INTEL_HEX)
		return dfu_firmware_write_data_ihex (firmware, error);

	/* invalid */
	g_set_error (error,
		     DFU_ERROR,
		     DFU_ERROR_INTERNAL,
		     "invalid format for write (0x%04x)",
		     priv->format);
	return NULL;
}

/**
 * dfu_firmware_write_file:
 * @firmware: a #DfuFirmware
 * @file: a #GFile
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Writes a DFU firmware with the optional footer.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
gboolean
dfu_firmware_write_file (DfuFirmware *firmware, GFile *file,
			 GCancellable *cancellable, GError **error)
{
	const guint8 *data;
	gsize length = 0;
	g_autoptr(GBytes) bytes = NULL;

	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), FALSE);
	g_return_val_if_fail (G_IS_FILE (file), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* get blob */
	bytes = dfu_firmware_write_data (firmware, error);
	if (bytes == NULL)
		return FALSE;

	/* save to firmware */
	data = g_bytes_get_data (bytes, &length);
	return g_file_replace_contents (file,
					(const gchar *) data,
					length,
					NULL,
					FALSE,
					G_FILE_CREATE_NONE,
					NULL,
					cancellable,
					error);
}

/**
 * dfu_firmware_to_string:
 * @firmware: a #DfuFirmware
 *
 * Returns a string representaiton of the object.
 *
 * Return value: NULL terminated string, or %NULL for invalid
 *
 * Since: 0.5.4
 **/
gchar *
dfu_firmware_to_string (DfuFirmware *firmware)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	DfuImage *image;
	GList *l;
	GString *str;
	guint i;
	g_autoptr(GList) keys = NULL;

	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), NULL);

	str = g_string_new ("");
	g_string_append_printf (str, "vid:         0x%04x\n", priv->vid);
	g_string_append_printf (str, "pid:         0x%04x\n", priv->pid);
	g_string_append_printf (str, "release:     0x%04x\n", priv->release);
	g_string_append_printf (str, "crc:         0x%08x\n", priv->crc);
	g_string_append_printf (str, "format:      %s [0x%04x]\n",
				dfu_firmware_format_to_string (priv->format),
				priv->format);
	g_string_append_printf (str, "cipher:      %s\n",
				dfu_cipher_kind_to_string (priv->cipher_kind));

	/* print metadata */
	keys = g_hash_table_get_keys (priv->metadata);
	for (l = keys; l != NULL; l = l->next) {
		const gchar *key;
		const gchar *value;
		key = l->data;
		value = g_hash_table_lookup (priv->metadata, key);
		g_string_append_printf (str, "metadata:    %s=%s\n", key, value);
	}

	/* print images */
	for (i = 0; i < priv->images->len; i++) {
		g_autofree gchar *tmp = NULL;
		image = g_ptr_array_index (priv->images, i);
		tmp = dfu_image_to_string (image);
		g_string_append_printf (str, "= IMAGE %i =\n", i);
		g_string_append_printf (str, "%s\n", tmp);
	}

	g_string_truncate (str, str->len - 1);
	return g_string_free (str, FALSE);
}

/**
 * dfu_firmware_format_to_string:
 * @format: a #DfuFirmwareFormat, e.g. %DFU_FIRMWARE_FORMAT_DFU_1_0
 *
 * Returns a string representaiton of the format.
 *
 * Return value: NULL terminated string, or %NULL for invalid
 *
 * Since: 0.5.4
 **/
const gchar *
dfu_firmware_format_to_string (DfuFirmwareFormat format)
{
	if (format == DFU_FIRMWARE_FORMAT_RAW)
		return "RAW";
	if (format == DFU_FIRMWARE_FORMAT_DFU_1_0)
		return "DFU";
	if (format == DFU_FIRMWARE_FORMAT_DFUSE)
		return "DfuSe";
	if (format == DFU_FIRMWARE_FORMAT_INTEL_HEX)
		return "IHEX";
	return NULL;
}

/**
 * dfu_firmware_get_cipher_kind:
 * @firmware: a #DfuFirmware
 *
 * Returns the kind of cipher used by the firmware file.
 *
 * NOTE: this value is based on a heuristic, and may not be accurate.
 * The value %DFU_CIPHER_KIND_NONE will be returned when the cipher
 * is not recognised.
 *
 * Return value: NULL terminated string, or %NULL for invalid
 *
 * Since: 0.5.4
 **/
DfuCipherKind
dfu_firmware_get_cipher_kind (DfuFirmware *firmware)
{
	DfuFirmwarePrivate *priv = GET_PRIVATE (firmware);
	g_return_val_if_fail (DFU_IS_FIRMWARE (firmware), 0);
	return priv->cipher_kind;
}
