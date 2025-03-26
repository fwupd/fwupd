/*
 * Copyright 2021 Synaptics Incorporated <simon.ho@synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-synaptics-cape-device.h"
#include "fu-synaptics-cape-hid-firmware.h"
#include "fu-synaptics-cape-struct.h"

/* defines timings */
#define FU_SYNAPTICS_CAPE_DEVICE_USB_CMD_WRITE_TIMEOUT	20   /* ms */
#define FU_SYNAPTICS_CAPE_DEVICE_USB_CMD_READ_TIMEOUT	30   /* ms */
#define FU_SYNAPTICS_CAPE_DEVICE_USB_CMD_RETRY_INTERVAL 10   /* ms */
#define FU_SYNAPTICS_CAPE_DEVICE_USB_CMD_RETRY_TIMEOUT	300  /* ms */
#define FU_SYNAPTICS_CAPE_DEVICE_USB_CMD_INTR_TIMEOUT	5000 /* ms */
#define FU_SYNAPTICS_CAPE_DEVICE_USB_RESET_DELAY_MS	5000 /* ms */
#define FU_SYNAPTICS_CAPE_DEVICE_HDR_WRITE_DELAY_MS	150  /* ms */

/* defines CAPE command constant values and macro */
#define FU_SYNAPTICS_CAPE_CMD_WRITE_DATAL_LEN 8 /* number of guint32 */

#define FU_SYNAPTICS_CAPE_CMD_GET_FLAG 0x100 /* GET flag */

#define FU_SYNAPTICS_CAPE_CMD_IS_REPLY 0x8000

#define FU_SYNAPTICS_CAPE_FM3_HID_INTR_IN_EP 0x83

/* CAPE fwupd device structure */
struct _FuSynapticsCapeDevice {
	FuHidDevice parent_instance;
	FuSynapticsCapeFirmwarePartition active_partition;
};

#define FU_SYNAPTICS_CAPE_DEVICE_FLAG_USE_IN_REPORT_INTERRUPT "use-in-report-interrupt"

G_DEFINE_TYPE(FuSynapticsCapeDevice, fu_synaptics_cape_device, FU_TYPE_HID_DEVICE)

/* sends SET_REPORT to device */
static gboolean
fu_synaptics_cape_device_set_report(FuSynapticsCapeDevice *self,
				    const FuSynapticsCapeCmdHidReport *data,
				    GError **error)
{
	g_return_val_if_fail(FU_IS_SYNAPTICS_CAPE_DEVICE(self), FALSE);
	g_return_val_if_fail(data != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	fu_dump_raw(G_LOG_DOMAIN, "SetReport", (guint8 *)data, sizeof(*data));

	return fu_hid_device_set_report(FU_HID_DEVICE(self),
					FU_SYNAPTICS_CAPE_CMD_HID_REPORT_DEFAULT_REPORT_ID,
					(guint8 *)data,
					sizeof(*data),
					FU_SYNAPTICS_CAPE_DEVICE_USB_CMD_WRITE_TIMEOUT,
					FU_HID_DEVICE_FLAG_NONE,
					error);
}

/* gets HID report over control ep */
static gboolean
fu_synaptics_cape_device_get_report(FuSynapticsCapeDevice *self,
				    FuSynapticsCapeCmdHidReport *st_report,
				    GError **error)
{
	return fu_hid_device_get_report(FU_HID_DEVICE(self),
					FU_SYNAPTICS_CAPE_CMD_HID_REPORT_DEFAULT_REPORT_ID,
					st_report->data,
					st_report->len,
					FU_SYNAPTICS_CAPE_DEVICE_USB_CMD_READ_TIMEOUT,
					FU_HID_DEVICE_FLAG_NONE,
					error);
}

/* gets HID report over interrupt ep */
static gboolean
fu_synaptics_cape_device_get_report_intr(FuSynapticsCapeDevice *self,
					 FuSynapticsCapeCmdHidReport *data,
					 GError **error)
{
	gsize actual_len = 0;
	g_return_val_if_fail(FU_IS_SYNAPTICS_CAPE_DEVICE(self), FALSE);
	g_return_val_if_fail(data != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
					      FU_SYNAPTICS_CAPE_FM3_HID_INTR_IN_EP,
					      (guint8 *)data,
					      sizeof(*data),
					      &actual_len,
					      FU_SYNAPTICS_CAPE_DEVICE_USB_CMD_INTR_TIMEOUT,
					      NULL,
					      error)) {
		g_prefix_error(error, "failed to get report over interrupt ep: ");
		return FALSE;
	}

	fu_dump_raw(G_LOG_DOMAIN, "GetReport", (guint8 *)data, sizeof(*data));

	/* success */
	return TRUE;
}

/* dump CAPE command error if any */
static gboolean
fu_synaptics_cape_device_rc_set_error(const FuSynapticsCapeMsg *rsp, GError **error)
{
	gint16 value;

	g_return_val_if_fail(rsp != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	value = (gint16)fu_synaptics_cape_msg_get_data_len(rsp);
	if (value >= 0)
		return TRUE;
	switch (value) {
	case FU_SYNAPTICS_CAPE_ERROR_GENERIC_FAILURE:
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "generic failure");
		break;
	case FU_SYNAPTICS_CAPE_ERROR_ALREADY_EXISTS:
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "already exists");
		break;
	case FU_SYNAPTICS_CAPE_ERROR_NULL_APP_POINTER:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "null app pointer");
		break;
	case FU_SYNAPTICS_CAPE_ERROR_NULL_MODULE_POINTER:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "null module pointer");
		break;
	case FU_SYNAPTICS_CAPE_ERROR_NULL_POINTER:
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "null pointer");
		break;
	case FU_SYNAPTICS_CAPE_ERROR_BAD_APP_ID:
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "bad app id");
		break;
	case FU_SYNAPTICS_CAPE_ERROR_MODULE_TYPE_HAS_NO_API:
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "has no api");
		break;
	case FU_SYNAPTICS_CAPE_ERROR_BAD_MAGIC_NUMBER:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "bad magic number");
		break;
	case FU_SYNAPTICS_CAPE_ERROR_CMD_MODE_UNSUPPORTED:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "mode unsupported");
		break;
	case FU_SYNAPTICS_CAPE_ERROR_EAGAIN:
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_TIMED_OUT, "query timeout");
		break;
	case FU_SYNAPTICS_CAPE_ERROR_SFU_FAIL:
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "command failed");
		break;
	case FU_SYNAPTICS_CAPE_ERROR_SFU_WRITE_FAIL:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "writing to flash failed");
		break;
	case FU_SYNAPTICS_CAPE_ERROR_SFU_READ_FAIL:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "reading from flash failed");
		break;
	case FU_SYNAPTICS_CAPE_ERROR_SFU_CRC_ERROR:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "firmware data has been corrupted");
		break;
	case FU_SYNAPTICS_CAPE_ERROR_SFU_USB_ID_NOT_MATCH:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "vendor and device IDs do not match");
		break;
	case FU_SYNAPTICS_CAPE_ERROR_SFU_VERSION_DOWNGRADE:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_VERSION_NEWER,
				    "update is older than current version");
		break;
	case FU_SYNAPTICS_CAPE_ERROR_SFU_HEADER_CORRUPTION:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "firmware header data has been corrupted");
		break;
	case FU_SYNAPTICS_CAPE_ERROR_SFU_IMAGE_CORRUPTION:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "firmware payload data has been corrupted");
		break;
	case FU_SYNAPTICS_CAPE_ERROR_SFU_ALREADY_ACTIVE:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to active new firmward");
		break;
	case FU_SYNAPTICS_CAPE_ERROR_SFU_NOT_READY:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_BUSY,
				    "firmware update is not ready");
		break;
	case FU_SYNAPTICS_CAPE_ERROR_SFU_SIGN_TRANSFER_CORRUPTION:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "signature data has been corrupted");
		break;
	case FU_SYNAPTICS_CAPE_ERROR_SFU_DIGITAL_SIGNATURE_VERIFICATION_FAILED:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_AUTH_FAILED,
				    "digital signature is invalid");
		break;
	case FU_SYNAPTICS_CAPE_ERROR_SFU_TASK_NOT_RUNNING:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "firmware update task is not running");
		break;
	default:
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "unknown error %d", value);
	}

	/* success */
	return FALSE;
}

/* sends a FuSynapticsCapeMsg structure command to device to get the response in the same structure
 */
static FuSynapticsCapeMsg *
fu_synaptics_cape_device_sendcmd_ex(FuSynapticsCapeDevice *self,
				    FuSynapticsCapeMsg *st_msg_req,
				    guint delay_ms,
				    GError **error)
{
	guint elapsed_ms = 0;
	g_autoptr(FuSynapticsCapeCmdHidReport) st_report = fu_synaptics_cape_cmd_hid_report_new();
	g_autoptr(FuSynapticsCapeMsg) st_msg_res = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(st_msg_req != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* sets data length to MAX for any GET commands */
	if (fu_synaptics_cape_msg_get_cmd_id(st_msg_req) & FU_SYNAPTICS_CAPE_CMD_GET_FLAG) {
		fu_synaptics_cape_msg_set_data_len(st_msg_req,
						   FU_SYNAPTICS_CAPE_MSG_N_ELEMENTS_DATA);
	}

	/* first two bytes are report id */
	if (!fu_synaptics_cape_cmd_hid_report_set_msg(st_report, st_msg_req, error))
		return NULL;

	if (!fu_synaptics_cape_device_set_report(self, st_report, error)) {
		g_prefix_error(error, "failed to send: ");
		return NULL;
	}

	fu_device_sleep(FU_DEVICE(self), delay_ms);

	/* waits for the command to complete. There are two approaches to get status from device:
	 *  1. gets IN_REPORT over interrupt endpoint. device won't reply until a command operation
	 *     has completed. This works only on devices support interrupt endpoint.
	 *  2. polls GET_REPORT over control endpoint. device will return 'reply==0' before a
	 * command operation has completed.
	 */
	if (fu_device_has_private_flag(FU_DEVICE(self),
				       FU_SYNAPTICS_CAPE_DEVICE_FLAG_USE_IN_REPORT_INTERRUPT)) {
		if (!fu_synaptics_cape_device_get_report_intr(self, st_report, &error_local)) {
			/* ignoring io error for software reset command */
			if ((fu_synaptics_cape_msg_get_cmd_id(st_msg_req) ==
			     FU_SYNAPTICS_CAPE_CMD_MCU_SOFT_RESET) &&
			    (fu_synaptics_cape_msg_get_module_id(st_msg_req) ==
			     FU_SYNAPTICS_CAPE_MODULE_ID_APP_CTRL) &&
			    (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND) ||
			     g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_INTERNAL))) {
				g_debug("ignoring: %s", error_local->message);
				return g_byte_array_ref(st_msg_req);
			}
			g_propagate_prefixed_error(error,
						   g_steal_pointer(&error_local),
						   "failed to get IN_REPORT: ");
			return NULL;
		}
	} else {
		for (; elapsed_ms < FU_SYNAPTICS_CAPE_DEVICE_USB_CMD_RETRY_TIMEOUT;
		     elapsed_ms += FU_SYNAPTICS_CAPE_DEVICE_USB_CMD_RETRY_INTERVAL) {
			g_autoptr(FuSynapticsCapeMsg) st_msg2 = NULL;
			if (!fu_synaptics_cape_device_get_report(self, st_report, &error_local)) {
				/* ignoring io error for software reset command */
				if ((fu_synaptics_cape_msg_get_cmd_id(st_msg_req) ==
				     FU_SYNAPTICS_CAPE_CMD_MCU_SOFT_RESET) &&
				    (fu_synaptics_cape_msg_get_module_id(st_msg_req) ==
				     FU_SYNAPTICS_CAPE_MODULE_ID_APP_CTRL) &&
				    (g_error_matches(error_local,
						     FWUPD_ERROR,
						     FWUPD_ERROR_NOT_FOUND) ||
				     g_error_matches(error_local,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INTERNAL))) {
					g_debug("ignoring: %s", error_local->message);
					g_byte_array_ref(st_msg_req);
				}
				g_propagate_prefixed_error(error,
							   g_steal_pointer(&error_local),
							   "failed to get GET_REPORT: ");
				return NULL;
			}
			st_msg2 = fu_synaptics_cape_cmd_hid_report_get_msg(st_report);
			if (fu_synaptics_cape_msg_get_cmd_id(st_msg2) &
			    FU_SYNAPTICS_CAPE_CMD_IS_REPLY)
				break;
			fu_device_sleep(FU_DEVICE(self),
					FU_SYNAPTICS_CAPE_DEVICE_USB_CMD_RETRY_INTERVAL);
		}
	}

	st_msg_res = fu_synaptics_cape_cmd_hid_report_get_msg(st_report);
	if ((fu_synaptics_cape_msg_get_cmd_id(st_msg_res) & FU_SYNAPTICS_CAPE_CMD_IS_REPLY) == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "firmware don't respond to command");
		return NULL;
	}
	if (!fu_synaptics_cape_device_rc_set_error(st_msg_res, error))
		return NULL;

	/* success */
	return g_steal_pointer(&st_msg_res);
}

/* a simple version of sendcmd_ex without returned data */
static gboolean
fu_synaptics_cape_device_sendcmd(FuSynapticsCapeDevice *self,
				 FuSynapticsCapeModuleId module_id,
				 FuSynapticsCapeCmd cmd_id,
				 const guint32 *data,
				 gsize data_len,
				 guint delay_ms,
				 GError **error)
{
	g_autoptr(FuSynapticsCapeMsg) st_msg_req = fu_synaptics_cape_msg_new();
	g_autoptr(FuSynapticsCapeMsg) st_msg_res = NULL;
	fu_synaptics_cape_msg_set_data_len(st_msg_req, data_len);
	fu_synaptics_cape_msg_set_cmd_id(st_msg_req, cmd_id);
	fu_synaptics_cape_msg_set_module_id(st_msg_req, module_id);
	for (guint i = 0; i < data_len; i++)
		fu_synaptics_cape_msg_set_data(st_msg_req, i, data[i]);
	st_msg_res = fu_synaptics_cape_device_sendcmd_ex(self, st_msg_req, delay_ms, error);
	return st_msg_res != NULL;
}

static void
fu_synaptics_cape_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuSynapticsCapeDevice *self = FU_SYNAPTICS_CAPE_DEVICE(device);

	g_return_if_fail(FU_IS_SYNAPTICS_CAPE_DEVICE(self));

	fwupd_codec_string_append_int(str, idt, "ActivePartition", self->active_partition);
}

/* resets device */
static gboolean
fu_synaptics_cape_device_reset(FuSynapticsCapeDevice *self, GError **error)
{
	g_autoptr(GTimer) timer = g_timer_new();

	g_return_val_if_fail(FU_IS_SYNAPTICS_CAPE_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_synaptics_cape_device_sendcmd(self,
					      FU_SYNAPTICS_CAPE_MODULE_ID_APP_CTRL,
					      FU_SYNAPTICS_CAPE_CMD_MCU_SOFT_RESET,
					      NULL,
					      0,
					      0,
					      error)) {
		g_prefix_error(error, "reset command is not supported: ");
		return FALSE;
	}

	fu_device_sleep(FU_DEVICE(self), FU_SYNAPTICS_CAPE_DEVICE_USB_RESET_DELAY_MS);

	g_debug("reset took %.2lfms", g_timer_elapsed(timer, NULL) * 1000);

	/* success */
	return TRUE;
}

/**
 * fu_synaptics_cape_device_get_active_partition:
 * @self: a #FuSynapticsCapeDevice
 * @error: return location for an error
 *
 * updates active partition information to FuSynapticsCapeDevice::active_partition
 *
 * Returns: returns TRUE if operation is successful, otherwise, return FALSE if
 *          unsuccessful.
 *
 **/
static gboolean
fu_synaptics_cape_device_setup_active_partition(FuSynapticsCapeDevice *self, GError **error)
{
	g_autoptr(FuSynapticsCapeMsg) st_msg_req = fu_synaptics_cape_msg_new();
	g_autoptr(FuSynapticsCapeMsg) st_msg_res = NULL;

	g_return_val_if_fail(FU_IS_SYNAPTICS_CAPE_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	fu_synaptics_cape_msg_set_cmd_id(st_msg_req, FU_SYNAPTICS_CAPE_CMD_FW_GET_ACTIVE_PARTITION);
	st_msg_res = fu_synaptics_cape_device_sendcmd_ex(self, st_msg_req, 0, error);
	if (st_msg_res == NULL)
		return FALSE;

	self->active_partition = fu_synaptics_cape_msg_get_data(st_msg_res, 0);
	if (self->active_partition != FU_SYNAPTICS_CAPE_FIRMWARE_PARTITION_1 &&
	    self->active_partition != FU_SYNAPTICS_CAPE_FIRMWARE_PARTITION_2) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "partition number out of range, returned partition number is %u",
			    self->active_partition);
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
	g_autoptr(FuSynapticsCapeMsg) st_msg_req = fu_synaptics_cape_msg_new();
	g_autoptr(FuSynapticsCapeMsg) st_msg_res = NULL;

	/* gets version number from device */
	fu_synaptics_cape_msg_set_cmd_id(st_msg_req, FU_SYNAPTICS_CAPE_CMD_GET_VERSION);
	fu_synaptics_cape_msg_set_data_len(st_msg_req, 4);
	st_msg_res = fu_synaptics_cape_device_sendcmd_ex(self, st_msg_req, 0, error);
	if (st_msg_res == NULL)
		return FALSE;

	/* the version number are stored in lowest byte of a sequence of returned data */
	version_raw = ((fu_synaptics_cape_msg_get_data(st_msg_res, 0) << 24) |
		       ((fu_synaptics_cape_msg_get_data(st_msg_res, 1) & 0xFF) << 16) |
		       ((fu_synaptics_cape_msg_get_data(st_msg_res, 2) & 0xFF) << 8) |
		       (fu_synaptics_cape_msg_get_data(st_msg_res, 3) & 0xFF));
	fu_device_set_version_raw(FU_DEVICE(self), version_raw);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_cape_device_setup(FuDevice *device, GError **error)
{
	FuSynapticsCapeDevice *self = FU_SYNAPTICS_CAPE_DEVICE(device);

	g_return_val_if_fail(FU_IS_SYNAPTICS_CAPE_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

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
					  GInputStream *stream,
					  FuProgress *progress,
					  FwupdInstallFlags flags,
					  GError **error)
{
	FuSynapticsCapeDevice *self = FU_SYNAPTICS_CAPE_DEVICE(device);
	gsize bufsz = 0;
	gsize offset = 0;
	g_autoptr(FuFirmware) firmware = fu_synaptics_cape_hid_firmware_new();
	g_autoptr(GInputStream) stream_fw = NULL;

	/* a "fw" includes two firmware data for each partition, we need to divide a 'fw' into
	 * two equal parts */
	if (!fu_input_stream_size(stream, &bufsz, error))
		return NULL;
	if ((guint32)bufsz % 4 != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "data not aligned to 32 bits");
		return NULL;
	}

	/* uses second partition if active partition is 1 */
	if (self->active_partition == FU_SYNAPTICS_CAPE_FIRMWARE_PARTITION_1)
		offset = bufsz / 2;

	stream_fw = fu_partial_input_stream_new(stream, offset, bufsz / 2, error);
	if (stream_fw == NULL)
		return NULL;
	if (!fu_firmware_parse_stream(firmware, stream_fw, 0x0, flags, error))
		return NULL;

	/* verify if correct device */
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_VID_PID) == 0) {
		const guint16 vid =
		    fu_synaptics_cape_firmware_get_vid(FU_SYNAPTICS_CAPE_FIRMWARE(firmware));
		const guint16 pid =
		    fu_synaptics_cape_firmware_get_pid(FU_SYNAPTICS_CAPE_FIRMWARE(firmware));
		if (vid != 0x0 && pid != 0x0 &&
		    (fu_device_get_vid(device) != vid || fu_device_get_pid(device) != pid)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "USB vendor or product incorrect, "
				    "got: %04X:%04X expected %04X:%04X",
				    vid,
				    pid,
				    fu_device_get_vid(device),
				    fu_device_get_pid(device));
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
	g_autofree guint32 *buf32 = NULL;

	g_return_val_if_fail(FU_IS_SYNAPTICS_CAPE_DEVICE(self), FALSE);
	g_return_val_if_fail(fw != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	buf = g_bytes_get_data(fw, &bufsz);

	/* checks size */
	if (bufsz != 20) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "firmware header is not 20 bytes");
		return FALSE;
	}

	/* 32 bit align */
	buf32 = g_new0(guint32, bufsz / sizeof(guint32));
	if (!fu_memcpy_safe((guint8 *)buf32,
			    bufsz,
			    0x0, /* dst */
			    buf,
			    bufsz,
			    0x0, /* src */
			    bufsz,
			    error))
		return FALSE;
	return fu_synaptics_cape_device_sendcmd(self,
						FU_SYNAPTICS_CAPE_MODULE_ID_APP_CTRL,
						FU_SYNAPTICS_CAPE_CMD_FW_UPDATE_START,
						buf32,
						bufsz / sizeof(guint32),
						FU_SYNAPTICS_CAPE_DEVICE_HDR_WRITE_DELAY_MS,
						error);
}

/* sends firmware image to device */
static gboolean
fu_synaptics_cape_device_write_firmware_image(FuSynapticsCapeDevice *self,
					      GInputStream *stream,
					      FuProgress *progress,
					      GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;

	chunks =
	    fu_chunk_array_new_from_stream(stream,
					   FU_CHUNK_ADDR_OFFSET_NONE,
					   FU_CHUNK_PAGESZ_NONE,
					   sizeof(guint32) * FU_SYNAPTICS_CAPE_CMD_WRITE_DATAL_LEN,
					   error);
	if (chunks == NULL)
		return FALSE;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		gsize bufsz;
		g_autofree guint32 *buf32 = NULL;
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		/* 32 bit align */
		bufsz = fu_chunk_get_data_sz(chk);
		buf32 = g_new0(guint32, bufsz / sizeof(guint32));
		if (!fu_memcpy_safe((guint8 *)buf32,
				    bufsz,
				    0x0, /* dst */
				    fu_chunk_get_data(chk),
				    bufsz,
				    0x0, /* src */
				    bufsz,
				    error))
			return FALSE;

		if (!fu_synaptics_cape_device_sendcmd(self,
						      FU_SYNAPTICS_CAPE_MODULE_ID_APP_CTRL,
						      FU_SYNAPTICS_CAPE_CMD_FW_UPDATE_WRITE,
						      buf32,
						      bufsz / sizeof(guint32),
						      0,
						      error)) {
			g_prefix_error(error, "failed send on chk %u: ", i);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

/* performs firmware update */
static gboolean
fu_synaptics_cape_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	FuSynapticsCapeDevice *self = FU_SYNAPTICS_CAPE_DEVICE(device);
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GBytes) fw_header = NULL;

	g_return_val_if_fail(FU_IS_SYNAPTICS_CAPE_DEVICE(self), FALSE);
	g_return_val_if_fail(firmware != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 2, "device-write-hdr");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 69, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 29, NULL);

	fw_header = fu_firmware_get_image_by_id_bytes(firmware, FU_FIRMWARE_ID_HEADER, error);
	if (fw_header == NULL)
		return FALSE;
	if (!fu_synaptics_cape_device_write_firmware_header(self, fw_header, error)) {
		g_prefix_error(error, "update header failed: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* performs the actual write */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	if (!fu_synaptics_cape_device_write_firmware_image(self,
							   stream,
							   fu_progress_get_child(progress),
							   error)) {
		g_prefix_error(error, "update image failed: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* verify the firmware image */
	if (!fu_synaptics_cape_device_sendcmd(self,
					      FU_SYNAPTICS_CAPE_MODULE_ID_APP_CTRL,
					      FU_SYNAPTICS_CAPE_CMD_FW_UPDATE_END,
					      NULL,
					      0,
					      0,
					      error)) {
		g_prefix_error(error, "failed to verify firmware: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* sends software reset to boot into the newly flashed firmware */
	if (!fu_synaptics_cape_device_reset(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static void
fu_synaptics_cape_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static gchar *
fu_synaptics_cape_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32(version_raw, fu_device_get_version_format(device));
}

static void
fu_synaptics_cape_device_init(FuSynapticsCapeDevice *self)
{
	fu_device_add_icon(FU_DEVICE(self), "audio-card");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_install_duration(FU_DEVICE(self), 3); /* seconds */
	fu_device_add_protocol(FU_DEVICE(self), "com.synaptics.cape");
	fu_device_retry_set_delay(FU_DEVICE(self), 100); /* ms */
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_SYNAPTICS_CAPE_DEVICE_FLAG_USE_IN_REPORT_INTERRUPT);
}

static void
fu_synaptics_cape_device_class_init(FuSynapticsCapeDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_synaptics_cape_device_to_string;
	device_class->setup = fu_synaptics_cape_device_setup;
	device_class->write_firmware = fu_synaptics_cape_device_write_firmware;
	device_class->prepare_firmware = fu_synaptics_cape_device_prepare_firmware;
	device_class->set_progress = fu_synaptics_cape_device_set_progress;
	device_class->convert_version = fu_synaptics_cape_device_convert_version;
}
