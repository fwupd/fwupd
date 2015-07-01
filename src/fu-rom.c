/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
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

#include "config.h"

#include <fwupd.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>

#include "fu-cleanup.h"
#include "fu-rom.h"

static void fu_rom_finalize			 (GObject *object);

#define FU_ROM_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), FU_TYPE_ROM, FuRomPrivate))

/**
 * FuRomPrivate:
 *
 * Private #FuRom data
 **/
struct _FuRomPrivate
{
	GChecksum			*checksum_wip;
	GInputStream			*stream;
	FuRomKind			 kind;
	gchar				*version;
	guint16				 vendor;
	guint16				 model;
	guint16				 rom_length;
};

G_DEFINE_TYPE (FuRom, fu_rom, G_TYPE_OBJECT)

/**
 * fu_rom_kind_to_string:
 **/
const gchar *
fu_rom_kind_to_string (FuRomKind kind)
{
	if (kind == FU_ROM_KIND_UNKNOWN)
		return "unknown";
	if (kind == FU_ROM_KIND_ATI)
		return "ati";
	if (kind == FU_ROM_KIND_NVIDIA)
		return "nvidia";
	if (kind == FU_ROM_KIND_INTEL)
		return "intel";
	if (kind == FU_ROM_KIND_PCI)
		return "pci";
	return NULL;
}

/**
 * fu_rom_strstr_bin:
 **/
static guint8 *
fu_rom_strstr_bin (guint8 *haystack, gsize haystack_len, const gchar *needle)
{
	guint i;
	guint needle_len = strlen (needle);
	for (i = 0; i < haystack_len - needle_len; i++) {
		if (memcmp (haystack + i, needle, needle_len) == 0)
			return &haystack[i];
	}
	return NULL;
}

/**
 * fu_rom_blank_serial_numbers:
 **/
static guint
fu_rom_blank_serial_numbers (guint8 *buffer, guint buffer_sz)
{
	guint i;
	for (i = 0; i < buffer_sz; i++) {
		if (buffer[i] == 0xff ||
		    buffer[i] == '\0' ||
		    buffer[i] == '\n' ||
		    buffer[i] == '\r')
			break;
		buffer[i] = '\0';
	}
	return i;
}

/**
 * fu_rom_find_and_blank_serial_numbers:
 **/
static void
fu_rom_find_and_blank_serial_numbers (guint8 *buffer, guint buffer_sz)
{
	guint8 *tmp;
	guint len;

	if (buffer_sz < 4)
		return;
	tmp = fu_rom_strstr_bin (buffer, buffer_sz, "PPID");
	if (tmp != NULL) {
		len = fu_rom_blank_serial_numbers (tmp, buffer_sz - (tmp - buffer));
		g_debug ("cleared %i chars", len);
	}
}

/* data from http://resources.infosecinstitute.com/pci-expansion-rom/ */
typedef struct {
	guint8	 rom_signature[2];	/* 0x55 0xaa */
	guint8	 rom_size;		/* of 512 bytes */
	guint8	 entry_point[3];
	guint8	 reserved[18];
	guint16	 cpi_ptr;
} FuRomPciHeader;

typedef struct {
	guint8	 signature[4];		/* PCIR */
	guint16	 vendor_id;
	guint16	 device_id;
	guint16	 device_list_ptr;
	guint16	 pci_data_length;
	guint8	 pci_data_revision;	/* expected 3 */
	guint8	 class_code[3];
	guint16	 image_len;		/* of 512 bytes */
	guint16	 revision_level;
	guint8	 code_type;
	guint8	 last_image_indicator;	/* 1 = last image */
	guint16	 max_runtime_len;	/* of 512 bytes */
	guint16	 config_header_ptr;
	guint16	 dmtf_clp_entry_point;
} FuRomPciData;

/**
 * fu_rom_pci_get_data:
 **/
static FuRomPciData *
fu_rom_pci_get_data (FuRomPciHeader *hdr, gssize sz)
{
	FuRomPciData *dtr;

	/* check valid */
	if (hdr == NULL ||
	    hdr->cpi_ptr == 0x0000 ||
	    hdr->cpi_ptr > hdr->rom_size * 512) {
		g_debug ("No PCI DATA");
		return NULL;
	}

	/* gahh, CPI is out of the first chunk */
	if (hdr->cpi_ptr > sz) {
		g_debug ("No available PCI DATA");
		return NULL;
	}

	/* check signature */
	dtr = (FuRomPciData *) &((guint8 *)hdr)[hdr->cpi_ptr];
	if (dtr == NULL ||
	    dtr->signature == NULL ||
	    memcmp (dtr->signature, "PCIR", 4) != 0) {
		g_debug ("Not PCI DATA");
		return NULL;
	}
	g_debug ("VendorID:  0x%04x", dtr->vendor_id);
	g_debug ("DeviceID:  0x%04x", dtr->device_id);
	g_debug ("DevList:   0x%04x", dtr->device_list_ptr);
	g_debug ("DataLen:   0x%04x", dtr->pci_data_length);
	g_debug ("DataRev:   0x%04x", dtr->pci_data_revision);
	g_debug ("ImageLen:  0x%04x", dtr->image_len * 512);
	g_debug ("RevLevel:  0x%04x", dtr->revision_level);
	g_debug ("CodeType:  0x%02x", dtr->code_type);
	g_debug ("LastImg:   0x%02x [%s]", dtr->last_image_indicator,
		 dtr->last_image_indicator == 0x80 ? "yes" : "no");
	g_debug ("MaxRunLen: 0x%04x", dtr->max_runtime_len);
	g_debug ("ConfigHdr: 0x%04x", dtr->config_header_ptr);
	g_debug ("ClpPtr:    0x%04x", dtr->dmtf_clp_entry_point);
	return dtr;
}

/**
 * fu_rom_pci_get_header:
 **/
static FuRomPciHeader *
fu_rom_pci_get_header (guint8 *buffer, gssize sz)
{
	FuRomPciHeader *hdr = (FuRomPciHeader *) buffer;
	guint i;
	_cleanup_string_free_ GString *str = NULL;

	/* check signature */
	if (hdr == NULL ||
	    hdr->rom_signature == NULL ||
	    memcmp (hdr->rom_signature, "\x55\xaa", 2) != 0) {
		g_debug ("Not PCI ROM");
		return NULL;
	}

	/* print details about the header */
	g_debug ("RomSize:   0x%04x", hdr->rom_size * 512);
	g_debug ("EntryPnt:  0x%02x%02x%02x",
		 hdr->entry_point[0],
		 hdr->entry_point[1],
		 hdr->entry_point[2]);
	str = g_string_new ("");
	for (i = 0; i < 18; i++) {
		gchar tmp = '?';
		if (g_ascii_isprint (hdr->reserved[i]))
			tmp = hdr->reserved[i];
		g_string_append_printf (str, "%02x [%c] ",
					hdr->reserved[i], tmp);
	}
	g_debug ("Reserved:  %s", str->str);
	g_debug ("CpiPtr:    0x%04x", hdr->cpi_ptr);
	return hdr;
}

/**
 * fu_rom_load_file:
 **/
gboolean
fu_rom_load_file (FuRom *rom, GFile *file, GCancellable *cancellable, GError **error)
{
	FuRomPrivate *priv = rom->priv;
	FuRomPciData *dtr = NULL;
	FuRomPciHeader *hdr = NULL;
	const guint block_sz = 0x4000;
	guint8 buffer[block_sz];
	gchar *str;
	guint hdr_sz = 0;
	guint i;
	gssize sz;
	_cleanup_free_ gchar *fn = NULL;
	_cleanup_error_free_ GError *error_local = NULL;
	_cleanup_object_unref_ GFileOutputStream *output_stream = NULL;

	g_return_val_if_fail (FU_IS_ROM (rom), FALSE);

	/* open file */
	priv->stream = G_INPUT_STREAM (g_file_read (file, cancellable, &error_local));
	if (priv->stream == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_AUTH_FAILED,
				     error_local->message);
		return FALSE;
	}

	/* we have to enable the read for devices */
	fn = g_file_get_path (file);
	if (g_str_has_prefix (fn, "/sys")) {
		output_stream = g_file_replace (file, NULL, FALSE, G_FILE_CREATE_NONE,
						cancellable, error);
		if (output_stream == NULL)
			return FALSE;
		if (g_output_stream_write (G_OUTPUT_STREAM (output_stream), "1", 1,
					   cancellable, error) < 0)
			return FALSE;
	}

	/* read out the header */
	sz = g_input_stream_read (priv->stream, buffer, block_sz,
				  cancellable, error);
	if (sz < 0)
		return FALSE;
	if (sz < 1024) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Firmware too small: %" G_GSIZE_FORMAT " bytes", sz);
		return FALSE;
	}

	/* detect signed header and skip to option ROM */
	if (memcmp (buffer, "NVGI", 4) == 0)
		hdr_sz = GUINT16_FROM_BE (buffer[0x15]);

	/* firmware magic bytes */
	if (memcmp (buffer + hdr_sz, "\x55\xaa", 2) == 0) {

		/* find generic PCI option ROM */
		hdr = fu_rom_pci_get_header (&buffer[hdr_sz], sz - hdr_sz);
		if (hdr != NULL) {
			priv->rom_length = hdr->rom_size * 512;
			dtr = fu_rom_pci_get_data (hdr, sz - hdr_sz);
			if (dtr != NULL) {
				priv->vendor = dtr->vendor_id;
				priv->model = dtr->device_id;
				priv->kind = FU_ROM_KIND_PCI;
			}
		}

		/* detect intel header */
		if (memcmp (hdr->reserved, "00000000000", 11) == 0)
			hdr_sz = (buffer[0x1b] << 8) + buffer[0x1a];

		if (memcmp (buffer + hdr_sz + 0x04, "K740", 4) == 0) {
			priv->kind = FU_ROM_KIND_NVIDIA;
		} else if (memcmp (buffer + hdr_sz, "$VBT", 4) == 0) {
			priv->kind = FU_ROM_KIND_INTEL;
			/* see drivers/gpu/drm/i915/intel_bios.h */
			hdr_sz += (buffer[hdr_sz + 23] << 8) + buffer[hdr_sz + 22];
		} else if (memcmp(buffer + 0x30, " 761295520", 10) == 0) {
			priv->kind = FU_ROM_KIND_ATI;
		}
	} else {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Failed to detect firmware header [%02x%02x]",
			     buffer[0], buffer[1]);
		return FALSE;
	}

	/* nothing */
	if (priv->kind == FU_ROM_KIND_UNKNOWN) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "Failed to detect firmware kind");
		return FALSE;
	}

	/* find version string */
	switch (priv->kind) {
	case FU_ROM_KIND_PCI:

		/* ARC storage */
		if (memcmp (hdr->reserved, "\0\0ARC", 5) == 0) {
			str = (gchar *) fu_rom_strstr_bin (buffer, sz, "BIOS: ");
			if (str != NULL)
				priv->version = g_strdup (str + 6);
		}

	case FU_ROM_KIND_NVIDIA:

		/* static location for some firmware */
		if (memcmp (buffer + hdr_sz + 0x0d7, "Version ", 8) == 0)
			priv->version = g_strdup ((gchar *) &buffer[0x0d7 + hdr_sz + 8]);
		else if (memcmp (buffer + hdr_sz + 0x155, "Version ", 8) == 0)
			priv->version = g_strdup ((gchar *) &buffer[0x155 + hdr_sz + 8]);

		/* usual search string */
		if (priv->version == NULL) {
			str = (gchar *) fu_rom_strstr_bin (buffer, sz, "Version ");
			if (str != NULL)
				priv->version = g_strdup (str + 8);
		}

		/* broken */
		if (priv->version == NULL) {
			str = (gchar *) fu_rom_strstr_bin (buffer, sz, "Vension:");
			if (str != NULL)
				priv->version = g_strdup (str + 8);
		}
		if (priv->version == NULL) {
			str = (gchar *) fu_rom_strstr_bin (buffer, sz, "Version");
			if (str != NULL)
				priv->version = g_strdup (str + 7);
		}

		/* fallback to VBIOS */
		if (priv->version == NULL &&
		    memcmp (buffer + hdr_sz + 0xfa, "VBIOS Ver", 9) == 0)
			priv->version = g_strdup ((gchar *) &buffer[0xfa + hdr_sz + 9]);

		/* urgh */
		if (priv->version != NULL) {
			g_strstrip (priv->version);
			g_strdelimit (priv->version, "\r\n ", '\0');
		}
		break;
	case FU_ROM_KIND_INTEL:
		if (priv->version == NULL) {
			/* 2175_RYan PC 14.34  06/06/2013  21:27:53 */
			str = (gchar *) fu_rom_strstr_bin (buffer, sz, "Build Number:");
			if (str != NULL) {
				_cleanup_strv_free_ gchar **split = NULL;
				split = g_strsplit (str + 14, " ", -1);
				for (i = 0; split[i] != NULL; i++) {
					if (g_strstr_len (split[i], -1, ".") == NULL)
						continue;
					priv->version = g_strdup (split[i]);
				}
			}
		}

		/* fallback to VBIOS */
		if (priv->version == NULL) {
			str = (gchar *) fu_rom_strstr_bin (buffer, sz, "VBIOS ");
			if (str != NULL) {
				priv->version = g_strdup (str + 6);
				g_strdelimit (priv->version, "\r\n ", '\0');
			}
		}
	case FU_ROM_KIND_ATI:
		if (priv->version == NULL) {
			str = (gchar *) fu_rom_strstr_bin (buffer, sz, " VER0");
			if (str != NULL)
				priv->version = g_strdup (str + 4);
		}
		/* broken */
		if (priv->version == NULL) {
			str = (gchar *) fu_rom_strstr_bin (buffer, sz, " VR");
			if (str != NULL)
				priv->version = g_strdup (str + 4);
		}
		break;
	default:
		break;
	}

	/* update checksum */
	fu_rom_find_and_blank_serial_numbers (buffer, sz);
	g_checksum_update (priv->checksum_wip, buffer, sz);

	/* not known */
	if (priv->version == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "Firmware version extractor not known");
		return FALSE;
	}

	return TRUE;
}

/**
 * fu_rom_get_kind:
 **/
FuRomKind
fu_rom_get_kind (FuRom *rom)
{
	g_return_val_if_fail (FU_IS_ROM (rom), FU_ROM_KIND_UNKNOWN);
	return rom->priv->kind;
}

/**
 * fu_rom_get_version:
 **/
const gchar *
fu_rom_get_version (FuRom *rom)
{
	g_return_val_if_fail (FU_IS_ROM (rom), NULL);
	return rom->priv->version;
}

/**
 * fu_rom_get_vendor:
 **/
guint16
fu_rom_get_vendor (FuRom *rom)
{
	g_return_val_if_fail (FU_IS_ROM (rom), 0x0000);
	return rom->priv->vendor;
}

/**
 * fu_rom_get_model:
 **/
guint16
fu_rom_get_model (FuRom *rom)
{
	g_return_val_if_fail (FU_IS_ROM (rom), 0x0000);
	return rom->priv->model;
}

/**
 * fu_rom_generate_checksum:
 *
 * This adds the entire firmware image to the checksum data.
 **/
gboolean
fu_rom_generate_checksum (FuRom *rom, GCancellable *cancellable, GError **error)
{
	FuRomPrivate *priv = rom->priv;
	const guint block_sz = 0x4000;
	gssize cnt = block_sz;
	guint8 buffer[block_sz];

	g_return_val_if_fail (FU_IS_ROM (rom), FALSE);

	while (TRUE) {
		gssize sz;
		sz = g_input_stream_read (G_INPUT_STREAM (priv->stream),
					  buffer, block_sz, NULL, NULL);
		if (sz <= 0)
			break;

		/* blank out serial numbers */
		fu_rom_find_and_blank_serial_numbers (buffer, sz);

		cnt += sz;
		fu_rom_find_and_blank_serial_numbers (buffer, sz);
		g_checksum_update (priv->checksum_wip, buffer, sz);
	}

	/* is the data wrapped up, e.g. NVGI */
	if (priv->rom_length == 0 || cnt <= priv->rom_length) {
		g_debug ("read %" G_GSSIZE_FORMAT " bytes from ROM", cnt);
	} else {
		g_debug ("read %" G_GSSIZE_FORMAT " bytes from ROM, %" G_GSIZE_FORMAT
			 " bytes more than expected", cnt, cnt - priv->rom_length);
	}

	return TRUE;
}

/**
 * fu_rom_get_checksum:
 *
 * This returns the checksum of the firmware.
 * If fu_rom_generate_checksum() has been called then the checksum is of the
 * entire firmware image, and not just the header.
 **/
const gchar *
fu_rom_get_checksum (FuRom *rom)
{
	FuRomPrivate *priv = rom->priv;
	return g_checksum_get_string (priv->checksum_wip);
}

/**
 * fu_rom_class_init:
 **/
static void
fu_rom_class_init (FuRomClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_rom_finalize;
	g_type_class_add_private (klass, sizeof (FuRomPrivate));
}

/**
 * fu_rom_init:
 **/
static void
fu_rom_init (FuRom *rom)
{
	rom->priv = FU_ROM_GET_PRIVATE (rom);
	rom->priv->checksum_wip = g_checksum_new (G_CHECKSUM_SHA1);
}

/**
 * fu_rom_finalize:
 **/
static void
fu_rom_finalize (GObject *object)
{
	FuRom *rom = FU_ROM (object);
	FuRomPrivate *priv = rom->priv;

	g_checksum_free (priv->checksum_wip);
	g_free (priv->version);
	if (priv->stream != NULL)
		g_object_unref (priv->stream);

	G_OBJECT_CLASS (fu_rom_parent_class)->finalize (object);
}

/**
 * fu_rom_new:
 **/
FuRom *
fu_rom_new (void)
{
	FuRom *rom;
	rom = g_object_new (FU_TYPE_ROM, NULL);
	return FU_ROM (rom);
}
