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
