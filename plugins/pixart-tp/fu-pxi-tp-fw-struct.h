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

/* ---- fixed sizes & limits --------------------------------------------- */
#define PXI_TP_HEADER_V1_LEN 0x0218 /* header size for v1.0 */
#define PXI_TP_MAX_SECTIONS  8	    /* max number of section descriptors */
#define PXI_TP_SECTION_SIZE  64	    /* size of each section header */

/* ---- section layout constants (on-disk layout, LE) ---------------------- */
#define PXI_TP_S_RESERVED_LEN 0x0c /* reserved bytes length */
#define PXI_TP_S_EXTNAME_LEN  34   /* extname bytes length (no NUL) */

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
