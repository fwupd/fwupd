/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#include "fu-ccgx-struct.h"

/* metadata valid signature "CY" */
#define CCGX_METADATA_VALID_SIG 0x4359

typedef struct __attribute__((packed)) {
	guint8 fw_checksum;	/* firmware checksum */
	guint32 fw_entry;	/* firmware entry address */
	guint16 last_boot_row;	/* last flash row of bootloader or previous firmware */
	guint8 reserved1[2];	/* reserved */
	guint32 fw_size;	/* firmware size */
	guint8 reserved2[9];	/* reserved */
	guint16 metadata_valid; /* metadata valid "CY" */
	guint8 reserved3[4];	/* reserved */
	guint32 boot_seq;	/* boot sequence number */
} CCGxMetaData;

gchar *
fu_ccgx_version_to_string(guint32 val);
FuCcgxFwMode
fu_ccgx_fw_mode_get_alternate(FuCcgxFwMode val);
