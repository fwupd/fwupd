/*
 * Copyright 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_GENESYS_USBHUB_DEVICE (fu_genesys_usbhub_device_get_type())

/* what is known about the firmware stored in a bank */
typedef enum {
	FU_GENESYS_USBHUB_BANK_STATE_UNKNOWN, /* not read, or the read failed */
	FU_GENESYS_USBHUB_BANK_STATE_VALID,   /* read, and the firmware is valid */
	FU_GENESYS_USBHUB_BANK_STATE_INVALID, /* read, and there is no valid firmware */
} FuGenesysUsbhubBankState;

G_DECLARE_FINAL_TYPE(FuGenesysUsbhubDevice,
		     fu_genesys_usbhub_device,
		     FU,
		     GENESYS_USBHUB_DEVICE,
		     FuUsbDevice)

FuGenesysUsbhubBankState
fu_genesys_usbhub_device_bank_state_from_error(const GError *error);
FuGenesysUsbhubBankState
fu_genesys_usbhub_device_bank_state_from_image(const guint8 *buf, gsize bufsz);
gboolean
fu_genesys_usbhub_device_select_update_bank2(gboolean is_gl3523,
					     guint16 bank1_version,
					     guint16 bank2_version,
					     gboolean *backup_bank1);
gboolean
fu_genesys_usbhub_device_validate_bank_code_size(guint32 code_size,
						 guint32 capacity,
						 GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_genesys_usbhub_device_check_update_bank(gboolean banks_compared,
					   gboolean newest_bank_runs,
					   gboolean update_bank2,
					   FuGenesysUsbhubBankState bank1_state,
					   FuGenesysUsbhubBankState bank2_state,
					   guint16 bank1_version,
					   guint16 bank2_version,
					   gboolean other_bank_runs,
					   guint16 target_version,
					   GError **error);
