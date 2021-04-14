/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2021 Jeffrey Lin <jlin@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fcntl.h>

#include "fu-kinetic-mst-common.h"
#include "fu-kinetic-mst-connection.h"
#include "fu-kinetic-mst-device.h"
#include "fu-kinetic-mst-firmware.h"
#include "fu-kinetic-secure-aux-isp.h"

struct _FuKineticMstDevice {
    FuUdevDevice        parent_instance;
    gchar *             system_type;
    FuKineticMstFamily  family;
    FuKineticMstMode    mode;
};

G_DEFINE_TYPE (FuKineticMstDevice, fu_kinetic_mst_device, FU_TYPE_UDEV_DEVICE)

static void
fu_kinetic_mst_device_finalize (GObject *object)
{
	FuKineticMstDevice *self = FU_KINETIC_MST_DEVICE (object);

	g_free (self->system_type);

	G_OBJECT_CLASS (fu_kinetic_mst_device_parent_class)->finalize (object);
}

static void
fu_kinetic_mst_device_init (FuKineticMstDevice *self)
{
	fu_device_set_protocol (FU_DEVICE (self), "com.kinetic.mst");
	fu_device_set_vendor (FU_DEVICE (self), "Kinetic");
	fu_device_add_vendor_id (FU_DEVICE (self), "DRM_DP_AUX_DEV:0x06CB");    // <TODO> How to determine the vendor ID?
	fu_device_set_summary (FU_DEVICE (self), "Multi-Stream Transport Device");
	fu_device_add_icon (FU_DEVICE (self), "video-display");
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_TRIPLET);  // <TODO> What's Kinetic's version format?
	fu_udev_device_set_flags (FU_UDEV_DEVICE (self),
                              FU_UDEV_DEVICE_FLAG_OPEN_READ |
                              FU_UDEV_DEVICE_FLAG_OPEN_WRITE |
                              FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT);
}

static gboolean
fu_kinetic_mst_device_enable_rc(FuKineticMstDevice *self, GError **error)
{
    // <TODO> is this function neccessary?
	return TRUE;
}

static gboolean
fu_kinetic_mst_device_disable_rc(FuKineticMstDevice *self, GError **error)
{
	// <TODO> is this function neccessary?
	return TRUE;
}

static gboolean
fu_kinetic_mst_device_restart (FuKineticMstDevice *self, GError **error)
{
	g_autoptr(FuKineticMstConnection) connection = NULL;
	//g_autoptr(GError) error_local = NULL;

	/* issue the reboot command, ignore return code (triggers before returning) */
#if 0
	connection = fu_synaptics_mst_connection_new(fu_udev_device_get_fd (FU_UDEV_DEVICE (self)),
						                         self->layer, self->rad);
	if (!fu_synaptics_mst_connection_rc_set_command (connection,
							 UPDC_WRITE_TO_MEMORY,
							 4, (gint) 0x2000FC, (guint8*) &buf,
							 &error_local))
		g_debug ("failed to restart: %s", error_local->message);
#else
    // <TODO> implement for Kinetic's reset command
#endif

	return TRUE;
}

static FuFirmware *
fu_kinetic_mst_device_prepare_firmware(FuDevice *device,
                                       GBytes *fw,
                                       FwupdInstallFlags flags,
                                       GError **error)
{
	//FuKineticMstDevice *self = FU_KINETIC_MST_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_kinetic_mst_firmware_new();

#if 0
	/* check firmware and board ID match */
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_VID_PID) == 0 &&
	    !fu_device_has_custom_flag (device, "ignore-board-id")) {
		guint16 board_id = fu_synaptics_mst_firmware_get_board_id (FU_SYNAPTICS_MST_FIRMWARE (firmware));
		if (board_id != self->board_id) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "board ID mismatch, got 0x%04x, expected 0x%04x",
				     board_id, self->board_id);
			return NULL;
		}
	}
#else
    // <TODO> check firmware according to Kinetic's Fw image
#endif

	return fu_firmware_new_from_bytes(fw);
}

#define SIZE_1KB        (1   * 1024)
#define SIZE_4KB        (4   * 1024)
#define SIZE_8KB        (8   * 1024)
#define SIZE_16KB       (16  * 1024)
#define SIZE_24KB       (24  * 1024)
#define SIZE_32KB       (32  * 1024)
#define SIZE_248KB      (248 * 1024)
#define SIZE_256KB      (256 * 1024)
#define SIZE_128KB      (128 * 1024)
#define SIZE_144KB      (144 * 1024)
#define SIZE_240KB      (240 * 1024)
#define SIZE_360KB      (360 * 1024)
#define SIZE_384KB      (384 * 1024)
#define SIZE_512KB      (512 * 1024)
#define SIZE_640KB      (640 * 1024)
#define SIZE_1MB        (1024 * 1024)

// Flash Memory Map
#define STD_FW_PAYLOAD_SIZE                 SIZE_1MB
#define STD_APP_ID_SIZE                     32
#define STD_FW_SIGNATURE_OFFSET             (STD_FW_PAYLOAD_SIZE - STD_APP_ID_SIZE + 4)  // 0xFFFE4
#define STD_FW_VER_OFFSET                   (STD_FW_PAYLOAD_SIZE - STD_APP_ID_SIZE + 12) // 0xFFFEC
#define STD_FW_VER_SIZE                     3
#define CUSTOMER_PROJ_ID_OFFSET             (STD_FW_PAYLOAD_SIZE - STD_APP_ID_SIZE + 15) // 0xFFFEF
#define CUSTOMER_FW_VER_OFFSET              (STD_FW_PAYLOAD_SIZE - STD_APP_ID_SIZE + 16) // 0xFFFF0
#define CUSTOMER_FW_VER_SIZE                2

#define FW_CERTIFICATE_SIZE                 SIZE_1KB
#define FW_RSA_SIGNATURE_SIZE               256
#define FW_RSA_SIGNATURE_BLOCK_SIZE         SIZE_1KB
#define ESM_PAYLOAD_BLOCK_SIZE              SIZE_256KB
#define APP_CODE_NORMAL_BLOCK_SIZE          SIZE_384KB
#define APP_CODE_EXTEND_BLOCK_SIZE          SIZE_640KB
#define APP_INIT_DATA_BLOCK_SIZE            SIZE_24KB
#define CMDB_BLOCK_SIZE                     SIZE_4KB

#define SPI_ESM_CERTIFICATE_START           0
#define SPI_APP_CERTIFICATE_START           (SPI_ESM_CERTIFICATE_START + FW_CERTIFICATE_SIZE)           // 0x00400
#define SPI_ESM_RSA_SIGNATURE_START         (SPI_APP_CERTIFICATE_START + FW_CERTIFICATE_SIZE)           // 0x00800
#define SPI_APP_RSA_SIGNATURE_START         (SPI_ESM_RSA_SIGNATURE_START + FW_RSA_SIGNATURE_BLOCK_SIZE) // 0x00C00
#define SPI_ESM_PAYLOAD_START               (SPI_APP_RSA_SIGNATURE_START + FW_RSA_SIGNATURE_BLOCK_SIZE) // 0x01000
#define SPI_APP_PAYLOAD_START               (SPI_ESM_PAYLOAD_START + ESM_PAYLOAD_BLOCK_SIZE)            // 0x41000
#define SPI_APP_NORMAL_INIT_DATA_START      (SPI_APP_PAYLOAD_START + APP_CODE_NORMAL_BLOCK_SIZE)        // 0xA1000
#define SPI_APP_EXTEND_INIT_DATA_START      (SPI_APP_PAYLOAD_START + APP_CODE_EXTEND_BLOCK_SIZE)        // 0xE1000
#define SPI_CMDB_BLOCK_START                0xFE000UL
#define SPI_APP_ID_DATA_START               (STD_FW_PAYLOAD_SIZE - STD_APP_ID_SIZE)

#define FLASH_PROGRAM_COUNT                 100000
#define WAIT_PROG_INTERVAL_MS               500

// Native DPCD fields defined in spec.
#define DPCD_ADDR_BRANCH_ID_STR         0x00503
#define DPCD_ADDR_IEEE_OUI              0x00300

#define DPCD_FIELD_SIZE_IEEE_OUI        3

#define MCA_OUI_BYTE_0                  0x00        // MegaChips OUI
#define MCA_OUI_BYTE_1                  0x60
#define MCA_OUI_BYTE_2                  0xAD
// Kinetic proprietary DPCD fields
#define DPCD_ADDR_KT_CMD_STATUS_REG     0x0050D
#define DPCD_ADDR_KT_PARAM_REG          0x0050E

#define DPCD_ADDR_KT_REPLY_LEN_REG      0x00513
#define DPCD_SIZE_KT_REPLY_LEN_REG      1           // 0x00513

#define DPCD_ADDR_KT_REPLY_DATA_REG     0x00514
#define DPCD_SIZE_KT_REPLY_DATA_REG     12          // 0x00514 ~ 0x0051F

#define DPCD_ADDR_KT_AUX_WIN			0x80000ul
#define DPCD_SIZE_KT_AUX_WIN            0x8000ul    // 0x80000ul ~ 0x87FFF
#define DPCD_ADDR_KT_AUX_WIN_END		(DPCD_ADDR_KT_AUX_WIN +  DPCD_SIZE_KT_AUX_WIN - 1)

#define INIT_CRC16                      0x1021

typedef enum
{
    MCDP_CHIP_NONE      = 0,
    MCDP_BOBCAT_2800    = 1,
    MCDP_BOBCAT_2850    = 2,
    MCDP_PEGASUS        = 3,
    MCDP_MYSTIQUE       = 4,
    MCDP_DP2VGA         = 5,
    MCDP_PUMA_2900      = 6,
    MCDP_PUMA_2920      = 7,
    MCDP_JAGUAR_5000    = 8,
    MCDP_MUSTANG_5200   = 9,
} KineticChipId;

typedef enum
{
    FU_KT_IMG_IDX_ISP_DRV   = 0,
    FU_KT_IMG_IDX_APP       = 1,
} FuKineticImgIdx;

//static MCDP_CHIP_ID_e target_chip_id;

static guint32 esm_payload_size;
static guint32 arm_app_code_size;
static guint32 app_init_data_size;
static guint32 cmdb_block_size;
static gboolean is_fw_esm_xip_enabled;

static guint32 isp_drv_bin_size;
static guint16 isp_drv_run_delay_time;

static guint16 read_flash_prog_time;
static guint16 user_flash_prog_time;
static guint16 flash_id;
static guint16 flash_size;

static guint32 isp_payload_procd_size = 0;
static guint32 isp_procd_size;
static guint32 isp_total_data_size;
static gboolean is_isp_secure_auth_mode = TRUE;

static gboolean kt_aux_read_src_oui(FuKineticMstDevice *self, guint8 *buf, GError **error)
{
    g_autoptr(FuKineticMstConnection) connection = NULL;

    connection = fu_kinetic_mst_connection_new(fu_udev_device_get_fd(FU_UDEV_DEVICE (self)));

    if (!fu_kinetic_mst_connection_read(connection, DPCD_ADDR_IEEE_OUI, buf, DPCD_FIELD_SIZE_IEEE_OUI, error))
    {
        g_prefix_error (error, "Reading source OUI failed!");
        return FALSE;
    }

    return TRUE;
}

static gboolean kt_aux_write_src_oui(FuKineticMstDevice *self, const guint8 *buf, GError **error)
{
    g_autoptr(FuKineticMstConnection) connection = NULL;

    connection = fu_kinetic_mst_connection_new(fu_udev_device_get_fd(FU_UDEV_DEVICE (self)));

	if (!fu_kinetic_mst_connection_write(connection, DPCD_ADDR_IEEE_OUI, buf, DPCD_FIELD_SIZE_IEEE_OUI, error))
	{
		g_prefix_error (error, "Writing source OUI failed!");
        return FALSE;
	}

    return TRUE;
}

static const gchar *_get_chip_id_str(KineticChipId chip_id)
{
    if (chip_id == MCDP_JAGUAR_5000)
        return "Jaguar";

    if (chip_id == MCDP_MUSTANG_5200)
        return "Mustang";
    
    return "";
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

static gboolean sec_aux_isp_read_param_reg(FuKineticMstConnection *self, guint8 *dpcd_val, GError **error)
{
	if (!fu_kinetic_mst_connection_read(self, DPCD_ADDR_KT_PARAM_REG, dpcd_val, 1, error))
	{
		g_prefix_error (error, "Failed to read DPCD_MCA_PARAMETER_REG!");
		return FALSE;
	}

    return TRUE;
}

static gboolean
sec_aux_isp_write_kt_prop_cmd(FuKineticMstConnection *self, guint8 cmd_id, GError **error)
{
    cmd_id |= DPCD_KT_CONFIRMATION_BIT;

    if (!fu_kinetic_mst_connection_write(self, DPCD_ADDR_KT_CMD_STATUS_REG, &cmd_id, 1, error))
    {
        g_prefix_error (error, "Failed to write DPCD_MCA_CMD_REG!");
        return FALSE;
    }

    return TRUE;
}

static gboolean
sec_aux_isp_send_kt_prop_cmd(FuKineticMstConnection *self,
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
        if (!fu_kinetic_mst_connection_read(self, DPCD_ADDR_KT_CMD_STATUS_REG, (guint8 *)&dpcd_val,
                                            1, error))
        {
            return FALSE;
        }

        if (dpcd_val != (cmd_id | DPCD_KT_CONFIRMATION_BIT))  // Target responses
        {
            if (dpcd_val != cmd_id)
            {
                *status = dpcd_val & DPCD_KT_COMMAND_MASK;

                if (*status == KT_DPCD_STS_CRC_FAILURE)
                {
                    //ret = MCTRL_STS_CHKSUM_ERROR;
                    g_prefix_error (error, "Chunk data CRC checking failed!");
                }
                else
                {
                    //ret = MCTRL_STS_INVALID_REPLY;
                    g_prefix_error (error, "Invalid DPCD_Cmd_Sts_Reg reply! (0x%X)", *status);
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
            //ret = MCTRL_STS_TIMEOUT;
            g_prefix_error (error, "Waiting DPCD_Cmd_Sts_Reg timed-out!");

            return FALSE;
        }
    }

    return ret;
}

static gboolean sec_aux_isp_write_dpcd_reply_data_reg(FuKineticMstConnection *self,
                                                      guint8 *buf,
                                                      guint8 len,
                                                      GError **error)
{
    gboolean ret = FALSE;
    gboolean res;

    if (len > DPCD_SIZE_KT_REPLY_DATA_REG)
        //return MCTRL_STS_INVALID_PARAM;
        return FALSE;

    res = fu_kinetic_mst_connection_write(self, DPCD_ADDR_KT_REPLY_DATA_REG, buf, len, error);
    if (!res)
    {
        g_prefix_error (error, "Failed to write DPCD_KT_REPLY_DATA_REG!");
        len = 0;    // Clear reply data length to 0 if failed to write reply data
    }

    if (fu_kinetic_mst_connection_write(self, DPCD_ADDR_KT_REPLY_LEN_REG, &len, DPCD_SIZE_KT_REPLY_LEN_REG, error) &&
        TRUE == res)
    {
        // Both reply data and reply length are written successfully
        ret = TRUE;
    }

    return ret;
}

static gboolean
sec_aux_isp_enter_code_loading_mode(FuKineticMstConnection *self,
                                    gboolean is_app_mode,
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
    if (!sec_aux_isp_write_dpcd_reply_data_reg(self, (guint8 *)&isp_drv_bin_size, sizeof(isp_drv_bin_size), error))
        return FALSE;

    if (!sec_aux_isp_send_kt_prop_cmd(self, KT_DPCD_CMD_ENTER_CODE_LOADING_MODE, 500, 10, &status, error))
        return FALSE;

    return TRUE;
}

static gboolean
sec_aux_isp_send_payload(FuKineticMstConnection *self,
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
            if (!sec_aux_isp_write_dpcd_reply_data_reg(self, (guint8 *)&crc16, sizeof(crc16), error))
            {
                g_prefix_error (error, "Failed to send CRC16 to reply data register");

                return FALSE;
            }

            crc16 = INIT_CRC16; // Reset to initial CRC16 value for new chunk
        }

        // Send 16 bytes payload in each AUX transaction
        if (!fu_kinetic_mst_connection_write(self, aux_win_addr, temp_buf, aux_wr_size, error))
        {
            g_prefix_error (error, "Failed to send payload on AUX write %u", isp_procd_size);

            return FALSE;
        }

        buf += aux_wr_size;
        aux_win_addr += aux_wr_size;
        remain_len -= aux_wr_size;
        isp_procd_size += aux_wr_size;
        isp_payload_procd_size += aux_wr_size;

        if ((aux_win_addr > DPCD_ADDR_KT_AUX_WIN_END) || (remain_len == 0))
        {
            // 32KB payload has been sent AUX window
            aux_win_addr = DPCD_ADDR_KT_AUX_WIN;

            if (!sec_aux_isp_send_kt_prop_cmd(self, KT_DPCD_CMD_CHUNK_DATA_PROCESSED, wait_time_ms,
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
sec_aux_isp_wait_dpcd_cmd_cleared(FuKineticMstConnection *self,
                                  guint16 wait_time_ms,
                                  guint16 poll_interval_ms,
                                  guint8 *status,
                                  GError **error)
{
    guint8 dpcd_val = KT_DPCD_CMD_STS_NONE;

    *status = KT_DPCD_CMD_STS_NONE;

    while (wait_time_ms > 0)
    {
        if (!fu_kinetic_mst_connection_read(self,
                                            DPCD_ADDR_KT_CMD_STATUS_REG,
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
            g_prefix_error (error, "Waiting DPCD_Isp_Sink_Status_Reg timed-out!");

            return FALSE;
        }
    }

    return TRUE;
}

// ---------------------------------------------------------------
// In Jaguar, it takes about 1000 ms to boot up and initialize
// ---------------------------------------------------------------
static gboolean
sec_aux_isp_execute_isp_drv(FuKineticMstConnection *self, GError **error)
{
    guint8 status;

    flash_id = 0;
    flash_size = 0;
    read_flash_prog_time = 10;

    if (!sec_aux_isp_write_kt_prop_cmd(self, KT_DPCD_CMD_EXECUTE_RAM_CODE, error))
        return FALSE;

    if (!sec_aux_isp_wait_dpcd_cmd_cleared(self, 1500, 100, &status, error))
    {
        if (KT_DPCD_STS_INVALID_IMAGE == status)
            g_prefix_error (error, "Invalid ISP driver!");
        else
            g_prefix_error (error, "Executing ISP driver... failed!");
        
        return FALSE;
    }
    
    if (!sec_aux_isp_read_param_reg(self, &status, error))
        return FALSE;

    if (KT_DPCD_STS_SECURE_ENABLED == status || KT_DPCD_STS_SECURE_DISABLED == status)
    {
        guint8 reply_data[6] = {0};

        if (KT_DPCD_STS_SECURE_ENABLED == status)
            is_isp_secure_auth_mode = TRUE;
        else
        {
            is_isp_secure_auth_mode = FALSE;
            isp_total_data_size -= (FW_CERTIFICATE_SIZE * 2 + FW_RSA_SIGNATURE_BLOCK_SIZE * 2);
        }

        if (!sec_aux_isp_write_dpcd_reply_data_reg(self, reply_data, sizeof(reply_data), error))
        {
            flash_id = (guint16)reply_data[0] << 8 | reply_data[1];
            flash_size = (guint16)reply_data[2] << 8 | reply_data[3];
            read_flash_prog_time = (guint16)reply_data[4] << 8 | reply_data[5];

			if (read_flash_prog_time == 0)
				read_flash_prog_time = 10;

            return TRUE;
        }

        g_prefix_error (error, "Reading flash ID... failed!");

        return FALSE;
    }

    g_prefix_error (error, "Waiting for ISP driver ready... failed!");

    return FALSE;
}

static gboolean sec_aux_isp_send_isp_drv(FuKineticMstConnection *self,
                                         gboolean is_app_mode,
                                         const guint8 *isp_drv_data,
                                         guint32 isp_drv_len,
                                         GError **error)
{
    g_message("Sending ISP driver payload... started");

    if (sec_aux_isp_enter_code_loading_mode(self, is_app_mode, error) == FALSE)
    {
        g_prefix_error(error, "Enabling code-loading mode... failed!");
        return FALSE;
    }
    
    if (!sec_aux_isp_send_payload(self, isp_drv_data, isp_drv_len, 10000, 50, error))
    {
        g_prefix_error(error, "Sending ISP driver payload... failed!");
        return FALSE;
    }

    g_message("Sending ISP driver payload... done!");

    if (!sec_aux_isp_execute_isp_drv(self, error))
    {
        g_prefix_error(error, "ISP driver booting up... failed!");
        return FALSE;
    }

    g_message("Flash ID: 0x%04X  ", flash_id);

    if (flash_size)
    {
        if (flash_size < 2048)    // One bank size in Jaguar is 1024KB
            g_message("(%d KB, Dual Bank not supported!)", flash_size);
        else
            g_message("(%d KB)", flash_size);
    }
    else
    {
        if (flash_id)
            g_message("(SPI flash not supported)");
        else
            g_message("(SPI flash not connected)");
    }

    return TRUE;
}

static gboolean
sec_aux_isp_enable_fw_update_mode(FuKineticMstConnection *self, GError **error)
{
    guint8 status;
    guint8 pl_size_data[12] = {0};

    g_message("Entering F/W loading mode...");

    // Send payload size to DPCD_MCA_REPLY_DATA_REG
    *(guint32 *)pl_size_data = esm_payload_size;
    *(guint32 *)&pl_size_data[4] = arm_app_code_size;
    *(guint16 *)&pl_size_data[8] = (guint16)app_init_data_size;
    *(guint16 *)&pl_size_data[10] = (guint16)cmdb_block_size | (is_fw_esm_xip_enabled << 15);

    if (!sec_aux_isp_write_dpcd_reply_data_reg(self, pl_size_data, sizeof(pl_size_data), error))
    {
        g_prefix_error(error, "Send payload size failed!");
        return FALSE;
    }

    if (!sec_aux_isp_send_kt_prop_cmd(self, KT_DPCD_CMD_ENTER_FW_UPDATE_MODE, 200000, 500, &status, error))
    {
        g_prefix_error(error, "Entering F/W update mode... failed!");
        return FALSE;
    }

    g_message("F/W loading mode... ready");

    return TRUE;
}


static gboolean
sec_aux_isp_send_fw_payload(FuKineticMstConnection *self,
                            const guint8 *fw_data,
                            guint32 fw_len,
                            GError **error)
{
    guint8 *ptr;

    if (is_isp_secure_auth_mode)
    {
        g_message("Sending Certificates... started!");
        // Send ESM and App Certificates & RSA Signatures
        ptr = (guint8 *)fw_data;
        if (!sec_aux_isp_send_payload(self, ptr, FW_CERTIFICATE_SIZE * 2 + FW_RSA_SIGNATURE_BLOCK_SIZE * 2, 10000, 200, error))
        {
            g_prefix_error(error, "Sending Certificates... failed!");
            return FALSE;
        }

        g_message("Sending Certificates... done!");
    }

    // Send ESM code
    g_message("Sending ESM... started!");

    ptr = (guint8 *)fw_data + SPI_ESM_PAYLOAD_START;
    if (!sec_aux_isp_send_payload(self, ptr, esm_payload_size, 10000, 200, error))
    {
        g_prefix_error(error, "Sending ESM... failed!");
        return FALSE;
    }

    g_message("Sending ESM... done!");

    // Send App code
    g_message("Sending App... started!");

    ptr = (guint8 *)fw_data + SPI_APP_PAYLOAD_START;
    if (!sec_aux_isp_send_payload(self, ptr, arm_app_code_size, 10000, 200, error))
    {
        g_prefix_error(error, "Sending App... failed!");
        return FALSE;
    }

    g_message("Sending App... done!");

    if (app_init_data_size) // It should not be zero in normal case. Just patch for GDB issue.
    {
        // Send App initialized data
        g_message("Sending App init data... started!");

        ptr = (guint8 *)fw_data + ((is_fw_esm_xip_enabled) ? SPI_APP_EXTEND_INIT_DATA_START : SPI_APP_NORMAL_INIT_DATA_START);
        if (!sec_aux_isp_send_payload(self, ptr, app_init_data_size, 10000, 200, error))
        {
            g_prefix_error(error, "Sending App init data... failed!");
            return FALSE;
        }

        g_message("Sending App init data... done!");
    }

    if (cmdb_block_size)
    {
        // Send CMDB
        g_message("Sending CMDB... started!");

        ptr = (guint8 *)fw_data + SPI_CMDB_BLOCK_START;
        if (!sec_aux_isp_send_payload(self, ptr, cmdb_block_size, 10000, 200, error))
        {
            g_prefix_error(error, "Sending CMDB... failed!");
            return FALSE;
        }

        g_message("Sending CMDB... done!");
    }

    ptr = (guint8 *)fw_data + SPI_APP_ID_DATA_START;
    if (!sec_aux_isp_send_payload(self, ptr, STD_APP_ID_SIZE, 10000, 200, error))
    {
        g_prefix_error(error, "Sending App ID data... failed!");
        return FALSE;
    }
    g_message("Sending App ID data... done!");
    
    return TRUE;
}

static gboolean
sec_aux_isp_install_fw_images(FuKineticMstConnection *self, GError **error)
{
    guint8 cmd_id = KT_DPCD_CMD_INSTALL_IMAGES;
    guint8 status;
    guint16 wait_count = 1500;
    guint16 progress_inc = FLASH_PROGRAM_COUNT / ((read_flash_prog_time * 1000) / WAIT_PROG_INTERVAL_MS);

    g_message("Installing F/W payload... started");

    if (!sec_aux_isp_write_kt_prop_cmd(self, cmd_id, error))
    {
        g_prefix_error(error, "Sending DPCD command... failed!");
        return FALSE;
    }

    while (wait_count-- > 0)
    {
        if (!fu_kinetic_mst_connection_read(self, DPCD_ADDR_KT_CMD_STATUS_REG, &status, 1, error))
        {
            g_prefix_error(error, "Reading DPCD_MCA_CMD_REG... failed!");
            return FALSE;
        }

        if (status != (cmd_id | DPCD_KT_CONFIRMATION_BIT))  // Target responsed
        {
            if (status == cmd_id)   // Confirmation bit if cleared
            {
                isp_payload_procd_size += (isp_total_data_size - isp_procd_size);
                g_message("Programming F/W payload... done!");

                return TRUE;
            }
            else
            {
                g_prefix_error(error, "Installing images... failed!");

                return FALSE;
            }
        }

        if (isp_procd_size < isp_total_data_size)
        {
            isp_procd_size += progress_inc;
            isp_payload_procd_size += progress_inc;
        }
        
        // Wait 50ms
        g_usleep(50 * 1000);
    }

    g_prefix_error(error, "Installing images... timed-out!");

    return FALSE;
}

static gboolean
sec_aux_isp_send_reset_command(FuKineticMstConnection *self, GError **error)
{
	g_message("Resetting system...");

    if (!sec_aux_isp_write_kt_prop_cmd(self, KT_DPCD_CMD_RESET_SYSTEM, error))
    {
        g_prefix_error(error, "Resetting system... failed!");
        return FALSE;
    }

    return TRUE;
}

static gboolean
sec_aux_isp_update_firmware(FuKineticMstDevice *self,
                            FuFirmware *firmware,
                            GError **error)
{
    gboolean ret = FALSE;
#if 0   // <TODO> Port the functions to get chip info
    gboolean is_app_mode = (DeviceInfo_In.FwRunState == MCDP_RUN_APP)? TRUE : FALSE;
#else
    gboolean is_app_mode = TRUE;
#endif
    g_autoptr(GBytes) isp_drv = NULL;
    g_autoptr(GBytes) app = NULL;
	const guint8 *payload_data;
	gsize payload_len;
	g_autoptr(FuDeviceLocker) locker = NULL;
    g_autoptr(FuKineticMstConnection) connection = NULL;
    g_autoptr(FuFirmwareImage) img = NULL;
    guint8 mca_oui[DPCD_FIELD_SIZE_IEEE_OUI] = {MCA_OUI_BYTE_0, MCA_OUI_BYTE_1, MCA_OUI_BYTE_2};

    connection = fu_kinetic_mst_connection_new (fu_udev_device_get_fd (FU_UDEV_DEVICE (self)));

    isp_procd_size = 0;

#if 0   // <TODO> Port the functions to get chip info
    g_message("Start secure AUX-ISP [%s]...", _get_chip_id_str(DeviceInfo_In.ChipId));
#endif

    // Write MCA OUI
    if (!kt_aux_write_src_oui(self, mca_oui, error))
        goto SECURE_AUX_ISP_END;

    // Send ISP driver and execute it
    img = fu_firmware_get_image_by_idx(firmware, FU_KT_IMG_IDX_ISP_DRV, error);
    if (NULL == img)
        return FALSE;
    isp_drv = fu_firmware_image_write (img, error);
	if (isp_drv == NULL)
		return FALSE;
	payload_data = g_bytes_get_data (isp_drv, &payload_len);

    if (payload_len)
    {
        if (!sec_aux_isp_send_isp_drv(connection, is_app_mode, payload_data, payload_len, error))
            goto SECURE_AUX_ISP_END;
    }

    // Enable FW update mode
    if (!sec_aux_isp_enable_fw_update_mode(connection, error))
        goto SECURE_AUX_ISP_END;

    // Send FW image
    img = fu_firmware_get_image_by_idx(firmware, FU_KT_IMG_IDX_APP, error);
    if (NULL == img)
        return FALSE;
    app = fu_firmware_image_write(img, error);
	if (app == NULL)
		return FALSE;
	payload_data = g_bytes_get_data (app, &payload_len);
    if (!sec_aux_isp_send_fw_payload(connection, payload_data, payload_len, error))
        goto SECURE_AUX_ISP_END;

    // Install FW images
    ret = sec_aux_isp_install_fw_images(connection, error);

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
fu_kinetic_mst_device_write_firmware(FuDevice *device,
               				         FuFirmware *firmware,
               				         FwupdInstallFlags flags,
               				         GError **error)
{
	FuKineticMstDevice *self = FU_KINETIC_MST_DEVICE (device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);

	/* update firmware */
#if 0
	if (self->family == FU_SYNAPTICS_MST_FAMILY_PANAMERA) {
		if (!fu_synaptics_mst_device_panamera_prepare_write (self, error)) {
			g_prefix_error (error, "Failed to prepare for write: ");
			return FALSE;
		}
		if (!fu_synaptics_mst_device_update_esm (self,
							 payload_data,
							 error)) {
			g_prefix_error (error, "ESM update failed: ");
			return FALSE;
		}
		if (!fu_synaptics_mst_device_update_panamera_firmware (self,
								       payload_len,
								       payload_data,
								       error)) {
			g_prefix_error (error, "Firmware update failed: ");
			return FALSE;
		}
	} else {
		if (!fu_synaptics_mst_device_update_tesla_leaf_firmware (self,
									 payload_len,
									 payload_data,
									 error)) {
			g_prefix_error (error, "Firmware update failed: ");
			return FALSE;
		}
	}
#else
    if (self->family == FU_KINETIC_MST_FAMILY_JAGUAR || self->family == FU_KINETIC_MST_FAMILY_MUSTANG)
    {
        if (!sec_aux_isp_update_firmware(self, firmware, error))
        {
            g_prefix_error (error, "Firmware update failed: ");
			return FALSE;
        }
    }
    else
    {
        // <TODO> support older chips?
    }
#endif

	/* wait for flash clear to settle */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	fu_device_sleep_with_progress (device, 2);

	return TRUE;
}

FuKineticMstDevice *
fu_kinetic_mst_device_new (FuUdevDevice *device)
{
	FuKineticMstDevice *self = g_object_new (FU_TYPE_KINETIC_MST_DEVICE, NULL);
	fu_device_incorporate (FU_DEVICE (self), FU_DEVICE (device));
	return self;
}

void
fu_kinetic_mst_device_set_system_type (FuKineticMstDevice *self, const gchar *system_type)
{
	g_return_if_fail (FU_IS_KINETIC_MST_DEVICE (self));
	self->system_type = g_strdup (system_type);
}

static void
fu_kinetic_mst_device_class_init (FuKineticMstDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	object_class->finalize = fu_kinetic_mst_device_finalize;
	//klass_device->to_string = fu_kinetic_mst_device_to_string;
	//klass_device->rescan = fu_kinetic_mst_device_rescan;
	klass_device->write_firmware = fu_kinetic_mst_device_write_firmware;
	klass_device->prepare_firmware = fu_kinetic_mst_device_prepare_firmware;
	//klass_device->probe = fu_kinetic_mst_device_probe;
}

