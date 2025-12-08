/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

/* ---- magic string ------------------------------------------------------- */
/* used by validate() for quick reject before doing full parse */
extern const char PXI_TP_MAGIC[5]; /* "FWHD" + NUL */

/* ---- fixed sizes & limits ----------------------------------------------- */
enum {
	PXI_TP_HEADER_V1_LEN = 0x0218, /* header size for v1.0 */
	PXI_TP_MAX_SECTIONS = 8,       /* max number of section descriptors */
	PXI_TP_SECTION_SIZE = 64       /* size of each section header */
};

/* ---- section layout constants (on-disk layout, LE) ---------------------- */
enum {
	/* reserved bytes length in section header */
	PXI_TP_S_RESERVED_LEN = 0x0c,
	/* extname bytes length (not including NUL) */
	PXI_TP_S_EXTNAME_LEN = 34
};

/* ---- update types (semantic enum) -------------------------------------- */
typedef enum {
	PXI_TP_UPDATE_TYPE_GENERAL = 0u,    /* standard flash update process */
	PXI_TP_UPDATE_TYPE_FW_SECTION = 1u, /* firmware section update */
	PXI_TP_UPDATE_TYPE_BOOTLOADER = 2u, /* bootloader section update */
	PXI_TP_UPDATE_TYPE_PARAM = 3u,	    /* parameter section update */
	PXI_TP_UPDATE_TYPE_TF_FORCE = 16u   /* TF Force (via DLL) */
} FuPxiTpUpdateType;

/* ---- update information bit definitions -------------------------------- */
enum {
	PXI_TP_UI_VALID = (1u << 0),   /* 1 = execute this section */
	PXI_TP_UI_EXTERNAL = (1u << 1) /* 1 = use external file */
};

/* -------------------------------------------------------------------------
 * Parsed section (C-consumer view)
 * ------------------------------------------------------------------------- */
typedef struct {
	guint8 update_type; /* matches FuPxiTpUpdateType */
	guint8 update_info; /* raw info bits */

	gboolean is_valid_update; /* (update_info & PXI_TP_UI_VALID) != 0 */
	gboolean is_external;	  /* (update_info & PXI_TP_UI_EXTERNAL) != 0 */

	guint32 target_flash_start;  /* only when GENERAL */
	guint32 internal_file_start; /* only when !external, absolute file offset */
	guint32 section_length;	     /* only when !external */
	guint32 section_crc;	     /* CRC of internal section */

	guint8 reserved[PXI_TP_S_RESERVED_LEN];

	/* extname from header, trimmed and NUL-terminated */
	gchar external_file_name[PXI_TP_S_EXTNAME_LEN + 1];
} FuPxiTpSection;
