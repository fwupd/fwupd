/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 * Copyright 2021 Jeffrey Lin <jlin@kinet-ic.com>
 * Copyright 2022 Hai Su <hsu@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-kinetic-dp-struct.h"

struct _FuKineticDpDeviceClass {
	FuDpauxDeviceClass parent_class;
};

#define FU_TYPE_KINETIC_DP_DEVICE (fu_kinetic_dp_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuKineticDpDevice,
			 fu_kinetic_dp_device,
			 FU,
			 KINETIC_DP_DEVICE,
			 FuDpauxDevice)

/* OUI for KT */
#define MCA_OUI_BYTE_0 0x00
#define MCA_OUI_BYTE_1 0x60
#define MCA_OUI_BYTE_2 0xAD

/* native DPCD fields defined in DP spec */
#define DPCD_ADDR_IEEE_OUI	    0x00300
#define DPCD_SIZE_IEEE_OUI	    3
#define DPCD_ADDR_BRANCH_DEV_ID_STR 0x00503
#define DPCD_ADDR_BRANCH_FW_SUB	    0x00508
#define DPCD_ADDR_BRANCH_HW_REV	    0x00509
#define DPCD_ADDR_BRANCH_FW_MAJ_REV 0x0050A
#define DPCD_ADDR_BRANCH_FW_MIN_REV 0x0050B
#define DPCD_ADDR_CUSTOMER_ID	    0x00515
#define DPCD_ADDR_CUSTOMER_BOARD    0x0050F

/* vendor-specific DPCD fields defined for Kinetic's usage */
#define DPCD_ADDR_BRANCH_FW_REV 0x0050C

#define FU_KINETIC_DP_DEVICE_TIMEOUT 1000

void
fu_kinetic_dp_device_set_fw_state(FuKineticDpDevice *self, FuKineticDpFwState fw_state);
FuKineticDpFwState
fu_kinetic_dp_device_get_fw_state(FuKineticDpDevice *self);
void
fu_kinetic_dp_device_set_chip_id(FuKineticDpDevice *self, FuKineticDpChip chip_id);
gboolean
fu_kinetic_dp_device_dpcd_read_oui(FuKineticDpDevice *self,
				   guint8 *buf,
				   gsize bufsz,
				   GError **error);
gboolean
fu_kinetic_dp_device_dpcd_write_oui(FuKineticDpDevice *self, const guint8 *buf, GError **error);
