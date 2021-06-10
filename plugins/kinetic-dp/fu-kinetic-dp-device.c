/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2021 Jeffrey Lin <jlin@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fcntl.h>

#include "fu-kinetic-dp-common.h"
#include "fu-kinetic-dp-connection.h"
#include "fu-kinetic-dp-device.h"
#include "fu-kinetic-dp-firmware.h"
#include "fu-kinetic-secure-aux-isp.h"

#define KINETIC_DP_CUSTOM_UPDATE_PROTOCOL   "com.kinet-ic.dp"
#define KINETIC_DP_FWUPD_PLUGIN_NAME        "kinetic_dp"

#define INIT_CRC16                          0x1021

struct _FuKineticDpDevice {
    FuUdevDevice        parent_instance;
    gchar *             system_type;
    FuKineticDpFamily   family;
    FuKineticDpMode     mode;
    guint16             chip_id;

    // <TODO> Declare as private member
    guint32 isp_payload_procd_size;
    guint32 isp_procd_size;
    guint32 isp_total_data_size;

    guint16 read_flash_prog_time;
    guint16 flash_id;
    guint16 flash_size;

    gboolean is_isp_secure_auth_mode;
};

G_DEFINE_TYPE (FuKineticDpDevice, fu_kinetic_dp_device, FU_TYPE_UDEV_DEVICE)

static void
fu_kinetic_dp_device_finalize(GObject *object)
{
	FuKineticDpDevice *self = FU_KINETIC_DP_DEVICE(object);

	g_free(self->system_type);

	G_OBJECT_CLASS(fu_kinetic_dp_device_parent_class)->finalize(object);
}

static void
fu_kinetic_dp_device_init(FuKineticDpDevice *self)
{
    self->read_flash_prog_time = 10;
    self->flash_id = 0;
    self->flash_size = 0;

    self->isp_payload_procd_size = 0;
    self->isp_procd_size = 0;
    self->isp_total_data_size = 0;
    self->is_isp_secure_auth_mode = TRUE;

	fu_device_set_protocol(FU_DEVICE(self), KINETIC_DP_CUSTOM_UPDATE_PROTOCOL);
	fu_device_set_vendor(FU_DEVICE(self), "Kinetic");
	fu_device_add_vendor_id(FU_DEVICE(self), "DRM_DP_AUX_DEV:0x[TBD]");    // <TODO> How to determine the vendor ID?
	fu_device_set_summary(FU_DEVICE(self), "Multi-Stream Transport Device");
	fu_device_add_icon(FU_DEVICE(self), "video-display");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_udev_device_set_flags(FU_UDEV_DEVICE(self),
                             FU_UDEV_DEVICE_FLAG_OPEN_READ |
                             FU_UDEV_DEVICE_FLAG_OPEN_WRITE |
                             FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT);
}

static gboolean
fu_kinetic_dp_device_probe(FuDevice *device, GError **error)
{
	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_kinetic_dp_device_parent_class)->probe(device, error))
		return FALSE;

	/* get from sysfs if not set from tests */
	if (fu_device_get_logical_id(device) == NULL)
	{
		g_autofree gchar *logical_id = NULL;
		logical_id = g_path_get_basename(fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device)));
		fu_device_set_logical_id(device, logical_id);
	}

	return fu_udev_device_set_physical_id(FU_UDEV_DEVICE (device), "pci,drm_dp_aux_dev", error);
}

static FuFirmware *
fu_kinetic_dp_device_prepare_firmware(FuDevice *device,
                                       GBytes *fw,
                                       FwupdInstallFlags flags,
                                       GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_kinetic_dp_firmware_new();

    /* parse input firmware file to two images */
    if (!fu_firmware_parse(firmware, fw, flags, error))
        return NULL;

    return g_steal_pointer(&firmware);
}

// OUI of MegaChips America
#define MCA_OUI_BYTE_0                          0x00
#define MCA_OUI_BYTE_1                          0x60
#define MCA_OUI_BYTE_2                          0xAD

/* Native DPCD fields defined in DP spec. */
#define DPCD_ADDR_IEEE_OUI                      0x00300
#define DPCD_SIZE_IEEE_OUI                      3   
#define DPCD_ADDR_BRANCH_DEV_ID_STR             0x00503
#define DPCD_SIZE_BRANCH_DEV_ID_STR             6
#define DPCD_ADDR_BRANCH_HW_REV                 0x00509
#define DPCD_SIZE_BRANCH_HW_REV                 1

/* Kinetic proprietary DPCD fields for Jaguar/Mustang, for both application and ISP driver */
#define DPCD_ADDR_FLOAT_CMD_STATUS_REG          0x0050D
#define DPCD_ADDR_FLOAT_PARAM_REG               0x0050E

/* Below DPCD registers are used while runnig application */
#define DPCD_ADDR_FLOAT_CUSTOMER_FW_MIN_REV     0x00514
#define DPCD_SIZE_FLOAT_CUSTOMER_FW_MIN_REV     1
#define DPCD_ADDR_FLOAT_CUSTOMER_PROJ_ID        0x00515
#define DPCD_SIZE_FLOAT_CUSTOMER_PROJ_ID        1
#define DPCD_ADDR_FLOAT_PRODUCT_TYPE            0x00516
#define DPCD_SIZE_FLOAT_PRODUCT_TYPE            1

/* Below DPCD registers are used while runnig ISP driver */
#define DPCD_ADDR_FLOAT_ISP_REPLY_LEN_REG       0x00513
#define DPCD_SIZE_FLOAT_ISP_REPLY_LEN_REG       1           // 0x00513

#define DPCD_ADDR_FLOAT_ISP_REPLY_DATA_REG      0x00514     // While running ISP driver
#define DPCD_SIZE_FLOAT_ISP_REPLY_DATA_REG      12          // 0x00514 ~ 0x0051F

#define DPCD_ADDR_KT_AUX_WIN			        0x80000ul
#define DPCD_SIZE_KT_AUX_WIN                    0x8000ul    // 0x80000ul ~ 0x87FFF
#define DPCD_ADDR_KT_AUX_WIN_END		        (DPCD_ADDR_KT_AUX_WIN +  DPCD_SIZE_KT_AUX_WIN - 1)

typedef enum
{
    BANK_A     = 0,
    BANK_B     = 1,
    BANK_TOTAL = 2,

    BANK_NONE  = 0xFF
} KtFlashBankIdx;

typedef struct
{
    guint32 std_fw_ver;
    guint16 boot_code_ver;
    guint16 std_cmdb_ver;
    guint32 cmdb_rev;
    guint16 customer_fw_ver;
    guint8  customer_project_id;
} KtDpFwInfo;

typedef struct
{
    KtChipId       chip_id;
    guint16        chip_rev;
    guint8         chip_type;
    guint32        chip_sn;
    KtFwRunState   fw_run_state;
    KtDpFwInfo     fw_info;
    guint8         branch_id_str[DPCD_SIZE_BRANCH_DEV_ID_STR];
    gboolean       is_dual_bank_supported;
    KtFlashBankIdx flash_bank_idx;
} KtDpDevInfo;

typedef struct
{
	KtChipId     chip_id;
    KtFwRunState fw_run_state;
    guint8       id_str[DPCD_SIZE_BRANCH_DEV_ID_STR];
    guint8       str_len;
} KtDpChipBrIdStrTable;

// ---------------------------------------------------------------
// Kinetic chip DPCD branch ID string table
// ---------------------------------------------------------------
static const KtDpChipBrIdStrTable kt_dp_branch_dev_info_table[] = 
{
    // Jaguar MCDP50x0
    { KT_CHIP_JAGUAR_5000,  KT_FW_STATE_RUN_IROM, {'5', '0', '1', '0', 'I', 'R'}, 6},
    { KT_CHIP_JAGUAR_5000,  KT_FW_STATE_RUN_APP,  {'K', 'T', '5', '0', 'X', '0'}, 6},
    // Mustang MCDP52x0
    { KT_CHIP_MUSTANG_5200, KT_FW_STATE_RUN_IROM, {'5', '2', '1', '0', 'I', 'R'}, 6},
    { KT_CHIP_MUSTANG_5200, KT_FW_STATE_RUN_APP,  {'K', 'T', '5', '2', 'X', '0'}, 6},

    { KT_CHIP_NONE,         KT_FW_STATE_RUN_NONE, {' ', ' ', ' ', ' ', ' ', ' '}, 6}
};

static const gchar *kt_dp_fw_run_state_strs[KT_FW_STATE_NUM] =
{
    "iROM",
    "App",
    "Boot-Code",
    "Unknown"
};

static KtDpDevInfo dp_dev_infos[MAX_DEV_NUM];
static KtChipId dp_root_dev_chip_id = KT_CHIP_NONE; // <TODO> declare as a private member of FuKineticDpDevice?
static KtFwRunState dp_root_dev_state = KT_FW_STATE_RUN_NONE; // <TODO> declare as a private member of FuKineticDpDevice?

static gboolean kt_aux_read_dpcd_oui(FuKineticDpConnection *connection, guint8 *buf, guint32 buf_size, GError **error)
{
    if (buf_size < DPCD_SIZE_IEEE_OUI)
        return FALSE;

    if (!fu_kinetic_dp_connection_read(connection, DPCD_ADDR_IEEE_OUI, buf, DPCD_SIZE_IEEE_OUI, error))
    {
        g_prefix_error (error, "Failed to read source OUI!");
        return FALSE;
    }

    return TRUE;
}

static gboolean kt_aux_write_dpcd_oui(FuKineticDpConnection *connection, const guint8 *buf, GError **error)
{
	if (!fu_kinetic_dp_connection_write(connection, DPCD_ADDR_IEEE_OUI, buf, DPCD_SIZE_IEEE_OUI, error))
	{
		g_prefix_error(error, "Failed to write source OUI: ");
        return FALSE;
	}

    return TRUE;
}

static gboolean kt_aux_read_dpcd_branch_id_str(FuKineticDpConnection *connection,
                                               guint8 *buf,
                                               guint32 buf_size,
                                               GError **error)
{
    if (buf_size < DPCD_SIZE_BRANCH_DEV_ID_STR)
        return FALSE;

    // Clear the buffer to all 0s as DP spec mentioned
    memset(buf, 0, DPCD_SIZE_BRANCH_DEV_ID_STR);

    if (!fu_kinetic_dp_connection_read(connection, DPCD_ADDR_BRANCH_DEV_ID_STR, buf, DPCD_SIZE_BRANCH_DEV_ID_STR, error))
    {
        g_prefix_error(error, "Failed to read branch device ID string: ");
        return FALSE;
    }

    return TRUE;
}

static guint16 _gen_crc16(guint16 accum, guint8 data_in)
{
    guint8 i, flag;

    for (i = 8; i; i--)
    {
        flag = data_in ^ (accum >> 8);
        accum <<= 1;

        if (flag & 0x80)
            accum ^= INIT_CRC16;

        data_in <<= 1;
    }

    return accum;
}

// ---------------------------------------------------------------
// ---------------------------------------------------------------
static void _accumulate_crc16(guint16 *prev_crc16, const guint8 *in_data_ptr, guint16 data_size)
{
    guint16 i;

    for (i = 0; i < data_size; i++)
    {
        *prev_crc16 = _gen_crc16(*prev_crc16, in_data_ptr[i]);
    }
}

static inline const gchar *sec_aux_isp_get_chip_id_str(KtChipId chip_id)
{
    if (chip_id == KT_CHIP_JAGUAR_5000)
        return "Jaguar";

    if (chip_id == KT_CHIP_MUSTANG_5200)
        return "Mustang";
    
    return "";
}

static inline const gchar *sec_aux_isp_get_fw_run_state_str(KtFwRunState fw_run_state)
{
    return (fw_run_state < KT_FW_STATE_NUM) ? kt_dp_fw_run_state_strs[fw_run_state] : NULL;
}

static gboolean sec_aux_isp_read_param_reg(FuKineticDpConnection *self, guint8 *dpcd_val, GError **error)
{
	if (!fu_kinetic_dp_connection_read(self, DPCD_ADDR_FLOAT_PARAM_REG, dpcd_val, 1, error))
	{
		g_prefix_error (error, "Failed to read DPCD_KT_PARAM_REG: ");
		return FALSE;
	}

    return TRUE;
}

static gboolean
sec_aux_isp_write_kt_prop_cmd(FuKineticDpConnection *connection, guint8 cmd_id, GError **error)
{
    cmd_id |= DPCD_KT_CONFIRMATION_BIT;

    if (!fu_kinetic_dp_connection_write(connection, DPCD_ADDR_FLOAT_CMD_STATUS_REG, &cmd_id, sizeof(cmd_id), error))
    {
        g_prefix_error(error, "Failed to write DPCD_KT_CMD_STATUS_REG: ");
        return FALSE;
    }

    return TRUE;
}

static gboolean
sec_aux_isp_clear_kt_prop_cmd(FuKineticDpConnection *connection, GError **error)
{
    guint8 cmd_id = KT_DPCD_CMD_STS_NONE;

    if (!fu_kinetic_dp_connection_write(connection, DPCD_ADDR_FLOAT_CMD_STATUS_REG, &cmd_id, sizeof(cmd_id), error))
    {
        g_prefix_error(error, "Failed to write DPCD_KT_CMD_STATUS_REG:");
        return FALSE;
    }

    return TRUE;
}

static gboolean
sec_aux_isp_send_kt_prop_cmd(FuKineticDpConnection *self,
                             guint8 cmd_id,
                             guint32 max_time_ms,
                             guint16 poll_interval_ms,
                             guint8 *status,
                             GError **error)
{
    gboolean ret = FALSE;
    guint8 dpcd_val = KT_DPCD_CMD_STS_NONE;

    if (!sec_aux_isp_write_kt_prop_cmd(self, cmd_id, error))
        return FALSE;

    *status = KT_DPCD_CMD_STS_NONE;

    while (max_time_ms != 0)
    {
        if (!fu_kinetic_dp_connection_read(self, DPCD_ADDR_FLOAT_CMD_STATUS_REG, (guint8 *)&dpcd_val, 1, error))
        {
            return FALSE;
        }

        if (dpcd_val != (cmd_id | DPCD_KT_CONFIRMATION_BIT))  // Target responses
        {
            if (dpcd_val != cmd_id)
            {
                *status = dpcd_val & DPCD_KT_COMMAND_MASK;

                if (KT_DPCD_STS_CRC_FAILURE == *status)
                {
                    g_prefix_error(error, "Chunk data CRC checking failed!");
                }
                else
                {
                    g_prefix_error(error, "Invalid replied value in DPCD_KT_CMD_STATUS_REG: 0x%X!", *status);
                }
            }
            else    // dpcd_val == cmd_id
            {
                // Confirmation bit is cleared by sink, means that sent command is processed
                ret = TRUE;
            }

            break;
        }

        g_usleep(((gulong) poll_interval_ms) * 1000);

        if (max_time_ms > poll_interval_ms)
        {
            max_time_ms -= poll_interval_ms;
        }
        else
        {
            max_time_ms = 0;
            g_prefix_error(error, "Waiting DPCD_KT_CMD_STATUS_REG timed-out!");

            return FALSE;
        }
    }

    return ret;
}

static gboolean
sec_aux_isp_read_dpcd_reply_data_reg(FuKineticDpConnection *self,
                                     guint8 *buf,
                                     const guint8 buf_size,
                                     guint8 *read_len,
                                     GError **error)
{
    guint8 read_data_len;

    *read_len = 0;  // Set the output to 0

    if (!fu_kinetic_dp_connection_read(self, DPCD_ADDR_FLOAT_ISP_REPLY_LEN_REG, &read_data_len, 1, error))
    {
        g_prefix_error(error, "Failed to read DPCD_ISP_REPLY_DATA_LEN_REG!");
        return FALSE;
    }

    if (buf_size < read_data_len)
    {
        g_prefix_error(error, "Buffer size is not enough to read DPCD_ISP_REPLY_DATA_REG!");
        return FALSE;
    }

    if (read_data_len > 0)
    {
        if (!fu_kinetic_dp_connection_read(self, DPCD_ADDR_FLOAT_ISP_REPLY_DATA_REG, buf, read_data_len, error))
        {
            g_prefix_error(error, "Failed to read DPCD_ISP_REPLY_DATA_REG!");
            return FALSE;
        }

        *read_len = read_data_len;
    }

    return TRUE;
}

static gboolean
sec_aux_isp_write_dpcd_reply_data_reg(FuKineticDpConnection *self,
                                      const guint8 *buf,
                                      guint8 len,
                                      GError **error)
{
    gboolean ret = FALSE;
    gboolean res;

    if (len > DPCD_SIZE_FLOAT_ISP_REPLY_DATA_REG)
        return FALSE;

    res = fu_kinetic_dp_connection_write(self, DPCD_ADDR_FLOAT_ISP_REPLY_DATA_REG, buf, len, error);
    if (!res)
    {
        g_prefix_error(error, "Failed to write DPCD_KT_REPLY_DATA_REG!");
        len = 0;    // Clear reply data length to 0 if failed to write reply data
    }

    if (fu_kinetic_dp_connection_write(self, DPCD_ADDR_FLOAT_ISP_REPLY_LEN_REG, &len, DPCD_SIZE_FLOAT_ISP_REPLY_LEN_REG, error) &&
        TRUE == res)
    {
        // Both reply data and reply length are written successfully
        ret = TRUE;
    }

    return ret;
}

static gboolean sec_aux_isp_write_mca_oui(FuKineticDpConnection *connection, GError **error)
{
    guint8 mca_oui[DPCD_SIZE_IEEE_OUI] = {MCA_OUI_BYTE_0, MCA_OUI_BYTE_1, MCA_OUI_BYTE_2};

    return kt_aux_write_dpcd_oui(connection, mca_oui, error);
}

static gboolean
sec_aux_isp_enter_code_loading_mode(FuKineticDpConnection *self,
                                    gboolean is_app_mode,
                                    const guint32 code_size,
                                    GError **error)
{
    guint8 status;

    if (is_app_mode)
    {
        // Send "DPCD_MCA_CMD_PREPARE_FOR_ISP_MODE" command first to make DPCD 514h ~ 517h writable.
        if (!sec_aux_isp_send_kt_prop_cmd(self, KT_DPCD_CMD_PREPARE_FOR_ISP_MODE, 500, 10, &status, error))
            return FALSE;
    }
    
    // Update payload size to DPCD reply data reg first
    if (!sec_aux_isp_write_dpcd_reply_data_reg(self, (guint8 *)&code_size, sizeof(code_size), error))
        return FALSE;

    if (!sec_aux_isp_send_kt_prop_cmd(self, KT_DPCD_CMD_ENTER_CODE_LOADING_MODE, 500, 10, &status, error))
        return FALSE;

    return TRUE;
}

static gboolean
sec_aux_isp_send_payload(FuKineticDpDevice *self,
                         FuKineticDpConnection *connection,
                         const guint8 *buf,
                         guint32 payload_size,
                         guint32 wait_time_ms,
                         gint32 wait_interval_ms,
                         GError **error)
{
    guint32 aux_win_addr = DPCD_ADDR_KT_AUX_WIN;
    guint8 temp_buf[16];
    guint32 remain_len = payload_size;
    guint32 crc16 = INIT_CRC16;
    guint8 status;

    while (remain_len)
    {
        guint8 aux_wr_size = (remain_len < 16) ? ((guint8)remain_len) : 16;

        memcpy(temp_buf, buf, aux_wr_size);

        _accumulate_crc16((guint16 *)&crc16, buf, aux_wr_size);

        // Put accumulated CRC16 of current 32KB chunk to DPCD_REPLY_DATA_REG
        if ((aux_win_addr + aux_wr_size) > DPCD_ADDR_KT_AUX_WIN_END ||
            (remain_len - aux_wr_size) == 0)
        {
            if (!sec_aux_isp_write_dpcd_reply_data_reg(connection, (guint8 *)&crc16, sizeof(crc16), error))
            {
                g_prefix_error(error, "Failed to send CRC16 to reply data register");

                return FALSE;
            }

            crc16 = INIT_CRC16; // Reset to initial CRC16 value for new chunk
        }

        // Send 16 bytes payload in each AUX transaction
        if (!fu_kinetic_dp_connection_write(connection, aux_win_addr, temp_buf, aux_wr_size, error))
        {
            g_prefix_error(error, "Failed to send payload on AUX write %u", self->isp_procd_size);

            return FALSE;
        }

        buf += aux_wr_size;
        aux_win_addr += aux_wr_size;
        remain_len -= aux_wr_size;
        self->isp_procd_size += aux_wr_size;
        self->isp_payload_procd_size += aux_wr_size;

        if ((aux_win_addr > DPCD_ADDR_KT_AUX_WIN_END) || (remain_len == 0))
        {
            // 32KB payload has been sent AUX window
            aux_win_addr = DPCD_ADDR_KT_AUX_WIN;

            if (!sec_aux_isp_send_kt_prop_cmd(connection, KT_DPCD_CMD_CHUNK_DATA_PROCESSED, wait_time_ms,
                                              wait_interval_ms, &status, error))
            {
                if (status == KT_DPCD_STS_CRC_FAILURE)
                {
                    // Check CRC failed
                    return FALSE;
                }
            }
        }
    }

    return TRUE;
}

static gboolean
sec_aux_isp_wait_dpcd_cmd_cleared(FuKineticDpConnection *self,
                                  guint16 wait_time_ms,
                                  guint16 poll_interval_ms,
                                  guint8 *status,
                                  GError **error)
{
    guint8 dpcd_val = KT_DPCD_CMD_STS_NONE;

    *status = KT_DPCD_CMD_STS_NONE;

    while (wait_time_ms > 0)
    {
        if (!fu_kinetic_dp_connection_read(self,
                                            DPCD_ADDR_FLOAT_CMD_STATUS_REG,
                                            (guint8 *)&dpcd_val,
                                            1,
                                            error))
        {
            return FALSE;
        }

        if (dpcd_val == KT_DPCD_CMD_STS_NONE)
        {
            // Status is cleared by sink
            break;
        }

        if ((dpcd_val & DPCD_KT_CONFIRMATION_BIT) != DPCD_KT_CONFIRMATION_BIT)
        {
            // Status is not cleared but confirmation bit is cleared, it means that target responses with failure
            *status = dpcd_val;
            return FALSE;
        }

        // Sleep for polling interval
        g_usleep(((gulong) poll_interval_ms) * 1000);
        
        if (wait_time_ms >= poll_interval_ms)
        {
            wait_time_ms -= poll_interval_ms;
        }
        else
        {
            g_prefix_error(error, "Waiting DPCD_Isp_Sink_Status_Reg timed-out!");

            return FALSE;
        }
    }

    return TRUE;
}

// ---------------------------------------------------------------
// In Jaguar, it takes about 1000 ms to boot up and initialize
// ---------------------------------------------------------------
static gboolean
sec_aux_isp_execute_isp_drv(FuKineticDpDevice *self,
                            FuKineticDpConnection *connection,
                            GError **error)
{
    guint8 status;
    guint8 read_len;
    guint8 reply_data[6] = {0};

    self->flash_id = 0;
    self->flash_size = 0;
    self->read_flash_prog_time = 10;

    if (!sec_aux_isp_write_kt_prop_cmd(connection, KT_DPCD_CMD_EXECUTE_RAM_CODE, error))
        return FALSE;

    if (!sec_aux_isp_wait_dpcd_cmd_cleared(connection, 1500, 100, &status, error))
    {
        if (KT_DPCD_STS_INVALID_IMAGE == status)
            g_prefix_error (error, "Invalid ISP driver!");
        else
            g_prefix_error (error, "Executing ISP driver... failed!");
        
        return FALSE;
    }
    
    if (!sec_aux_isp_read_param_reg(connection, &status, error))
        return FALSE;

    if (status != KT_DPCD_STS_SECURE_ENABLED && status != KT_DPCD_STS_SECURE_DISABLED)
    {
        g_prefix_error(error, "Waiting for ISP driver ready... failed!");
        return FALSE;
    }

    if (KT_DPCD_STS_SECURE_ENABLED == status)
    {
        self->is_isp_secure_auth_mode = TRUE;
    }
    else
    {
        self->is_isp_secure_auth_mode = FALSE;
        self->isp_total_data_size -= (FW_CERTIFICATE_SIZE * 2 + FW_RSA_SIGNATURE_BLOCK_SIZE * 2);
    }

    if (!sec_aux_isp_read_dpcd_reply_data_reg(connection, reply_data, sizeof(reply_data), &read_len, error))
    {
        g_prefix_error(error, "Failed to read flash ID and size!");
        return FALSE;
    }

    self->flash_id = (guint16)reply_data[0] << 8 | reply_data[1];
    self->flash_size = (guint16)reply_data[2] << 8 | reply_data[3];
    self->read_flash_prog_time = (guint16)reply_data[4] << 8 | reply_data[5];

	if (self->read_flash_prog_time == 0)
		self->read_flash_prog_time = 10;

    return TRUE;        
}

static gboolean
sec_aux_isp_send_isp_drv(FuKineticDpDevice *self,
                         FuKineticDpConnection *connection,
                         gboolean is_app_mode,
                         const guint8 *isp_drv_data,
                         guint32 isp_drv_len,
                         GError **error)
{
    g_message("Sending ISP driver payload... started");

    if (sec_aux_isp_enter_code_loading_mode(connection, is_app_mode, isp_drv_len, error) == FALSE)
    {
        g_prefix_error(error, "Enabling code-loading mode... failed!");
        return FALSE;
    }
    
    if (!sec_aux_isp_send_payload(self, connection, isp_drv_data, isp_drv_len, 10000, 50, error))
    {
        g_prefix_error(error, "Sending ISP driver payload... failed!");
        return FALSE;
    }

    g_message("Sending ISP driver payload... done!");

    if (!sec_aux_isp_execute_isp_drv(self, connection, error))
    {
        g_prefix_error(error, "ISP driver booting up... failed!");
        return FALSE;
    }

    g_message("Flash ID: 0x%04X  ", self->flash_id);

    if (self->flash_size)
    {
        if (self->flash_size < 2048)    // One bank size in Jaguar is 1024KB
            g_message("Flash Size: %d KB, Dual Bank is not supported!", self->flash_size);
        else
            g_message("Flash Size: %d KB", self->flash_size);
    }
    else
    {
        if (self->flash_id)
            g_message("(SPI flash not supported)");
        else
            g_message("(SPI flash not connected)");
    }

    return TRUE;
}

static gboolean
sec_aux_isp_enable_fw_update_mode(FuKineticDpFirmware *firmware,
                                  FuKineticDpConnection *connection,
                                  GError **error)
{
    guint8 status;
    guint8 pl_size_data[12] = {0};

    g_message("Entering F/W loading mode...");

    // Send payload size to DPCD_MCA_REPLY_DATA_REG
    *(guint32 *)pl_size_data = fu_kinetic_dp_firmware_get_esm_payload_size(firmware);
    *(guint32 *)&pl_size_data[4] = fu_kinetic_dp_firmware_get_arm_app_code_size(firmware);
    *(guint16 *)&pl_size_data[8] = (guint16)fu_kinetic_dp_firmware_get_app_init_data_size(firmware);
    *(guint16 *)&pl_size_data[10] = (guint16)fu_kinetic_dp_firmware_get_cmdb_block_size(firmware) |
                                     (fu_kinetic_dp_firmware_get_is_fw_esm_xip_enabled(firmware) << 15);

    if (!sec_aux_isp_write_dpcd_reply_data_reg(connection, pl_size_data, sizeof(pl_size_data), error))
    {
        g_prefix_error(error, "Send payload size failed!");
        return FALSE;
    }

    if (!sec_aux_isp_send_kt_prop_cmd(connection, KT_DPCD_CMD_ENTER_FW_UPDATE_MODE, 200000, 500, &status, error))
    {
        g_prefix_error(error, "Entering F/W update mode... failed!");
        return FALSE;
    }

    g_message("F/W loading mode... ready");

    return TRUE;
}


static gboolean
sec_aux_isp_send_fw_payload(FuKineticDpDevice *self,
                            FuKineticDpConnection *connection,
                            FuKineticDpFirmware *firmware,
                            const guint8 *fw_data,
                            guint32 fw_len,
                            GError **error)
{
    guint8 *ptr;

    if (self->is_isp_secure_auth_mode)
    {
        g_message("Sending Certificates... started!");
        // Send ESM and App Certificates & RSA Signatures
        ptr = (guint8 *)fw_data;
        if (!sec_aux_isp_send_payload(self, connection, ptr, FW_CERTIFICATE_SIZE * 2 + FW_RSA_SIGNATURE_BLOCK_SIZE * 2, 10000, 200, error))
        {
            g_prefix_error(error, "Sending Certificates... failed!");
            return FALSE;
        }

        g_message("Sending Certificates... done!");
    }

    // Send ESM code
    g_message("Sending ESM... started!");

    ptr = (guint8 *)fw_data + SPI_ESM_PAYLOAD_START;
    if (!sec_aux_isp_send_payload(self, connection, ptr, fu_kinetic_dp_firmware_get_esm_payload_size(firmware), 10000, 200, error))
    {
        g_prefix_error(error, "Sending ESM... failed!");
        return FALSE;
    }

    g_message("Sending ESM... done!");

    // Send App code
    g_message("Sending App... started!");

    ptr = (guint8 *)fw_data + SPI_APP_PAYLOAD_START;
    if (!sec_aux_isp_send_payload(self, connection, ptr, fu_kinetic_dp_firmware_get_arm_app_code_size(firmware), 10000, 200, error))
    {
        g_prefix_error(error, "Sending App... failed!");
        return FALSE;
    }

    g_message("Sending App... done!");

    // Send App initialized data
    g_message("Sending App init data... started!");

    ptr = (guint8 *)fw_data + (fu_kinetic_dp_firmware_get_is_fw_esm_xip_enabled(firmware) ? SPI_APP_EXTEND_INIT_DATA_START : SPI_APP_NORMAL_INIT_DATA_START);
    if (!sec_aux_isp_send_payload(self, connection, ptr, fu_kinetic_dp_firmware_get_app_init_data_size(firmware), 10000, 200, error))
    {
        g_prefix_error(error, "Sending App init data... failed!");
        return FALSE;
    }

    g_message("Sending App init data... done!");

    if (fu_kinetic_dp_firmware_get_cmdb_block_size(firmware))
    {
        // Send CMDB
        g_message("Sending CMDB... started!");

        ptr = (guint8 *)fw_data + SPI_CMDB_BLOCK_START;
        if (!sec_aux_isp_send_payload(self, connection, ptr, fu_kinetic_dp_firmware_get_cmdb_block_size(firmware), 10000, 200, error))
        {
            g_prefix_error(error, "Sending CMDB... failed!");
            return FALSE;
        }

        g_message("Sending CMDB... done!");
    }

    // Send Application Identifier
    ptr = (guint8 *)fw_data + SPI_APP_ID_DATA_START;
    if (!sec_aux_isp_send_payload(self, connection, ptr, STD_APP_ID_SIZE, 10000, 200, error))
    {
        g_prefix_error(error, "Sending App ID data... failed!");
        return FALSE;
    }

    g_message("Sending App ID data... done!");
    
    return TRUE;
}

static gboolean
sec_aux_isp_install_fw_images(FuKineticDpDevice *self,
                              FuKineticDpConnection *connection,
                              GError **error)
{
    guint8 cmd_id = KT_DPCD_CMD_INSTALL_IMAGES;
    guint8 status;
    guint16 wait_count = 1500;
    guint16 progress_inc = FLASH_PROGRAM_COUNT / ((self->read_flash_prog_time * 1000) / WAIT_PROG_INTERVAL_MS);

    g_message("Installing F/W payload... started");

    if (!sec_aux_isp_write_kt_prop_cmd(connection, cmd_id, error))
    {
        g_prefix_error(error, "Sending DPCD command... failed!");
        return FALSE;
    }

    while (wait_count-- > 0)
    {
        if (!fu_kinetic_dp_connection_read(connection, DPCD_ADDR_FLOAT_CMD_STATUS_REG, &status, 1, error))
        {
            g_prefix_error(error, "Reading DPCD_MCA_CMD_REG... failed!");
            return FALSE;
        }

        if (status != (cmd_id | DPCD_KT_CONFIRMATION_BIT))  // Target responsed
        {
            if (status == cmd_id)   // Confirmation bit if cleared
            {
                self->isp_payload_procd_size += (self->isp_total_data_size - self->isp_procd_size);
                g_message("Programming F/W payload... done!");

                return TRUE;
            }
            else
            {
                g_prefix_error(error, "Installing images... failed!");

                return FALSE;
            }
        }

        if (self->isp_procd_size < self->isp_total_data_size)
        {
            self->isp_procd_size += progress_inc;
            self->isp_payload_procd_size += progress_inc;
        }
        
        // Wait 50ms
        g_usleep(50 * 1000);
    }

    g_prefix_error(error, "Installing images... timed-out!");

    return FALSE;
}

static gboolean
sec_aux_isp_send_reset_command(FuKineticDpConnection *connection, GError **error)
{
	g_message("Resetting system...");

    if (!sec_aux_isp_write_kt_prop_cmd(connection, KT_DPCD_CMD_RESET_SYSTEM, error))
    {
        g_prefix_error(error, "Resetting system... failed!");
        return FALSE;
    }

    return TRUE;
}

static gboolean
sec_aux_isp_enable_aux_forward(FuKineticDpConnection *connection,
                               KtDpDevPort target_port,
                               GError **error)
{
    gboolean ret;
    guint8 status;
    guint8 cmd_id;

    if (!sec_aux_isp_write_mca_oui(connection, error))
        return FALSE;

    cmd_id = (guint8)target_port;
    if (!fu_kinetic_dp_connection_write(connection, DPCD_ADDR_FLOAT_PARAM_REG, &cmd_id, sizeof(cmd_id), error))
        return FALSE;

    ret = sec_aux_isp_send_kt_prop_cmd(connection, KT_DPCD_CMD_ENABLE_AUX_FORWARD, 1000, 20, &status, error);

    // Clear CMD_STS_REG
    cmd_id = KT_DPCD_CMD_STS_NONE;
    fu_kinetic_dp_connection_write(connection, DPCD_ADDR_FLOAT_CMD_STATUS_REG, &cmd_id, sizeof(cmd_id), error);
                                   
    return ret;
}

static gboolean
sec_aux_isp_disable_aux_forward(FuKineticDpConnection *connection, GError **error)
{
    gboolean ret;
    guint8 status;
    guint8 cmd_id;

    if (!sec_aux_isp_write_mca_oui(connection, error))
        return FALSE;

    ret = sec_aux_isp_send_kt_prop_cmd(connection, KT_DPCD_CMD_DISABLE_AUX_FORWARD, 1000, 20, &status, error);

    // Clear CMD_STS_REG
    cmd_id = KT_DPCD_CMD_STS_NONE;
    fu_kinetic_dp_connection_write(connection, DPCD_ADDR_FLOAT_CMD_STATUS_REG, &cmd_id, sizeof(cmd_id), error);

    return ret;
}

static KtFlashBankIdx
sec_aux_isp_get_flash_bank_idx(FuKineticDpConnection *connection, GError **error)
{
    guint8 status;
    guint8 prev_src_oui[DPCD_SIZE_IEEE_OUI] = {0};
    guint8 res = BANK_NONE;

    if (!kt_aux_read_dpcd_oui(connection, prev_src_oui, sizeof(prev_src_oui), error))
        return BANK_NONE;

    if (!sec_aux_isp_write_mca_oui(connection, error))
        return BANK_NONE;

    if (sec_aux_isp_send_kt_prop_cmd(connection, KT_DPCD_CMD_GET_ACTIVE_FLASH_BANK, 100, 20, &status, error))
    {
        if (!sec_aux_isp_read_param_reg(connection, &res, error))
            res = BANK_NONE;
    }

    sec_aux_isp_clear_kt_prop_cmd(connection, error);

    // Restore previous source OUI
    kt_aux_write_dpcd_oui(connection, prev_src_oui, error);

    return (KtFlashBankIdx)res;
}

static gboolean
sec_aux_isp_get_device_info(FuKineticDpConnection *connection, KtDpDevInfo *dev_info, GError **error)
{
    // ---------------------------------------------------------------
    // Chip ID, FW work state, and branch ID string are known
    // ---------------------------------------------------------------
    guint8 dpcd_buf[16] = {0};

    if (!fu_kinetic_dp_connection_read(connection,
                                        DPCD_ADDR_BRANCH_HW_REV,
                                        dpcd_buf,
                                        sizeof(dpcd_buf),
                                        error))
    {
        return FALSE;
    }
    
    dev_info->chip_rev = dpcd_buf[0];  // DPCD 0x509
    dev_info->fw_info.std_fw_ver = (guint32)dpcd_buf[1] << 16 | (guint32)dpcd_buf[2] << 8 | dpcd_buf[3];  // DPCD 0x50A ~ 0x50C
    dev_info->fw_info.customer_project_id = dpcd_buf[12];   // DPCD 0x515
    dev_info->fw_info.customer_fw_ver = (guint16)dpcd_buf[6] << 8 | (guint16)dpcd_buf[11];  // DPCD (0x50F | 0x514)
    dev_info->chip_type = dpcd_buf[13];  // DPCD 0x516

    if (KT_FW_STATE_RUN_APP == dev_info->fw_run_state)
    {
        dev_info->is_dual_bank_supported = TRUE;
        dev_info->flash_bank_idx = sec_aux_isp_get_flash_bank_idx(connection, error);
    }

    dev_info->fw_info.boot_code_ver = 0;
    // <TODO> Add function to read CMDB information
    dev_info->fw_info.std_cmdb_ver = 0;
    dev_info->fw_info.cmdb_rev = 0;

	return TRUE;
}

static gboolean
sec_aux_isp_start_isp(FuKineticDpDevice *self,
                      FuFirmware *firmware,
                      const KtDpDevInfo *dev_info,
                      GError **error)
{
    FuKineticDpFirmware *firmware_self = FU_KINETIC_DP_FIRMWARE(firmware);
    gboolean ret = FALSE;
    gboolean is_app_mode = (KT_FW_STATE_RUN_APP == dev_info->fw_run_state)? TRUE : FALSE;
    g_autoptr(GBytes) isp_drv = NULL;
    g_autoptr(GBytes) app = NULL;
	const guint8 *payload_data;
	gsize payload_len;
	g_autoptr(FuDeviceLocker) locker = NULL;
    g_autoptr(FuKineticDpConnection) connection = NULL;
    g_autoptr(FuFirmwareImage) img = NULL;

    connection = fu_kinetic_dp_connection_new(fu_udev_device_get_fd(FU_UDEV_DEVICE(self)));

    self->isp_procd_size = 0;

    g_message("Start secure AUX-ISP [%s]...", sec_aux_isp_get_chip_id_str(dev_info->chip_id));

    // Write MCA OUI
    if (!sec_aux_isp_write_mca_oui(connection, error))
        goto SECURE_AUX_ISP_END;

    // Send ISP driver and execute it
    img = fu_firmware_get_image_by_idx(firmware, FU_KT_FW_IMG_IDX_ISP_DRV, error);
    if (NULL == img)
        return FALSE;

    isp_drv = fu_firmware_image_write(img, error);
	if (isp_drv == NULL)
		return FALSE;

	payload_data = g_bytes_get_data(isp_drv, &payload_len);
    if (payload_len)
    {
        if (!sec_aux_isp_send_isp_drv(self, connection, is_app_mode, payload_data, payload_len, error))
            goto SECURE_AUX_ISP_END;
    }

    // Enable FW update mode
    if (!sec_aux_isp_enable_fw_update_mode(firmware_self, connection, error))
        goto SECURE_AUX_ISP_END;

    // Send FW image
    img = fu_firmware_get_image_by_idx(firmware, FU_KT_FW_IMG_IDX_APP_FW, error);
    if (NULL == img)
        return FALSE;

    app = fu_firmware_image_write(img, error);
	if (app == NULL)
		return FALSE;

	payload_data = g_bytes_get_data(app, &payload_len);
    if (!sec_aux_isp_send_fw_payload(self, connection, firmware_self, payload_data, payload_len, error))
        goto SECURE_AUX_ISP_END;

    // Install FW images
    ret = sec_aux_isp_install_fw_images(self, connection, error);

SECURE_AUX_ISP_END:
#if 0
    if (ret == false)
    {
        sec_aux_isp_abort_code_loading_mode();
    }
#endif

    // Send reset command
    sec_aux_isp_send_reset_command(connection, error);

    return ret;
}

static gboolean
sec_aux_isp_update_firmware(FuKineticDpDevice *self,
                            FuFirmware *firmware,
                            GError **error)
{
    /* <TODO> test ISP for host device now
     *        ISP for DFP devices is not implemented yet
     */
    return sec_aux_isp_start_isp(self, firmware, &dp_dev_infos[DEV_HOST], error);;
}

guint16 kt_dp_get_numeric_chip_id(KtChipId chip_id)
{
    if (chip_id == KT_CHIP_MUSTANG_5200)
        return 0x5200U;
    if (chip_id == KT_CHIP_JAGUAR_5000)
        return 0x5000U;
    return 0;
}

gboolean
kt_dp_get_dev_info_from_branch_id(const guint8 *br_id_str_buf,
                                  const guint8 br_id_str_buf_size,
                                  KtDpDevInfo *dev_info,
                                  GError **error)
{
    guint8 i = 0;
    g_autofree gchar *str = NULL;   // Just for printing error log

    g_return_val_if_fail(br_id_str_buf != NULL, FALSE);
    g_return_val_if_fail(br_id_str_buf_size >= DPCD_SIZE_BRANCH_DEV_ID_STR, FALSE);
    g_return_val_if_fail(dev_info != NULL, FALSE);
    g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

    dev_info->chip_id = KT_CHIP_NONE;
    dev_info->fw_run_state = KT_FW_STATE_RUN_NONE;
    memset(dev_info->branch_id_str, 0, DPCD_SIZE_BRANCH_DEV_ID_STR);    // Clear the buffer to all 0s as DP spec mentioned

    // Find the device info by branch ID string
    while (kt_dp_branch_dev_info_table[i].chip_id != KT_CHIP_NONE)
    {
        if (0 == memcmp(br_id_str_buf, kt_dp_branch_dev_info_table[i].id_str, kt_dp_branch_dev_info_table[i].str_len))
        {
            // Found the chip in the table
            dev_info->chip_id = kt_dp_branch_dev_info_table[i].chip_id;
            dev_info->fw_run_state = kt_dp_branch_dev_info_table[i].fw_run_state;
            memcpy(dev_info->branch_id_str, br_id_str_buf, DPCD_SIZE_BRANCH_DEV_ID_STR);

            return TRUE;
        }

        i++;
    }

    // There might not always be null-terminated character '\0' in DPCD branch ID string (when length is 6)
    str = g_strndup((const gchar *)br_id_str_buf, DPCD_SIZE_BRANCH_DEV_ID_STR);
    g_set_error(error,
                FWUPD_ERROR,
	            FWUPD_ERROR_INTERNAL,
	            "%s is not a Kinetic device",
	            str);

    return FALSE;
}

gboolean kt_dp_read_chip_id_and_state(FuKineticDpConnection *connection,
                                      /*KtDpDevPort dev_port,*/
                                      KtDpDevInfo *dev_info,
                                      GError **error)
{
    guint8 branch_id[DPCD_SIZE_BRANCH_DEV_ID_STR] = {0};

    // Detail information is from DPCD branch ID string
    if (!kt_aux_read_dpcd_branch_id_str(connection, branch_id, DPCD_SIZE_BRANCH_DEV_ID_STR, error))
    {
        return FALSE;
    }

    if (!kt_dp_get_dev_info_from_branch_id(branch_id, DPCD_SIZE_BRANCH_DEV_ID_STR, dev_info, error))
        return FALSE;

    return TRUE;
}

gboolean kt_dp_enable_aux_forward(FuKineticDpConnection *connection,
                                  KtChipId root_dev_chip_id,
                                  KtFwRunState root_dev_state,
                                  KtDpDevPort target_port,
                                  GError **error)
{
    if (root_dev_state == KT_FW_STATE_RUN_APP)
    {
        if ((root_dev_chip_id == KT_CHIP_JAGUAR_5000) || (root_dev_chip_id == KT_CHIP_MUSTANG_5200))
        {
            if (!sec_aux_isp_enable_aux_forward(connection, target_port, error))
            {
                g_prefix_error(error, "Failed to enable AUX forwarding!");
                return FALSE;
            }

            g_usleep(10 * 1000);    // Wait 10ms for host processing AUX forwarding command

            return TRUE;
        }

        g_prefix_error(error, "Host device [%s] doesn't support AUX forwarding!",
                              sec_aux_isp_get_chip_id_str(dp_root_dev_chip_id));
    }
    else
    {
        g_prefix_error(error, "Host device [%s %s] doesn't support AUX forwarding!",
                              sec_aux_isp_get_chip_id_str(dp_root_dev_chip_id),
                              sec_aux_isp_get_fw_run_state_str(dp_root_dev_state));
    }

    return FALSE;
}

// ---------------------------------------------------------------
// ---------------------------------------------------------------
gboolean kt_dp_disable_aux_forward(FuKineticDpConnection *connection,
                                   KtChipId root_dev_chip_id,
                                   KtFwRunState root_dev_state,
                                   GError **error)
{
    if (root_dev_state != KT_FW_STATE_RUN_APP)
        return FALSE;

    if (root_dev_chip_id == KT_CHIP_JAGUAR_5000)
    {
        g_usleep(5 * 1000); // Wait 5ms
        return sec_aux_isp_disable_aux_forward(connection, error);
    }

    return FALSE;
}

gboolean kt_dp_read_device_info(FuKineticDpDevice *self,
                                /*KtDpDevPort target_port,*/    // <TODO> AUX-ISP for DFP device
                                KtDpDevInfo *dev_info,
                                GError **error)
{
    g_autoptr(FuKineticDpConnection) connection = NULL;
    KtDpDevInfo dev_info_local;

    if (dev_info == NULL)
        return FALSE;

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

    // Get basic chip information (Chip ID, F/W work state)
    connection = fu_kinetic_dp_connection_new(fu_udev_device_get_fd(FU_UDEV_DEVICE(self)));

#if 1
    if (!kt_dp_read_chip_id_and_state(connection, &dev_info_local, error))
    {
        g_prefix_error(error, "Failed to read chip ID and state: ");
        return FALSE;
    }

    // Get more information from each control library
    /* <TODO> Make the control a derivable class to support different behaviors and DPCD definitions
     *        while processing ISP. (if needed)
     */
    if (!sec_aux_isp_get_device_info(connection, &dev_info_local, error))
    {
        g_prefix_error(error, "Failed to read other device information: ");
        return FALSE;
    }

    if (dp_root_dev_chip_id == KT_CHIP_NONE)
    {
        dp_root_dev_chip_id = dev_info_local.chip_id;
        dp_root_dev_state = dev_info_local.fw_run_state;
    }
#else
    // <TODO> AUX-ISP for DFP device
#endif

    memcpy(dev_info, &dev_info_local, sizeof(KtDpDevInfo));
    
    return TRUE;
}

/* <TODO> put to fu-kinetic-dp-common.c */
FuKineticDpFamily
fu_kinetic_dp_family_from_chip_id(KtChipId chip_id)
{
	if (chip_id == KT_CHIP_MUSTANG_5200)
		return FU_KINETIC_DP_FAMILY_MUSTANG;
	if (chip_id == KT_CHIP_JAGUAR_5000)
		return FU_KINETIC_DP_FAMILY_JAGUAR;
	return FU_KINETIC_DP_FAMILY_UNKNOWN;
}

static gboolean
fu_kinetic_dp_device_write_firmware(FuDevice *device,
               				         FuFirmware *firmware,
               				         FwupdInstallFlags flags,
               				         GError **error)
{
	FuKineticDpDevice *self = FU_KINETIC_DP_DEVICE(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	fu_device_set_status(device, FWUPD_STATUS_DEVICE_WRITE);

	/* update firmware */
    if (self->family == FU_KINETIC_DP_FAMILY_JAGUAR || self->family == FU_KINETIC_DP_FAMILY_MUSTANG)
    {
        if (!sec_aux_isp_update_firmware(self, firmware, error))
        {
            g_prefix_error(error, "Firmware update failed: ");
			return FALSE;
        }
    }
    else
    {
        // <TODO> support older chips?
    }

	/* wait for flash clear to settle */
	fu_device_set_status(device, FWUPD_STATUS_DEVICE_RESTART);
	fu_device_sleep_with_progress(device, 2);

	return TRUE;
}

FuKineticDpDevice *
fu_kinetic_dp_device_new(FuUdevDevice *device)
{
	FuKineticDpDevice *self = g_object_new(FU_TYPE_KINETIC_DP_DEVICE, NULL);
	fu_device_incorporate(FU_DEVICE(self), FU_DEVICE(device));
	return self;
}

void
fu_kinetic_dp_device_set_system_type(FuKineticDpDevice *self, const gchar *system_type)
{
	g_return_if_fail(FU_IS_KINETIC_DP_DEVICE (self));
	self->system_type = g_strdup(system_type);
}

static gboolean
fu_kinetic_dp_device_rescan(FuDevice *device, GError **error)
{
	FuKineticDpDevice *self = FU_KINETIC_DP_DEVICE(device);
	FuQuirks *quirks;
	g_autoptr(FuKineticDpConnection) connection = NULL;
	g_autofree gchar *group = NULL;
	g_autofree gchar *name = NULL;
	g_autofree gchar *guid1 = NULL;
	g_autofree gchar *guid2 = NULL;
	const gchar *name_parent;
	const gchar *name_family;
	const gchar *plugin;

	KtDpDevInfo *dp_dev_info = &dp_dev_infos[DEV_HOST];  // <TODO> Now it's for test AUX-ISP protocol

    if (!kt_dp_read_device_info(self, /*DEV_HOST,*/ dp_dev_info, error))
    {
        g_prefix_error(error, "Failed to read device info: ");
        return FALSE;
    }

    g_debug("branch_id_str = %s", dp_dev_info->branch_id_str);

    /* read firmware version */
    // <TODO>

    self->family = fu_kinetic_dp_family_from_chip_id(dp_dev_info->chip_id);

    /* Convert Kinetic chip id to numeric representation */
    self->chip_id = kt_dp_get_numeric_chip_id(dp_dev_info->chip_id);

    /* set up the device name via quirks */
	group = g_strdup_printf("CustomerProjectID=%u", dp_dev_info->fw_info.customer_project_id);
	quirks = fu_device_get_quirks(FU_DEVICE(self));
	name_parent = fu_quirks_lookup_by_id(quirks, group, FU_QUIRKS_NAME);
	if (name_parent != NULL)
	{
		name = g_strdup_printf("KT%04x inside %s", self->chip_id, name_parent);
	}
	else
	{
		name = g_strdup_printf("KT%04x", self->chip_id);
	}
	fu_device_set_name(FU_DEVICE(self), name);

	plugin = fu_quirks_lookup_by_id(quirks, group, FU_QUIRKS_PLUGIN);
	if (plugin != NULL && g_strcmp0(plugin, KINETIC_DP_FWUPD_PLUGIN_NAME) != 0)
	{
		g_set_error(error,
    			    FWUPD_ERROR,
    			    FWUPD_ERROR_NOT_SUPPORTED,
    			    "%s is only supported by %s",
    			    name, plugin);
		return FALSE;
	}

    /* detect chip family */
    switch (self->family)
    {
    case FU_KINETIC_DP_FAMILY_JAGUAR:
        //fu_device_set_firmware_size_max(device, 0x10000);    // <TODO> Determine max firmware size for Jaguar
        fu_device_add_instance_id_full(device, "KTDP-KT50X0", FU_DEVICE_INSTANCE_FLAG_ONLY_QUIRKS);
        break;
    case FU_KINETIC_DP_FAMILY_MUSTANG:
    	//fu_device_set_firmware_size_max (device, 0x10000);    // <TODO> Determine max firmware size for Mustang
    	fu_device_add_instance_id_full(device, "KTDP-KT52X0", FU_DEVICE_INSTANCE_FLAG_ONLY_QUIRKS);
        break;
    default:
        break;
    }

    /* add non-standard GUIDs */
    name_family = fu_kinetic_dp_family_to_string(self->family);
    guid1 = g_strdup_printf("KT-DP-%s-KT%04x", name_family, self->chip_id);
    fu_device_add_instance_id(FU_DEVICE(self), guid1);
    guid2 = g_strdup_printf("KT-DP-%s", name_family);
    fu_device_add_instance_id(FU_DEVICE(self), guid2);

    // <TODO> check if a valid device to update?
    fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);

	return TRUE;
}

static void
fu_kinetic_dp_device_class_init(FuKineticDpDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);

	object_class->finalize = fu_kinetic_dp_device_finalize;

	//klass_device->to_string = fu_kinetic_dp_device_to_string;
	klass_device->rescan = fu_kinetic_dp_device_rescan;
	klass_device->write_firmware = fu_kinetic_dp_device_write_firmware;
	klass_device->prepare_firmware = fu_kinetic_dp_device_prepare_firmware;
	klass_device->probe = fu_kinetic_dp_device_probe;
}

