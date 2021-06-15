/*
 * Copyright (C) 2021 Jimmy Yu <Jimmy_yu@pixart.com>
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

/* OTA spec check result */
enum ota_spec_check_result {
	OTA_SPEC_CHECK_OK		= 1,	/* Spec check ok */
	OTA_FW_OUT_OF_BOUNDS		= 2,	/* OTA firmware size out of bound */
	OTA_PROCESS_ILLEGAL		= 3,	/* Illegal OTA process */
	OTA_RECONNECT			= 4,	/* Inform OTA app do reconnect */
	OTA_FW_IMG_VERSION_ERROR	= 5,	/* FW image file version check error */
	OTA_DEVICE_LOW_BATTERY		= 6,	/* Device is under low battery */
	OTA_SPEC_CHECK_MAX_NUM,			/* Max number of OTA driver defined error code */
};

struct ota_fw_state {
	guint8		 status;
	guint8		 new_flow;
	guint16		 offset;
	guint16		 checksum;
	guint32		 max_object_size;
	guint16		 mtu_size;
	guint16		 prn_threshold;
	guint8		 spec_check_result;
};

guint8		 fu_pxi_common_sum8		(const guint8	*buf,
						 gsize		 bufsz);
guint16		 fu_pxi_common_sum16		(const guint8	*buf,
						 gsize		 bufsz);
const gchar	*fu_pxi_spec_check_result_to_string (guint8	 spec_check_result);

void		 fu_pxi_ota_fw_state_to_string	(struct ota_fw_state *fwstate,
						 guint		 idt,
						 GString	*str);
gboolean	 fu_pxi_ota_fw_state_parse	(struct ota_fw_state *fwstate,
						 const guint8	*buf,
						 gsize		 bufsz,
						 gsize		 offset,
						 GError		**error);
