/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-ccgx-dmc-device.h"
#include "fu-ccgx-dmc-devx-device.h"
#include "fu-ccgx-dmc-firmware.h"
#include "fu-ccgx-dmc-struct.h"

#define DMC_FW_WRITE_STATUS_RETRY_COUNT	   3
#define DMC_FW_WRITE_STATUS_RETRY_DELAY_MS 30

#define DMC_CONTROL_TRANSFER_DEFAULT_TIMEOUT 5000  /* control in/out pipe policy in ms */
#define DMC_BULK_OUT_PIPE_TIMEOUT	     2000  /* bulk out pipe policy in ms */
#define DMC_GET_REQUEST_TIMEOUT		     20000 /* bulk out pipe policy in ms */

#define DMC_INTERRUPT_PIPE_ID 0x82 /* interrupt ep for DMC Dock */
#define DMC_BULK_PIPE_ID      1	   /* USB bulk end point for DMC Dock */

/* maximum number of programmable devices expected to be connected in dock */
#define DMC_DOCK_MAX_DEV_COUNT 16

struct _FuCcgxDmcDevice {
	FuUsbDevice parent_instance;
	FuCcgxDmcDeviceStatus device_status;
	guint8 ep_intr_in;
	guint8 ep_bulk_out;
	FuCcgxDmcUpdateModel update_model;
	guint16 trigger_code; /* trigger code for update */
	guint8 custom_meta_flag;
};

/**
 * FU_CCGX_DMC_DEVICE_FLAG_HAS_MANUAL_REPLUG:
 *
 * Needs a manual replug from the end-user.
 */
#define FU_CCGX_DMC_DEVICE_FLAG_HAS_MANUAL_REPLUG (1 << 0)

G_DEFINE_TYPE(FuCcgxDmcDevice, fu_ccgx_dmc_device, FU_TYPE_USB_DEVICE)

static gboolean
fu_ccgx_dmc_device_ensure_dock_id(FuCcgxDmcDevice *self, GError **error)
{
	g_autoptr(GByteArray) st_id = fu_struct_ccgx_dmc_dock_identity_new();
	if (!g_usb_device_control_transfer(fu_usb_device_get_dev(FU_USB_DEVICE(self)),
					   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   FU_CCGX_DMC_RQT_CODE_DOCK_IDENTITY, /* request */
					   0,				       /* value */
					   0,				       /* index */
					   st_id->data,
					   st_id->len,
					   NULL, /* actual length */
					   DMC_CONTROL_TRANSFER_DEFAULT_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "get_dock_id error: ");
		return FALSE;
	}
	self->custom_meta_flag = fu_struct_ccgx_dmc_dock_identity_get_custom_meta_data_flag(st_id);
	return TRUE;
}

static gboolean
fu_ccgx_dmc_device_ensure_status(FuCcgxDmcDevice *self, GError **error)
{
	guint remove_delay = 20 * 1000; /* guard band */
	gsize bufsz;
	gsize offset = FU_STRUCT_CCGX_DMC_DOCK_STATUS_SIZE;
	g_autofree guint8 *buf = NULL;
	g_autoptr(GByteArray) st = fu_struct_ccgx_dmc_dock_status_new();

	/* read minimum status length */
	fu_byte_array_set_size(st, 32, 0x0);
	if (!g_usb_device_control_transfer(fu_usb_device_get_dev(FU_USB_DEVICE(self)),
					   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   FU_CCGX_DMC_RQT_CODE_DOCK_STATUS, /* request */
					   0,				     /* value */
					   0,				     /* index */
					   st->data,
					   st->len,
					   NULL, /* actual length */
					   DMC_CONTROL_TRANSFER_DEFAULT_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "get_dock_status min size error: ");
		return FALSE;
	}

	/* read full status length */
	bufsz = FU_STRUCT_CCGX_DMC_DOCK_STATUS_SIZE +
		(DMC_DOCK_MAX_DEV_COUNT * FU_STRUCT_CCGX_DMC_DEVX_STATUS_SIZE);
	buf = g_malloc0(bufsz);
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		/* copying the old buffer preserves compatibility with old emulation files */
		if (!fu_memcpy_safe(buf, bufsz, 0x0, st->data, st->len, 0x0, st->len, error))
			return FALSE;
	}
	if (!g_usb_device_control_transfer(fu_usb_device_get_dev(FU_USB_DEVICE(self)),
					   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   FU_CCGX_DMC_RQT_CODE_DOCK_STATUS, /* request */
					   0,				     /* value */
					   0,				     /* index */
					   buf,
					   bufsz,
					   NULL, /* actual length */
					   DMC_CONTROL_TRANSFER_DEFAULT_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "get_dock_status actual size error: ");
		return FALSE;
	}
	fu_dump_raw(G_LOG_DOMAIN, "DmcDockStatus", buf, bufsz);

	/* add devx children */
	for (guint i = 0; i < fu_struct_ccgx_dmc_dock_status_get_device_count(st); i++) {
		g_autoptr(FuCcgxDmcDevxDevice) devx =
		    fu_ccgx_dmc_devx_device_new(FU_DEVICE(self), buf, bufsz, offset, error);
		if (devx == NULL)
			return FALSE;
		remove_delay += fu_ccgx_dmc_devx_device_get_remove_delay(devx);
		fu_device_add_child(FU_DEVICE(self), FU_DEVICE(devx));
	}

	/* ensure the remove delay is set */
	if (fu_device_get_remove_delay(FU_DEVICE(self)) == 0) {
		g_debug("autosetting remove delay to %ums using DMC devx components", remove_delay);
		fu_device_set_remove_delay(FU_DEVICE(self), remove_delay);
	}

	/* success */
	self->device_status = fu_struct_ccgx_dmc_dock_status_get_device_status(st);
	fu_device_set_version_u32(FU_DEVICE(self),
				  fu_struct_ccgx_dmc_dock_status_get_composite_version(st));
	return TRUE;
}

static gboolean
fu_ccgx_dmc_device_send_reset_state_machine(FuCcgxDmcDevice *self, GError **error)
{
	if (!g_usb_device_control_transfer(fu_usb_device_get_dev(FU_USB_DEVICE(self)),
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   FU_CCGX_DMC_RQT_CODE_RESET_STATE_MACHINE, /* request */
					   0,					     /* value */
					   0,					     /* index */
					   0,					     /* data */
					   0,					     /* length */
					   NULL, /* actual length */
					   DMC_CONTROL_TRANSFER_DEFAULT_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "send reset state machine error: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_dmc_device_send_sort_reset(FuCcgxDmcDevice *self, gboolean reset_later, GError **error)
{
	if (!g_usb_device_control_transfer(fu_usb_device_get_dev(FU_USB_DEVICE(self)),
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   FU_CCGX_DMC_RQT_CODE_SOFT_RESET, /* request */
					   reset_later,			    /* value */
					   0,				    /* index */
					   0,				    /* data */
					   0,				    /* length */
					   NULL,			    /* actual length */
					   DMC_CONTROL_TRANSFER_DEFAULT_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "send reset error: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_dmc_device_send_start_upgrade(FuCcgxDmcDevice *self,
				      const guint8 *buf,
				      guint16 bufsz,
				      GError **error)
{
	guint16 value = 0;
	g_autofree guint8 *buf_mut = NULL;

	buf_mut = fu_memdup_safe(buf, bufsz, error);
	if (buf_mut == NULL)
		return FALSE;

	if (bufsz > 0)
		value = 1;

	if (bufsz > 0 && buf == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "invalid metadata, buffer is NULL but size = %d",
			    bufsz);
		return FALSE;
	}
	if (!g_usb_device_control_transfer(fu_usb_device_get_dev(FU_USB_DEVICE(self)),
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   FU_CCGX_DMC_RQT_CODE_UPGRADE_START, /* request */
					   value,			       /* value */
					   1,	    /* index, forced update */
					   buf_mut, /* data */
					   bufsz,   /* length */
					   NULL,    /* actual length */
					   DMC_CONTROL_TRANSFER_DEFAULT_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "send reset error: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_dmc_device_send_download_trigger(FuCcgxDmcDevice *self, guint16 trigger, GError **error)
{
	if (!g_usb_device_control_transfer(fu_usb_device_get_dev(FU_USB_DEVICE(self)),
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   FU_CCGX_DMC_RQT_CODE_TRIGGER, /* request */
					   trigger,			 /* value */
					   0,				 /* index */
					   0,				 /* data */
					   0,				 /* length */
					   NULL,			 /* actual length */
					   DMC_CONTROL_TRANSFER_DEFAULT_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "send download trigger error: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_dmc_device_send_fwct(FuCcgxDmcDevice *self,
			     const guint8 *buf,
			     guint16 bufsz,
			     GError **error)
{
	g_autofree guint8 *buf_mut = NULL;

	g_return_val_if_fail(buf != NULL, FALSE);

	buf_mut = fu_memdup_safe(buf, bufsz, error);
	if (buf_mut == NULL)
		return FALSE;
	if (!g_usb_device_control_transfer(fu_usb_device_get_dev(FU_USB_DEVICE(self)),
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   FU_CCGX_DMC_RQT_CODE_FWCT_WRITE, /* request */
					   0,				    /* value */
					   0,				    /* index */
					   buf_mut,			    /* data */
					   bufsz,			    /* length */
					   NULL,			    /* actual length */
					   DMC_CONTROL_TRANSFER_DEFAULT_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "send fwct error: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_dmc_device_read_intr_req(FuCcgxDmcDevice *self, GByteArray *intr_rqt, GError **error)
{
	guint8 rqt_opcode;
	g_autofree gchar *title = NULL;

	g_return_val_if_fail(intr_rqt != NULL, FALSE);

	if (!g_usb_device_interrupt_transfer(fu_usb_device_get_dev(FU_USB_DEVICE(self)),
					     self->ep_intr_in,
					     intr_rqt->data,
					     intr_rqt->len,
					     NULL,
					     DMC_GET_REQUEST_TIMEOUT,
					     NULL,
					     error)) {
		g_prefix_error(error, "read intr rqt error: ");
		return FALSE;
	}

	/* success */
	rqt_opcode = fu_struct_ccgx_dmc_int_rqt_get_opcode(intr_rqt);
	title = g_strdup_printf("DmcIntRqt-opcode=0x%02x[%s]",
				rqt_opcode,
				fu_ccgx_dmc_int_opcode_to_string(rqt_opcode));
	fu_dump_raw(G_LOG_DOMAIN,
		    title,
		    fu_struct_ccgx_dmc_int_rqt_get_data(intr_rqt, NULL),
		    MIN(fu_struct_ccgx_dmc_int_rqt_get_length(intr_rqt),
			FU_STRUCT_CCGX_DMC_INT_RQT_SIZE_DATA));
	return TRUE;
}

static gboolean
fu_ccgx_dmc_device_send_write_command(FuCcgxDmcDevice *self,
				      guint16 start_row,
				      guint16 num_of_row,
				      GError **error)
{
	if (!g_usb_device_control_transfer(fu_usb_device_get_dev(FU_USB_DEVICE(self)),
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   FU_CCGX_DMC_RQT_CODE_IMG_WRITE, /* request */
					   start_row,			   /* value */
					   num_of_row,			   /* index */
					   0,				   /* data */
					   0,				   /* length */
					   NULL,			   /* actual length */
					   DMC_CONTROL_TRANSFER_DEFAULT_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "send fwct error: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_dmc_device_send_row_data(FuCcgxDmcDevice *self,
				 const guint8 *row_buffer,
				 guint16 row_size,
				 GError **error)
{
	g_return_val_if_fail(row_buffer != NULL, FALSE);

	if (!g_usb_device_bulk_transfer(fu_usb_device_get_dev(FU_USB_DEVICE(self)),
					self->ep_bulk_out,
					(guint8 *)row_buffer,
					row_size,
					NULL,
					DMC_BULK_OUT_PIPE_TIMEOUT,
					NULL,
					error)) {
		g_prefix_error(error, "write row data error: ");
		return FALSE;
	}
	return TRUE;
}

static void
fu_ccgx_dmc_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuCcgxDmcDevice *self = FU_CCGX_DMC_DEVICE(device);
	fu_string_append(str,
			 idt,
			 "UpdateModel",
			 fu_ccgx_dmc_update_model_to_string(self->update_model));
	fu_string_append_kx(str, idt, "EpBulkOut", self->ep_bulk_out);
	fu_string_append_kx(str, idt, "EpIntrIn", self->ep_intr_in);
	fu_string_append_kx(str, idt, "TriggerCode", self->trigger_code);
	fu_string_append(str,
			 idt,
			 "DeviceStatus",
			 fu_ccgx_dmc_device_status_to_string(self->device_status));
}

static gboolean
fu_ccgx_dmc_get_image_write_status_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuCcgxDmcDevice *self = FU_CCGX_DMC_DEVICE(device);
	const guint8 *req_data;
	guint8 req_opcode;
	g_autoptr(GByteArray) dmc_int_req = fu_struct_ccgx_dmc_int_rqt_new();

	/* get interrupt request */
	if (!fu_ccgx_dmc_device_read_intr_req(self, dmc_int_req, error)) {
		g_prefix_error(error, "failed to read intr req in image write status: ");
		return FALSE;
	}

	/* check opcode for fw write */
	req_opcode = fu_struct_ccgx_dmc_int_rqt_get_opcode(dmc_int_req);
	if (req_opcode != FU_CCGX_DMC_INT_OPCODE_IMG_WRITE_STATUS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "invalid intr req opcode in image write status: %u [%s]",
			    req_opcode,
			    fu_ccgx_dmc_int_opcode_to_string(req_opcode));
		return FALSE;
	}

	/* retry if data[0] is 1 otherwise error */
	req_data = fu_struct_ccgx_dmc_int_rqt_get_data(dmc_int_req, NULL);
	if (req_data[0] != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "invalid intr req data in image write status = %u",
			    req_data[0]);
		fu_device_sleep(device, DMC_FW_WRITE_STATUS_RETRY_DELAY_MS);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_dmc_write_firmware_record(FuCcgxDmcDevice *self,
				  FuCcgxDmcFirmwareSegmentRecord *seg_rcd,
				  gsize *fw_data_written,
				  FuProgress *progress,
				  GError **error)
{
	GPtrArray *data_records = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 99, NULL);

	/* write start row and number of rows to a device */
	if (!fu_ccgx_dmc_device_send_write_command(self,
						   seg_rcd->start_row,
						   seg_rcd->num_rows,
						   error))
		return FALSE;
	fu_progress_step_done(progress);

	/* send data records */
	data_records = seg_rcd->data_records;
	for (guint32 data_index = 0; data_index < data_records->len; data_index++) {
		GBytes *data_rcd = g_ptr_array_index(data_records, data_index);
		const guint8 *row_buffer = NULL;
		gsize row_size = 0;

		/* write row data */
		row_buffer = g_bytes_get_data(data_rcd, &row_size);
		if (!fu_ccgx_dmc_device_send_row_data(self, row_buffer, (guint16)row_size, error))
			return FALSE;

		/* increase fw written size */
		*fw_data_written += row_size;

		/* get status */
		if (!fu_device_retry(FU_DEVICE(self),
				     fu_ccgx_dmc_get_image_write_status_cb,
				     DMC_FW_WRITE_STATUS_RETRY_COUNT,
				     NULL,
				     error))
			return FALSE;

		/* done */
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						data_index + 1,
						data_records->len);
	}
	fu_progress_step_done(progress);

	/* success */
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
	FuCcgxDmcDevice *self = FU_CCGX_DMC_DEVICE(device);
	GPtrArray *seg_records;

	g_return_val_if_fail(img_rcd != NULL, FALSE);
	g_return_val_if_fail(fw_data_written != NULL, FALSE);

	/* get segment records */
	seg_records = img_rcd->seg_records;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, seg_records->len);
	for (guint32 seg_index = 0; seg_index < seg_records->len; seg_index++) {
		FuCcgxDmcFirmwareSegmentRecord *seg_rcd = g_ptr_array_index(seg_records, seg_index);
		if (!fu_ccgx_dmc_write_firmware_record(self,
						       seg_rcd,
						       fw_data_written,
						       fu_progress_get_child(progress),
						       error))
			return FALSE;
		fu_progress_step_done(progress);
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
	FuCcgxDmcDevice *self = FU_CCGX_DMC_DEVICE(device);
	FuCcgxDmcFirmwareRecord *img_rcd = NULL;
	GBytes *custom_meta_blob;
	GBytes *fwct_blob;
	GPtrArray *image_records;
	const guint8 *custom_meta_data = NULL;
	const guint8 *fwct_buf = NULL;
	const guint8 *rqt_data = NULL;
	gsize custom_meta_bufsz = 0;
	gsize fwct_sz = 0;
	gsize fw_data_size = 0;
	gsize fw_data_written = 0;
	guint8 img_index = 0;
	guint8 rqt_opcode;
	g_autoptr(GByteArray) dmc_int_rqt = fu_struct_ccgx_dmc_int_rqt_new();

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "fwct");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, "img");

	/* get fwct record */
	fwct_blob = fu_ccgx_dmc_firmware_get_fwct_record(FU_CCGX_DMC_FIRMWARE(firmware));
	fwct_buf = g_bytes_get_data(fwct_blob, &fwct_sz);
	if (fwct_buf == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "invalid fwct data");
		return FALSE;
	}

	/* get custom meta record */
	custom_meta_blob =
	    fu_ccgx_dmc_firmware_get_custom_meta_record(FU_CCGX_DMC_FIRMWARE(firmware));
	if (custom_meta_blob != NULL)
		custom_meta_data = g_bytes_get_data(custom_meta_blob, &custom_meta_bufsz);

	/* reset */
	if (!fu_ccgx_dmc_device_send_reset_state_machine(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* start fw upgrade with custom metadata */
	if (!fu_ccgx_dmc_device_send_start_upgrade(self,
						   custom_meta_data,
						   custom_meta_bufsz,
						   error))
		return FALSE;

	/* send fwct data */
	if (!fu_ccgx_dmc_device_send_fwct(self, fwct_buf, fwct_sz, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* get total fw size */
	image_records = fu_ccgx_dmc_firmware_get_image_records(FU_CCGX_DMC_FIRMWARE(firmware));
	fw_data_size = fu_ccgx_dmc_firmware_get_fw_data_size(FU_CCGX_DMC_FIRMWARE(firmware));
	while (1) {
		/* get interrupt request */
		if (!fu_ccgx_dmc_device_read_intr_req(self, dmc_int_rqt, error))
			return FALSE;
		rqt_data = fu_struct_ccgx_dmc_int_rqt_get_data(dmc_int_rqt, NULL);

		/* fw upgrade request */
		rqt_opcode = fu_struct_ccgx_dmc_int_rqt_get_opcode(dmc_int_rqt);
		if (rqt_opcode != FU_CCGX_DMC_INT_OPCODE_FW_UPGRADE_RQT)
			break;
		img_index = rqt_data[0];
		if (img_index >= image_records->len) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "invalid image index %d, expected less than %u",
				    img_index,
				    image_records->len);
			return FALSE;
		}

		/* write image */
		g_debug("writing image index %u/%u", img_index, image_records->len - 1);
		img_rcd = g_ptr_array_index(image_records, img_index);
		if (!fu_ccgx_dmc_write_firmware_image(device,
						      img_rcd,
						      &fw_data_written,
						      fw_data_size,
						      fu_progress_get_child(progress),
						      error))
			return FALSE;
	}
	if (rqt_opcode != FU_CCGX_DMC_INT_OPCODE_FW_UPGRADE_STATUS) {
		if (rqt_opcode == FU_CCGX_DMC_INT_OPCODE_FWCT_ANALYSIS_STATUS) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "invalid fwct analysis failed with status 0x%02x[%s]",
				    rqt_data[0],
				    fu_ccgx_dmc_fwct_analysis_status_to_string(rqt_data[0]));
			return FALSE;
		}
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "invalid dmc intr req opcode 0x%02x[%s] with status 0x%02x",
			    rqt_opcode,
			    fu_ccgx_dmc_int_opcode_to_string(rqt_opcode),
			    rqt_data[0]);
		return FALSE;
	}

	self->update_model = FU_CCGX_DMC_UPDATE_MODEL_NONE;
	if (rqt_data[0] == FU_CCGX_DMC_DEVICE_STATUS_UPDATE_PHASE1_COMPLETE) {
		self->update_model = FU_CCGX_DMC_UPDATE_MODEL_DOWNLOAD_TRIGGER;
	} else if (rqt_data[0] == FU_CCGX_DMC_DEVICE_STATUS_FW_DOWNLOADED_UPDATE_PEND) {
		self->update_model = FU_CCGX_DMC_UPDATE_MODEL_PENDING_RESET;
	} else if (rqt_data[0] >= FU_CCGX_DMC_DEVICE_STATUS_PHASE2_UPDATE_FAIL_INVALID_FWCT) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "invalid status code = %u",
			    rqt_data[0]);
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_ccgx_dmc_device_prepare_firmware(FuDevice *device,
				    GBytes *fw,
				    FwupdInstallFlags flags,
				    GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_ccgx_dmc_firmware_new();
	FuCcgxDmcDevice *self = FU_CCGX_DMC_DEVICE(device);
	GBytes *custom_meta_blob = NULL;
	gboolean custom_meta_exist = FALSE;

	/* parse all images */
	if (!fu_firmware_parse(firmware, fw, flags, error))
		return NULL;

	/* get custom meta record */
	custom_meta_blob =
	    fu_ccgx_dmc_firmware_get_custom_meta_record(FU_CCGX_DMC_FIRMWARE(firmware));
	if (custom_meta_blob)
		if (g_bytes_get_size(custom_meta_blob) > 0)
			custom_meta_exist = TRUE;

	/* check custom meta flag */
	if (self->custom_meta_flag != custom_meta_exist) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "custom metadata mismatch");
		return NULL;
	}
	return g_steal_pointer(&firmware);
}

static gboolean
fu_ccgx_dmc_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuCcgxDmcDevice *self = FU_CCGX_DMC_DEVICE(device);
	gboolean manual_replug;

	manual_replug =
	    fu_device_has_private_flag(device, FU_CCGX_DMC_DEVICE_FLAG_HAS_MANUAL_REPLUG);

	if (fu_device_get_update_state(self) != FWUPD_UPDATE_STATE_SUCCESS)
		return TRUE;

	if (manual_replug) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
		fu_device_add_problem(device, FWUPD_DEVICE_PROBLEM_UPDATE_PENDING);
	} else {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	}

	if (self->update_model == FU_CCGX_DMC_UPDATE_MODEL_DOWNLOAD_TRIGGER) {
		if (self->trigger_code > 0) {
			if (!fu_ccgx_dmc_device_send_download_trigger(self,
								      self->trigger_code,
								      error)) {
				if (manual_replug == FALSE) {
					fu_device_remove_flag(device,
							      FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
				}
				g_prefix_error(error, "download trigger error: ");
				return FALSE;
			}
		}
	} else if (self->update_model == FU_CCGX_DMC_UPDATE_MODEL_PENDING_RESET) {
		if (!fu_ccgx_dmc_device_send_sort_reset(self, manual_replug, error)) {
			if (manual_replug == FALSE) {
				fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
			}
			g_prefix_error(error, "soft reset error: ");
			return FALSE;
		}
	}

	return TRUE;
}

static void
fu_ccgx_dmc_device_ensure_factory_version(FuCcgxDmcDevice *self)
{
	GPtrArray *children = fu_device_get_children(FU_DEVICE(self));
	for (guint i = 0; i < children->len; i++) {
		FuCcgxDmcDevxDevice *child = g_ptr_array_index(children, i);
		const guint8 *fw_version = fu_ccgx_dmc_devx_device_get_fw_version(child);
		FuCcgxDmcDevxDeviceType device_type =
		    fu_ccgx_dmc_devx_device_get_device_type(child);
		guint64 fwver_img1 = fu_memread_uint64(fw_version + 0x08, G_LITTLE_ENDIAN);
		guint64 fwver_img2 = fu_memread_uint64(fw_version + 0x10, G_LITTLE_ENDIAN);
		if (device_type == FU_CCGX_DMC_DEVX_DEVICE_TYPE_DMC && fwver_img1 == fwver_img2 &&
		    fwver_img1 != 0) {
			g_info("overriding version as device is in factory mode");
			fu_device_set_version_u32(FU_DEVICE(self), 0x1);
			return;
		}
	}
}

static gboolean
fu_ccgx_dmc_device_setup(FuDevice *device, GError **error)
{
	FuCcgxDmcDevice *self = FU_CCGX_DMC_DEVICE(device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_ccgx_dmc_device_parent_class)->setup(device, error))
		return FALSE;

	/* get dock identity */
	if (!fu_ccgx_dmc_device_ensure_dock_id(self, error))
		return FALSE;
	if (!fu_ccgx_dmc_device_ensure_status(self, error))
		return FALSE;

	/* use composite version, but also try to detect "factory mode" where the SPI has been
	 * imaged but has not been updated manually to the initial version */
	if (fu_device_get_version_raw(device) == 0)
		fu_ccgx_dmc_device_ensure_factory_version(self);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);

	if (self->custom_meta_flag > 0)
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	else
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);

	return TRUE;
}

static gboolean
fu_ccgx_dmc_device_set_quirk_kv(FuDevice *device,
				const gchar *key,
				const gchar *value,
				GError **error)
{
	FuCcgxDmcDevice *self = FU_CCGX_DMC_DEVICE(device);
	if (g_strcmp0(key, "CcgxDmcTriggerCode") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, error))
			return FALSE;
		self->trigger_code = tmp;
		return TRUE;
	}
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "no supported");
	return FALSE;
}

static void
fu_ccgx_dmc_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_NO_PROFILE); /* actually 0, 20, 0, 80! */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 75, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 25, "reload");
}

static void
fu_ccgx_dmc_device_init(FuCcgxDmcDevice *self)
{
	self->ep_intr_in = DMC_INTERRUPT_PIPE_ID;
	self->ep_bulk_out = DMC_BULK_PIPE_ID;
	self->trigger_code = 0x1;
	fu_device_add_protocol(FU_DEVICE(self), "com.cypress.ccgx.dmc");
	fu_device_add_protocol(FU_DEVICE(self), "com.infineon.ccgx.dmc");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_ONLY_WAIT_FOR_REPLUG);
	fu_usb_device_add_interface(FU_USB_DEVICE(self), 0x01);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_CCGX_DMC_DEVICE_FLAG_HAS_MANUAL_REPLUG,
					"has-manual-replug");
}

static void
fu_ccgx_dmc_device_class_init(FuCcgxDmcDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->to_string = fu_ccgx_dmc_device_to_string;
	klass_device->write_firmware = fu_ccgx_dmc_write_firmware;
	klass_device->prepare_firmware = fu_ccgx_dmc_device_prepare_firmware;
	klass_device->attach = fu_ccgx_dmc_device_attach;
	klass_device->setup = fu_ccgx_dmc_device_setup;
	klass_device->set_quirk_kv = fu_ccgx_dmc_device_set_quirk_kv;
	klass_device->set_progress = fu_ccgx_dmc_device_set_progress;
}
