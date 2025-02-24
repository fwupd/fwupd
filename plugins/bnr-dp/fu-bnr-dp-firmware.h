/*
 * Copyright 2024 B&R Industrial Automation GmbH
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include <glib-object.h>
#include <glib.h>
#include <glib/gtypes.h>

#include "fu-bnr-dp-struct.h"
#include "fu-firmware.h"

#define FU_TYPE_BNR_DP_FIRMWARE (fu_bnr_dp_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuBnrDpFirmware, fu_bnr_dp_firmware, FU, BNR_DP_FIRMWARE, FuFirmware)

FuFirmware *
fu_bnr_dp_firmware_new(void);

/* payload is 3MiB, XML header is variable size but really shouldn't be very large */
#define FU_BNR_DP_FIRMWARE_SIZE	    (3 * 1024 * 1024)
#define FU_BNR_DP_FIRMWARE_SIZE_MAX (FU_BNR_DP_FIRMWARE_SIZE + (4 * 1024))

/* location of the payload header in firmware images */
#define FU_BNR_DP_FIRMWARE_HEADER_OFFSET 0x10

gboolean
fu_bnr_dp_firmware_parse_from_device(FuBnrDpFirmware *self,
				     const FuStructBnrDpFactoryData *factory_data,
				     const FuStructBnrDpPayloadHeader *fw_header,
				     GError **error);

gboolean
fu_bnr_dp_firmware_patch_boot_counter(FuBnrDpFirmware *self,
				      guint32 active_boot_counter,
				      GError **error);

gboolean
fu_bnr_dp_firmware_check(FuBnrDpFirmware *self,
			 const FuStructBnrDpFactoryData *factory_data,
			 const FuStructBnrDpPayloadHeader *active_header,
			 const FuStructBnrDpPayloadHeader *fw_header,
			 GError **error);
