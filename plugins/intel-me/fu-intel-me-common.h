/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

typedef struct __attribute__((packed)) {
	guint8 group_id;
	guint8 command : 7;
	guint8 is_resp : 1;
	guint8 rsvd;
	guint8 result;
} FuMkhiHeader;

typedef enum {
	MKHI_GROUP_ID_CBM,
	MKHI_GROUP_ID_PM, /* no longer used */
	MKHI_GROUP_ID_PWD,
	MKHI_GROUP_ID_FWCAPS,
	MKHI_GROUP_ID_APP,	/* no longer used */
	MKHI_GROUP_ID_FWUPDATE, /* for manufacturing downgrade */
	MKHI_GROUP_ID_FIRMWARE_UPDATE,
	MKHI_GROUP_ID_BIST,
	MKHI_GROUP_ID_MDES,
	MKHI_GROUP_ID_ME_DBG,
	MKHI_GROUP_ID_MCA, /* sometimes called "FPF" */
	MKHI_GROUP_ID_GEN = 0xFF
} FuMkhiGroupId;

#define MCA_READ_FILE_EX     0x02
#define MCA_READ_FILE_EX_CMD 0x0A

typedef enum {
	MKHI_STATUS_SUCCESS,
	MKHI_STATUS_INVALID_STATE,
	MKHI_STATUS_MESSAGE_SKIPPED,
	MKHI_STATUS_SIZE_ERROR = 0x05,
	MKHI_STATUS_NOT_SET = 0x0F,	  /* guessed */
	MKHI_STATUS_NOT_AVAILABLE = 0x18, /* guessed */
	MKHI_STATUS_INVALID_ACCESS = 0x84,
	MKHI_STATUS_INVALID_PARAMS = 0x85,
	MKHI_STATUS_NOT_READY = 0x88,
	MKHI_STATUS_NOT_SUPPORTED = 0x89,
	MKHI_STATUS_INVALID_ADDRESS = 0x8C,
	MKHI_STATUS_INVALID_COMMAND = 0x8D,
	MKHI_STATUS_FAILURE = 0x9E,
	MKHI_STATUS_INVALID_RESOURCE = 0xE4,
	MKHI_STATUS_RESOURCE_IN_USE = 0xE5,
	MKHI_STATUS_NO_RESOURCE = 0xE6,
	MKHI_STATUS_GENERAL_ERROR = 0xFF
} FuMkhiResult;

GString *
fu_intel_me_convert_checksum(GByteArray *buf, GError **error);
gboolean
fu_intel_me_mkhi_result_to_error(FuMkhiResult result, GError **error);
gboolean
fu_intel_me_mkhi_verify_header(const FuMkhiHeader *hdr_req,
			       const FuMkhiHeader *hdr_res,
			       GError **error);
