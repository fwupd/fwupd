/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-dfu-firmware.h"

/**
 * DfuVersion:
 * @DFU_VERSION_UNKNOWN:			Format unknown
 * @DFU_VERSION_DFU_1_0:			DFU 1.0
 * @DFU_VERSION_DFU_1_1:			DFU 1.1
 * @DFU_VERSION_DFUSE:				DfuSe
 * @DFU_VERSION_ATMEL_AVR:			Atmel AVR
 *
 * The known versions of the DFU standard in BCD format.
 **/
typedef enum {
	DFU_VERSION_UNKNOWN			= 0,
	DFU_VERSION_DFU_1_0			= 0x0100,
	DFU_VERSION_DFU_1_1			= 0x0110,
	DFU_VERSION_DFUSE			= 0x011a, /* defined by ST */
	DFU_VERSION_ATMEL_AVR			= 0xff01, /* made up */
	/*< private >*/
	DFU_VERSION_LAST
} DfuVersion;

guint8		 fu_dfu_firmware_get_footer_len		(FuDfuFirmware	*self);
GBytes		*fu_dfu_firmware_append_footer		(FuDfuFirmware	*self,
							 GBytes		*contents,
							 GError		**error);
gboolean	 fu_dfu_firmware_parse_footer		(FuDfuFirmware	*self,
							 GBytes		*fw,
							 FwupdInstallFlags flags,
							 GError		**error);
