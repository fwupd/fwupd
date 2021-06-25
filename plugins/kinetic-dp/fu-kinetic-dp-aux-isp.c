/*
 * Copyright (C) 2021 Jeffrey Lin <jlin@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "fu-kinetic-dp-aux-isp.h"

#include <fwupdplugin.h>

#include "fu-kinetic-dp-aux-dpcd.h"
#include "fu-kinetic-dp-secure-aux-isp.h"

typedef struct {
	KtChipId root_dev_chip_id;
	KtFwRunState root_dev_state;
} FuKineticDpAuxIspPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuKineticDpAuxIsp, fu_kinetic_dp_aux_isp, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fu_kinetic_dp_aux_isp_get_instance_private(o))

typedef struct {
	KtChipId chip_id;
	KtFwRunState fw_run_state;
	guint8 id_str[DPCD_SIZE_BRANCH_DEV_ID_STR];
	guint8 str_len;
} KtDpChipBrIdStrTable;

/* Kinetic chip DPCD branch ID string table */
static const KtDpChipBrIdStrTable kt_dp_branch_dev_info_table[] = {
    /* Jaguar MCDP50x0 */
    {KT_CHIP_JAGUAR_5000, KT_FW_STATE_RUN_IROM, {'5', '0', '1', '0', 'I', 'R'}, 6},
    {KT_CHIP_JAGUAR_5000, KT_FW_STATE_RUN_APP, {'K', 'T', '5', '0', 'X', '0'}, 6},
    /* Mustang MCDP52x0 */
    {KT_CHIP_MUSTANG_5200, KT_FW_STATE_RUN_IROM, {'5', '2', '1', '0', 'I', 'R'}, 6},
    {KT_CHIP_MUSTANG_5200, KT_FW_STATE_RUN_APP, {'K', 'T', '5', '2', 'X', '0'}, 6},
};

static KtDpDevInfo dp_dev_infos[MAX_DEV_NUM];
static const gchar *kt_dp_fw_run_state_strs[KT_FW_STATE_NUM] = {"iROM",
								"App",
								"Boot-Code",
								"Unknown"};

const gchar *
fu_kinetic_dp_aux_isp_get_chip_id_str(KtChipId chip_id)
{
	if (chip_id == KT_CHIP_JAGUAR_5000)
		return "KTM50X0";
	if (chip_id == KT_CHIP_MUSTANG_5200)
		return "KTM52X0";
	return "";
}

const gchar *
fu_kinetic_dp_aux_isp_get_fw_run_state_str(KtFwRunState fw_run_state)
{
	return (fw_run_state < KT_FW_STATE_NUM) ? kt_dp_fw_run_state_strs[fw_run_state] : NULL;
}

static gboolean
fu_kinetic_dp_aux_isp_get_basic_dev_info_from_branch_id(const guint8 *br_id_str_buf,
							const guint8 br_id_str_buf_size,
							KtDpDevInfo *dev_info,
							GError **error)
{
	guint32 num = G_N_ELEMENTS(kt_dp_branch_dev_info_table);
	g_autofree gchar *str = NULL;

	g_return_val_if_fail(br_id_str_buf != NULL, FALSE);
	g_return_val_if_fail(br_id_str_buf_size >= DPCD_SIZE_BRANCH_DEV_ID_STR, FALSE);
	g_return_val_if_fail(dev_info != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	dev_info->chip_id = KT_CHIP_NONE;
	dev_info->fw_run_state = KT_FW_STATE_RUN_NONE;

	/* find the device info by branch ID string */
	for (guint32 i = 0; i < num; i++) {
		if (memcmp(br_id_str_buf,
			   kt_dp_branch_dev_info_table[i].id_str,
			   kt_dp_branch_dev_info_table[i].str_len) == 0) {
			/* found the chip in the table */
			dev_info->chip_id = kt_dp_branch_dev_info_table[i].chip_id;
			dev_info->fw_run_state = kt_dp_branch_dev_info_table[i].fw_run_state;

			return TRUE;
		}
	}

	/* there might not always be null-terminated character '\0' in DPCD branch ID string (when
	 * length is 6) */
	str = g_strndup((const gchar *)br_id_str_buf, DPCD_SIZE_BRANCH_DEV_ID_STR);
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INTERNAL,
		    "%s is not a supported Kinetic device",
		    str);

	return FALSE;
}

gboolean
fu_kinetic_dp_aux_isp_enable_aux_forward(FuKineticDpConnection *connection,
					 KtChipId root_dev_chip_id,
					 KtFwRunState root_dev_state,
					 KtDpDevPort target_port,
					 GError **error)
{
	if (root_dev_state != KT_FW_STATE_RUN_APP) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Host device [%s %s] doesn't support to enable AUX forwarding!",
			    fu_kinetic_dp_aux_isp_get_chip_id_str(root_dev_chip_id),
			    fu_kinetic_dp_aux_isp_get_fw_run_state_str(root_dev_state));

		return FALSE;
	}

	if (root_dev_chip_id == KT_CHIP_JAGUAR_5000 || root_dev_chip_id == KT_CHIP_MUSTANG_5200) {
		if (!fu_kinetic_dp_secure_aux_isp_enable_aux_forward(connection,
								     target_port,
								     error)) {
			g_prefix_error(error, "failed to enable AUX forwarding: ");
			return FALSE;
		}

		g_usleep(10 * 1000); /* Wait 10ms for host to process AUX forwarding command */

		return TRUE;
	}

	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INTERNAL,
		    "Host device [%s] doesn't support to enable AUX forwarding!",
		    fu_kinetic_dp_aux_isp_get_chip_id_str(root_dev_chip_id));
	return FALSE;
}

gboolean
fu_kinetic_dp_aux_isp_disable_aux_forward(FuKineticDpConnection *connection,
					  KtChipId root_dev_chip_id,
					  KtFwRunState root_dev_state,
					  GError **error)
{
	if (root_dev_state != KT_FW_STATE_RUN_APP) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Host device [%s %s] doesn't support to disable AUX forwarding!",
			    fu_kinetic_dp_aux_isp_get_chip_id_str(root_dev_chip_id),
			    fu_kinetic_dp_aux_isp_get_fw_run_state_str(root_dev_state));

		return FALSE;
	}

	if (root_dev_chip_id == KT_CHIP_JAGUAR_5000 || root_dev_chip_id == KT_CHIP_MUSTANG_5200) {
		g_usleep(5 * 1000); /* wait 5ms */
		return fu_kinetic_dp_secure_aux_isp_disable_aux_forward(connection, error);
	}

	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INTERNAL,
		    "Host device [%s] doesn't support to disable AUX forwarding!",
		    fu_kinetic_dp_aux_isp_get_chip_id_str(root_dev_chip_id));

	return FALSE;
}

gboolean
fu_kinetic_dp_aux_isp_read_basic_device_info(FuKineticDpDevice *device,
					     KtDpDevPort target_port,
					     KtDpDevInfo **dev_info,
					     GError **error)
{
	g_autoptr(FuKineticDpConnection) connection = NULL;
	KtDpDevInfo dev_info_local;

	dev_info_local.chip_id = KT_CHIP_NONE;
	dev_info_local.chip_type = 0;
	dev_info_local.chip_sn = 0;
	dev_info_local.fw_run_state = KT_FW_STATE_RUN_NONE;
	dev_info_local.fw_info.std_fw_ver = 0;
	dev_info_local.fw_info.std_cmdb_ver = 0;
	dev_info_local.fw_info.cmdb_rev = 0;
	dev_info_local.fw_info.boot_code_ver = 0;
	dev_info_local.fw_info.customer_project_id = 0;
	dev_info_local.fw_info.customer_fw_ver = 0;
	dev_info_local.is_dual_bank_supported = FALSE;
	dev_info_local.flash_bank_idx = BANK_NONE;

	connection = fu_kinetic_dp_connection_new(fu_udev_device_get_fd(FU_UDEV_DEVICE(device)));

	/* basic chip information (chip ID, F/W work state) is obtained from DPCD branch device ID
	 * string */
	if (!fu_kinetic_dp_aux_dpcd_read_branch_id_str(connection,
						       dev_info_local.branch_id_str,
						       sizeof(dev_info_local.branch_id_str),
						       error))
		return FALSE;

	if (!fu_kinetic_dp_aux_isp_get_basic_dev_info_from_branch_id(
		dev_info_local.branch_id_str,
		sizeof(dev_info_local.branch_id_str),
		&dev_info_local,
		error))
		return FALSE;

	/* store read info to static allocated structure */
	if (!fu_memcpy_safe((guint8 *)&dp_dev_infos[target_port],
			    sizeof(dp_dev_infos[target_port]),
			    0x0, /* dst */
			    (guint8 *)&dev_info_local,
			    sizeof(dev_info_local),
			    0x0, /* src */
			    sizeof(KtDpDevInfo),
			    error)) /* size */
		return FALSE;

	/* assign pointer to specified structure to output parameter */
	*dev_info = &dp_dev_infos[target_port];

	return TRUE;
}

gboolean
fu_kinetic_dp_aux_isp_get_device_info(FuKineticDpAuxIsp *self,
				      FuKineticDpDevice *device,
				      KtDpDevPort target_port,
				      GError **error)
{
	FuKineticDpAuxIspClass *klass = FU_KINETIC_DP_AUX_ISP_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_KINETIC_DP_AUX_ISP(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* no chip-specific method */
	if (klass->get_device_info == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "not supported to get detailed device info");
		return FALSE;
	}

	/* call vfunc */
	return klass->get_device_info(self, device, &dp_dev_infos[target_port], error);
}

gboolean
fu_kinetic_dp_aux_isp_start(FuKineticDpAuxIsp *self,
			    FuKineticDpDevice *device,
			    FuFirmware *firmware,
			    FuProgress *progress,
			    GError **error)
{
	FuKineticDpAuxIspClass *klass = FU_KINETIC_DP_AUX_ISP_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_KINETIC_DP_AUX_ISP(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* no chip-specific method */
	if (klass->start == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "not supported to start AUX-ISP");
		return FALSE;
	}

	/* TODO: Only test ISP for host device now
	 *        AUX-ISP for DFP devices is not implemented yet
	 */

	/* call vfunc */
	return klass->start(self, device, firmware, progress, &dp_dev_infos[DEV_HOST], error);
}

static void
fu_kinetic_dp_aux_isp_init(FuKineticDpAuxIsp *self)
{
	FuKineticDpAuxIspPrivate *priv = GET_PRIVATE(self);

	priv->root_dev_chip_id = KT_CHIP_NONE;
	priv->root_dev_state = KT_FW_STATE_RUN_NONE;
}

static void
fu_kinetic_dp_aux_isp_class_init(FuKineticDpAuxIspClass *klass)
{
}

FuKineticDpAuxIsp *
fu_kinetic_dp_aux_isp_new(void)
{
	FuKineticDpAuxIsp *self = g_object_new(FU_TYPE_KINETIC_DP_AUX_ISP, NULL);
	return FU_KINETIC_DP_AUX_ISP(self);
}
