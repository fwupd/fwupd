/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>
#include <glib/gstdio.h>
#include <string.h>

#include "fu-rom.h"

static void fu_rom_finalize			 (GObject *object);

/* data from http://resources.infosecinstitute.com/pci-expansion-rom/ */
typedef struct {
	guint8		*rom_data;
	guint32		 rom_len;
	guint32		 rom_offset;
	guint32		 entry_point;
	guint8		 reserved[18];
	guint16		 cpi_ptr;
	guint16		 vendor_id;
	guint16		 device_id;
	guint16		 device_list_ptr;
	guint16		 data_len;
	guint8		 data_rev;
	guint32		 class_code;
	guint32		 image_len;
	guint16		 revision_level;
	guint8		 code_type;
	guint8		 last_image;
	guint32		 max_runtime_len;
	guint16		 config_header_ptr;
	guint16		 dmtf_clp_ptr;
} FuRomPciHeader;

struct _FuRom {
	GObject				 parent_instance;
	FuRomKind			 kind;
	gchar				*version;
	guint16				 vendor_id;
	guint16				 device_id;
	GPtrArray			*hdrs; /* of FuRomPciHeader */
};

G_DEFINE_TYPE (FuRom, fu_rom, G_TYPE_OBJECT)

static void
fu_rom_pci_header_free (FuRomPciHeader *hdr)
{
	g_free (hdr->rom_data);
	g_free (hdr);
}

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

static guint8 *
fu_rom_pci_strstr (FuRomPciHeader *hdr, const gchar *needle)
{
	gsize needle_len;
	guint8 *haystack;
	gsize haystack_len;

	if (needle == NULL || needle[0] == '\0')
		return NULL;
	if (hdr->rom_data == NULL)
		return NULL;
	if (hdr->data_len > hdr->rom_len)
		return NULL;
	haystack = &hdr->rom_data[hdr->data_len];
	haystack_len = hdr->rom_len - hdr->data_len;
	needle_len = strlen (needle);
	if (needle_len > haystack_len)
		return NULL;
	for (guint i = 0; i < haystack_len - needle_len; i++) {
		if (memcmp (haystack + i, needle, needle_len) == 0)
			return &haystack[i];
	}
	return NULL;
}

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

static gchar *
fu_rom_get_hex_dump (guint8 *buffer, guint32 sz)
{
	GString *str = g_string_new ("");
	for (guint32 i = 0; i < sz; i++)
		g_string_append_printf (str, "%02x ", buffer[i]);
	g_string_append (str, "   ");
	for (guint32 i = 0; i < sz; i++) {
		gchar tmp = '?';
		if (g_ascii_isprint (buffer[i]))
			tmp = (gchar) buffer[i];
		g_string_append_printf (str, "%c", tmp);
	}
	return g_string_free (str, FALSE);
}

typedef struct {
	guint8		 segment_kind;
	guint8		*data;
	guint16		 data_len;
	guint16		 next_offset;
} FooRomPciCertificateHdr;

static void
fu_rom_pci_print_certificate_data (guint8 *buffer, gssize sz)
{
	guint16 off = 0;
	g_autofree gchar *hdr_str = NULL;

	/* 27 byte header, unknown purpose */
	hdr_str = fu_rom_get_hex_dump (buffer+off, 27);
	g_debug ("    ISBN header: %s", hdr_str);
	buffer += 27;

	while (TRUE) {
		/* 29 byte header to the segment, then data:
		 * 0x01      = type. 0x1 = certificate, 0x2 = hashes?
		 * 0x13,0x14 = offset to next segment */
		FooRomPciCertificateHdr h;
		g_autofree gchar *segment_str = NULL;
		segment_str = fu_rom_get_hex_dump (buffer+off, 29);
		g_debug ("     ISBN segment @%02x: %s", off, segment_str);
		h.segment_kind = buffer[off+1];
		h.next_offset = (guint16) (((guint16) buffer[off+14] << 8) + buffer[off+13]);
		h.data = &buffer[off+29];

		/* calculate last block length automatically */
		if (h.next_offset == 0)
			h.data_len = (guint16) (sz - off - 29 - 27);
		else
			h.data_len = (guint16) (h.next_offset - off - 29);

		/* print the certificate */
		if (h.segment_kind == 0x01) {
			g_autofree gchar *tmp = NULL;
			tmp = fu_rom_get_hex_dump (h.data, h.data_len);
			g_debug ("%s(%i)", tmp, h.data_len);
		} else if (h.segment_kind == 0x02) {
			g_autofree gchar *tmp = NULL;
			tmp = fu_rom_get_hex_dump (h.data,
						   h.data_len < 32 ? h.data_len : 32);
			g_debug ("%s(%i)", tmp, h.data_len);
		} else {
			g_warning ("unknown segment kind %i", h.segment_kind);
		}

		/* last block */
		if (h.next_offset == 0x0000)
			break;
		off = h.next_offset;
	}
}

static const gchar *
fu_rom_pci_code_type_to_string (guint8 code_type)
{
	if (code_type == 0)
		return "Intel86";
	if (code_type == 1)
		return "OpenFirmware";
	if (code_type == 2)
		return "PA-RISC";
	if (code_type == 3)
		return "EFI";
	return "reserved";
}

static guint8
fu_rom_pci_header_get_checksum (FuRomPciHeader *hdr)
{
	guint8 chksum_check = 0x00;
	for (guint i = 0; i < hdr->rom_len; i++)
		chksum_check += hdr->rom_data[i];
	return chksum_check;
}

static void
fu_rom_pci_print_header (FuRomPciHeader *hdr)
{
	guint8 chksum_check;
	guint8 *buffer;
	g_autofree gchar *data_str = NULL;
	g_autofree gchar *reserved_str = NULL;

	g_debug ("PCI Header");
	g_debug (" RomOffset: 0x%04x", hdr->rom_offset);
	g_debug (" RomSize:   0x%04x", hdr->rom_len);
	g_debug (" EntryPnt:  0x%06x", hdr->entry_point);
	reserved_str = fu_rom_get_hex_dump (hdr->reserved, 18);
	g_debug (" Reserved:  %s", reserved_str);
	g_debug (" CpiPtr:    0x%04x", hdr->cpi_ptr);

	/* sanity check */
	if (hdr->cpi_ptr > hdr->rom_len) {
		g_debug ("  PCI DATA: Invalid as cpi_ptr > rom_len");
		return;
	}
	if (hdr->data_len > hdr->rom_len) {
		g_debug ("  PCI DATA: Invalid as data_len > rom_len");
		return;
	}

	/* print the data */
	buffer = &hdr->rom_data[hdr->cpi_ptr];
	g_debug ("  PCI Data");
	g_debug ("   VendorID:  0x%04x", hdr->vendor_id);
	g_debug ("   DeviceID:  0x%04x", hdr->device_id);
	g_debug ("   DevList:   0x%04x", hdr->device_list_ptr);
	g_debug ("   DataLen:   0x%04x", hdr->data_len);
	g_debug ("   DataRev:   0x%04x", hdr->data_rev);
	if (hdr->image_len < 0x0f) {
		data_str = fu_rom_get_hex_dump (&buffer[hdr->data_len], hdr->image_len);
		g_debug ("   ImageLen:  0x%04x [%s]", hdr->image_len, data_str);
	} else if (hdr->image_len >= 0x0f) {
		data_str = fu_rom_get_hex_dump (&buffer[hdr->data_len], 0x0f);
		g_debug ("   ImageLen:  0x%04x [%s...]", hdr->image_len, data_str);
	} else {
		g_debug ("   ImageLen:  0x%04x", hdr->image_len);
	}
	g_debug ("   RevLevel:  0x%04x", hdr->revision_level);
	g_debug ("   CodeType:  0x%02x [%s]", hdr->code_type,
		 fu_rom_pci_code_type_to_string (hdr->code_type));
	g_debug ("   LastImg:   0x%02x [%s]", hdr->last_image,
		 hdr->last_image == 0x80 ? "yes" : "no");
	g_debug ("   MaxRunLen: 0x%04x", hdr->max_runtime_len);
	g_debug ("   ConfigHdr: 0x%04x", hdr->config_header_ptr);
	g_debug ("   ClpPtr:    0x%04x", hdr->dmtf_clp_ptr);

	/* dump the ISBN */
	if (hdr->code_type == 0x70 &&
	    memcmp (&buffer[hdr->data_len], "ISBN", 4) == 0) {
		fu_rom_pci_print_certificate_data (&buffer[hdr->data_len],
						   hdr->image_len);
	}

	/* verify the checksum byte */
	if (hdr->image_len <= hdr->rom_len && hdr->image_len > 0) {
		buffer = hdr->rom_data;
		chksum_check = fu_rom_pci_header_get_checksum (hdr);
		if (chksum_check == 0x00) {
			g_debug ("   ChkSum:    0x%02x [valid]",
				 buffer[hdr->image_len-1]);
		} else {
			g_debug ("   ChkSum:    0x%02x [failed, got 0x%02x]",
				 buffer[hdr->image_len-1],
				 chksum_check);
		}
	} else {
		g_debug ("   ChkSum:    0x?? [unknown]");
	}
}

gboolean
fu_rom_extract_all (FuRom *self, const gchar *path, GError **error)
{
	FuRomPciHeader *hdr;

	for (guint i = 0; i < self->hdrs->len; i++) {
		g_autofree gchar *fn = NULL;
		hdr = g_ptr_array_index (self->hdrs, i);
		fn = g_strdup_printf ("%s/%02u.bin", path, i);
		g_debug ("dumping ROM #%u at 0x%04x [0x%02x] to %s",
			 i, hdr->rom_offset, hdr->rom_len, fn);
		if (hdr->rom_len == 0)
			continue;
		if (!g_file_set_contents (fn,
					  (const gchar *) hdr->rom_data,
					  (gssize) hdr->rom_len, error))
			return FALSE;
	}
	return TRUE;
}

static void
fu_rom_find_and_blank_serial_numbers (FuRom *self)
{
	FuRomPciHeader *hdr;
	guint8 *tmp;

	/* bail if not likely */
	if (self->kind == FU_ROM_KIND_PCI ||
	    self->kind == FU_ROM_KIND_INTEL) {
		g_debug ("no serial numbers likely");
		return;
	}

	for (guint i = 0; i < self->hdrs->len; i++) {
		hdr = g_ptr_array_index (self->hdrs, i);
		g_debug ("looking for PPID at 0x%04x", hdr->rom_offset);
		tmp = fu_rom_pci_strstr (hdr, "PPID");
		if (tmp != NULL) {
			guint len;
			guint8 chk;
			len = fu_rom_blank_serial_numbers (tmp, hdr->rom_len - hdr->data_len);
			g_debug ("cleared %u chars @ 0x%04lx",
				 len, (gulong) (tmp - &hdr->rom_data[hdr->data_len]));

			/* we have to fix the checksum */
			chk = fu_rom_pci_header_get_checksum (hdr);
			hdr->rom_data[hdr->rom_len - 1] -= chk;
			fu_rom_pci_print_header (hdr);
		}
	}
}

static gboolean
fu_rom_pci_parse_data (FuRomPciHeader *hdr)
{
	guint8 *buffer;

	/* check valid */
	if (hdr->cpi_ptr == 0x0000) {
		g_debug ("No PCI DATA @ 0x%04x", hdr->rom_offset);
		return FALSE;
	}
	if (hdr->rom_len > 0 && hdr->cpi_ptr > hdr->rom_len) {
		g_debug ("Invalid PCI DATA @ 0x%04x", hdr->rom_offset);
		return FALSE;
	}

	/* gahh, CPI is out of the first chunk */
	if (hdr->cpi_ptr > hdr->rom_len) {
		g_debug ("No available PCI DATA @ 0x%04x : 0x%04x > 0x%04x",
			 hdr->rom_offset, hdr->cpi_ptr, hdr->rom_len);
		return FALSE;
	}

	/* check signature */
	buffer = &hdr->rom_data[hdr->cpi_ptr];
	if (memcmp (buffer, "PCIR", 4) != 0) {
		if (memcmp (buffer, "RGIS", 4) == 0 ||
		    memcmp (buffer, "NPDS", 4) == 0 ||
		    memcmp (buffer, "NPDE", 4) == 0) {
			g_debug ("-- using NVIDIA DATA quirk");
		} else {
			g_debug ("Not PCI DATA: %02x%02x%02x%02x [%c%c%c%c]",
				 buffer[0], buffer[1],
				 buffer[2], buffer[3],
				 buffer[0], buffer[1],
				 buffer[2], buffer[3]);
			return FALSE;
		}
	}

	/* parse */
	hdr->vendor_id = ((guint16) buffer[0x05] << 8) + buffer[0x04];
	hdr->device_id = ((guint16) buffer[0x07] << 8) + buffer[0x06];
	hdr->device_list_ptr = ((guint16) buffer[0x09] << 8) + buffer[0x08];
	hdr->data_len = ((guint16) buffer[0x0b] << 8) + buffer[0x0a];
	hdr->data_rev = buffer[0x0c];
	hdr->class_code = ((guint16) buffer[0x0f] << 16) +
			  ((guint16) buffer[0x0e] << 8) +
			  buffer[0x0d];
	hdr->image_len = (((guint16) buffer[0x11] << 8) + buffer[0x10]) * 512;
	hdr->revision_level = ((guint16) buffer[0x13] << 8) + buffer[0x12];
	hdr->code_type = buffer[0x14];
	hdr->last_image = buffer[0x15];
	hdr->max_runtime_len = (((guint16) buffer[0x17] << 8) +
				buffer[0x16]) * 512;
	hdr->config_header_ptr = ((guint16) buffer[0x19] << 8) + buffer[0x18];
	hdr->dmtf_clp_ptr = ((guint16) buffer[0x1b] << 8) + buffer[0x1a];
	return TRUE;
}

static FuRomPciHeader *
fu_rom_pci_get_header (guint8 *buffer, guint32 sz)
{
	FuRomPciHeader *hdr;

	/* check signature */
	if (memcmp (buffer, "\x55\xaa", 2) != 0) {
		if (memcmp (buffer, "\x56\x4e", 2) == 0) {
			g_debug ("-- using NVIDIA ROM quirk");
		} else {
			g_autofree gchar *sig_str = NULL;
			sig_str = fu_rom_get_hex_dump (buffer, MIN (16, sz));
			g_debug ("Not PCI ROM %s", sig_str);
			return NULL;
		}
	}

	/* decode structure */
	hdr = g_new0 (FuRomPciHeader, 1);
	hdr->rom_len = buffer[0x02] * 512;

	/* fix up misreporting */
	if (hdr->rom_len == 0) {
		g_debug ("fixing up last image size");
		hdr->rom_len = sz;
	}

	/* copy this locally to the header */
	hdr->rom_data = g_memdup (buffer, hdr->rom_len);

	/* parse out CPI */
	hdr->entry_point = ((guint32) buffer[0x05] << 16) +
			   ((guint16) buffer[0x04] << 8) +
			   buffer[0x03];
	memcpy (&hdr->reserved, &buffer[6], 18);
	hdr->cpi_ptr = ((guint16) buffer[0x19] << 8) + buffer[0x18];

	/* parse the header data */
	g_debug ("looking for PCI DATA @ 0x%04x", hdr->cpi_ptr);
	fu_rom_pci_parse_data (hdr);
	return hdr;
}

static gchar *
fu_rom_find_version_pci (FuRomPciHeader *hdr)
{
	gchar *str;

	/* ARC storage */
	if (memcmp (hdr->reserved, "\0\0ARC", 5) == 0) {
		str = (gchar *) fu_rom_pci_strstr (hdr, "BIOS: ");
		if (str != NULL)
			return g_strdup (str + 6);
	}
	return NULL;
}

static gchar *
fu_rom_find_version_nvidia (FuRomPciHeader *hdr)
{
	gchar *str;

	/* static location for some firmware */
	if (memcmp (hdr->rom_data + 0x013d, "Version ", 8) == 0)
		return g_strdup ((gchar *) &hdr->rom_data[0x013d + 8]);

	/* usual search string */
	str = (gchar *) fu_rom_pci_strstr (hdr, "Version ");
	if (str != NULL)
		return g_strdup (str + 8);

	/* broken */
	str = (gchar *) fu_rom_pci_strstr (hdr, "Vension:");
	if (str != NULL)
		return g_strdup (str + 8);
	str = (gchar *) fu_rom_pci_strstr (hdr, "Version");
	if (str != NULL)
		return g_strdup (str + 7);

	/* fallback to VBIOS */
	if (memcmp (hdr->rom_data + 0xfa, "VBIOS Ver", 9) == 0)
		return g_strdup ((gchar *) &hdr->rom_data[0xfa + 9]);
	return NULL;
}

static gchar *
fu_rom_find_version_intel (FuRomPciHeader *hdr)
{
	gchar *str;

	/* 2175_RYan PC 14.34  06/06/2013  21:27:53 */
	str = (gchar *) fu_rom_pci_strstr (hdr, "Build Number:");
	if (str != NULL) {
		g_auto(GStrv) split = NULL;
		split = g_strsplit (str + 14, " ", -1);
		for (guint i = 0; split[i] != NULL; i++) {
			if (g_strstr_len (split[i], -1, ".") == NULL)
				continue;
			return g_strdup (split[i]);
		}
	}

	/* fallback to VBIOS */
	str = (gchar *) fu_rom_pci_strstr (hdr, "VBIOS ");
	if (str != NULL)
		return g_strdup (str + 6);
	return NULL;
}

static gchar *
fu_rom_find_version_ati (FuRomPciHeader *hdr)
{
	gchar *str;

	str = (gchar *) fu_rom_pci_strstr (hdr, " VER0");
	if (str != NULL)
		return g_strdup (str + 4);

	/* broken */
	str = (gchar *) fu_rom_pci_strstr (hdr, " VR");
	if (str != NULL)
		return g_strdup (str + 4);
	return NULL;
}

static gchar *
fu_rom_find_version (FuRomKind kind, FuRomPciHeader *hdr)
{
	if (kind == FU_ROM_KIND_PCI)
		return fu_rom_find_version_pci (hdr);
	if (kind == FU_ROM_KIND_NVIDIA)
		return fu_rom_find_version_nvidia (hdr);
	if (kind == FU_ROM_KIND_INTEL)
		return fu_rom_find_version_intel (hdr);
	if (kind == FU_ROM_KIND_ATI)
		return fu_rom_find_version_ati (hdr);
	return NULL;
}

gboolean
fu_rom_load_data (FuRom *self,
		  guint8 *buffer, gsize buffer_sz,
		  FuRomLoadFlags flags,
		  GCancellable *cancellable,
		  GError **error)
{
	FuRomPciHeader *hdr = NULL;
	guint32 sz = buffer_sz;
	guint32 jump = 0;
	guint32 hdr_sz = 0;

	g_return_val_if_fail (FU_IS_ROM (self), FALSE);

	/* detect optional IFR header and skip to option ROM */
	if (memcmp (buffer, "NVGI", 4) == 0) {
		guint16 ifr_sz_raw;
		memcpy (&ifr_sz_raw, &buffer[0x15], 2);
		hdr_sz = GUINT16_FROM_BE (ifr_sz_raw);
		g_debug ("detected IFR header, skipping %x bytes", hdr_sz);
	}

	/* read all the ROM headers */
	while (sz > hdr_sz + jump) {
		guint32 jump_sz;
		g_debug ("looking for PCI ROM @ 0x%04x", hdr_sz + jump);
		hdr = fu_rom_pci_get_header (&buffer[hdr_sz + jump], sz - hdr_sz - jump);
		if (hdr == NULL) {
			gboolean found_data = FALSE;

			/* check it's not just NUL padding */
			for (guint i = jump + hdr_sz; i < buffer_sz; i++) {
				if (buffer[i] != 0x00) {
					found_data = TRUE;
					break;
				}
			}
			if (found_data) {
				g_debug ("found junk data, adding fake");
				hdr = g_new0 (FuRomPciHeader, 1);
				hdr->vendor_id = 0x0000;
				hdr->device_id = 0x0000;
				hdr->code_type = 0x00;
				hdr->last_image = 0x80;
				hdr->rom_offset = hdr_sz + jump;
				hdr->rom_len = sz - hdr->rom_offset;
				hdr->rom_data = g_memdup (&buffer[hdr->rom_offset], hdr->rom_len);
				hdr->image_len = hdr->rom_len;
				g_ptr_array_add (self->hdrs, hdr);
			} else {
				g_debug ("ignoring 0x%04x bytes of padding",
					 (guint) (buffer_sz - (jump + hdr_sz)));
			}
			break;
		}

		/* save this so we can fix checksums */
		hdr->rom_offset = hdr_sz + jump;

		/* we can't break on hdr->last_image as
		 * NVIDIA uses packed but not merged extended headers */
		g_ptr_array_add (self->hdrs, hdr);

		/* NVIDIA don't always set a ROM size for extensions */
		jump_sz = hdr->rom_len;
		if (jump_sz == 0)
			jump_sz = hdr->image_len;
		if (jump_sz == 0x0)
			break;
		jump += jump_sz;
	}

	/* we found nothing */
	if (self->hdrs->len == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Failed to detect firmware header [%02x%02x]",
			     buffer[0], buffer[1]);
		return FALSE;
	}

	/* print all headers */
	for (guint i = 0; i < self->hdrs->len; i++) {
		hdr = g_ptr_array_index (self->hdrs, i);
		fu_rom_pci_print_header (hdr);
	}

	/* find first ROM header */
	hdr = g_ptr_array_index (self->hdrs, 0);
	self->vendor_id = hdr->vendor_id;
	self->device_id = hdr->device_id;
	self->kind = FU_ROM_KIND_PCI;

	/* detect intel header */
	if (memcmp (hdr->reserved, "00000000000", 11) == 0)
		hdr_sz = (guint32) (((guint16) buffer[0x1b] << 8) + buffer[0x1a]);
	if (hdr_sz > sz) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "firmware corrupt (overflow)");
		return FALSE;
	}

	if (hdr->entry_point == 0x374beb) {
		self->kind = FU_ROM_KIND_NVIDIA;
	} else if (memcmp (buffer + hdr_sz, "$VBT", 4) == 0) {
		self->kind = FU_ROM_KIND_INTEL;
	} else if (memcmp(buffer + 0x30, " 761295520", 10) == 0) {
		self->kind = FU_ROM_KIND_ATI;
	}

	/* nothing */
	if (self->kind == FU_ROM_KIND_UNKNOWN) {
		g_autofree gchar *str = NULL;
		str = fu_rom_get_hex_dump (buffer + hdr_sz, 0x32);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Failed to detect firmware kind from [%s]",
			     str);
		return FALSE;
	}

	/* find version string */
	self->version = fu_rom_find_version (self->kind, hdr);
	if (self->version != NULL) {
		g_strstrip (self->version);
		g_strdelimit (self->version, "\r\n ", '\0');
	}

	/* update checksum */
	if (flags & FU_ROM_LOAD_FLAG_BLANK_PPID)
		fu_rom_find_and_blank_serial_numbers (self);

	/* not known */
	if (self->version == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "Firmware version extractor not known");
		return FALSE;
	}

	return TRUE;
}

gboolean
fu_rom_load_file (FuRom *self, GFile *file, FuRomLoadFlags flags,
		  GCancellable *cancellable, GError **error)
{
	const gssize buffer_sz = 0x400000;
	gssize sz;
	guint number_reads = 0;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *fn = NULL;
	g_autofree guint8 *buffer = NULL;
	g_autoptr(GInputStream) stream = NULL;

	g_return_val_if_fail (FU_IS_ROM (self), FALSE);

	/* open file */
	stream = G_INPUT_STREAM (g_file_read (file, cancellable, &error_local));
	if (stream == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_AUTH_FAILED,
				     error_local->message);
		return FALSE;
	}

	/* we have to enable the read for devices */
	fn = g_file_get_path (file);
	if (g_str_has_prefix (fn, "/sys")) {
		g_autoptr(GFileOutputStream) output_stream = NULL;
		output_stream = g_file_replace (file, NULL, FALSE, G_FILE_CREATE_NONE,
						cancellable, error);
		if (output_stream == NULL)
			return FALSE;
		if (g_output_stream_write (G_OUTPUT_STREAM (output_stream), "1", 1,
					   cancellable, error) < 0)
			return FALSE;
	}

	/* read out the header */
	buffer = g_malloc ((gsize) buffer_sz);
	sz = g_input_stream_read (stream, buffer, buffer_sz,
				  cancellable, error);
	if (sz < 0)
		return FALSE;
	if (sz < 512) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Firmware too small: %" G_GSSIZE_FORMAT " bytes", sz);
		return FALSE;
	}

	/* ensure we got enough data to fill the buffer */
	while (sz < buffer_sz) {
		gssize sz_chunk;
		sz_chunk = g_input_stream_read (stream,
						buffer + sz,
						buffer_sz - sz,
						cancellable,
						error);
		if (sz_chunk == 0)
			break;
		g_debug ("ROM returned 0x%04x bytes, adding 0x%04x...",
			 (guint) sz, (guint) sz_chunk);
		if (sz_chunk < 0)
			return FALSE;
		sz += sz_chunk;

		/* check the firmware isn't serving us small chunks */
		if (number_reads++ > 16) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "firmware not fulfilling requests");
			return FALSE;
		}
	}
	g_debug ("ROM buffer filled %" G_GSSIZE_FORMAT "kb/%" G_GSSIZE_FORMAT "kb",
		 sz / 0x400, buffer_sz / 0x400);
	return fu_rom_load_data (self, buffer, sz, flags, cancellable, error);
}

FuRomKind
fu_rom_get_kind (FuRom *self)
{
	g_return_val_if_fail (FU_IS_ROM (self), FU_ROM_KIND_UNKNOWN);
	return self->kind;
}

const gchar *
fu_rom_get_version (FuRom *self)
{
	g_return_val_if_fail (FU_IS_ROM (self), NULL);
	return self->version;
}

guint16
fu_rom_get_vendor (FuRom *self)
{
	g_return_val_if_fail (FU_IS_ROM (self), 0x0000);
	return self->vendor_id;
}

guint16
fu_rom_get_model (FuRom *self)
{
	g_return_val_if_fail (FU_IS_ROM (self), 0x0000);
	return self->device_id;
}

GBytes *
fu_rom_get_data (FuRom *self)
{
	GByteArray *buf = g_byte_array_new ();
	for (guint i = 0; i < self->hdrs->len; i++) {
		FuRomPciHeader *hdr = g_ptr_array_index (self->hdrs, i);
		g_byte_array_append (buf, hdr->rom_data, hdr->rom_len);
	}
	return g_byte_array_free_to_bytes (buf);
}

static void
fu_rom_class_init (FuRomClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_rom_finalize;
}

static void
fu_rom_init (FuRom *self)
{
	self->hdrs = g_ptr_array_new_with_free_func ((GDestroyNotify) fu_rom_pci_header_free);
}

static void
fu_rom_finalize (GObject *object)
{
	FuRom *self = FU_ROM (object);

	g_free (self->version);
	g_ptr_array_unref (self->hdrs);

	G_OBJECT_CLASS (fu_rom_parent_class)->finalize (object);
}

FuRom *
fu_rom_new (void)
{
	FuRom *self;
	self = g_object_new (FU_TYPE_ROM, NULL);
	return FU_ROM (self);
}
