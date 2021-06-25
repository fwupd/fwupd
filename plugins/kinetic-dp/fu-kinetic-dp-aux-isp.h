/*
 * Copyright (C) 2021 Jeffrey Lin <jlin@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include <glib.h>

#include "fu-kinetic-dp-aux-dpcd.h"
#include "fu-kinetic-dp-common.h"
#include "fu-kinetic-dp-connection.h"

#define FU_TYPE_KINETIC_DP_AUX_ISP (fu_kinetic_dp_aux_isp_get_type())
G_DECLARE_DERIVABLE_TYPE(FuKineticDpAuxIsp, fu_kinetic_dp_aux_isp, FU, KINETIC_DP_AUX_ISP, GObject)

typedef enum {
	KT_FW_STATE_RUN_NONE = 0,
	KT_FW_STATE_RUN_IROM = 1,
	KT_FW_STATE_RUN_BOOT_CODE = 2,
	KT_FW_STATE_RUN_APP = 3,
	KT_FW_STATE_NUM = 4
} KtFwRunState;

typedef struct {
	guint32 std_fw_ver;
	guint16 boot_code_ver;
	guint16 std_cmdb_ver;
	guint32 cmdb_rev;
	guint16 customer_fw_ver;
	guint8 customer_project_id;
} KtDpFwInfo;

typedef enum {
	BANK_A = 0,
	BANK_B = 1,
	BANK_TOTAL = 2,

	BANK_NONE = 0xFF
} KtFlashBankIdx;

typedef struct {
	KtChipId chip_id;
	guint16 chip_rev;
	guint8 chip_type;
	guint32 chip_sn;
	KtFwRunState fw_run_state;
	KtDpFwInfo fw_info;
	guint8 branch_id_str[DPCD_SIZE_BRANCH_DEV_ID_STR];
	gboolean is_dual_bank_supported;
	KtFlashBankIdx flash_bank_idx;
} KtDpDevInfo;

typedef enum {
	DEV_HOST = 0,
	DEV_PORT1 = 1,
	DEV_PORT2 = 2,
	DEV_PORT3 = 3,
	MAX_DEV_NUM = 4,
	DEV_ALL = 0xFF
} KtDpDevPort;

/* forward declaration */
typedef struct _FuKineticDpDevice FuKineticDpDevice;

struct _FuKineticDpAuxIspClass {
	GObjectClass parent_class;
	gboolean (*get_device_info)(FuKineticDpAuxIsp *self,
				    FuKineticDpDevice *device,
				    KtDpDevInfo *dev_info,
				    GError **error);
	gboolean (*start)(FuKineticDpAuxIsp *self,
			  FuKineticDpDevice *device,
			  FuFirmware *firmware,
			  FuProgress *progress,
			  const KtDpDevInfo *dev_info,
			  GError **error);
	/*< private >*/
	gpointer padding[10];
};

FuKineticDpAuxIsp *
fu_kinetic_dp_aux_isp_new(void);

const gchar *
fu_kinetic_dp_aux_isp_get_chip_id_str(KtChipId chip_id);
const gchar *
fu_kinetic_dp_aux_isp_get_fw_run_state_str(KtFwRunState fw_run_state);
gboolean
fu_kinetic_dp_aux_isp_enable_aux_forward(FuKineticDpConnection *connection,
					 KtChipId root_dev_chip_id,
					 KtFwRunState root_dev_state,
					 KtDpDevPort target_port,
					 GError **error);
gboolean
fu_kinetic_dp_aux_isp_disable_aux_forward(FuKineticDpConnection *connection,
					  KtChipId root_dev_chip_id,
					  KtFwRunState root_dev_state,
					  GError **error);
gboolean
fu_kinetic_dp_aux_isp_read_basic_device_info(FuKineticDpDevice *device,
					     KtDpDevPort target_port,
					     KtDpDevInfo **dev_info,
					     GError **error);
gboolean
fu_kinetic_dp_aux_isp_get_device_info(FuKineticDpAuxIsp *self,
				      FuKineticDpDevice *device,
				      KtDpDevPort target_port,
				      GError **error);
gboolean
fu_kinetic_dp_aux_isp_start(FuKineticDpAuxIsp *self,
			    FuKineticDpDevice *device,
			    FuFirmware *firmware,
			    FuProgress *progress,
			    GError **error);
