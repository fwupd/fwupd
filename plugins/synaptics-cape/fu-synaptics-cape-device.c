/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2021 Synaptics Incorporated <simon.ho@synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <string.h>

#include "fu-synaptics-cape-device.h"
#include "fu-synaptics-cape-firmware.h"

 /* defines timings*/
#define FU_SYNAPTICS_CAPE_DEVICE_USB_CMD_WRITE_TIMEOUT       20000 /* us */
#define FU_SYNAPTICS_CAPE_DEVICE_USB_CMD_READ_TIMEOUT        30000 /* us */
#define FU_SYNAPTICS_CAPE_DEVICE_USB_CMD_RETRY_INTERVAL      10    /* ms */
#define FU_SYNAPTICS_CAPE_DEVICE_USB_CMD_RETRY_TIMEOUT       300   /* ms */
#define FU_SYNAPTICS_CAPE_DEVICE_USB_RESET_DELAY_MS	         3000   /* ms */

/* define CAPE command constant values and macro */                      
#define FU_SYNAPTICS_CAPE_DEVICE_GOLEM_REPORT_ID 1 /* HID report id */

#define FU_SYNAPTICS_CAPE_CMD_MAX_DATA_LEN    13 /* number of guint32 */
#define FU_SYNAPTICS_CAPE_CMD_WRITE_DATAL_LEN 8	 /* number of guint32 */
#define FU_SYNAPTICS_CAPE_WORD_IN_BYTES	      4	 /* bytes */

#define FU_SYNAPTICS_CAPE_CMD_APP_ID(a, b, c, d) \
	((((a)-0x20) << 8) | (((b)-0x20) << 14) | (((c)-0x20) << 20) | (((d)-0x20) << 26))

/* CAPE command return codes */
#define FU_SYNAPTICS_CAPE_MODULE_RC_GENERIC_FAILURE	(-1025)
#define FU_SYNAPTICS_CAPE_MODULE_RC_ALREADY_EXISTS	(-1026)
#define FU_SYNAPTICS_CAPE_MODULE_RC_NULL_APP_POINTER	(-1027)
#define FU_SYNAPTICS_CAPE_MODULE_RC_NULL_MODULE_POINTER (-1028)
#define FU_SYNAPTICS_CAPE_MODULE_RC_NULL_STREAM_POINTER (-1029)
#define FU_SYNAPTICS_CAPE_MODULE_RC_NULL_POINTER	(-1030)

#define FU_SYNAPTICS_CAPE_MODULE_RC_BAD_APP_ID		   (-1031)
#define FU_SYNAPTICS_CAPE_MODULE_RC_MODULE_TYPE_HAS_NO_API (-1034)
#define FU_SYNAPTICS_CAPE_MODULE_RC_BAD_MAGIC_NUMBER	   (-1052)
#define FU_SYNAPTICS_CAPE_MODULE_RC_CMD_MODE_UNSUPPORTED   (-1056)

#define FU_SYNAPTICS_CMD_GET_FLAG 0x100  /* GET flag */


/* CAPE message structure, Little endian */
typedef struct __attribute__((packed)) {
	gint16 data_len : 16; /* data length in dwords */
	guint16 cmd_id : 15;  /* Command id */
	guint16 reply : 1;    /* Host want a reply from device, 1 = true */
	guint32 module_id;    /* Module id */
	guint32 data[FU_SYNAPTICS_CAPE_CMD_MAX_DATA_LEN]; /* Command data */
} FuCapCmd;

/* CAPE HID report structure */
typedef struct __attribute__((packed)) {
	guint16 report_id; /* two bytes of report id, this should be 1 */
	FuCapCmd cmd;
} FuCapCmdHidReport;

/* CAPE Commands */
typedef enum {
	FU_SYNAPTICS_CMD_FW_UPDATE_START = 0xC8,	  /* notifies firmware update started */
	FU_SYNAPTICS_CMD_FW_UPDATE_WRITE = 0xC9,	  /* updates firmware data */
	FU_SYNAPTICS_CMD_FW_UPDATE_END = 0xCA,		  /* notifies firmware update finished */
	FU_SYNAPTICS_CMD_MCU_SOFT_RESET = 0xAF,		  /* reset device*/
	FU_SYNAPTICS_CMD_FW_GET_ACTIVE_PARTITION = 0x1CF, /* gets cur active partition number */
	FU_SYNAPTICS_CMD_GET_VERSION = 0x103,		  /* gets cur firmware version */
} FuCommand;


/* CAPE Fuupd device structure */
struct _FuSynapticsCapeDevice {
	FuHidDevice parent_instance;
	guint32 ActivePartition; /* active partition, either 1 or 2 */
};

G_DEFINE_TYPE(FuSynapticsCapeDevice, fu_synaptics_cape_device, FU_TYPE_HID_DEVICE)

/* Sends SET_REPORT to device */
static gboolean
fu_synaptics_cape_device_set_report(FuSynapticsCapeDevice *self,
				    const guint8 *data,
				    guint datasz,
				    GError **error)
{
	if (g_getenv("FWUPD_SYNAPTICS_CAPE_HID_REPORT_VERBOSE") != NULL)
		fu_common_dump_raw(G_LOG_DOMAIN, "SetReport", data, datasz);

	return fu_hid_device_set_report(FU_HID_DEVICE(self),
					data[0],
					(guint8 *)data,
					datasz,
					FU_SYNAPTICS_CAPE_DEVICE_USB_CMD_WRITE_TIMEOUT,
					FU_HID_DEVICE_FLAG_NONE,
					error);
}

/* Gets data from device via GET_REPORT */
static gboolean
fu_synaptics_cape_device_get_report(FuSynapticsCapeDevice *self,
				    guint8 *data,
				    guint datasz,
				    GError **error)
{
	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      data[0],
				      (guint8 *)data,
				      datasz,
				      FU_SYNAPTICS_CAPE_DEVICE_USB_CMD_READ_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) 
		return FALSE;
	
	if (g_getenv("FWUPD_SYNAPTICS_CAPE_HID_REPORT_VERBOSE") != NULL)
		fu_common_dump_raw(G_LOG_DOMAIN, "GetReport", data, datasz);

	/* success */
	return TRUE;
}

/* dump CAPE command error if any */
static gboolean
fu_synaptics_cape_device_rc_set_error(const FuCapCmd *rsp, GError **error)
{
	if (rsp->data_len >= 0)
		return TRUE;

	switch (rsp->data_len) {
	case FU_SYNAPTICS_CAPE_MODULE_RC_GENERIC_FAILURE:
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_BUSY, "CMD ERROR: generic failure");
		break;

	case FU_SYNAPTICS_CAPE_MODULE_RC_ALREADY_EXISTS:
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_BUSY, "CMD ERROR: alraedy exists");
		break;
	case FU_SYNAPTICS_CAPE_MODULE_RC_NULL_APP_POINTER:
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_BUSY, "CMD ERROR: null app pointer");
		break;
	case FU_SYNAPTICS_CAPE_MODULE_RC_NULL_MODULE_POINTER:
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_BUSY, "CMD ERROR: null module pointer");
		break;

	case FU_SYNAPTICS_CAPE_MODULE_RC_NULL_POINTER:
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_BUSY, "CMD ERROR: null pointer");
		break;

	case FU_SYNAPTICS_CAPE_MODULE_RC_BAD_APP_ID:
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_BUSY, "CMD ERROR: bad app id");
		break;

	case FU_SYNAPTICS_CAPE_MODULE_RC_MODULE_TYPE_HAS_NO_API:
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_BUSY, "CMD ERROR: has no api");
		break;

	case FU_SYNAPTICS_CAPE_MODULE_RC_BAD_MAGIC_NUMBER:
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_BUSY, "CMD ERROR: bad magic number");
		break;

	case FU_SYNAPTICS_CAPE_MODULE_RC_CMD_MODE_UNSUPPORTED:
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_BUSY, "CMD ERROR: mode unsupported");
		break;
	default:
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_BUSY,
			    "CMD ERROR: unknown error: %d",
			    rsp->data_len);
	}

	/* success */
	return FALSE;
}

/* sends a FuCapCmd structure command to device to get the response in the same structure */
static gboolean
fu_synaptics_cape_device_sendcmd_ex(FuSynapticsCapeDevice *self,
				    FuCapCmd *req,
				    gulong delay_us,
				    GError **error)
{
	FuCapCmdHidReport report;
	guint elapsed_ms = 0;
	gboolean is_get = FALSE;

	g_return_val_if_fail(FU_IS_SYNAPTICS_CAPE_DEVICE(self), FALSE);
	g_return_val_if_fail(req != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* First two bytes are report id. */
	report.report_id = GINT16_TO_LE(FU_SYNAPTICS_CAPE_DEVICE_GOLEM_REPORT_ID);

	if (!fu_memcpy_safe((guint8 *)&report.cmd,
			    sizeof(report.cmd),
			    0, /* dst */
			    (const guint8 *)req,
			    sizeof(*req),
			    0, /* src */
			    sizeof(*req),
			    error))
		return FALSE;

	/* Sets data length to MAX for any GET commands */
	if (FU_SYNAPTICS_CMD_GET_FLAG & report.cmd.cmd_id) {
		is_get = TRUE;
		report.cmd.data_len = GINT16_TO_LE(FU_SYNAPTICS_CAPE_CMD_MAX_DATA_LEN);
	} else {
		report.cmd.data_len = GINT16_TO_LE(report.cmd.data_len);
	}
	
	report.cmd.cmd_id = GUINT32_TO_LE(report.cmd.cmd_id);
	report.cmd.module_id = GUINT32_TO_LE(report.cmd.module_id);

	if (!fu_synaptics_cape_device_set_report(self,
						 (const guint8 *)&report,
						 sizeof(report),
						 error)) {
		g_prefix_error(error, "failed to send: ");
		return FALSE;
	}
	if (delay_us > 0)
		g_usleep(delay_us);

	/* wait for the command to complete */
	for (; elapsed_ms < FU_SYNAPTICS_CAPE_DEVICE_USB_CMD_RETRY_TIMEOUT;
	     elapsed_ms += FU_SYNAPTICS_CAPE_DEVICE_USB_CMD_RETRY_INTERVAL) {
		if (!fu_synaptics_cape_device_get_report(self,
							 (guint8 *)&report,
							 sizeof(report),
							 error))
			return FALSE;
		if (report.cmd.reply)
			break;
		g_usleep(FU_SYNAPTICS_CAPE_DEVICE_USB_CMD_RETRY_INTERVAL * 1000);
	}

	if (!report.cmd.reply) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "send command time out");
		return FALSE;
	}

	/* copy returned data if it is GET command */
	if (is_get) {
		req->data_len = (gint16)fu_common_read_uint16((guint8 *)&report.cmd, G_LITTLE_ENDIAN);

		for (int i = 0; i < FU_SYNAPTICS_CAPE_CMD_MAX_DATA_LEN; i++)
			req->data[i] = GUINT32_FROM_LE(report.cmd.data[i]);
	}

	return fu_synaptics_cape_device_rc_set_error(&report.cmd, error);
}

/* a simple version of sendcmd_ex without returned data */
static gboolean
fu_synaptics_cape_device_sendcmd(FuSynapticsCapeDevice *self,
				 const guint32 module_id,
				 const guint32 cmd_id,
				 const guint32 *data,
				 const guint32 data_len,
				 const gulong delay_us,
				 GError **error)
{
	FuCapCmd cmd = {0};
	const guint32 dataszbyte = data_len * FU_SYNAPTICS_CAPE_WORD_IN_BYTES;

	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	cmd.cmd_id = cmd_id;
	cmd.module_id = module_id;

	if (data_len != 0 && data != NULL) {
		cmd.data_len = data_len;
		if (!fu_memcpy_safe((guint8 *)cmd.data,
				    sizeof(cmd.data),
				    0, /* dst */
				    (const guint8 *)data,
				    dataszbyte,
				    0, /* src */
				    dataszbyte,
				    error))
			return FALSE;
	}
	return fu_synaptics_cape_device_sendcmd_ex(self, &cmd, delay_us, error);
}

static void
fu_synaptics_cape_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuSynapticsCapeDevice *self = FU_SYNAPTICS_CAPE_DEVICE(device);
	fu_common_string_append_ku(str, idt, "Active Partition", self->ActivePartition);
}

/* reset device */
static gboolean
fu_synaptics_cape_device_reset(FuSynapticsCapeDevice *self, GError **error)
{
	g_autoptr(GTimer) timer = g_timer_new();
	g_return_val_if_fail(FU_IS_SYNAPTICS_CAPE_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_synaptics_cape_device_sendcmd(self,
						   FU_SYNAPTICS_CAPE_CMD_APP_ID('C', 'T', 'R', 'L'),
						   FU_SYNAPTICS_CMD_MCU_SOFT_RESET,
						   NULL,
						   0,
						   0,
						   error)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "reset command is not supported");

		return FALSE;
	}

	g_usleep(1000 * FU_SYNAPTICS_CAPE_DEVICE_USB_RESET_DELAY_MS);

	g_debug("reset took %.2lfms", g_timer_elapsed(timer, NULL) * 1000);

	/* success */
	return TRUE;
}

/**
 * fu_synaptics_cape_device_get_active_partition:
 * @self: a #FuSynapticsCapeDevice
 * @error: return location for an error
 *
 * Updates active partition information to FuSynapticsCapeDevice::active_partition
 *
 * Returns: retruns TRUE if operation is successful, otherwise, return FALSE if
 *          unsuccessful.
 *
 **/
static gboolean
fu_synaptics_cape_device_setup_active_partition(FuSynapticsCapeDevice *self, GError **error)
{
	FuCapCmd cmd = {0};

	g_return_val_if_fail(FU_IS_SYNAPTICS_CAPE_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	
	cmd.cmd_id = FU_SYNAPTICS_CMD_FW_GET_ACTIVE_PARTITION;
	cmd.module_id = FU_SYNAPTICS_CAPE_CMD_APP_ID('C', 'T', 'R', 'L');

	if (!fu_synaptics_cape_device_sendcmd_ex(self, &cmd, 0, error))
		return FALSE;

	self->ActivePartition = GUINT32_FROM_LE(cmd.data[0]);

	if (self->ActivePartition != 1 && self->ActivePartition != 2) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "partition number out of range, returned partition number is %u",
			    self->ActivePartition);
		return FALSE;
	}

	/* success */
	return TRUE;
}

/* gets version number from device and saves to FU_DEVICE */
static gboolean
fu_synaptics_cape_device_setup_version(FuSynapticsCapeDevice *self, GError **error)
{
	guint32 version_raw;
	FuCapCmd cmd = {0};
	g_autofree gchar *version_str = NULL;

	cmd.cmd_id = GUINT32_TO_LE(FU_SYNAPTICS_CMD_GET_VERSION);
	cmd.module_id = FU_SYNAPTICS_CAPE_CMD_APP_ID('C', 'T', 'R', 'L');
	cmd.data_len = 4;

	/* gets version number from device */
	fu_synaptics_cape_device_sendcmd_ex(self, &cmd, 0, error);

	/* The version number are stored in lowest byte of a sequence of returned data */
	version_raw =
	    (GUINT32_FROM_LE(cmd.data[0]) << 24) | ((GUINT32_FROM_LE(cmd.data[1]) & 0xFF) << 16) |
	    ((GUINT32_FROM_LE(cmd.data[2]) & 0xFF) << 8) | (GUINT32_FROM_LE(cmd.data[3]) & 0xFF);

	version_str = fu_common_version_from_uint32(version_raw, FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_version(FU_DEVICE(self), version_str);
	fu_device_set_version_raw(FU_DEVICE(self), version_raw);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_cape_device_setup(FuDevice *device, GError **error)
{
	FuSynapticsCapeDevice *self = FU_SYNAPTICS_CAPE_DEVICE(device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_synaptics_cape_device_parent_class)->setup(device, error))
		return FALSE;

	if (!fu_synaptics_cape_device_setup_version(self, error)) {
		g_prefix_error(error, "failed to get firmware version info: ");
		return FALSE;
	}

	if (!fu_synaptics_cape_device_setup_active_partition(self, error)) {
		g_prefix_error(error, "failed to get active partition info: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static FuFirmware *
fu_synaptics_cape_device_prepare_firmware(FuDevice *device,
					  GBytes *fw,
					  FwupdInstallFlags flags,
					  GError **error)
{
	FuSynapticsCapeDevice *self = FU_SYNAPTICS_CAPE_DEVICE(device);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	g_autoptr(FuFirmware) firmware = fu_synaptics_cape_firmware_new();
	gsize offset = 0;

	g_autoptr(GBytes) new_fw = NULL;

	/* the "fw" includes two firmware data for each partition, we need to divide 'fw' into
	 * two equal parts.
	 */
	gsize bufsz = g_bytes_get_size(fw);

	if ((guint32)bufsz % 4 != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "data not aligned to 32 bits");
		return NULL;
	}

	/* check file size */
	if (bufsz < FW_CAPE_HID_HEADER_SIZE * 2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "file size is too small");
		return NULL;
	}

	/* use second partition if active partition is 1. */
	if (self->ActivePartition == 1)
		offset = bufsz / 2;

	new_fw = g_bytes_new_from_bytes(fw, offset, bufsz / 2);

	if (!fu_firmware_parse(firmware, new_fw, flags, error))
		return NULL;

	/* verify if correct device */
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_VID_PID) == 0) {
		const guint16 vid =
		    fu_synaptics_cape_firmware_get_vid(FU_SYNAPTICS_CAPE_FIRMWARE(firmware));
		const guint16 pid =
		    fu_synaptics_cape_firmware_get_pid(FU_SYNAPTICS_CAPE_FIRMWARE(firmware));
		if (vid != 0x0 && pid != 0x0 &&
		    (g_usb_device_get_vid(usb_device) != vid ||
		     g_usb_device_get_pid(usb_device) != pid)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "USB vendor or product incorrect, "
				    "got: %04X:%04X expected %04X:%04X",
				    vid,
				    pid,
				    g_usb_device_get_vid(usb_device),
				    g_usb_device_get_pid(usb_device));
			return NULL;
		}
	}

	/* success */
	return g_steal_pointer(&firmware);
}

/* sends firmware header to device */
static gboolean
fu_synaptics_cape_device_write_firmware_header(FuSynapticsCapeDevice *self,
					       GBytes *fw,
					       GError **error)
{
	const guint8 *buf = NULL;
	gsize bufsz = 0;

	g_return_val_if_fail(FU_IS_SYNAPTICS_CAPE_DEVICE(self), FALSE);
	g_return_val_if_fail(fw != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	buf = g_bytes_get_data(fw, &bufsz);

	/* check size */
	if (bufsz != 20) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "firmware header is not 20 bytes");
		return FALSE;
	}

	return fu_synaptics_cape_device_sendcmd(self,
						FU_SYNAPTICS_CAPE_CMD_APP_ID('C', 'T', 'R', 'L'),
						FU_SYNAPTICS_CMD_FW_UPDATE_START,
						(const guint32 *)buf,
						bufsz / FU_SYNAPTICS_CAPE_WORD_IN_BYTES,
						0,
						error);
}

/* sends firmware image to device */
static gboolean
fu_synaptics_cape_device_write_firmware_image(FuSynapticsCapeDevice *self,
					      GBytes *fw,
					      GError **error)
{
	g_autoptr(GPtrArray) chunks = NULL;

	g_return_val_if_fail(FU_IS_SYNAPTICS_CAPE_DEVICE(self), FALSE);
	g_return_val_if_fail(fw, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	chunks = fu_chunk_array_new_from_bytes(fw,
					       0x00,
					       0x00,
					       FU_SYNAPTICS_CAPE_WORD_IN_BYTES *
						   FU_SYNAPTICS_CAPE_CMD_WRITE_DATAL_LEN);

	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_synaptics_cape_device_sendcmd(
			self,
			FU_SYNAPTICS_CAPE_CMD_APP_ID('C', 'T', 'R', 'L'),
			FU_SYNAPTICS_CMD_FW_UPDATE_WRITE,
			(const guint32 *)fu_chunk_get_data(chk),
			fu_chunk_get_data_sz(chk) / FU_SYNAPTICS_CAPE_WORD_IN_BYTES,
			0,
			error)) {
				g_prefix_error(error, "failed send on chk %u: ", i);
				return FALSE;
		}
		fu_device_set_progress_full(FU_DEVICE(self), i, chunks->len - 1);
	}


	/* success */
	return TRUE;
}

/* performs firmware update */
static gboolean
fu_synaptics_cape_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FwupdInstallFlags flags,
					GError **error)
{
	FuSynapticsCapeDevice *self = FU_SYNAPTICS_CAPE_DEVICE(device);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GBytes) fw_header = NULL;


	fw_header = fu_firmware_get_image_by_id_bytes(firmware, FU_FIRMWARE_ID_HEADER, error);
	if (fw_header == NULL)
		return FALSE;
	if (!fu_synaptics_cape_device_write_firmware_header(self, fw_header, error)) {
		g_prefix_error(error, "update header failed: ");
		return FALSE;
	}

	/* perform the actual write */
	fu_device_set_status(device, FWUPD_STATUS_DEVICE_WRITE);

	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	if (!fu_synaptics_cape_device_write_firmware_image(self, fw, error)) {
		g_prefix_error(error, "update image failed: ");
		return FALSE;
	}

	/* verify the firmware image */
	fu_device_set_status(device, FWUPD_STATUS_DEVICE_VERIFY);
	if (!fu_synaptics_cape_device_sendcmd(self,
					      FU_SYNAPTICS_CAPE_CMD_APP_ID('C', 'T', 'R', 'L'),
					      FU_SYNAPTICS_CMD_FW_UPDATE_END,
					      NULL,
					      0,
					      0,
					      error)) {
		g_prefix_error(error, "failed to verify firmware: ");
		return FALSE;
	}

	/* send software reset to run available flash code */
	fu_device_set_status(device, FWUPD_STATUS_DEVICE_RESTART);

	if (!fu_synaptics_cape_device_reset(self, error))
		return FALSE;

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* success */
	return TRUE;
}

static void
fu_synaptics_cape_device_init(FuSynapticsCapeDevice *self)
{
	fu_device_add_icon(FU_DEVICE(self), "audio-card");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_install_duration(FU_DEVICE(self), 3); /* seconds */
	fu_device_add_protocol(FU_DEVICE(self), "com.synaptics.cape");
	fu_device_retry_set_delay(FU_DEVICE(self), 100); /* ms */
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
}

static void
fu_synaptics_cape_device_class_init(FuSynapticsCapeDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->to_string = fu_synaptics_cape_device_to_string;
	klass_device->setup = fu_synaptics_cape_device_setup;
	klass_device->write_firmware = fu_synaptics_cape_device_write_firmware;
	klass_device->prepare_firmware = fu_synaptics_cape_device_prepare_firmware;
}
