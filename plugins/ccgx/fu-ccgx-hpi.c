/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "fu-ccgx-common.h"
#include "fu-ccgx-hpi.h"
#include "fu-ccgx-cyacd-file.h"

#define MAX_NO_PORTS  0x02
#define SI_ID_COMP_VAL_HPI 0x1800
#define FLASH_ROW_SIZE_MASK 0x70
#define CY_PD_GET_SILICON_ID_CMD_SIG  0x53
#define CY_PD_REG_INTR_REG_CLEAR_RQT 0x01
#define CY_PD_JUMP_TO_BOOT_CMD_SIG 0x4A
#define CY_PD_JUMP_TO_ALT_FW_CMD_SIG 0x41
#define CY_PD_DEVICE_RESET_CMD_SIG 0x52
#define CY_PD_REG_RESET_DEVICE_CMD 0x01
#define CY_PD_ENTER_FLASHING_MODE_CMD_SIG 0x50
#define CY_PD_FLASH_READ_WRITE_CMD_SIG 0x46
#define CY_PD_REG_FLASH_ROW_READ_CMD 0x00
#define CY_PD_REG_FLASH_ROW_WRITE_CMD 0x01
#define CY_PD_REG_FLASH_READ_WRITE_ROW_LSB 0x02
#define CY_PD_REG_FLASH_READ_WRITE_ROW_MSB 0x03
#define CY_PD_U_VDM_TYPE 0x00
#define HPI_GET_SILICON_ID_CMD_SIG 0x53
#define HPI_REG_INTR_REG_CLEAR_RQT 0x01
#define HPI_JUMP_TO_BOOT_CMD_SIG 0x4A
#define HPI_DEVICE_RESET_CMD_SIG 0x52
#define HPI_REG_RESET_DEVICE_CMD 0x01
#define HPI_ENTER_FLASHING_MODE_CMD_SIG 0x50
#define HPI_FLASH_READ_WRITE_CMD_SIG 0x46
#define HPI_REG_FLASH_ROW_READ_CMD 0x00
#define HPI_REG_FLASH_ROW_WRITE_CMD 0x01
#define HPI_REG_FLASH_READ_WRITE_ROW_LSB 0x02
#define HPI_REG_FLASH_READ_WRITE_ROW_MSB 0x03
#define HPI_PORT_DISABLE_CMD 0x11

#define HPI_DEVICE_VERSION_SIZE_HPIV1 16
#define HPI_DEVICE_VERSION_SIZE_HPIV2 24
#define HPI_META_DATA_OFFSET_ROW_128 64
#define HPI_META_DATA_OFFSET_ROW_256 (64 + 128)
#define PD_I2C_USB_EP_BULK_OUT 0x01
#define PD_I2C_USB_EP_BULK_IN 0x82
#define PD_I2C_USB_EP_INTR_IN 0x83
#define PD_I2CM_USB_EP_BULK_OUT 0x02
#define PD_I2CM_USB_EP_BULK_IN 0x83
#define PD_I2CM_USB_EP_INTR_IN 0x84

typedef enum
{
	HPI_REG_SECTION_DEV = 0,/* device information registers */
	HPI_REG_SECTION_PORT_0,	/* USB-PD Port 0 related registers */
	HPI_REG_SECTION_PORT_1,	/* USB-PD Port 1 related registers */
	HPI_REG_SECTION_ALL	/* special definition to select	all register spaces */
} HPIRegSection;

typedef enum {
	CY_PD_REG_DEVICE_MODE_ADDR,
	CY_PD_BOOT_MODE_REASON,
	CY_PD_SILICON_ID,
	CY_PD_BL_LAST_ROW = 0x04,
	CY_PD_REG_INTR_REG_ADDR = 0x06,
	CY_PD_JUMP_TO_BOOT_REG_ADDR,
	CY_PD_REG_RESET_ADDR,
	CY_PD_REG_ENTER_FLASH_MODE_ADDR = 0x0A,
	CY_PD_REG_VALIDATE_FW_ADDR,
	CY_PD_REG_FLASH_READ_WRITE_ADDR,
	CY_PD_GET_VERSION = 0x10,
	CY_PD_REG_DBG_PD_INIT = 0x12,
	CY_PD_REG_U_VDM_CTRL_ADDR = 0x20,
	CY_PD_REG_READ_PD_PROFILE = 0x22,
	CY_PD_REG_EFFECTIVE_SOURCE_PDO_MASK = 0x24,
	CY_PD_REG_EFFECTIVE_SINK_PDO_MASK,
	CY_PD_REG_SELECT_SOURCE_PDO,
	CY_PD_REG_SELECT_SINK_PDO,
	CY_PD_REG_PD_CONTROL,
	CY_PD_REG_PD_STATUS = 0x2C,
	CY_PD_REG_TYPE_C_STATUS	= 0x30,
	CY_PD_REG_CURRENT_PDO = 0x34,
	CY_PD_REG_CURRENT_RDO = 0x38,
	CY_PD_REG_CURRENT_CABLE_VDO = 0x3C,
	CY_PD_REG_DISPLAY_PORT_STATUS = 0x40,
	CY_PD_REG_DISPLAY_PORT_CONFIG = 0x44,
	CY_PD_REG_ALTERNATE_MODE_MUX_SELECTION = 0X45,
	CY_PD_REG_EVENT_MASK = 0x48,
	CY_PD_REG_RESPONSE_ADDR = 0x7E,
	CY_PD_REG_BOOTDATA_MEMEORY_ADDR = 0x80,
	CY_PD_REG_FWDATA_MEMEORY_ADDR = 0xC0,
} CyPDReg;

typedef enum {
	HPI_DEV_REG_DEVICE_MODE = 0,
	HPI_DEV_REG_BOOT_MODE_REASON,
	HPI_DEV_REG_SI_ID,
	HPI_DEV_REG_SI_ID_LSB,
	HPI_DEV_REG_BL_LAST_ROW,
	HPI_DEV_REG_BL_LAST_ROW_LSB,
	HPI_DEV_REG_INTR_ADDR,
	HPI_DEV_REG_JUMP_TO_BOOT,
	HPI_DEV_REG_RESET_ADDR,
	HPI_DEV_REG_RESET_CMD,
	HPI_DEV_REG_ENTER_FLASH_MODE,
	HPI_DEV_REG_VALIDATE_FW_ADDR,
	HPI_DEV_REG_FLASH_READ_WRITE,
	HPI_DEV_REG_FLASH_READ_WRITE_CMD,
	HPI_DEV_REG_FLASH_ROW,
	HPI_DEV_REG_FLASH_ROW_LSB,
	HPI_DEV_REG_ALL_VERSION,
	HPI_DEV_REG_ALL_VERSION_BYTE_1,
	HPI_DEV_REG_ALL_VERSION_BYTE_2,
	HPI_DEV_REG_ALL_VERSION_BYTE_3,
	HPI_DEV_REG_ALL_VERSION_BYTE_4,
	HPI_DEV_REG_ALL_VERSION_BYTE_5,
	HPI_DEV_REG_ALL_VERSION_BYTE_6,
	HPI_DEV_REG_ALL_VERSION_BYTE_7,
	HPI_DEV_REG_ALL_VERSION_BYTE_8,
	HPI_DEV_REG_ALL_VERSION_BYTE_9,
	HPI_DEV_REG_ALL_VERSION_BYTE_10,
	HPI_DEV_REG_ALL_VERSION_BYTE_11,
	HPI_DEV_REG_ALL_VERSION_BYTE_12,
	HPI_DEV_REG_ALL_VERSION_BYTE_13,
	HPI_DEV_REG_ALL_VERSION_BYTE_14,
	HPI_DEV_REG_ALL_VERSION_BYTE_15,
	HPI_DEV_REG_FW_2_VERSION,
	HPI_DEV_REG_FW_2_VERSION_BYTE_1,
	HPI_DEV_REG_FW_2_VERSION_BYTE_2,
	HPI_DEV_REG_FW_2_VERSION_BYTE_3,
	HPI_DEV_REG_FW_2_VERSION_BYTE_4,
	HPI_DEV_REG_FW_2_VERSION_BYTE_5,
	HPI_DEV_REG_FW_2_VERSION_BYTE_6,
	HPI_DEV_REG_FW_2_VERSION_BYTE_7,
	HPI_DEV_REG_FW_BIN_LOC,
	HPI_DEV_REG_FW_1_BIN_LOC_LSB,
	HPI_DEV_REG_FW_2_BIN_LOC_MSB,
	HPI_DEV_REG_FW_2_BIN_LOC_LSB,
	HPI_DEV_REG_PORT_ENABLE,
	HPI_DEV_SPACE_REG_LEN,
	HPI_DEV_REG_RESPONSE = 0x007E,
	HPI_DEV_REG_FLASH_MEM = 0x0200
} HPIDevReg;

typedef enum {
	/* responses */
	CY_PD_RESP_NO_RESPONSE,
	CY_PD_RESP_SUCCESS = 0x02,
	CY_PD_RESP_FLASH_DATA_AVAILABLE,
	CY_PD_RESP_INVALID_COMMAND = 0x05,
	CY_PD_RESP_COLLISION_DETECTED,
	CY_PD_RESP_FLASH_UPDATE_FAILED,
	CY_PD_RESP_INVALID_FW,
	CY_PD_RESP_INVALID_ARGUMENTS,
	CY_PD_RESP_NOT_SUPPORTED,
	CY_PD_RESP_TRANSACTION_FAILED = 0x0C,
	CY_PD_RESP_PD_COMMAND_FAILED,
	CY_PD_RESP_UNDEFINED,
	CY_PD_RESP_RA_DETECT = 0x10,
	CY_PD_RESP_RA_REMOVED,

	/* device specific events */
	CY_PD_RESP_RESET_COMPLETE = 0x80,
	CY_PD_RESP_MESSAGE_QUEUE_OVERFLOW,

	/* type-c specific events */
	CY_PD_RESP_OVER_CURRENT_DETECTED,
	CY_PD_RESP_OVER_VOLTAGE_DETECTED,
	CY_PD_RESP_TYPC_C_CONNECTED,
	CY_PD_RESP_TYPE_C_DISCONNECTED,

	/* pd specific events and asynchronous messages */
	CY_PD_RESP_PD_CONTRACT_ESTABLISHED,
	CY_PD_RESP_DR_SWAP,
	CY_PD_RESP_PR_SWAP,
	CY_PD_RESP_VCON_SWAP,
	CY_PD_RESP_PS_RDY,
	CY_PD_RESP_GOTOMIN,
	CY_PD_RESP_ACCEPT_MESSAGE,
	CY_PD_RESP_REJECT_MESSAGE,
	CY_PD_RESP_WAIT_MESSAGE,
	CY_PD_RESP_HARD_RESET,
	CY_PD_RESP_VDM_RECEIVED,
	CY_PD_RESP_SRC_CAP_RCVD,
	CY_PD_RESP_SINK_CAP_RCVD,
	CY_PD_RESP_DP_ALTERNATE_MODE,
	CY_PD_RESP_DP_DEVICE_CONNECTED,
	CY_PD_RESP_DP_DEVICE_NOT_CONNECTED,
	CY_PD_RESP_DP_SID_NOT_FOUND,
	CY_PD_RESP_MULTIPLE_SVID_DISCOVERED,
	CY_PD_RESP_DP_FUNCTION_NOT_SUPPORTED,
	CY_PD_RESP_DP_PORT_CONFIG_NOT_SUPPORTED,
	CY_PD_HARD_RESET_SENT,
	CY_PD_SOFT_RESET_SENT,
	CY_PD_CABLE_RESET_SENT,
	CY_PD_SOURCE_DISBALED_STATE_ENTERED,
	CY_PD_SENDER_RESPONSE_TIMER_TIMEOUT,
	CY_PD_NO_VDM_RESPONSE_RECEIVED
}CyPDResp;

typedef enum hpi_port_reg_address
{
	HPI_PORT_REG_VDM_CTRL = 0,
	HPI_PORT_REG_VDM_CTRL_LEN,
	HPI_PORT_REG_EFF_SRC_PDO_MASK,
	HPI_PORT_REG_EFF_SINK_PDO_MASK,
	HPI_PORT_REG_SOURCE_PDO_ADDR,
	HPI_PORT_REG_SINK_PDO_ADDR,
	HPI_PORT_REG_PD_CTRL,
	HPI_PORT_REG_BYTE_7_RESERVED,
	HPI_PORT_REG_PD_STATUS,
	HPI_PORT_REG_PD_STATUS_BYTE_1,
	HPI_PORT_REG_PD_STATUS_BYTE_2,
	HPI_PORT_REG_PD_STATUS_BYTE_3,
	HPI_PORT_REG_TYPE_C_STATUS,
	HPI_PORT_REG_BYTE_13_RESERVED,
	HPI_PORT_REG_BYTE_14_RESERVED,
	HPI_PORT_REG_BYTE_15_RESERVED,
	HPI_PORT_REG_CUR_PDO,
	HPI_PORT_REG_CUR_PDO_BYTE_1,
	HPI_PORT_REG_CUR_PDO_BYTE_2,
	HPI_PORT_REG_CUR_PDO_BYTE_3,
	HPI_PORT_REG_CUR_RDO,
	HPI_PORT_REG_CUR_RDO_BYTE_1,
	HPI_PORT_REG_CUR_RDO_BYTE_2,
	HPI_PORT_REG_CUR_RDO_BYTE_3,
	HPI_PORT_REG_CABLE_VDO,
	HPI_PORT_REG_CABLE_VDO_BYTE_1,
	HPI_PORT_REG_CABLE_VDO_BYTE_2,
	HPI_PORT_REG_CABLE_VDO_BYTE_3,
	HPI_PORT_REG_BYTE_28_RESERVED,
	HPI_PORT_REG_BYTE_29_RESERVED,
	HPI_PORT_REG_BYTE_30_RESERVED,
	HPI_PORT_REG_BYTE_31_RESERVED,
	HPI_PORT_DP_HPD_CTRL,
	HPI_PORT_DP_MUX_CTRL,
	HPI_PORT_DP_TRIGGER_MODE,
	HPI_PORT_DP_CONFIGURE_MODE,
	HPI_PORT_REG_EVENT_MASK,
	HPI_PORT_REG_EVENT_MASK_BYTE_1,
	HPI_PORT_REG_EVENT_MASK_BYTE_2,
	HPI_PORT_REG_EVENT_MASK_BYTE_3,
	HPI_PORT_REG_SWAP_RESPONSE,
	HPI_PORT_REG_ACTIVE_EC_MODES,
	HPI_PORT_REG_VDM_EC_CTRL,
	HPI_PORT_SPACE_REG_LEN,
	HPI_PORT_READ_DATA_MEM_ADDR = 0x400,
	HPI_PORT_WRITE_DATA_MEM_ADDR = 0x800,
} HPIPortReg;

typedef enum {
	HPI_RESPONSE_NO_RESPONSE,
	HPI_RESPONSE_SUCCESS = 0x02,
	HPI_RESPONSE_FLASH_DATA_AVAILABLE,
	HPI_RESPONSE_INVALID_COMMAND = 0x05,
	HPI_RESPONSE_FLASH_UPDATE_FAILED = 0x07,
	HPI_RESPONSE_INVALID_FW,
	HPI_RESPONSE_INVALID_ARGUMENT,
	HPI_RESPONSE_NOT_SUPPORTED,
	HPI_RESPONSE_PD_TRANSACTION_FAILED = 0x0C,
	HPI_RESPONSE_PD_COMMAND_FAILED,
	HPI_RESPONSE_UNDEFINED_ERROR = 0x0F,
	HPI_EVENT_RESET_COMPLETE = 0x80,
	HPI_EVENT_MSG_OVERFLOW,
	HPI_EVENT_OC_DETECT,
	HPI_EVENT_OV_DETECT,
	HPI_EVENT_CONNECT_DETECT,
	HPI_EVENT_DISCONNECT_DETECT,
	HPI_EVENT_NEGOTIATION_COMPLETE,
	HPI_EVENT_SWAP_COMPLETE,
	HPI_EVENT_PS_RDY_RECEIVED = 0x8A,
	HPI_EVENT_GOTO_MIN_RECEIVED,
	HPI_EVENT_ACCEPT_RECEIVED,
	HPI_EVENT_REJECT_RECEIVED,
	HPI_EVENT_WAIT_RECEIVED,
	HPI_EVENT_HARD_RESET_RECEIVED,
	HPI_EVENT_VDM_RECEIVED = 0x90,
	HPI_EVENT_SOURCE_CAP_RECEIVED,
	HPI_EVENT_SINK_CAP_RECEIVED,
	HPI_EVENT_DP_MODE_ENTERED,
	HPI_EVENT_DP_STATUS_UPDATE,
	HPI_EVENT_DP_SID_NOT_FOUND = 0x96,
	HPI_EVENT_DP_MANY_SID_FOUND,
	HPI_EVENT_DP_NO_CABLE_SUPPORT,
	HPI_EVENT_DP_NO_UFP_SUPPORT,
	HPI_EVENT_HARD_RESET_SENT,
	HPI_EVENT_SOFT_RESET_SENT,
	HPI_EVENT_CABLE_RESET_SENT,
	HPI_EVENT_SOURCE_DISABLED,
	HPI_EVENT_SENDER_TIMEOUT,
	HPI_EVENT_VDM_NO_RESPONSE,
	HPI_EVENT_UNEXPECTED_VOLTAGE,
	HPI_EVENT_ERROR_RECOVERY,
	HPI_EVENT_EMCA_DETECT = 0xA6,
	HPI_EVENT_RP_CHANGE_DETECT = 0xAA,
	HPI_EVENT_TB_ENTERED = 0xB0,
	HPI_EVENT_TB_EXITED
}HPIResp;

typedef enum
{
	/* register region */
	HPI_REG_PART_REG = 0,
	/* data memory for device section */
	HPI_REG_PART_DATA = 1,
	/* flash memory	*/
	HPI_REG_PART_FLASH = 2,
	/* read data memory for port section */
	HPI_REG_PART_PDDATA_READ = 4,
	/* write data memory for port section */
	HPI_REG_PART_PDDATA_WRITE = 8
} HPIRegPart;

typedef struct __attribute__((packed))
{
	guint16	event_code;
	guint16	event_length;
	guint8	event_data[128];
}HPIEvent;

static guint16
reg_addr_gen (guint8 section, guint8 part, guint8 reg_addr)
{
	return (guint16) ( (section << 12) | (part << 8) | reg_addr);
}

static gboolean
reg_read (FuDevice *device, CyI2CDeviceHandle *i2c_handle, guint8 hpi_addr_byte, guint16 reg_addr, guint8* reg_data, guint16 size, GError **error)
{
	guint32 i;
	g_autofree guint8 *write_buffer = NULL;

	CyDataBuffer data_buffer = {0};
	CyI2CDataConfig data_cfg = {0};

	g_return_val_if_fail (i2c_handle != NULL, FALSE);
	g_return_val_if_fail (reg_data != NULL, FALSE);
	g_return_val_if_fail (hpi_addr_byte > 0, FALSE);

	write_buffer = g_malloc0 (hpi_addr_byte + 1) ;

	g_return_val_if_fail (write_buffer != NULL, FALSE);

	data_cfg.is_stop_bit = FALSE;
	data_cfg.is_nak_bit = TRUE;
	data_buffer.length = hpi_addr_byte;

	for(i=0; i < hpi_addr_byte; i++) {
		write_buffer[i] = (guint8)(reg_addr >> (8*i));
	}

	data_buffer.buffer = write_buffer;
	data_buffer.transfer_count = 0;

	if (!fu_ccgx_i2c_write(device, i2c_handle, &data_cfg, &data_buffer, error))
	{
		g_prefix_error (error, "reg_read - write error:");
		return FALSE;
	}

	data_cfg.is_stop_bit = TRUE;
	data_cfg.is_nak_bit = TRUE;

	data_buffer.buffer = reg_data;
	data_buffer.length = size;

	if (!fu_ccgx_i2c_read(device, i2c_handle, &data_cfg, &data_buffer, error)) {
		g_prefix_error (error, "reg_read - read	error:");
		return FALSE;
	}

	return TRUE;
}

static gboolean
reg_write (FuDevice *device, CyI2CDeviceHandle *i2c_handle, guint8 hpi_addr_byte, guint16 reg_addr, guint8 *reg_data, guint16 size, GError **error)
{
	guint32 i ;
	g_autofree guint8* write_buffer = NULL;

	CyDataBuffer data_buffer;
	CyI2CDataConfig data_cfg;

	g_return_val_if_fail (i2c_handle != NULL, FALSE);
	g_return_val_if_fail (reg_data != NULL, FALSE);
	g_return_val_if_fail (hpi_addr_byte > 0, FALSE);

	write_buffer = g_malloc0 (size + hpi_addr_byte + 1);

	g_return_val_if_fail (write_buffer != NULL, FALSE);

	data_cfg.is_stop_bit = TRUE;
	data_cfg.is_nak_bit = TRUE;
	data_buffer.length = size + hpi_addr_byte;

	for(i=0; i < hpi_addr_byte; i++) {
		write_buffer[i] = (guint8)(reg_addr >> (8*i));
	}

	memcpy (&write_buffer [hpi_addr_byte], reg_data, size);

	data_buffer.buffer = write_buffer;
	data_buffer.transfer_count = 0;

	if (!fu_ccgx_i2c_write(device, i2c_handle, &data_cfg, &data_buffer, error)) {
		g_prefix_error (error, "reg_write error:");
		return FALSE;
	}

	return TRUE;
}	

static gboolean
reg_write_no_resp (FuDevice *device, CyI2CDeviceHandle *i2c_handle, guint8 hpi_addr_byte, guint16 reg_addr, guint8 *reg_data, guint16 size, GError **error)
{
	g_autofree guint8* write_buffer = NULL;
	guint32 i ;
	CyDataBuffer data_buffer;
	CyI2CDataConfig data_cfg;

	g_return_val_if_fail (i2c_handle != NULL, FALSE);
	g_return_val_if_fail (reg_data != NULL, FALSE);
	g_return_val_if_fail (hpi_addr_byte > 0, FALSE);

	write_buffer = g_malloc0 (size + hpi_addr_byte + 1);

	g_return_val_if_fail (write_buffer != NULL, FALSE);

	data_cfg.is_stop_bit = TRUE;
	data_cfg.is_nak_bit = TRUE;
	data_buffer.length = size + hpi_addr_byte;

	for(i=0; i < hpi_addr_byte; i++) {
		write_buffer[i] = (guint8)(reg_addr >> (8*i));
	}

	memcpy (&write_buffer [hpi_addr_byte], reg_data, size);

	data_buffer.buffer = write_buffer;
	data_buffer.transfer_count = 0;

	if (!fu_ccgx_i2c_write_no_resp (device, i2c_handle, &data_cfg, &data_buffer, error)) {
		g_prefix_error (error, "reg_write_no_resp error:");
		return FALSE;
	}

	return TRUE;
}


static gboolean
clear_intr (FuDevice *device, CyHPIHandle *handle, HPIRegSection section, GError **error)
{
	guint8 i, clear_intr = 0;
	g_return_val_if_fail (handle != NULL, FALSE);

	for (i = 0; i <= handle->num_of_ports; i++) {
		if ((i == section) | (section == HPI_REG_SECTION_ALL)) {
			clear_intr |= (1 << i);
		}
	}

	if (!reg_write (device, &handle->i2c_handle,handle->hpi_addr_byte, HPI_DEV_REG_INTR_ADDR, &clear_intr, 1,error)) {
		g_prefix_error (error, "clear_intr error:");
		return FALSE;
	}

	return TRUE;
}

static gboolean
read_event_reg (FuDevice *device, CyHPIHandle *handle, HPIRegSection section, HPIEvent *event, GError **error)
{
	guint8 data_buffer[4] = {0};
	g_return_val_if_fail (handle != NULL, FALSE);

	if (section)
	{
		/* first read the response register */
		if (!reg_read (device, &handle->i2c_handle,handle->hpi_addr_byte, 
				reg_addr_gen (section, HPI_REG_PART_PDDATA_READ, 0), data_buffer, 4,error)) {
			g_prefix_error (error, "read_event_reg - read response reg error:");
			return FALSE;
		}

		/* byte 1 is reserved and should read as zero */
		data_buffer[1] = 0;
		memcpy ((guint8*)event, data_buffer, 4);
		if (event->event_length != 0)
		{
			if (!reg_read (device, &handle->i2c_handle, handle->hpi_addr_byte, 
					reg_addr_gen (section, HPI_REG_PART_PDDATA_READ, 4), event->event_data,	
					event->event_length, error)) {
				g_prefix_error (error, "read_event_reg - read event data error:");
				return FALSE;
			}
		}
	}
	else
	{
		if (!reg_read (device, &handle->i2c_handle,handle->hpi_addr_byte, CY_PD_REG_RESPONSE_ADDR ,data_buffer, 2, error)) {
			g_prefix_error (error, "read_event_reg - read response reg error:");
			return FALSE;
		}
		event->event_code   = data_buffer[0];
		event->event_length = data_buffer[1];

		if (event->event_length != 0) {
			/* read the data memory */
			if (!reg_read (device, &handle->i2c_handle, handle->hpi_addr_byte, 
				       CY_PD_REG_BOOTDATA_MEMEORY_ADDR,event->event_data, event->event_length,error)) {
				g_prefix_error (error, "read_event_reg - read event data error:");
				return FALSE;
			}
		}
	}

	return clear_intr (device, handle, section, error);
}

static gint8
hpiapp_read_intr_reg (FuDevice *device, CyHPIHandle *handle, HPIRegSection section, HPIEvent *event_array, GError **error)
{
	gint8 event_count = -1;
	guint8 intr_reg =0;
	guint8 i;

	g_return_val_if_fail (handle != NULL, -1);

	if (!reg_read (device, &handle->i2c_handle,handle->hpi_addr_byte,
		reg_addr_gen (HPI_REG_SECTION_DEV, HPI_REG_PART_REG, HPI_DEV_REG_INTR_ADDR), &intr_reg, 1, error)) {
		g_prefix_error (error, "read_intr_reg  read intr reg error:");
		return -1;
	}

	event_count = 0;
	/* device section will not come here. */
	for (i = 0; i <= handle->num_of_ports; i++) {
		/* check wheteher this section is needed */
		if ((section == i) || (section == HPI_REG_SECTION_ALL)) {
			/* check whether this section has any event/response */
			if ((1 << i) & intr_reg) {
				if (!read_event_reg (device, handle, section, &event_array[i], error)) {
					g_prefix_error (error, "read_intr_reg  read event error:");
					return -1;
				}
				event_count++;
			}
		}
	}

	return event_count;
}

static gboolean
wait_for_event(FuDevice *device, CyHPIHandle *handle, HPIRegSection section, HPIEvent *event_array, guint32 timeout_ms, GError **error)
{
	guint64 elapsed_time_ms = 0;
	struct timeval start_time;
	gint8 event_count;

	g_return_val_if_fail (handle != NULL, FALSE);
	g_return_val_if_fail (event_array != NULL, FALSE);

	fu_ccgx_util_init_elapsed_time(&start_time);
	
	do {
			event_count = hpiapp_read_intr_reg (device,handle, section, event_array,error);
			elapsed_time_ms	= fu_ccgx_util_get_elapsed_time_ms(&start_time);
			
			if ( event_count > 0) 
				return TRUE;
			else if ( event_count< 0) 
				break;
	} while	(elapsed_time_ms <= timeout_ms);
	
	return FALSE;
}

/**
 * hpi_get_device_mode:
 *
 * @device FuDevice Object
 * @handle HPI handle
 * @device_mode[out]  device mode register output
 * @error GError Object
 *
 * Get device mode register
 * 
 * Return value: TRUE if it success, FALSE otherwise
*/
static gboolean
hpi_get_device_mode (FuDevice *device, CyHPIHandle *handle, guint8 *device_mode, GError **error)
{
	g_return_val_if_fail (handle != NULL, FALSE);
	g_return_val_if_fail (device_mode != NULL, FALSE);
	if (!reg_read (device, &handle->i2c_handle, handle->hpi_addr_byte, CY_PD_REG_DEVICE_MODE_ADDR, device_mode, 1, error)) {
		g_prefix_error (error, "get device mode	error:");
		return FALSE;
	}
	return TRUE;
}

/**
 * hpi_get_silicon_id:
 *
 * @device FuDevice Object
 * @handle HPI handle
 * @sid[out]  Silicon ID output
 * @error GError Object
 *
 * Get 2 byte Silicon ID
 * 
 * Return value: TRUE if it success, FALSE otherwise
*/
static gboolean
hpi_get_silicon_id (FuDevice *device, CyHPIHandle *handle,	guint16 *sid, GError **error)
{
	g_return_val_if_fail (handle != NULL, FALSE);
	g_return_val_if_fail (sid != NULL, FALSE);
	
	if (!reg_read(device, &handle->i2c_handle, handle->hpi_addr_byte, CY_PD_SILICON_ID, (guint8*)sid, 2, error)) {
		g_prefix_error (error, "get silicon id error:");
		return FALSE;
	}
	return TRUE;
}

/**
 * hpi_get_device_version:
 *
 * @device FuDevice Object
 * @handle HPI handle
 * @version[out]  Device Version buffer, It should be 6 byte buffer
 * @error GError Object
 *
 * Get Device Version
 * 
 * Return value: TRUE if it success, FALSE otherwise
*/
static gboolean
hpi_get_device_version (FuDevice *device, CyHPIHandle *handle, guint32* version, GError **error)
{
	guint16 size = 0;
	g_return_val_if_fail (handle != NULL, FALSE);
	g_return_val_if_fail (version != NULL, FALSE);
	g_return_val_if_fail (handle->hpi_addr_byte > 0, FALSE);

	if (handle->hpi_addr_byte == 1) 
		size = HPI_DEVICE_VERSION_SIZE_HPIV1;
	else
		size = HPI_DEVICE_VERSION_SIZE_HPIV2;

	if (!reg_read (device, &handle->i2c_handle, handle->hpi_addr_byte, CY_PD_GET_VERSION, (guint8*)version, size, error)) {
		g_prefix_error (error, "get version error:");
		return FALSE;
	}
	return TRUE;
}

/**
 * hpi_jump_to_boot:
 *
 * @device FuDevice Object
 * @handle HPI handle
 * @error GError Object
 *
 * Jump to Boot Mode
 * 
 * Return value: TRUE if it success, FALSE otherwise
*/
static gboolean
hpi_jump_to_boot (FuDevice	*device, CyHPIHandle *handle,  GError **error)
{
	guint8 jump_sig = CY_PD_JUMP_TO_BOOT_CMD_SIG;
	g_return_val_if_fail (handle != NULL, FALSE);
	if (!reg_write (device, &handle->i2c_handle, handle->hpi_addr_byte, CY_PD_JUMP_TO_BOOT_REG_ADDR, &jump_sig, 1, error)) {
		g_prefix_error (error, "jump to boot error:");
		return FALSE;
	}
	return TRUE;
}

/**
 * hpi_jump_to_alt_fw:
 *
 * @device FuDevice Object
 * @handle HPI handle
 * @error GError Object
 *
 * Jump	to Alternative FW Mode, It support only for dual port device
 * 
 * Return value: TRUE if it success, FALSE otherwise
*/
static gboolean
hpi_jump_to_alt_fw (FuDevice *device, CyHPIHandle *handle, GError **error)
{
	guint8 jump_sig = CY_PD_JUMP_TO_ALT_FW_CMD_SIG;
	g_return_val_if_fail (handle != NULL, FALSE);
	
	if (!reg_write (device, &handle->i2c_handle, handle->hpi_addr_byte, CY_PD_JUMP_TO_BOOT_REG_ADDR, &jump_sig, 1, error)) {
		g_prefix_error (error, "jump to alt mode error:");
		return FALSE;
	}
	return TRUE;
}

/**
 * hpi_enter_flash_mode:
 *
 * @device FuDevice Object
 * @handle HPI handle
 * @error GError Object
 *
 * Enter Flash mode to write firmware
 *
 * Return value: TRUE if it success, FALSE otherwise
*/
static gboolean
hpi_enter_flash_mode (FuDevice *device, CyHPIHandle *handle, GError **error)
{
	guint8 enter_flash_sig = CY_PD_ENTER_FLASHING_MODE_CMD_SIG;
	g_return_val_if_fail (handle != NULL, FALSE);
	if (!reg_write (device, &handle->i2c_handle, handle->hpi_addr_byte, CY_PD_REG_ENTER_FLASH_MODE_ADDR, &enter_flash_sig, 1,error)) {
		g_prefix_error (error, "enter flash mode error:");
		return FALSE;
	}
	return TRUE;
}

/**
 * hpi_leave_flash_mode:
 *
 * @device FuDevice Object
 * @handle HPI handle
 * @error GError Object
 *
 * Leave Flash mode to write firmware
 * 
 * Return value: TRUE if it success, FALSE otherwise
*/
static gboolean 
hpi_leave_flash_mode (FuDevice *device, CyHPIHandle *handle, GError **error)
{
	guint8 enter_flash_sig = 0;
	g_return_val_if_fail (handle != NULL, FALSE);
	if (!reg_write (device, &handle->i2c_handle, handle->hpi_addr_byte, CY_PD_REG_ENTER_FLASH_MODE_ADDR, &enter_flash_sig, 1, error)) {
		g_prefix_error (error, "leave flash mode error:");
		return FALSE;
	}
	return TRUE;
}

/**
 * hpi_write_flash:
 *
 * @device FuDevice Object
 * @handle HPI handle
 * @row_num row number of flash
 * @data buffer of flash row data
 * @size size of flash row data
 * @error GError Object
 *
 * Write flash with row number and buffer
 * 
 * Return value: TRUE if it success, FALSE otherwise
*/
static gboolean
hpi_write_flash (FuDevice *device, CyHPIHandle *handle, guint16 row_num, GError **error)
{
	guint8 flash_cmd[4];
	flash_cmd[0] = CY_PD_FLASH_READ_WRITE_CMD_SIG;
	flash_cmd[1] = CY_PD_REG_FLASH_ROW_WRITE_CMD;
	flash_cmd[2] = row_num & 0xFF;
	flash_cmd[3] = (row_num >> 8);

	g_return_val_if_fail (handle != NULL, FALSE);
	if (!reg_write(device, &handle->i2c_handle, handle->hpi_addr_byte, CY_PD_REG_FLASH_READ_WRITE_ADDR, flash_cmd, 4, error)) {
		g_prefix_error (error, "write flash error:");
		return FALSE;
	}
	return TRUE;
}

/**
 * hpi_read_flash:
 *
 * @device FuDevice Object
 * @handle HPI handle
 * @row_num row number of flash
 * @data buffer of flash row data
 * @size size of flash row data
 * @error GError Object
 *
 * Read	flash with row number and buffer
 *
 * Return value: TRUE if it success, FALSE otherwise
*/
static gboolean
hpi_read_flash (FuDevice *device, CyHPIHandle *handle, guint16 row_num, GError **error)
{
	guint8 flash_cmd[4];
	flash_cmd[0] = CY_PD_FLASH_READ_WRITE_CMD_SIG;
	flash_cmd[1] = CY_PD_REG_FLASH_ROW_READ_CMD;
	flash_cmd[2] = row_num & 0xFF;
	flash_cmd[3] = (row_num >> 8);

	g_return_val_if_fail (handle != NULL, FALSE);
	if (!reg_write(device, &handle->i2c_handle, handle->hpi_addr_byte, CY_PD_REG_FLASH_READ_WRITE_ADDR, flash_cmd, 4, error)) {
		g_prefix_error (error, "read flash error:");
		return FALSE;
	}
	return TRUE;
}

/**
 * hpi_validate_fw:
 *
 * @device FuDevice Object
 * @handle HPI handle
 * @fw_index firmware index (FW1 or FW2)
 * @error GError Object
 *
 * Validate firmware written
 * 
 * Return value: TRUE if it success, FALSE otherwise
*/
static gboolean
hpi_validate_fw (FuDevice *device,CyHPIHandle *handle, guint8 fw_index, GError **error)
{
	g_return_val_if_fail (handle != NULL, FALSE);
	if (!reg_write(device, &handle->i2c_handle, handle->hpi_addr_byte, CY_PD_REG_VALIDATE_FW_ADDR, &fw_index, 1, error)) {
		g_prefix_error (error, "validate fw error:");
		return FALSE;
	}
	return TRUE;
}

/**
 * hpi_reset_device:
 *
 * @device FuDevice Object
 * @handle HPI handle
 * @error GError Object
 *
 * Reset device
 *
 * Return value: TRUE if it success, FALSE otherwise
*/
static gboolean
hpi_reset_device (FuDevice *device, CyHPIHandle *handle, GError **error)
{
	guint8 reset_cmd[2];
	reset_cmd[0] = CY_PD_DEVICE_RESET_CMD_SIG;
	reset_cmd[1] = CY_PD_REG_RESET_DEVICE_CMD;

	g_return_val_if_fail (handle != NULL, FALSE);
	if (!reg_write_no_resp(device, &handle->i2c_handle, handle->hpi_addr_byte, CY_PD_REG_RESET_ADDR, reset_cmd, 2, error)) {
		g_prefix_error (error, "reset device error:");
		return FALSE;
	}
	return TRUE;
}


/**
 * hpi_get_event:
 *
 * @device FuDevice Object
 * @handle HPI handle
 * @reg_section register section type. see HPIRegSection
 * @event[out] event response from device
 * @io_timeout timeout to wait event
 * @error GError Object
 *
 * Get event from device within specificed timeout
 *
 * Return value: TRUE if it success, FALSE otherwise
*/
static gboolean
hpi_get_event (FuDevice *device, CyHPIHandle *handle, HPIRegSection reg_section, CyPDResp*	event, guint32 io_timeout, GError **error )
{
	HPIEvent event_array[HPI_REG_SECTION_ALL + 1] = {0};
	guint32 timeout_ms = io_timeout;
	
	g_return_val_if_fail (handle != NULL, FALSE);
	g_return_val_if_fail (event != NULL, FALSE);
	if (wait_for_event(device,handle,reg_section,event_array,timeout_ms,error))
	{
		*event = event_array[reg_section].event_code;
		return TRUE;
	}
	return FALSE;
}

/**
 * hpi_clear_all_event:
 *
 * @device FuDevice Object
 * @handle HPI handle
 * @io_timeout timeout to clear event
 * @error GError Object
 *
 * Clear all event from device
 * 
*/
static void
hpi_clear_all_event (FuDevice *device, CyHPIHandle *handle, guint32 io_timeout, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	HPIEvent event_array[HPI_REG_SECTION_ALL + 1] = {0};
	guint32 timeout_ms = io_timeout;
	guint8 section;

	g_return_if_fail (handle != NULL);
	if(timeout_ms == 0) {
		hpiapp_read_intr_reg (device, handle, HPI_REG_SECTION_ALL, event_array, &error_local);
	} else {
		for (section = 0; section < handle->num_of_ports; section++) {
			wait_for_event (device,	handle,	section, event_array, timeout_ms, &error_local);
		}
	}
}

/**
 * hpi_write_reg:
 *
 * @device FuDevice Object
 * @handle HPI handle
 * @reg_addr address of	regisger
 * @reg_data data buffer
 * @size size to wrte
 * @error GError Object
 * 
 * Write data to register
 * 
 * Return value: TRUE if it success, FALSE otherwise
*/
static gboolean
hpi_write_reg (FuDevice *device, CyHPIHandle *handle, guint16 reg_addr, guint8* reg_data, guint16 size, GError **error)
{
	g_return_val_if_fail (handle != NULL, FALSE);
	if (!reg_write(device, &handle->i2c_handle,handle->hpi_addr_byte, reg_addr, reg_data, size, error)) {
		g_prefix_error (error, "write reg error:");
		return FALSE;
	}
	return TRUE;
}

/**
 * hpi_read_reg:
 *
 * @device FuDevice Object
 * @handle HPI handle
 * @reg_addr address of	regisger
 * @reg_data[out] data buffer
 * @size size to read
 * @error GError Object
 *
 * Read data from  register
 * 
 * Return value: TRUE if it success, FALSE otherwise
*/
static gboolean
hpi_read_reg (FuDevice *device, CyHPIHandle *handle, guint16 reg_addr, guint8* reg_data, guint16 size, GError **error)
{
	g_return_val_if_fail (handle != NULL, FALSE);
	if (!reg_read(device, &handle->i2c_handle,handle->hpi_addr_byte, reg_addr, reg_data, size, error)) {
		g_prefix_error (error, "read reg error:");
		return FALSE;
	}
	return TRUE;
}

/**
 * hpi_configure:
 *
 * @device FuDevice Object
 * @handle[in,out] HPI handle
 * @device_mode[out]  device mode output
 * @error GError Object
 *
 * Open USB Serial and configure HPI handle according to device mode
 *
 * Return value: TRUE if it success, FALSE otherwise
*/
static gboolean
hpi_configure (FuDevice *device, CyHPIHandle *handle, guint8 *device_mode, GError **error)
{
	CyI2CConfig i2c_config;
	guint8 mode;

	g_return_val_if_fail (handle != NULL, FALSE);
	g_return_val_if_fail (device_mode != NULL, FALSE);

	handle->hpi_addr_byte = 1;
	handle->num_of_ports = 1;

	if (!fu_ccgx_i2c_get_config(device, &handle->i2c_handle, &i2c_config, error)) {
		g_prefix_error (error, "hpi_configure  get config error:");
		return FALSE;
	}

	i2c_config.frequency = FU_CCGX_I2C_FREQ;
	i2c_config.is_master = TRUE;

	if (!fu_ccgx_i2c_set_config(device,&handle->i2c_handle,&i2c_config,error)) {
		g_prefix_error (error, "hpi_configure -set config error:");
		return FALSE;
	}

	if (!hpi_get_device_mode(device,handle,&mode,error)) {
		g_prefix_error (error, "hpi_configure -get device mode:");
		return FALSE;
	}

	*device_mode = mode;

	if(mode & 0x80) {
		handle->hpi_addr_byte = 2;
	} else {
		handle->hpi_addr_byte = 1;
	}

	if((mode >> 2) & 0x03) {
		handle->num_of_ports = 2;
	} else {
		handle->num_of_ports = 1;
	}

	handle->fw_mode = (FWMode)(mode&0x03);
	return TRUE;
}



/**
 * fu_ccgx_hpi_cmd_setup: 
 *
 * @device FuDevice Object
 * @hpi_handle HPI handle
 * @dm_device Device Type for Device Manager
 * @usb_inf_num USB interface number for I2C
 * @slave_address Slave Address for I2C
 * @error GError Object
 * 
 * Setup PD I2C device using HPI interface
 *
 * Return value: TRUE if it success, FALSE otherwise
*/
gboolean
fu_ccgx_hpi_cmd_setup (FuDevice *device, CyHPIHandle *hpi_handle, DMDevice dm_device, guint16 usb_inf_num, guint8 slave_address, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	CyI2CDeviceHandle *i2c_handle = &hpi_handle->i2c_handle;
	guint8 device_mode = 0;
	guint32	hpi_event = 0;

	g_return_val_if_fail (hpi_handle != NULL, FALSE);

	i2c_handle->inf_num = usb_inf_num;
	i2c_handle->slave_address = slave_address;

	if(dm_device == DM_DEVICE_PD_I2C) {
		i2c_handle->ep.bulk_out = PD_I2C_USB_EP_BULK_OUT;
		i2c_handle->ep.bulk_in = PD_I2C_USB_EP_BULK_IN;
		i2c_handle->ep.intr_in = PD_I2C_USB_EP_INTR_IN;
	} else if(dm_device == DM_DEVICE_PD_I2CM) {
		i2c_handle->ep.bulk_out = PD_I2CM_USB_EP_BULK_OUT;
		i2c_handle->ep.bulk_in = PD_I2CM_USB_EP_BULK_IN;
		i2c_handle->ep.intr_in = PD_I2CM_USB_EP_INTR_IN;
	} else {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "i2c setup not supported device");
		g_warning ("not supported device in %s",__func__);
		return FALSE;
	}

	if  (!hpi_configure(device, hpi_handle, &device_mode, error)) {
		g_prefix_error (error, "i2c_setup error:");
		g_warning ("hpi config error in %s",__func__);
		return FALSE;
	}

	if(hpi_get_event (device, hpi_handle, HPI_REG_SECTION_DEV, &hpi_event, 
				       HPI_CMD_SETUP_EVENT_WAIT_TIME_MS,	&error_local)) {
		if(hpi_event == CY_PD_RESP_RESET_COMPLETE) {  /* reset completed */
			g_usleep(HPI_CMD_RESET_COMPLETE_DELAY_US);  /* at least sleep 100 msec */
		}
	}

	hpi_clear_all_event (device, hpi_handle, HPI_CMD_SETUP_EVENT_CLEAR_TIME_MS, &error_local);
	return TRUE;
}

 static gboolean 
 get_fw_version(FuDevice *device, CyHPIHandle *hpi_handle, guint32 start_row, guint32 row_size, PDFWAppVersion *fw_version, GError **error)
 {
	guint32 version_row_index = 0;
	guint32 version_row_offset = 0;
	gint32 version_row_num = 0;
	g_autofree guint8* row_buffer = NULL;

	version_row_index = CCGX_APP_VERSION_OFFSET / row_size;
	version_row_offset = CCGX_APP_VERSION_OFFSET % row_size;

	version_row_num = start_row + version_row_index;
	row_buffer = g_malloc0 (row_size);

	g_return_val_if_fail (hpi_handle != NULL, FALSE);
	g_return_val_if_fail (fw_version != NULL, FALSE);
	g_return_val_if_fail (row_buffer != NULL, FALSE);
	
	if (!fu_ccgx_hpi_cmd_read_flash (device, hpi_handle, version_row_num, row_buffer, row_size, error)) {
		g_prefix_error (error, "get fw version error:");
		return FALSE;
	}

	fw_version->val= (guint32) (row_buffer[version_row_offset + 3] << 24 |
					row_buffer[version_row_offset + 2] << 16 |
					row_buffer[version_row_offset + 1] << 8 |
					row_buffer[version_row_offset + 0]);
	return TRUE;
 }


/**
 * fu_ccgx_hpi_cmd_get_device_data:
 *
 * @device FuDevice Object
 * @hpi_handle HPI handle
 * @device_data device data information
 * @error GError Object
 * 
 * Get device data (version, fw mode, num of ports, silicon ID) from PD I2C Device
 * 
 * Return value: TRUE if it success, FALSE otherwise
*/
gboolean
fu_ccgx_hpi_cmd_get_device_data (FuDevice *device, CyHPIHandle *hpi_handle,	PDDeviceData *device_data, GError **error)
{
	guint32 device_versions[6] = {0};
	guint8 device_mode = 0;
	guint16 silicon_id = 0;
	CCGxPartInfo *ccgx_info = NULL;
	g_autofree guint8* row_buffer = NULL;
	guint32 row_size = 0;
	guint32 row_max = 0;
	guint32 fw_meta_row_num;
	guint32 fw_meta_offset = 0;
	guint8 fw_index = 0;
	CCGxMetaData *metadata = NULL;
	PDFWAppVersion *fw_version = NULL;
	
	g_return_val_if_fail (hpi_handle != NULL, FALSE);
	g_return_val_if_fail (device_data != NULL, FALSE);

	if (!hpi_get_device_mode (device,hpi_handle,&device_mode,error)) {
		return FALSE;
	}

	device_data->fw_mode = (FWMode)(device_mode & 0x03);

	if (!hpi_get_silicon_id (device,hpi_handle,&silicon_id,error)) {
		return FALSE;
	}

	device_data->silicon_id = (guint16)silicon_id;

	device_data->current_version.val = 0;
	if (device_data->fw_mode != FW_MODE_BOOT) {
		if (!hpi_get_device_version (device, hpi_handle, device_versions, error)) {
			return FALSE;
		}
		device_data->fw_version[FW_MODE_FW1].val = device_versions[3];
		device_data->fw_version[FW_MODE_FW2].val = device_versions[5];
		device_data->current_version.val = device_data->fw_version[device_data->fw_mode].val ;
	} else {
		g_warning("device in boot mode");
	}

	ccgx_info = fu_ccgx_util_find_ccgx_info (silicon_id);
	if (ccgx_info == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "not supported Silicon ID");
		return FALSE;
	}

	row_size = ccgx_info->flash_row_size;
	device_data->fw_row_size = row_size;

	if(row_size > CYACD_FLASH_ROW_MAX) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Not row size");
		return FALSE;
	}

	row_max = ccgx_info->flash_size / row_size;
	row_buffer = g_malloc0(row_size);

	if (row_size == 128) {
		fw_meta_offset = HPI_META_DATA_OFFSET_ROW_128;
	}
	else if (row_size == 256) {
		fw_meta_offset = HPI_META_DATA_OFFSET_ROW_256;
	} else {
		fw_meta_offset = 0;
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "not support row size");
		return FALSE;
	}

	device_data->fw_meta_offset = fw_meta_offset;
	device_data->fw1_meta_row_num = row_max-1;
	device_data->fw2_meta_row_num = row_max-2;

	device_data->fw_meta_valid = FALSE;
	if (!fu_ccgx_hpi_cmd_enter_flash_mode(device,hpi_handle,error)) {
		return FALSE;
	}

	g_usleep(HPI_CMD_ENTER_FLASH_MODE_DELAY_US);

	/* fw1 meta data */
	fw_index = FW_MODE_FW1;
	fw_meta_row_num = device_data->fw1_meta_row_num;

	(void)hpi_clear_all_event (device, hpi_handle, 10, error);

	if (!fu_ccgx_hpi_cmd_read_flash (device, hpi_handle, fw_meta_row_num, row_buffer, row_size,error)) {
		return FALSE;
	}

	metadata = &device_data->fw_metadata[fw_index];
	memcpy(metadata,&row_buffer[fw_meta_offset],sizeof(CCGxMetaData));

	fw_version = &device_data->fw_version[fw_index];
	if(metadata->metadata_valid == CCGX_METADATA_VALID_SIG)
	{
		if (!get_fw_version(device,  hpi_handle, (metadata->last_boot_row + 1), row_size ,fw_version, error)) {
			return FALSE;
		}
	}

	/* fw2 meta data */
	fw_index = FW_MODE_FW2;
	fw_meta_row_num = device_data->fw2_meta_row_num;

	if (!fu_ccgx_hpi_cmd_read_flash (device, hpi_handle, fw_meta_row_num, row_buffer, row_size, error)) {
		return FALSE;
	}

	metadata = &device_data->fw_metadata[fw_index];
	memcpy(metadata,&row_buffer[fw_meta_offset],sizeof(CCGxMetaData));

	fw_version = &device_data->fw_version[fw_index];
	if(metadata->metadata_valid == CCGX_METADATA_VALID_SIG)
	{
		if (!get_fw_version(device,  hpi_handle, (metadata->last_boot_row + 1), row_size ,fw_version, error)) {
			return FALSE;
		}
	}

	if (!fu_ccgx_hpi_cmd_leave_flash_mode(device,hpi_handle,error)) {
		return FALSE;
	}

	device_data->fw_meta_valid = TRUE;
	g_usleep(HPI_CMD_ENTER_FLASH_MODE_DELAY_US);
	return TRUE;
}

/**
 * fu_ccgx_hpi_cmd_enter_flash_mode:
 *
 * @device FuDevice Object
 * @hpi_handle HPI handle
 * @error GError Object
 *
 * Enter flash mode in PD I2C Device
 *
 * Return value: TRUE if it success, FALSE otherwise
*/
gboolean
fu_ccgx_hpi_cmd_enter_flash_mode (FuDevice *device, CyHPIHandle *hpi_handle,  GError **error)
{
	CyPDResp hpi_event = 0;

	g_return_val_if_fail (hpi_handle != NULL, FALSE);

	(void)hpi_clear_all_event(device,hpi_handle,HPI_CMD_COMMAND_CLEAR_EVENT_TIME_MS,error);

	if (!hpi_enter_flash_mode (device, hpi_handle, error)) {
		g_warning("enter flash mode cmd	error");
		return FALSE;
	}

	if (!hpi_get_event (device, hpi_handle, HPI_REG_SECTION_DEV, &hpi_event, HPI_CMD_COMMAND_RESPONSE_TIME_MS ,error)) {
		g_warning("enter flash mode resp error");
		return FALSE;
	}

	if (hpi_event != CY_PD_RESP_SUCCESS) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "enter flash mode resp code error:	0x%x",hpi_event);
		g_warning ("enter flash	mode reps code error in	%s",__func__);
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_ccgx_hpi_cmd_leave_flash_mode:
 *
 * @device FuDevice Object
 * @hpi_handle HPI handle
 * @error GError Object
 * 
 * Leave flash mode in PD I2C Device
 * 
 * Return value: TRUE if it success, FALSE otherwise
*/
gboolean
fu_ccgx_hpi_cmd_leave_flash_mode (FuDevice *device, CyHPIHandle *hpi_handle, GError	**error)
 {
	CyPDResp hpi_event = 0;

	g_return_val_if_fail (hpi_handle != NULL, FALSE);
	(void)hpi_clear_all_event(device,hpi_handle,HPI_CMD_COMMAND_CLEAR_EVENT_TIME_MS,error);

	if (!hpi_leave_flash_mode (device, hpi_handle, error)) {
		 g_warning("leave flash mode cmd error");
		 return	FALSE;
	}


	if (!hpi_get_event (device, hpi_handle, HPI_REG_SECTION_DEV, &hpi_event, HPI_CMD_COMMAND_RESPONSE_TIME_MS ,error)) {
		g_warning("leave flash mode resp error");
		return FALSE;
	}

	if (hpi_event != CY_PD_RESP_SUCCESS) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "leave flash mode resp code error: 0x%x",hpi_event);
		g_warning ("leave flash mode response error in %s",__func__);
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_ccgx_hpi_cmd_write_flash:
 *
 * @device FuDevice Object
 * @hpi_handle HPI handle
 * @row_num row number
 * @data flash row data
 * @size size of row data
 * @error GError Object
 *
 * Write Flash data to PD I2C Device
 *
 * Return value: TRUE if it success, FALSE otherwise
*/
gboolean
fu_ccgx_hpi_cmd_write_flash (FuDevice *device, CyHPIHandle *hpi_handle, guint16 row_num, guint8 *data, guint16 size, GError **error)
{
	CyPDResp hpi_event = 0;
	guint16 reg_addr = 0;

	g_return_val_if_fail (hpi_handle != NULL, FALSE);
	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (size > 0, FALSE);

	(void)hpi_clear_all_event(device,hpi_handle,HPI_CMD_COMMAND_CLEAR_EVENT_TIME_MS,error);

	if(hpi_handle->hpi_addr_byte > 1) {
		reg_addr = HPI_DEV_REG_FLASH_MEM;
	} else {
		reg_addr = CY_PD_REG_BOOTDATA_MEMEORY_ADDR;
	}

	/* write Data to memory */
	if (!hpi_write_reg(device,hpi_handle, reg_addr, data, size,error)) {
		g_warning("write data to memory error");
		return FALSE;
	}

	g_usleep(HPI_CMD_FALSH_READ_WRITE_DELAY_US);

	/* send write command */
	if (!hpi_write_flash (device, hpi_handle, row_num, error)) {
		g_warning("write flash cmd error");
		return FALSE;
	}

	g_usleep(HPI_CMD_FALSH_READ_WRITE_DELAY_US); /* wait until flash is written */

	if (!hpi_get_event (device, hpi_handle, HPI_REG_SECTION_DEV, &hpi_event, HPI_CMD_COMMAND_RESPONSE_TIME_MS ,error)) {
		g_warning("write flash resp error");
		return FALSE;
	}

	if( hpi_event != CY_PD_RESP_SUCCESS) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "write flash resp code error: 0x%x",hpi_event);
		g_warning ("write flash response error in %s",__func__);
		return FALSE;
	}
	return TRUE;
}


/**
 * fu_ccgx_hpi_cmd_read_flash:
 *
 * @device FuDevice Object
 * @hpi_handle HPI handle
 * @row_num row	number
 * @data[out] flash row	data
 * @size size of row data
 * @error GError Object
 *
 * Read Flash data to PD I2C Device
 * 
 * Return value: TRUE if it success, FALSE otherwise
*/
gboolean
fu_ccgx_hpi_cmd_read_flash (FuDevice *device, CyHPIHandle *hpi_handle, guint16 row_num, guint8 *data, guint16 size, GError **error)
{
	guint16 reg_addr = 0;
	CyPDResp hpi_event = 0;

	g_return_val_if_fail (hpi_handle != NULL, FALSE);
	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (size > 0, FALSE);


	(void)hpi_clear_all_event(device,hpi_handle,HPI_CMD_COMMAND_CLEAR_EVENT_TIME_MS,error);

	if (!hpi_read_flash(device, hpi_handle, row_num, error)) {
		g_warning("read flash cmd error");
		return FALSE;
	}

	g_usleep(HPI_CMD_FALSH_READ_WRITE_DELAY_US); /* wait until flash is reaad */

	if (!hpi_get_event (device, hpi_handle,  HPI_REG_SECTION_DEV, &hpi_event, HPI_CMD_COMMAND_RESPONSE_TIME_MS, error)) {
		g_warning("read flash resp error");
		return FALSE;
	}

	if (hpi_event != CY_PD_RESP_FLASH_DATA_AVAILABLE) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "read flash resp code error: 0x%x",hpi_event);
		g_warning ("read flash response error in %s",__func__);
		return FALSE;
	}

	g_usleep(HPI_CMD_FALSH_READ_WRITE_DELAY_US);
	
	if(hpi_handle->hpi_addr_byte > 1) {
		reg_addr = HPI_DEV_REG_FLASH_MEM;
	} else {
		reg_addr = CY_PD_REG_BOOTDATA_MEMEORY_ADDR;
	}

	if (!hpi_read_reg(device,hpi_handle, reg_addr, data, size,error)) {
		g_warning("read data from memory error");
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_ccgx_hpi_cmd_validate_fw:
 *
 * @device FuDevice Object
 * @hpi_handle HPI handle
 * @fw_index firmeware Index (FW1 or FW2)
 * @error GError Object
 * 
 * Validate firmware written in PD I2C Device
 *
 * Return value: TRUE if it success, FALSE otherwise
*/

gboolean
fu_ccgx_hpi_cmd_validate_fw (FuDevice *device, CyHPIHandle *hpi_handle, guint8 fw_index, GError **error)
{
	CyPDResp hpi_event = 0;

	g_return_val_if_fail (hpi_handle != NULL, FALSE);

	(void)hpi_clear_all_event (device,hpi_handle,HPI_CMD_COMMAND_CLEAR_EVENT_TIME_MS,error);

	if (!hpi_validate_fw (device, hpi_handle, fw_index,error)) {
		g_warning("validate fw cmd error");
		return FALSE;
	}

	if (!hpi_get_event	(device, hpi_handle, HPI_REG_SECTION_DEV, &hpi_event, HPI_CMD_COMMAND_RESPONSE_TIME_MS ,error)) {
		g_warning("validate fw resp error");
		return FALSE;
	}

	if (hpi_event != CY_PD_RESP_SUCCESS) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "validate fw resp code error: 0x%x",hpi_event);
		g_warning ("validate fw	response error in %s",__func__);
	}
	return TRUE;
}


/**
 * fu_ccgx_hpi_cmd_reset_device:
 *
 * @device FuDevice Object
 * @hpi_handle HPI handle
 * @error GError Object
 *
 * Reset PD I2C Device
 *
 * Return value: TRUE if it success, FALSE otherwise
*/
gboolean
fu_ccgx_hpi_cmd_reset_device (FuDevice *device, CyHPIHandle *hpi_handle, GError **error)
{
	g_return_val_if_fail (hpi_handle != NULL, FALSE);

	(void)hpi_clear_all_event (device, hpi_handle, HPI_CMD_COMMAND_CLEAR_EVENT_TIME_MS,	error);

	if (!hpi_reset_device(device,hpi_handle,error)) {
		g_warning("reset device cmd error");
	}
	return TRUE;
}

/**
 * fu_ccgx_hpi_cmd_jump_to_alt_fw:
 *
 * @device FuDevice Object
 * @hpi_handle HPI handle
 * @error GError Object
 * 
 * Jump to alt FW in  PD I2C Device
 * 
 * Return value: TRUE if it success, FALSE otherwise
*/
gboolean
fu_ccgx_hpi_cmd_jump_to_alt_fw(FuDevice *device, CyHPIHandle *hpi_handle,  GError **error)
{
	g_return_val_if_fail (hpi_handle != NULL, FALSE);

	(void)hpi_clear_all_event(device,hpi_handle,HPI_CMD_COMMAND_CLEAR_EVENT_TIME_MS,error);

	if (!hpi_jump_to_alt_fw(device,hpi_handle,error)) {
		g_warning("jump to alt fw cmd error");
		return FALSE;
	}
	return TRUE;
}

/**
 * 
 *
 * @device FuDevice Object
 * @hpi_handle HPI handle
 * @error GError Object
 *
 * Jump to Boot in  PD I2C Device
 * 
 * Return value: TRUE if it success, FALSE otherwise
*/
gboolean
fu_ccgx_hpi_cmd_jump_to_boot(FuDevice *device, CyHPIHandle *hpi_handle,  GError **error)
{
	g_return_val_if_fail (hpi_handle != NULL, FALSE);

	(void)hpi_clear_all_event(device,hpi_handle,HPI_CMD_COMMAND_CLEAR_EVENT_TIME_MS,error);

	if (!hpi_jump_to_boot(device,hpi_handle,error)) {
		g_warning("jump to boot cmd error");
		return FALSE;
	}
	return TRUE;
}
