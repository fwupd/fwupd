/*
 * Copyright (C) 2022 Hai Su <hsu@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-kinetic-dp-aux-isp.h"
#include "fu-kinetic-dp-device.h"
#include "fu-kinetic-dp-firmware.h"

#define FU_TYPE_KINETIC_DP_PUMA_AUX_ISP (fu_kinetic_dp_puma_aux_isp_get_type())
G_DECLARE_FINAL_TYPE(FuKineticDpPumaAuxIsp,
		     fu_kinetic_dp_puma_aux_isp,
		     FU,
		     KINETIC_DP_PUMA_AUX_ISP,
		     FuKineticDpAuxIsp)

FuKineticDpPumaAuxIsp *
fu_kinetic_dp_puma_aux_isp_new(void);

gboolean
fu_kinetic_dp_puma_aux_isp_parse_app_fw(FuKineticDpFirmware *firmware,
					const guint8 *fw_bin_buf,
					gsize fw_bin_size,
					const guint16 fw_bin_flag,
					GError **error);
