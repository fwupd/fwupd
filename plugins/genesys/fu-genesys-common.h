/*
 * Copyright (C) 2021 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

typedef struct {
	guint8 reg;
	guint8 expected_val;
} FuGenesysWaitFlashRegisterHelper;

typedef enum {
	ISP_MODEL_UNKNOWN,

	/* hub */
	ISP_MODEL_HUB_GL3510,
	ISP_MODEL_HUB_GL3521,
	ISP_MODEL_HUB_GL3523,
	ISP_MODEL_HUB_GL3590,
	ISP_MODEL_HUB_GL7000,
	ISP_MODEL_HUB_GL3525,

	/* pd */
	ISP_MODEL_PD_GL9510,
} FuGenesysModel;

typedef struct {
	FuGenesysModel model;
	gint32 revision;
} FuGenesysChip;

typedef struct __attribute__((packed)) {
	guint8 tool_string_version;

	/* byte arrays are ASCII encoded and not NUL terminated */
	guint8 mask_project_code[4];
	guint8 mask_project_hardware[1]; /* 0=a, 1=b... */
	guint8 mask_project_firmware[2]; /* 01,02,03... */
	guint8 mask_project_ic_type[6];	 /* 352310=GL3523-10 (ASCII string) */

	guint8 running_project_code[4];
	guint8 running_project_hardware[1];
	guint8 running_project_firmware[2];
	guint8 running_project_ic_type[6];

	guint8 firmware_version[4]; /* MMmm=MM.mm (ASCII string) */
} FuGenesysStaticToolString;

#define GENESYS_USBHUB_FW_SIG_OFFSET	    0xFC
#define GENESYS_USBHUB_FW_SIG_LEN	    4
#define GENESYS_USBHUB_FW_SIG_TEXT_HUB	    "XROM"
#define GENESYS_USBHUB_FW_SIG_TEXT_HUB_SIGN "SROM"

#define GENESYS_USBHUB_CODE_SIZE_OFFSET			0xFB
#define GENESYS_USBHUB_VERSION_OFFSET			0x10E
#define GENESYS_USBHUB_STATIC_TOOL_STRING_OFFSET_GL3521 0x221
#define GENESYS_USBHUB_STATIC_TOOL_STRING_OFFSET_GL3523 0x221
#define GENESYS_USBHUB_STATIC_TOOL_STRING_OFFSET_GL3590 0x241
