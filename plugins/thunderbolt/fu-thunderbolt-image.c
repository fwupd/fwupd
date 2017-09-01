/* -*- mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Intel Corporation.
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

#include "fu-thunderbolt-image.h"

#include <string.h>
#include <libfwupd/fwupd-error.h>

enum FuThunderboltSection {
	DIGITAL_SECTION,
	DROM_SECTION,
	ARC_PARAMS_SECTION,
	DRAM_UCODE_SECTION,
	SECTION_COUNT
};

typedef struct {
	enum FuThunderboltSection  section; /* default is DIGITAL_SECTION */
	guint32                    offset;
	guint32                    len;
	guint8                     mask;    /* 0 means "no mask" */
	const gchar               *description;
} FuThunderboltFwLocation;

typedef struct {
	const guint8 *data;
	gsize         len;
	guint32      *sections;
} FuThunderboltFwObject;

typedef struct {
	guint16 id;
	guint   gen;
	guint   ports;
} FuThunderboltHwInfo;

static const FuThunderboltHwInfo *
get_hw_info (guint16 id)
{
	static const FuThunderboltHwInfo hw_info_arr[] = {
		{ 0x156D, 2, 2 }, /* FR 4C */
		{ 0x156B, 2, 1 }, /* FR 2C */
		{ 0x157E, 2, 1 }, /* WR */

		{ 0x1578, 3, 2 }, /* AR 4C */
		{ 0x1576, 3, 1 }, /* AR 2C */
		{ 0x15C0, 3, 1 }, /* AR LP */
		{ 0x15D3, 3, 2 }, /* AR-C 4C */
		{ 0x15DA, 3, 1 }, /* AR-C 2C */

		{ 0 }
	};

	for (gint i = 0; hw_info_arr[i].id != 0; i++)
		if (hw_info_arr[i].id == id)
			return hw_info_arr + i;
	return NULL;
}

static inline gboolean
valid_farb_pointer (guint32 pointer)
{
	return pointer != 0 && pointer != 0xFFFFFF;
}

/* returns NULL on error */
static GByteArray *
read_location (const FuThunderboltFwLocation  *location,
	       const FuThunderboltFwObject    *fw,
	       GError                        **error)
{
	guint32 location_start = fw->sections[location->section] + location->offset;
	g_autoptr(GByteArray) read = g_byte_array_new ();

	if (location_start > fw->len || location_start + location->len > fw->len) {
		g_set_error (error,
			     FWUPD_ERROR, FWUPD_ERROR_READ,
			     "Given location is outside of the given FW (%s)",
			     location->description ? location->description : "N/A");
		return NULL;
	}

	read = g_byte_array_append (read,
				    fw->data + location_start,
				    location->len);

	if (location->mask)
		read->data[0] &= location->mask;
	return g_steal_pointer (&read);
}

static gboolean
read_farb_pointer_impl (const FuThunderboltFwLocation  *location,
			const FuThunderboltFwObject    *fw,
			guint32                        *value,
			GError                        **error)
{
	g_autoptr(GByteArray) farb = read_location (location, fw, error);
	if (farb == NULL)
		return FALSE;
	*value = 0;
	memcpy (value, farb->data, farb->len);
	*value = GUINT32_FROM_LE (*value);
	return TRUE;
}

/* returns invalid FARB pointer on error */
static guint32
read_farb_pointer (const FuThunderboltFwObject *fw, GError **error)
{
	const FuThunderboltFwLocation farb0 = { .offset = 0,      .len = 3, .description = "farb0" };
	const FuThunderboltFwLocation farb1 = { .offset = 0x1000, .len = 3, .description = "farb1" };

	guint32 value;
	if (!read_farb_pointer_impl (&farb0, fw, &value, error))
		return 0;
	if (valid_farb_pointer (value))
		return value;

	if (!read_farb_pointer_impl (&farb1, fw, &value, error))
		return 0;
	if (!valid_farb_pointer (value)) {
		g_set_error_literal (error,
				     FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE,
				     "Invalid FW image file format");
		return 0;
	}

	return value;
}

static gboolean
compare (const FuThunderboltFwLocation  *location,
	 const FuThunderboltFwObject    *controller_fw,
	 const FuThunderboltFwObject    *image_fw,
	 gboolean                       *result,
	 GError                        **error)
{
	g_autoptr(GByteArray) controller_data = NULL;
	g_autoptr(GByteArray) image_data      = NULL;

	controller_data = read_location (location, controller_fw, error);
	if (controller_data == NULL)
		return FALSE;

	image_data = read_location (location, image_fw, error);
	if (image_data == NULL)
		return FALSE;

	*result = memcmp (controller_data->data, image_data->data, location->len) == 0;
	return TRUE;
}

static gboolean
read_bool (const FuThunderboltFwLocation  *location,
	   const FuThunderboltFwObject    *fw,
	   gboolean                       *val,
	   GError                        **error)
{
	g_autoptr(GByteArray) read = read_location (location, fw, error);
	if (read == NULL)
		return FALSE;
	for (gsize i = 0; i < read->len; i++)
		if (read->data[i] != 0) {
			*val = TRUE;
			return TRUE;
		}
	*val = FALSE;
	return TRUE;
}

static gboolean
read_uint16 (const FuThunderboltFwLocation  *location,
	     const FuThunderboltFwObject    *fw,
	     guint16                        *value,
	     GError                        **error)
{
	g_autoptr(GByteArray) read = read_location (location, fw, error);
	g_assert_cmpuint (location->len, ==, sizeof (guint16));
	if (read == NULL)
		return FALSE;

	*value = 0;
	memcpy (value, read->data, read->len);
	*value = GUINT16_FROM_LE (*value);
	return TRUE;
}

static gboolean
read_uint32 (const FuThunderboltFwLocation  *location,
	     const FuThunderboltFwObject    *fw,
	     guint32                        *value,
	     GError                        **error)
{
	g_autoptr(GByteArray) read = read_location (location, fw, error);
	g_assert_cmpuint (location->len, ==, sizeof (guint32));
	if (read == NULL)
		return FALSE;

	*value = 0;
	memcpy (value, read->data, read->len);
	*value = GUINT32_FROM_LE (*value);
	return TRUE;
}

/*
 * Size of ucode sections is uint16 value saved at the start of the section,
 * it's in DWORDS (4-bytes) units and it doesn't include itself. We need the
 * offset to the next section, so we translate it to bytes and add 2 for the
 * size field itself.
 *
 * offset parameter must be relative to digital section
 */
static gboolean
read_ucode_section_len (guint32                       offset,
			const FuThunderboltFwObject  *fw,
			guint16                      *value,
			GError                      **error)
{
	const FuThunderboltFwLocation section_size = { .offset = offset, .len = 2, .description = "size field" };
	if (!read_uint16 (&section_size, fw, value, error))
		return FALSE;
	*value *= sizeof (guint32);
	*value += section_size.len;
	return TRUE;
}

/*
 * Takes a FwObject and fills its section array up
 * Assumes sections[DIGITAL_SECTION].offset is already set
 */
static gboolean
read_sections (const FuThunderboltFwObject *fw, gboolean is_host, guint gen, GError **error)
{
	const FuThunderboltFwLocation arc_params_offset = { .offset = 0x75,  .len = 4, .description = "arc params offset" };
	const FuThunderboltFwLocation drom_offset       = { .offset = 0x10E, .len = 4, .description = "DROM offset" };
	guint32 offset;

	if (gen >= 3 || gen == 0) {
		if (!read_uint32 (&drom_offset, fw, &offset, error))
			return FALSE;
		fw->sections[DROM_SECTION] = offset + fw->sections[DIGITAL_SECTION];
	}

	if (!is_host) {
		if (!read_uint32 (&arc_params_offset, fw, &offset, error))
			return FALSE;
		fw->sections[ARC_PARAMS_SECTION] = offset + fw->sections[DIGITAL_SECTION];
	}

	if (is_host && gen > 2) {
		/*
		 * Algorithm:
		 * To find the DRAM section, we have to jump from section to
		 * section in a chain of sections.
		 * available_sections location tells what sections exist at all
		 * (with a flag per section).
		 * ee_ucode_start_addr location tells the offset of the first
		 * section in the list relatively to the digital section start.
		 * After having the offset of the first section, we have a loop
		 * over the section list. If the section exists, we read its
		 * length (2 bytes at section start) and add it to current
		 * offset to find the start of the next section. Otherwise, we
		 * already have the next section offset...
		 */
		const unsigned DRAM_FLAG = 1 << 6;
		const FuThunderboltFwLocation available_sections_loc  = { .offset = 0x2, .len = 1, .description = "sections" };
		const FuThunderboltFwLocation ee_ucode_start_addr_loc = { .offset = 0x3, .len = 2, .description = "ucode start" };

		guint16 ucode_offset;

		g_autoptr(GByteArray) available_sections =
				read_location (&available_sections_loc, fw, error);
		if (available_sections == NULL)
			return FALSE;

		if (!read_uint16 (&ee_ucode_start_addr_loc, fw, &ucode_offset, error))
			return FALSE;
		offset = ucode_offset;

		if ((available_sections->data[0] & DRAM_FLAG) == 0) {
			g_set_error_literal (error,
					     FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE,
					     "Can't find needed FW sections in the FW image file");
			return FALSE;
		}

		for (unsigned u = 1; u < DRAM_FLAG; u <<= 1)
			if (u & available_sections->data[0]) {
				if (!read_ucode_section_len (offset, fw, &ucode_offset, error))
					return FALSE;
				offset += ucode_offset;
			}

		fw->sections[DRAM_UCODE_SECTION] = offset + fw->sections[DIGITAL_SECTION];
	}

	return TRUE;
}

static inline gboolean
missing_needed_drom (const FuThunderboltFwObject *fw, gboolean is_host, guint gen)
{
	if (fw->sections[DROM_SECTION] != 0)
		return FALSE;
	if (is_host && gen < 3)
		return FALSE;
	return TRUE;
}

/*
 * Controllers that can have 1 or 2 ports have additional locations to check in
 * the 2 ports case. To make this as generic as possible, both sets are stored
 * in the same array with an empty entry separating them. The 1 port case should
 * stop comparing at the separator and the 2 ports case should continue
 * iterating the array to compare the rest.
 */
static const FuThunderboltFwLocation *
get_host_locations (guint16 id)
{
	static const FuThunderboltFwLocation FR[] = {
		{ .offset = 0x10,   .len = 4, .description = "PCIe Settings" },
		{ .offset = 0x143,  .len = 1, .description = "CIO-Port0_TX"  },
		{ .offset = 0x153,  .len = 1, .description = "CIO-Port0_RX"  },
		{ .offset = 0x147,  .len = 1, .description = "CIO-Port1_TX"  },
		{ .offset = 0x157,  .len = 1, .description = "CIO-Port1_RX"  },
		{ .offset = 0x211,  .len = 1, .description = "Snk0_0(DP-in)" },
		{ .offset = 0x215,  .len = 1, .description = "Snk0_1(DP-in)" },
		{ .offset = 0x219,  .len = 1, .description = "Snk0_2(DP-in)" },
		{ .offset = 0x21D,  .len = 1, .description = "Snk0_3(DP-in)" },
		{ .offset = 0X2175, .len = 1, .description = "PA(DP-out)"    },
		{ .offset = 0X2179, .len = 1, .description = "PB(DP-out)"    },
		{ .offset = 0X217D, .len = 1, .description = "Src0(DP-out)", .mask = 0xAA },
		{ 0 },

		{ .offset = 0x14B,  .len = 1, .description = "CIO-Port2_TX"  },
		{ .offset = 0x15B,  .len = 1, .description = "CIO-Port2_RX"  },
		{ .offset = 0x14F,  .len = 1, .description = "CIO-Port3_TX"  },
		{ .offset = 0x15F,  .len = 1, .description = "CIO-Port3_RX"  },
		{ .offset = 0X11C3, .len = 1, .description = "Snk1_0(DP-in)" },
		{ .offset = 0X11C7, .len = 1, .description = "Snk1_1(DP-in)" },
		{ .offset = 0X11CB, .len = 1, .description = "Snk1_2(DP-in)" },
		{ .offset = 0X11CF, .len = 1, .description = "Snk1_3(DP-in)" },
		{ 0 }
	};

	static const FuThunderboltFwLocation WR[] = {
		{ .offset = 0x10,   .len = 4, .description = "PCIe Settings" },
		{ .offset = 0x14F,  .len = 1, .description = "CIO-Port0_TX"  },
		{ .offset = 0x157,  .len = 1, .description = "CIO-Port0_RX"  },
		{ .offset = 0x153,  .len = 1, .description = "CIO-Port1_TX"  },
		{ .offset = 0x15B,  .len = 1, .description = "CIO-Port1_RX"  },
		{ .offset = 0x1F1,  .len = 1, .description = "Snk0_0(DP-in)" },
		{ .offset = 0x1F5,  .len = 1, .description = "Snk0_1(DP-in)" },
		{ .offset = 0x1F9,  .len = 1, .description = "Snk0_2(DP-in)" },
		{ .offset = 0x1FD,  .len = 1, .description = "Snk0_3(DP-in)" },
		{ .offset = 0X11A5, .len = 1, .description = "PA(DP-out)"    },
		{ 0 }
	};

	static const FuThunderboltFwLocation AR[] = {
		{ .offset = 0x10,  .len = 4, .description = "PCIe Settings" },
		{ .offset = 0x12,  .len = 1, .description = "PA", .mask = 0xCC, .section = DRAM_UCODE_SECTION },
		{ .offset = 0x121, .len = 1, .description = "Snk0" },
		{ .offset = 0x129, .len = 1, .description = "Snk1" },
		{ .offset = 0x136, .len = 1, .description = "Src0", .mask = 0xF0 },
		{ .offset = 0xB6,  .len = 1, .description = "PA/PB (USB2)", .mask = 0xC0 },
		{ 0 },

		{ .offset = 0x13, .len = 1, .description = "PB", .mask = 0xCC, .section = DRAM_UCODE_SECTION },
		{ 0 }
	};

	static const FuThunderboltFwLocation AR_LP[] = {
		{ .offset = 0x10,  .len = 4, .description = "PCIe Settings" },
		{ .offset = 0x12,  .len = 1, .description = "PA", .mask = 0xCC, .section = DRAM_UCODE_SECTION },
		{ .offset = 0x13,  .len = 1, .description = "PB", .mask = 0x44, .section = DRAM_UCODE_SECTION },
		{ .offset = 0x121, .len = 1, .description = "Snk0" },
		{ .offset = 0xB6,  .len = 1, .description = "PA/PB (USB2)", .mask = 0xC0 },
		{ 0 }
	};

	switch (id) {
	case 0x156D:
	case 0x156B:
		return FR;
	case 0x157E:
		return WR;
	case 0x1578:
	case 0x1576:
	case 0x15D3:
	case 0x15DA:
		return AR;
	case 0x15C0:
		return AR_LP;
	default:
		return NULL;
	}
}

static const FuThunderboltFwLocation *
get_device_locations (guint16 id)
{
	static const FuThunderboltFwLocation locations[] = {
		{ .offset = 0x124, .len = 1, .section = ARC_PARAMS_SECTION, .description = "X of N" },
		{ 0 }
	};

	switch (id) {
	case 0x1578:
	case 0x1576:
	case 0x15D3:
	case 0x15DA:
	case 0x15C0:
	case 0:
		return locations;
	default:
		return NULL;
	}
}

/*
 * Compares the given locations, assuming locations is an array.
 * Returns FALSE and sets error upon failure.
 * locations points to the end of the array (the empty entry) upon
 * successful return.
 */
static gboolean
compare_locations (const FuThunderboltFwLocation **locations,
		   const FuThunderboltFwObject    *controller,
		   const FuThunderboltFwObject    *image,
		   GError                        **error)
{
	gboolean result;
	do {
		if (!compare (*locations, controller, image, &result, error))
			return FALSE;
		if (!result) {
			g_set_error (error,
				     FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE,
				     "FW image image not compatible to this controller (%s)",
				     (*locations)->description);
			return FALSE;
		}
	} while ((++(*locations))->offset != 0);
	return TRUE;
}

FuPluginValidation
fu_plugin_thunderbolt_validate_image (GBytes  *controller_fw,
				      GBytes  *blob_fw,
				      GError **error)
{
	gboolean is_host;
	guint16 device_id;
	gboolean compare_result;
	const FuThunderboltHwInfo *hw_info;
	const FuThunderboltHwInfo unknown = { 0 };
	const FuThunderboltFwLocation *locations;

	gsize fw_size;
	const guint8 *fw_data = g_bytes_get_data (controller_fw, &fw_size);

	gsize blob_size;
	const guint8 *blob_data = g_bytes_get_data (blob_fw, &blob_size);

	guint32 controller_sections[SECTION_COUNT] = { [DIGITAL_SECTION] = 0 };
	guint32 image_sections     [SECTION_COUNT] = { 0 };

	const FuThunderboltFwObject controller = { fw_data,   fw_size,   controller_sections };
	const FuThunderboltFwObject image      = { blob_data, blob_size, image_sections };

	const FuThunderboltFwLocation is_host_loc   = { .offset = 0x10, .len = 1, .mask = 1 << 1, .description = "host flag" };
	const FuThunderboltFwLocation device_id_loc = { .offset = 0x5,  .len = 2, .description = "devID" };

	image_sections[DIGITAL_SECTION] = read_farb_pointer (&image, error);
	if (image_sections[DIGITAL_SECTION] == 0)
		return VALIDATION_FAILED;

	if (!read_bool (&is_host_loc, &controller, &is_host, error))
		return VALIDATION_FAILED;

	if (!read_uint16 (&device_id_loc, &controller, &device_id, error))
		return VALIDATION_FAILED;

	hw_info = get_hw_info (device_id);
	if (hw_info == NULL) {
		if (is_host) {
			g_set_error (error,
				     FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
				     "Unknown controller");
			return FALSE;
		}
		hw_info = &unknown;
	}

	if (!compare (&is_host_loc, &controller, &image, &compare_result, error))
		return VALIDATION_FAILED;
	if (!compare_result) {
		g_set_error (error,
			     FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE,
			     "The FW image file is for a %s controller",
			     is_host ? "device" : "host");
		return VALIDATION_FAILED;
	}

	if (!compare (&device_id_loc, &controller, &image, &compare_result, error))
		return VALIDATION_FAILED;
	if (!compare_result) {
		g_set_error_literal (error,
				     FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE,
				     "The FW image file is for a different HW type");
		return VALIDATION_FAILED;
	}

	if (!read_sections (&controller, is_host, hw_info->gen, error))
		return VALIDATION_FAILED;
	if (missing_needed_drom (&controller, is_host, hw_info->gen)) {
		g_set_error_literal (error,
				     FWUPD_ERROR, FWUPD_ERROR_READ,
				     "Can't find needed FW sections in the controller");
		return VALIDATION_FAILED;
	}

	if (!read_sections (&image, is_host, hw_info->gen, error))
		return VALIDATION_FAILED;
	if (missing_needed_drom (&image, is_host, hw_info->gen)) {
		g_set_error_literal (error,
				     FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE,
				     "Can't find needed FW sections in the FW image file");
		return VALIDATION_FAILED;
	}

	if (controller.sections[DROM_SECTION] != 0) {
		const FuThunderboltFwLocation drom_locations[] = {
			{ .offset = 0x10, .len = 2, .section = DROM_SECTION, .description = "vendor ID" },
			{ .offset = 0x12, .len = 2, .section = DROM_SECTION, .description = "model ID" },
			{ 0 }
		};
		locations = drom_locations;
		if (!compare_locations (&locations, &controller, &image, error))
			return VALIDATION_FAILED;
	}

	locations = is_host ?
			    get_host_locations (hw_info->id) :
			    get_device_locations (hw_info->id);
	if (locations == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
				     "FW locations to check not found for this controller");
		return VALIDATION_FAILED;
	}

	if (!compare_locations (&locations, &controller, &image, error))
		return VALIDATION_FAILED;

	if (is_host && hw_info->ports == 2) {
		locations++;
		if (!compare_locations (&locations, &controller, &image, error))
			return VALIDATION_FAILED;
	}

	return hw_info->id != 0 ? VALIDATION_PASSED : UNKNOWN_DEVICE;
}
