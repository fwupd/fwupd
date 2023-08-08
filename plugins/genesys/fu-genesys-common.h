/*
 * Copyright (C) 2023 Adam.Chen <Adam.Chen@genesyslogic.com.tw>
 * Copyright (C) 2021 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

typedef struct {
	guint8 N[0x206];
	guint8 E[0x00c];
} FuGenesysPublicKey;

typedef struct {
	guint8 reg;
	guint8 expected_val;
} FuGenesysWaitFlashRegisterHelper;

typedef enum {
	ISP_MODEL_UNKNOWN,

	/* hub */
	ISP_MODEL_HUB_GL3521, /* EOL */
	ISP_MODEL_HUB_GL3523,
	ISP_MODEL_HUB_GL3510,
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

#define GENESYS_USBHUB_FW_SIG_OFFSET	      0xFC
#define GENESYS_USBHUB_FW_SIG_LEN	      4
#define GENESYS_USBHUB_FW_SIG_TEXT_HUB	      "XROM"
#define GENESYS_USBHUB_FW_SIG_TEXT_HUB_SIGN   "SROM" // not formal usage
#define GENESYS_USBHUB_FW_SIG_TEXT_DEV_BRIDGE "HOST"
#define GENESYS_USBHUB_FW_SIG_TEXT_PD	      "PRDY"

#define GENESYS_USBHUB_FW_CONFIGURATION_OFFSET	       0x100
#define GENESYS_USBHUB_FW_CONFIGURATION_WITHOUT_SERIAL 0x55
#define GENESYS_USBHUB_FW_CONFIGURATION_WITH_SERIAL    0xAA
#define GENESYS_USBHUB_FW_CONFIGURATION_NEW_FORMAT     0xA5
#define GENESYS_USBHUB_FW_CONFIGURATION_NEW_FORMAT_V2  0xA6

#define GENESYS_USBHUB_CODE_SIZE_OFFSET			   0xFB
#define GENESYS_USBHUB_VERSION_OFFSET			   0x10E
#define GENESYS_USBHUB_STATIC_TOOL_STRING_OFFSET_GL3521	   0x221
#define GENESYS_USBHUB_STATIC_TOOL_STRING_OFFSET_GL3523	   0x221
#define GENESYS_USBHUB_STATIC_TOOL_STRING_OFFSET_GL3590	   0x241
#define GENESYS_USBHUB_STATIC_TOOL_STRING_OFFSET_GL3525	   0x251
#define GENESYS_USBHUB_STATIC_TOOL_STRING_OFFSET_GL3525_V2 0x1E1
