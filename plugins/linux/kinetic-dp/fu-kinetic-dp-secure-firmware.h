/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 * Copyright 2021 Jeffrey Lin <jlin@kinet-ic.com>
 * Copyright 2022 Hai Su <hsu@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_KINETIC_DP_SECURE_FIRMWARE (fu_kinetic_dp_secure_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuKineticDpSecureFirmware,
		     fu_kinetic_dp_secure_firmware,
		     FU,
		     KINETIC_DP_SECURE_FIRMWARE,
		     FuFirmware)

guint32
fu_kinetic_dp_secure_firmware_get_esm_payload_size(FuKineticDpSecureFirmware *self);
guint32
fu_kinetic_dp_secure_firmware_get_arm_app_code_size(FuKineticDpSecureFirmware *self);
guint16
fu_kinetic_dp_secure_firmware_get_app_init_data_size(FuKineticDpSecureFirmware *self);
guint16
fu_kinetic_dp_secure_firmware_get_cmdb_block_size(FuKineticDpSecureFirmware *self);
gboolean
fu_kinetic_dp_secure_firmware_get_esm_xip_enabled(FuKineticDpSecureFirmware *self);
guint8
fu_kinetic_dp_secure_firmware_get_customer_project_id(FuKineticDpSecureFirmware *self);
