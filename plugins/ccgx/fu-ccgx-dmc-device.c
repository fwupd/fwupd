/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-ccgx-common.h"
#include "fu-ccgx-dmc-common.h"
#include "fu-ccgx-dmc-device.h"
#include "fu-ccgx-dmc-firmware.h"

#define DMC_FW_WRITE_STATUS_RETRY_COUNT		3
#define DMC_FW_WRITE_STATUS_RETRY_DELAY_MS	30

struct _FuCcgxDmcDevice {
	FuUsbDevice		parent_instance;
	FWImageType		fw_image_type;
	DmcDockIdentity		dock_id;
	guint8			ep_intr_in;
	guint8			ep_bulk_out;
	DmcUpdateModel		update_model;
};


/**
 * FU_CCGX_DMC_DEVICE_FLAG_HAS_MANUAL_REPLUG:
 *
 * Needs a manual replug from the end-user.
 */
#define FU_CCGX_DMC_DEVICE_FLAG_HAS_MANUAL_REPLUG		(1 << 0)

G_DEFINE_TYPE (FuCcgxDmcDevice, fu_ccgx_dmc_device, FU_TYPE_USB_DEVICE)

static gboolean
fu_ccgx_dmc_device_get_dock_id (FuCcgxDmcDevice *self,
				DmcDockIdentity *dock_id,
				GError **error)
{
	g_return_val_if_fail (dock_id != NULL, FALSE);

	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    DMC_RQT_CODE_DOCK_IDENTITY, /* request */
					    0, /* value */
					    0, /* index */
					    (guint8 *) dock_id, /* data */
					    sizeof (DmcDockIdentity),  /* length */
					    NULL, /* actual length */
					    DMC_CONTROL_TRANSFER_DEFAULT_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "get_dock_id error: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_dmc_device_get_dock_status (FuCcgxDmcDevice *self,
				    DmcDockStatus *dock_status,
				    GError **error)
{
	g_return_val_if_fail (dock_status != NULL, FALSE);

	/* read minimum status length */
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    DMC_RQT_CODE_DOCK_STATUS, /* request */
					    0, /* value */
					    0, /* index */
					    (guint8 *) dock_status, /* data */
					    DMC_GET_STATUS_MIN_LEN,  /* length */
					    NULL, /* actual length */
					    DMC_CONTROL_TRANSFER_DEFAULT_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "get_dock_status min size error: ");
		return FALSE;
	}
	if (dock_status->status_length <= sizeof(DmcDockStatus)) {
		/* read full status length */
		if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
						    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
						    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
						    G_USB_DEVICE_RECIPIENT_DEVICE,
						    DMC_RQT_CODE_DOCK_STATUS, /* request */
						    0, /* value */
						    0, /* index */
						    (guint8 *) dock_status, /* data */
						    sizeof(DmcDockStatus),  /* length */
						    NULL, /* actual length */
						    DMC_CONTROL_TRANSFER_DEFAULT_TIMEOUT,
						    NULL, error)) {
			g_prefix_error (error, "get_dock_status actual size error: ");
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_ccgx_dmc_device_send_reset_state_machine (FuCcgxDmcDevice *self, GError **error)
{
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    DMC_RQT_CODE_RESET_STATE_MACHINE, /* request */
					    0, /* value */
					    0, /* index */
					    0, /* data */
					    0,  /* length */
					    NULL, /* actual length */
					    DMC_CONTROL_TRANSFER_DEFAULT_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "send reset state machine error: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_dmc_device_send_sort_reset (FuCcgxDmcDevice *self,
				    gboolean reset_later,
				    GError **error)
{
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    DMC_RQT_CODE_SOFT_RESET, /* request */
					    reset_later, /* value */
					    0, /* index */
					    0, /* data */
					    0,  /* length */
					    NULL, /* actual length */
					    DMC_CONTROL_TRANSFER_DEFAULT_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "send reset error: ");
		return FALSE;
	}
	return TRUE;
}


static gboolean
fu_ccgx_dmc_device_send_start_upgrade (FuCcgxDmcDevice *self,
				       const guint8 *custom_meta_data,
				       guint16 custom_meta_bufsz,
				       GError **error)
{
	guint16 value = 0;
	if (custom_meta_bufsz > 0)
		value = 1;

	if (custom_meta_bufsz > 0 && custom_meta_data == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "invalid metadata, buffer is NULL but size = %d",custom_meta_bufsz);
			return FALSE;
	}
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    DMC_RQT_CODE_UPGRADE_START, /* request */
					    value, /* value */
					    1, /* index, forced update for Adicora only, other dock will ignore it */
					    (guint8 *)custom_meta_data, /* data */
					    custom_meta_bufsz,  /* length */
					    NULL, /* actual length */
					    DMC_CONTROL_TRANSFER_DEFAULT_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "send reset error: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_dmc_device_send_download_trigger (FuCcgxDmcDevice *self,
					  DmcTriggerCode trigger,
					  GError **error)
{
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    DMC_RQT_CODE_TRIGGER, /* request */
					    trigger, /* value */
					    0, /* index */
					    0, /* data */
					    0,  /* length */
					    NULL, /* actual length */
					    DMC_CONTROL_TRANSFER_DEFAULT_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "send download trigger error: ");
		return FALSE;
	}
	return TRUE;
}


static gboolean
fu_ccgx_dmc_device_send_fwct (FuCcgxDmcDevice *self,
			      const guint8 *fwct_buf,
			      guint16 fwct_sz,
			      GError **error)
{
	g_return_val_if_fail (fwct_buf != NULL, FALSE);

	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    DMC_RQT_CODE_FWCT_WRITE, /* request */
					    0, /* value */
					    0, /* index */
					    (guint8 *) fwct_buf, /* data */
					    fwct_sz,  /* length */
					    NULL, /* actual length */
					    DMC_CONTROL_TRANSFER_DEFAULT_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "send fwct error: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_dmc_device_read_intr_req (FuCcgxDmcDevice *self,
				  DmcIntRqt *intr_rqt,
				  GError **error)
{
	g_return_val_if_fail (intr_rqt != NULL, FALSE);

	if (!g_usb_device_interrupt_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					      self->ep_intr_in,
					      (guint8 *) intr_rqt,
					      sizeof(DmcIntRqt),
					      NULL,
					      DMC_GET_REQUEST_TIMEOUT,
					      NULL, error)) {
		g_prefix_error (error, "read intr rqt error: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_dmc_device_send_write_command (FuCcgxDmcDevice *self,
				       guint16 start_row,
				       guint16 num_of_row,
				       GError **error)
{
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    DMC_RQT_CODE_IMG_WRITE, /* request */
					    start_row, /* value */
					    num_of_row, /* index */
					    0, /* data */
					    0,  /* length */
					    NULL, /* actual length */
					    DMC_CONTROL_TRANSFER_DEFAULT_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "send fwct error: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_dmc_device_send_row_data (FuCcgxDmcDevice *self,
				  const guint8 *row_buffer,
				  guint16 row_size,
				  GError **error)
{
	g_return_val_if_fail (row_buffer != NULL, FALSE);

	if (!g_usb_device_bulk_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					 self->ep_bulk_out,
					 (guint8 *)row_buffer, row_size, NULL,
					 DMC_BULK_OUT_PIPE_TIMEOUT,
					 NULL, error)) {
		g_prefix_error (error, "write row data error: ");
		return FALSE;
	}
	return TRUE;
}


static void
fu_ccgx_dmc_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuCcgxDmcDevice *self = FU_CCGX_DMC_DEVICE (device);
	fu_common_string_append_kv (str, idt, "UpdateModel",
				    fu_ccgx_dmc_update_model_type_to_string (self->update_model));
	fu_common_string_append_kv (str, idt, "FwImageType",
				    fu_ccgx_fw_image_type_to_string (self->fw_image_type));
	fu_common_string_append_kx (str, idt, "EpBulkOut", self->ep_bulk_out);
	fu_common_string_append_kx (str, idt, "EpIntrIn", self->ep_intr_in);
}

static gboolean
fu_ccgx_dmc_get_image_write_status_cb (FuDevice *device, gpointer user_data, GError **error)
{
	FuCcgxDmcDevice *self = FU_CCGX_DMC_DEVICE (device);
	DmcIntRqt dmc_int_req = {0};

	/* get interrupt request */
	if (!fu_ccgx_dmc_device_read_intr_req (self, &dmc_int_req, error)) {
		g_prefix_error (error, "read intr req error in image write status: ");
		return FALSE;
	}
	/* check opcode for fw write */
	if (dmc_int_req.opcode != DMC_INT_OPCODE_IMG_WRITE_STATUS) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "invalid dmc intr req opcode in image write status = %d",
			     dmc_int_req.opcode);
		return FALSE;
	}

	/* retry if data[0] is 1 otherwise error */
	if (dmc_int_req.data[0] != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "invalid dmc intr req data in image write status = %d",
			     dmc_int_req.data[0]);
		g_usleep (DMC_FW_WRITE_STATUS_RETRY_DELAY_MS * 1000);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_dmc_write_firmware_image(FuDevice *device,
				 FuCcgxDmcFirmwareRecord *img_rcd,
				 gsize *fw_data_written,
				 const gsize fw_data_size,
				 FuProgress *progress,
				 GError **error)
{
	FuCcgxDmcDevice *self = FU_CCGX_DMC_DEVICE (device);
	GPtrArray *seg_records;

	g_return_val_if_fail (img_rcd != NULL, FALSE);
	g_return_val_if_fail (fw_data_written != NULL, FALSE);

	/* get segment records */
	seg_records = img_rcd->seg_records;
	for (guint32 seg_index = 0; seg_index < seg_records->len; seg_index++) {
		GPtrArray *data_records = NULL;
		FuCcgxDmcFirmwareSegmentRecord *seg_rcd = g_ptr_array_index (seg_records, seg_index);

		/* write start row and number of rows to a device */
		if (!fu_ccgx_dmc_device_send_write_command (self,
							    seg_rcd->start_row,
							    seg_rcd->num_rows,
							    error))
			return FALSE;

		/* get data records */
		data_records = seg_rcd->data_records;
		for (guint32 data_index = 0; data_index < data_records->len; data_index++) {
			GBytes *data_rcd = g_ptr_array_index (data_records, data_index);
			const guint8 *row_buffer = NULL;
			gsize row_size = 0;

			/* write row data */
			row_buffer = g_bytes_get_data (data_rcd, &row_size);
			if (!fu_ccgx_dmc_device_send_row_data (self,
							       row_buffer,
							       (guint16) row_size,
							       error))
				return FALSE;

			/* increase fw written size */
			*fw_data_written += row_size;
			fu_progress_set_percentage_full(progress, *fw_data_written, fw_data_size);

			/* get status */
			if (!fu_device_retry (FU_DEVICE (self),
					      fu_ccgx_dmc_get_image_write_status_cb,
					      DMC_FW_WRITE_STATUS_RETRY_COUNT,
					      NULL, error))
				return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_ccgx_dmc_write_firmware(FuDevice *device,
			   FuFirmware *firmware,
			   FuProgress *progress,
			   FwupdInstallFlags flags,
			   GError **error)
{
	FuCcgxDmcDevice *self = FU_CCGX_DMC_DEVICE (device);
	FuCcgxDmcFirmwareRecord *img_rcd = NULL;
	DmcIntRqt dmc_int_rqt = {0};
	GBytes *custom_meta_blob;
	GBytes *fwct_blob;
	GPtrArray *image_records;
	const guint8 *custom_meta_data = NULL;
	const guint8 *fwct_buf = NULL;
	gsize custom_meta_bufsz = 0;
	gsize fwct_sz = 0;
	gsize fw_data_size = 0;
	gsize fw_data_written = 0;
	guint8 img_index = 0;

	/* get fwct record */
	fwct_blob = fu_ccgx_dmc_firmware_get_fwct_record (FU_CCGX_DMC_FIRMWARE (firmware));
	fwct_buf = g_bytes_get_data (fwct_blob, &fwct_sz);
	if (fwct_buf == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "invalid fwct data");
		return FALSE;
	}

	/* get custom meta record */
	custom_meta_blob = fu_ccgx_dmc_firmware_get_custom_meta_record (FU_CCGX_DMC_FIRMWARE (firmware));
	if (custom_meta_blob != NULL)
		custom_meta_data = g_bytes_get_data (custom_meta_blob, &custom_meta_bufsz);

	/* reset */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_BUSY);
	if (!fu_ccgx_dmc_device_send_reset_state_machine (self, error))
		return FALSE;

	/* start fw upgrade with custom metadata */
	if (!fu_ccgx_dmc_device_send_start_upgrade (self, custom_meta_data, custom_meta_bufsz, error))
		return FALSE;

	/* send fwct data */
	if (!fu_ccgx_dmc_device_send_fwct (self, fwct_buf, fwct_sz, error))
		return FALSE;

	/* get total fw size */
	image_records = fu_ccgx_dmc_firmware_get_image_records (FU_CCGX_DMC_FIRMWARE (firmware));
	fw_data_size = fu_ccgx_dmc_firmware_get_fw_data_size (FU_CCGX_DMC_FIRMWARE (firmware));
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	while (1) {

		/* get interrupt request */
		if (!fu_ccgx_dmc_device_read_intr_req (self, &dmc_int_rqt, error))
			return FALSE;

		/* fw upgrade request */
		if (dmc_int_rqt.opcode != DMC_INT_OPCODE_FW_UPGRADE_RQT)
			break;

		img_index = dmc_int_rqt.data[0];
		if (img_index >= image_records->len) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "invalid image index %d, expected less than %u",
				     img_index, image_records->len);
			return FALSE;
		}

		/* write image */
		img_rcd = g_ptr_array_index (image_records, img_index);
		if (!fu_ccgx_dmc_write_firmware_image(device,
						      img_rcd,
						      &fw_data_written,
						      fw_data_size,
						      progress,
						      error))
			return FALSE;
	}

	if (dmc_int_rqt.opcode != DMC_INT_OPCODE_FW_UPGRADE_STATUS) {
		if (dmc_int_rqt.opcode == DMC_INT_OPCODE_FWCT_ANALYSIS_STATUS) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "fwct analysis failed with status = %d",
				     dmc_int_rqt.data[0]);
			return FALSE;
		}
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "invalid dmc intr req opcode = %d with status = %d",
			     dmc_int_rqt.opcode, dmc_int_rqt.data[0]);
		return FALSE;
	}

	if (dmc_int_rqt.data[0] == DMC_DEVICE_STATUS_UPDATE_PHASE_1_COMPLETE) {
		self->update_model = DMC_UPDATE_MODEL_DOWNLOAD_TRIGGER;
	} else if (dmc_int_rqt.data[0] == DMC_DEVICE_STATUS_FW_DOWNLOADED_UPDATE_PEND) {
		self->update_model = DMC_UPDATE_MODEL_PENDING_RESET;
	} else {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "invalid status code = %u",
			     dmc_int_rqt.data[0]);
		return FALSE;
	}
	return TRUE;
}

static FuFirmware *
fu_ccgx_dmc_device_prepare_firmware (FuDevice *device,
				     GBytes *fw,
				     FwupdInstallFlags flags,
				     GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_ccgx_dmc_firmware_new ();
	FuCcgxDmcDevice *self = FU_CCGX_DMC_DEVICE (device);
	GBytes* custom_meta_blob = NULL;
	gboolean custom_meta_exist = FALSE;

	/* parse all images */
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;

	/* get custom meta record */
	custom_meta_blob = fu_ccgx_dmc_firmware_get_custom_meta_record (FU_CCGX_DMC_FIRMWARE (firmware));
	if (custom_meta_blob)
		if (g_bytes_get_size (custom_meta_blob) > 0)
			custom_meta_exist = TRUE;

	/* check custom meta flag */
	if (self->dock_id.custom_meta_data_flag != custom_meta_exist) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "custom metadata mismatch");
		return NULL;
	}
	return g_steal_pointer (&firmware);
}

static gboolean
fu_ccgx_dmc_device_attach (FuDevice *device, GError **error)
{
	FuCcgxDmcDevice *self = FU_CCGX_DMC_DEVICE (device);
	gboolean manual_replug;

	manual_replug = fu_device_has_private_flag (device, FU_CCGX_DMC_DEVICE_FLAG_HAS_MANUAL_REPLUG);

	if (fu_device_get_update_state (self) != FWUPD_UPDATE_STATE_SUCCESS)
		return TRUE;

	if (self->update_model == DMC_UPDATE_MODEL_DOWNLOAD_TRIGGER) {
		DmcTriggerCode trigger_code = DMC_TRIGGER_CODE_UPDATE_NOW;

		if (manual_replug)
			trigger_code = DMC_TRIGGER_CODE_UPDATE_ON_DISCONNECT;

		if (!fu_ccgx_dmc_device_send_download_trigger (self,
							       trigger_code,
							       error)) {
			g_prefix_error (error, "download trigger error: ");
			return FALSE;
		}
	} else if (self->update_model == DMC_UPDATE_MODEL_PENDING_RESET) {
		if (!fu_ccgx_dmc_device_send_sort_reset (self,
							 manual_replug,
							 error)) {
			g_prefix_error (error, "soft reset error: ");
			return FALSE;
		}
	} else {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "invalid update model = %u",
			     self->update_model);
		return FALSE;
	}

	if (manual_replug)
		return TRUE;

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_ccgx_dmc_device_setup (FuDevice *device, GError **error)
{
	FuCcgxDmcDevice *self = FU_CCGX_DMC_DEVICE (device);
	DmcDockStatus dock_status = {0};
	DmcDockIdentity dock_id = {0};
	guint32 version_raw = 0;
	g_autofree gchar *version = NULL;

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS (fu_ccgx_dmc_device_parent_class)->setup (device, error))
		return FALSE;

	/* get dock identity */
	if (!fu_ccgx_dmc_device_get_dock_id (self, &dock_id, error))
		return FALSE;

	/* store dock identity */
	if (!fu_memcpy_safe ((guint8 *) &self->dock_id, sizeof(DmcDockIdentity), 0x0, /* dst */
			     (guint8 *) &dock_id, sizeof(DmcDockIdentity), 0, /* src */
			     sizeof(DmcDockIdentity), error))
		return FALSE;

	/* get dock status */
	if (!fu_ccgx_dmc_device_get_dock_status (self, &dock_status, error))
		return FALSE;

	/* set composite version */
	version_raw = dock_status.composite_version;
	version = fu_common_version_from_uint32 (version_raw, FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_version (FU_DEVICE (self), version);
	fu_device_set_version_raw (FU_DEVICE (self), version_raw);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	return TRUE;
}

static gboolean
fu_ccgx_dmc_device_set_quirk_kv (FuDevice *device,
				 const gchar *key,
				 const gchar *value,
				 GError **error)
{
	FuCcgxDmcDevice *self = FU_CCGX_DMC_DEVICE (device);

	if (g_strcmp0 (key, "CcgxImageKind") == 0) {
		self->fw_image_type = fu_ccgx_fw_image_type_from_string (value);
		if (self->fw_image_type != FW_IMAGE_TYPE_UNKNOWN)
			return TRUE;
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid CcgxImageKind");
		return FALSE;
	}
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "no supported");
	return FALSE;
}

static void
fu_ccgx_dmc_device_init (FuCcgxDmcDevice *self)
{
	self->ep_intr_in = DMC_INTERRUPT_PIPE_ID;
	self->ep_bulk_out = DMC_BULK_PIPE_ID;
	fu_device_add_protocol (FU_DEVICE (self), "com.cypress.ccgx.dmc");
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	fu_device_add_internal_flag (FU_DEVICE (self), FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID);
	fu_device_register_private_flag (FU_DEVICE (self),
					 FU_CCGX_DMC_DEVICE_FLAG_HAS_MANUAL_REPLUG,
					 "has-manual-replug");
}

static void
fu_ccgx_dmc_device_class_init (FuCcgxDmcDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->to_string = fu_ccgx_dmc_device_to_string;
	klass_device->write_firmware = fu_ccgx_dmc_write_firmware;
	klass_device->prepare_firmware = fu_ccgx_dmc_device_prepare_firmware;
	klass_device->attach = fu_ccgx_dmc_device_attach;
	klass_device->setup = fu_ccgx_dmc_device_setup;
	klass_device->set_quirk_kv = fu_ccgx_dmc_device_set_quirk_kv;
}
