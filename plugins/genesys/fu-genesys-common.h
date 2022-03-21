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
	guint8 tool_string_version; /* 0xff = not supported */

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

typedef struct __attribute__((packed)) {
	guint8 running_mode; /* 'M' or 'C' */

	guint8 ss_port_number; /* super-speed port number */
	guint8 hs_port_number; /* high-speed port number */

	guint8 ss_connection_status; /* bit field. ON = DFP is a super-speed device */
	guint8 hs_connection_status; /* bit field. ON = DFP is a high-speed device */
	guint8 fs_connection_status; /* bit field. ON = DFP is a full-speed device */
	guint8 ls_connection_status; /* bit field. ON = DFP is a low-speed device */

	guint8 charging;		  /* bit field. ON = DFP is a charging port */
	guint8 non_removable_port_status; /* bit field. ON = DFP is a non-removable port */

	/*
	 * Bonding reports Hardware register status for GL3523:
	 *   2 / 4 ports         : 1 means 4 ports, 0 means 2 ports
	 *   MTT / STT           : 1 means Multi Token Transfer, 0 means Single TT
	 *   Type - C            : 1 means disable, 0 means enable
	 *   QC                  : 1 means disable, 0 means enable
	 *   Flash dump location : 1 means 32KB offset bank 1, 0 means 0 offset bank 0.
	 *
	 * Tool string Version 1:
	 *   Bit3 : Flash dump location
	 *   BIT2 : Type - C
	 *   BIT1 : MTT / STT
	 *   BIT0 : 2 / 4 ports
	 *
	 * Tool string Version 2 or newer :
	 *   Bit4 : Flash dump location
	 *   BIT3 : Type - C
	 *   BIT2 : MTT / STT
	 *   BIT1 : 2 / 4 ports
	 *   BIT0 : QC
	 *
	 * Default use '0'~'F', plus Bit4 may over value, should extract that.
	 *
	 * Bonding for GL3590:
	 *   Bit7 : Flash dump location, 0 means bank 0, 1 means bank 1.
	 */
	guint8 bonding;

	guint8 reserved[22];
} FuGenesysDynamicToolString;

#define GENESYS_USBHUB_FW_SIG_OFFSET	    0xFC
#define GENESYS_USBHUB_FW_SIG_LEN	    4
#define GENESYS_USBHUB_FW_SIG_TEXT_HUB	    "XROM"
#define GENESYS_USBHUB_FW_SIG_TEXT_HUB_SIGN "SROM"

#define GENESYS_USBHUB_CODE_SIZE_OFFSET			0xFB
#define GENESYS_USBHUB_VERSION_OFFSET			0x10E
#define GENESYS_USBHUB_STATIC_TOOL_STRING_OFFSET_GL3523 0x221
#define GENESYS_USBHUB_STATIC_TOOL_STRING_OFFSET_GL3590 0x241
