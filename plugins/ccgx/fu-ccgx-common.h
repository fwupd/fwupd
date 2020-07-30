/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

/* metadata valid signature "CY" */
#define CCGX_METADATA_VALID_SIG 0x4359

typedef struct __attribute__((packed)) {
	guint8	fw_checksum;		/* firmware checksum */
	guint32	fw_entry;		/* firmware entry address */
	guint16	last_boot_row;		/* last flash row of bootloader or previous firmware */
	guint8	reserved1[2];		/* reserved */
	guint32	fw_size;		/* firmware size */
	guint8	reserved2[9];		/* reserved */
	guint16	metadata_valid;		/* metadata valid "CY" */
	guint8	reserved3[4];		/* reserved */
	guint32	boot_seq;		/* boot sequence number */
} CCGxMetaData;

/* firmware mode in device */
typedef enum {
	FW_MODE_BOOT = 0,
	FW_MODE_FW1,
	FW_MODE_FW2,
	FW_MODE_LAST
} FWMode;

/* firmware image type */
typedef enum {
	FW_IMAGE_TYPE_UNKNOWN = 0,
	FW_IMAGE_TYPE_SINGLE,
	FW_IMAGE_TYPE_DUAL_SYMMETRIC,		/* A/B runtime */
	FW_IMAGE_TYPE_DUAL_ASYMMETRIC,		/* A=bootloader (fixed), B=runtime */
	FW_IMAGE_TYPE_DUAL_ASYMMETRIC_VARIABLE,	/* A=bootloader (variable), B=runtime */
	FW_IMAGE_TYPE_DMC_COMPOSITE,		/* composite firmware image for dmc */
} FWImageType;

gchar		*fu_ccgx_version_to_string		(guint32	 val);
const gchar	*fu_ccgx_fw_mode_to_string		(FWMode		 val);
FWMode		 fu_ccgx_fw_mode_get_alternate		(FWMode		 val);
const gchar	*fu_ccgx_fw_image_type_to_string	(FWImageType	 val);
FWImageType	 fu_ccgx_fw_image_type_from_string	(const gchar	*val);
