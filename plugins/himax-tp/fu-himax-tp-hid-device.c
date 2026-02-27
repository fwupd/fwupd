/*
 * Copyright 2026 Himax Company, Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-himax-tp-firmware.h"
#include "fu-himax-tp-hid-device.h"
#include "fu-himax-tp-struct.h"

/* used to trigger the FW update sequence, not as a return code */
#define FU_HIMAX_TP_HID_DEVICE_CMD_UPDATE_MAIN 0x55
#define FU_HIMAX_TP_HID_DEVICE_CMD_UPDATE_BL   0x77

#define FLASH_ID_P25Q40SL 0x00136085

struct _FuHimaxTpHidDevice {
	FuHidrawDevice parent_instance;
	guint32 flash_id;
	FuStructHimaxTpHidInfo *st_info;
	GPtrArray *id_items; /* element-type FuHimaxTpHidDeviceIdItem */
};

G_DEFINE_TYPE(FuHimaxTpHidDevice, fu_himax_tp_hid_device, FU_TYPE_HIDRAW_DEVICE)

typedef struct {
	FuHimaxTpReportId report_id;
	gsize size;
} FuHimaxTpHidDeviceIdItem;

static void
fu_himax_tp_hid_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuHimaxTpHidDevice *self = FU_HIMAX_TP_HID_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "FlashId", self->flash_id);
	for (guint i = 0; i < self->id_items->len; i++) {
		FuHimaxTpHidDeviceIdItem *item = g_ptr_array_index(self->id_items, i);
		g_autofree gchar *title =
		    g_strdup_printf("HidIdSize[%s]",
				    fu_himax_tp_report_id_to_string(item->report_id));
		fwupd_codec_string_append_hex(str, idt, title, item->size);
	}
}

static gboolean
fu_himax_tp_hid_device_size_lookup(FuHimaxTpHidDevice *self,
				   FuHimaxTpReportId report_id,
				   gsize *value,
				   GError **error)
{
	for (guint i = 0; i < self->id_items->len; i++) {
		FuHimaxTpHidDeviceIdItem *item = g_ptr_array_index(self->id_items, i);
		if (item->report_id == report_id) {
			if (item->size == 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "invalid HID report size: %u",
					    report_id);
				return FALSE;
			}
			if (value != NULL)
				*value = item->size;
			return TRUE;
		}
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "unsupported HID report: %u",
		    report_id);
	return FALSE;
}

static gboolean
fu_himax_tp_hid_device_set_feature(FuHimaxTpHidDevice *self,
				   FuHimaxTpReportId report_id,
				   const guint8 *buf,
				   gsize bufsz,
				   FuProgress *progress, /* nullable */
				   GError **error)
{
	gsize unit_sz = 0;
	g_autoptr(GPtrArray) chunks = NULL;

	/* send in chunks */
	if (!fu_himax_tp_hid_device_size_lookup(self, report_id, &unit_sz, error))
		return FALSE;
	chunks = fu_chunk_array_new(buf, bufsz, 0, 0, unit_sz);

	/* progress */
	if (progress != NULL) {
		fu_progress_set_id(progress, G_STRLOC);
		fu_progress_set_steps(progress, chunks->len);
	}

	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		g_autoptr(GByteArray) buf_tmp = g_byte_array_new();

		fu_byte_array_append_uint8(buf_tmp, report_id);
		g_byte_array_append(buf_tmp, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
		fu_byte_array_set_size(buf_tmp, unit_sz + 1, 0x0);

		/* SetFeature */
		if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
						  buf_tmp->data,
						  buf_tmp->len,
						  FU_IOCTL_FLAG_NONE,
						  error))
			return FALSE;
		fu_device_sleep(FU_DEVICE(self), 1);
		if (progress != NULL)
			fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_himax_tp_hid_device_get_feature(FuHimaxTpHidDevice *self,
				   FuHimaxTpReportId report_id,
				   guint8 *buf,
				   gsize bufsz,
				   GError **error)
{
	g_autoptr(GByteArray) buf_tmp = g_byte_array_new();

	/* allocate buffer */
	fu_byte_array_set_size(buf_tmp, bufsz + 1, 0x0);
	if (!fu_memwrite_uint8_safe(buf_tmp->data, buf_tmp->len, 0x0, report_id, error))
		return FALSE;

	/* GetFeature */
	if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
					  buf_tmp->data,
					  buf_tmp->len,
					  FU_IOCTL_FLAG_NONE,
					  error)) {
		g_prefix_error(error, "GetFeature failed for id %u: ", report_id);
		return FALSE;
	}

	/* copy out */
	if (!fu_memcpy_safe(buf,
			    bufsz,
			    0, /* dst */
			    buf_tmp->data,
			    buf_tmp->len,
			    1, /* src */
			    bufsz,
			    error)) {
		g_prefix_error(error, "failed to copy data for id %u: ", report_id);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_himax_tp_hid_device_register_write(FuHimaxTpHidDevice *self,
				      guint32 reg_addr,
				      guint32 reg_value,
				      GError **error)
{
	g_autoptr(FuStructHimaxTpRegRw) st = fu_struct_himax_tp_reg_rw_new();

	fu_struct_himax_tp_reg_rw_set_rw_flag(st, 0x01);
	fu_struct_himax_tp_reg_rw_set_reg_addr(st, reg_addr);
	fu_struct_himax_tp_reg_rw_set_reg_value(st, reg_value);
	if (!fu_himax_tp_hid_device_set_feature(self,
						FU_HIMAX_TP_REPORT_ID_REG_RW,
						st->buf->data,
						st->buf->len,
						NULL, /* progress */
						error)) {
		g_prefix_error(error, "failed to write register 0x%08X: ", reg_addr);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_himax_tp_hid_device_register_read(FuHimaxTpHidDevice *self,
				     guint32 reg_addr,
				     guint32 *reg_value,
				     GError **error)
{
	g_autoptr(FuStructHimaxTpRegRw) st = fu_struct_himax_tp_reg_rw_new();

	fu_struct_himax_tp_reg_rw_set_reg_addr(st, reg_addr);
	if (!fu_himax_tp_hid_device_set_feature(self,
						FU_HIMAX_TP_REPORT_ID_REG_RW,
						st->buf->data,
						st->buf->len,
						NULL, /* progress */
						error)) {
		g_prefix_error(error, "failed to initiate register read for 0x%08X: ", reg_addr);
		return FALSE;
	}
	if (!fu_himax_tp_hid_device_get_feature(self,
						FU_HIMAX_TP_REPORT_ID_REG_RW,
						st->buf->data,
						st->buf->len,
						error)) {
		g_prefix_error(error, "failed to read register for 0x%08X: ", reg_addr);
		return FALSE;
	}

	/* success */
	*reg_value = fu_struct_himax_tp_reg_rw_get_reg_value(st);
	return TRUE;
}

static gboolean
fu_himax_tp_hid_device_get_size_by_id(FuHidReport *report,
				      guint8 report_id,
				      gsize *size,
				      GError **error)
{
	guint32 item_size_value;
	guint32 item_count_value;
	g_autoptr(FuFirmware) item_count = NULL;
	g_autoptr(FuFirmware) item_size = NULL;

	item_count = fu_firmware_get_image_by_id(FU_FIRMWARE(report), "report-count", error);
	if (item_count == NULL)
		return FALSE;
	item_size = fu_firmware_get_image_by_id(FU_FIRMWARE(report), "report-size", error);
	if (item_size == NULL)
		return FALSE;

	item_size_value = fu_hid_report_item_get_value(FU_HID_REPORT_ITEM(item_size));
	if (item_size_value % 8 != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "report-id %u has misaligned report-size",
			    report_id);
		return FALSE;
	}
	item_count_value = fu_hid_report_item_get_value(FU_HID_REPORT_ITEM(item_count));
	*size = (item_size_value / 8) * item_count_value;

	/* success */
	return TRUE;
}

typedef struct {
	FuHimaxTpRegisterAddr addr;
	guint32 value;
} FuHimaxTpHidDeviceRegisterWriteItem;

static gboolean
fu_himax_tp_hid_device_register_write_items(FuHimaxTpHidDevice *self,
					    FuHimaxTpHidDeviceRegisterWriteItem *sequence,
					    gsize seq_count,
					    GError **error)
{
	for (guint i = 0; i < seq_count; i++) {
		if (!fu_himax_tp_hid_device_register_write(self,
							   sequence[i].addr,
							   sequence[i].value,
							   error)) {
			g_prefix_error(
			    error,
			    "failed to write register sequence step %u: 0x%08X<-0x%08X: ",
			    i,
			    sequence[i].addr,
			    sequence[i].value);
			return FALSE;
		}
		fu_device_sleep(FU_DEVICE(self), 1);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_himax_tp_hid_device_ensure_flash_id(FuHimaxTpHidDevice *self, GError **error)
{
	guint32 reg_value = 0;
	FuHimaxTpHidDeviceRegisterWriteItem write_items[] = {
	    {.addr = FU_HIMAX_TP_REGISTER_ADDR_BLOCK_PROTECT_CMD1, .value = 0x00020780},
	    {.addr = FU_HIMAX_TP_REGISTER_ADDR_BLOCK_PROTECT_CMD2, .value = 0x42000002},
	    {.addr = FU_HIMAX_TP_REGISTER_ADDR_BLOCK_PROTECT_CMD3, .value = 0x0000009F},
	};

	if (!fu_himax_tp_hid_device_register_write_items(self,
							 write_items,
							 G_N_ELEMENTS(write_items),
							 error)) {
		g_prefix_error_literal(error, "cannot write flash id read sequence: ");
		return FALSE;
	}

	fu_device_sleep(FU_DEVICE(self), 1);

	if (!fu_himax_tp_hid_device_register_read(self,
						  FU_HIMAX_TP_REGISTER_ADDR_BLOCK_PROTECT_STATUS,
						  &reg_value,
						  error)) {
		g_prefix_error_literal(error, "cannot read flash id: ");
		return FALSE;
	}

	if (reg_value == 0 || reg_value >= 0x00FFFFFF) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid flash id read");
		return FALSE;
	}

	/* success */
	self->flash_id = (guint32)reg_value;
	return TRUE;
}

static guint8
fu_himax_tp_hid_device_get_block_protect_mask(FuHimaxTpHidDevice *self)
{
	if (self->flash_id == FLASH_ID_P25Q40SL)
		return 0x7C;
	return 0x00;
}

static guint
fu_himax_tp_hid_device_get_write_delay(FuHimaxTpHidDevice *self)
{
	if (self->flash_id == FLASH_ID_P25Q40SL)
		return 8;
	return 1;
}

static gboolean
fu_himax_tp_hid_device_get_flash_status(FuHimaxTpHidDevice *self,
					gboolean *flash_status,
					GError **error)
{
	guint8 block_protect_mask = fu_himax_tp_hid_device_get_block_protect_mask(self);
	guint32 reg_value = 0;
	FuHimaxTpHidDeviceRegisterWriteItem write_items[] = {
	    {.addr = FU_HIMAX_TP_REGISTER_ADDR_BLOCK_PROTECT_CMD1, .value = 0x00020780},
	    {.addr = FU_HIMAX_TP_REGISTER_ADDR_BLOCK_PROTECT_CMD2, .value = 0x42000000},
	    {.addr = FU_HIMAX_TP_REGISTER_ADDR_BLOCK_PROTECT_CMD3, .value = 0x00000005},
	};

	if (!fu_himax_tp_hid_device_register_write_items(self,
							 write_items,
							 G_N_ELEMENTS(write_items),
							 error)) {
		g_prefix_error_literal(error, "cannot write flash status get sequence: ");
		return FALSE;
	}

	if (!fu_himax_tp_hid_device_register_read(self,
						  FU_HIMAX_TP_REGISTER_ADDR_BLOCK_PROTECT_STATUS,
						  &reg_value,
						  error)) {
		g_prefix_error_literal(error, "cannot read flash status: ");
		return FALSE;
	}

	*flash_status = ((guint16)reg_value & (guint16)block_protect_mask) != 0;
	return TRUE;
}

static gboolean
fu_himax_tp_hid_device_switch_write_protect(FuHimaxTpHidDevice *self,
					    gboolean enable,
					    GError **error)
{
	guint32 reg_value = 0;

	if (!fu_himax_tp_hid_device_register_read(self,
						  FU_HIMAX_TP_REGISTER_ADDR_WRITE_PROTECT_PIN,
						  &reg_value,
						  error)) {
		g_prefix_error_literal(error, "cannot read write protect pin status: ");
		return FALSE;
	}
	if (enable)
		FU_BIT_SET(reg_value, 0);
	else
		FU_BIT_CLEAR(reg_value, 0);
	if (!fu_himax_tp_hid_device_register_write(self,
						   FU_HIMAX_TP_REGISTER_ADDR_WRITE_PROTECT_PIN,
						   reg_value,
						   error)) {
		g_prefix_error_literal(error, "cannot write write protect pin status: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_himax_tp_hid_device_switch_block_protect_retry_cb(FuDevice *device,
						     gpointer user_data,
						     GError **error)
{
	FuHimaxTpHidDevice *self = FU_HIMAX_TP_HID_DEVICE(device);
	guint32 reg_value = 0;

	if (!fu_himax_tp_hid_device_register_write(self,
						   FU_HIMAX_TP_REGISTER_ADDR_BLOCK_PROTECT_CMD3,
						   0x00000005,
						   error)) {
		g_prefix_error_literal(error, "cannot write flash block protect status: ");
		return FALSE;
	}
	if (!fu_himax_tp_hid_device_register_read(self,
						  FU_HIMAX_TP_REGISTER_ADDR_BLOCK_PROTECT_STATUS,
						  &reg_value,
						  error)) {
		g_prefix_error_literal(error, "cannot read flash block protect status: ");
		return FALSE;
	}
	if ((reg_value & 0x03) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "flash is still block protected");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_himax_tp_hid_device_switch_block_protect(FuHimaxTpHidDevice *self,
					    gboolean enable,
					    GError **error)
{
	guint32 reg_value = 0;
	FuHimaxTpHidDeviceRegisterWriteItem write_items[] = {
	    {.addr = FU_HIMAX_TP_REGISTER_ADDR_BLOCK_PROTECT_CMD1, .value = 0x00020780},
	    {.addr = FU_HIMAX_TP_REGISTER_ADDR_BLOCK_PROTECT_CMD2, .value = 0x47000000},
	    {.addr = FU_HIMAX_TP_REGISTER_ADDR_BLOCK_PROTECT_CMD3, .value = 0x00000006},
	    {.addr = FU_HIMAX_TP_REGISTER_ADDR_BLOCK_PROTECT_CMD2, .value = 0x41000000},
	    {.addr = FU_HIMAX_TP_REGISTER_ADDR_BLOCK_PROTECT_STATUS, .value = 0x00000000},
	    {.addr = FU_HIMAX_TP_REGISTER_ADDR_BLOCK_PROTECT_CMD3, .value = 0x00000001},
	};

	if (enable)
		write_items[4].value = (guint32)fu_himax_tp_hid_device_get_block_protect_mask(self);
	if (!fu_himax_tp_hid_device_register_write_items(self,
							 write_items,
							 G_N_ELEMENTS(write_items),
							 error)) {
		g_prefix_error_literal(error, "cannot write flash block protect switch sequence: ");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), fu_himax_tp_hid_device_get_write_delay(self));

	reg_value = 0x42000000;
	if (!fu_himax_tp_hid_device_register_write(self,
						   FU_HIMAX_TP_REGISTER_ADDR_BLOCK_PROTECT_CMD2,
						   reg_value,
						   error)) {
		g_prefix_error_literal(error, "cannot write flash block protect switch cmd: ");
		return FALSE;
	}
	fu_device_retry_set_delay(FU_DEVICE(self), 1);
	if (!fu_device_retry(FU_DEVICE(self),
			     fu_himax_tp_hid_device_switch_block_protect_retry_cb,
			     100,
			     NULL,
			     error)) {
		g_prefix_error_literal(error, "flash block protect switch timeout: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_himax_tp_hid_device_unlock_flash(FuHimaxTpHidDevice *self, GError **error)
{
	gboolean flash_locked = FALSE;

	/* already unlocked? */
	if (!fu_himax_tp_hid_device_get_flash_status(self, &flash_locked, error))
		return FALSE;
	if (!flash_locked)
		return TRUE;

	/* unlock */
	if (!fu_himax_tp_hid_device_switch_write_protect(self, FALSE, error)) {
		g_prefix_error_literal(error, "unable to disable write protect pin: ");
		return FALSE;
	}
	if (!fu_himax_tp_hid_device_switch_block_protect(self, FALSE, error)) {
		g_prefix_error_literal(error, "unable to disable block protect: ");
		return FALSE;
	}

	/* verify */
	if (!fu_himax_tp_hid_device_get_flash_status(self, &flash_locked, error))
		return FALSE;
	if (flash_locked) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "flash is still write protected");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_himax_tp_hid_device_wait_fw_update_handshaking_cb(FuDevice *device,
						     gpointer user_data,
						     GError **error)
{
	FuHimaxTpHidDevice *self = FU_HIMAX_TP_HID_DEVICE(device);
	FuHimaxTpFwStatus *status = (FuHimaxTpFwStatus *)user_data;
	guint8 status_tmp = 0;

	if (!fu_himax_tp_hid_device_get_feature(self,
						FU_HIMAX_TP_REPORT_ID_FW_UPDATE_HANDSHAKING,
						&status_tmp,
						sizeof(status_tmp),
						error))
		return FALSE;
	if (status_tmp != *status) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "status was %s [0x%02x] but expected %s [0x%02x]",
			    fu_himax_tp_fw_status_to_string(status_tmp),
			    status_tmp,
			    fu_himax_tp_fw_status_to_string(*status),
			    *status);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_himax_tp_hid_device_wait_fw_update_handshaking(FuHimaxTpHidDevice *self,
						  FuHimaxTpFwStatus status,
						  guint timeout_ms,
						  GError **error)
{
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_himax_tp_hid_device_wait_fw_update_handshaking_cb,
				  timeout_ms / 400,
				  400, /* ms */
				  &status,
				  error)) {
		g_prefix_error(error,
			       "failed to wait for %s [0x%x]: ",
			       fu_himax_tp_fw_status_to_string(status),
			       status);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_himax_tp_hid_device_write_unit(FuHimaxTpHidDevice *self,
				  FuStructHimaxTpHidFwUnit *st_unit,
				  GBytes *fw,
				  FuProgress *progress,
				  GError **error)
{
	gsize bufsz = 0;
	const guint8 *buf;
	g_autoptr(GByteArray) buf_slice = g_byte_array_new();

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 95, "main");

	/* wait for correct cmd */
	if (!fu_himax_tp_hid_device_wait_fw_update_handshaking(
		self,
		fu_struct_himax_tp_hid_fw_unit_get_cmd(st_unit),
		7000,
		error))
		return FALSE;
	fu_progress_step_done(progress);

	/* send chunks */
	buf = g_bytes_get_data(fw, &bufsz);
	if (!fu_byte_array_append_safe(
		buf_slice,
		buf,
		bufsz,
		fu_struct_himax_tp_hid_fw_unit_get_bin_start_offset(st_unit) * 1024,
		fu_struct_himax_tp_hid_fw_unit_get_bin_size(st_unit) * 1024,
		error))
		return FALSE;
	if (!fu_himax_tp_hid_device_set_feature(self,
						FU_HIMAX_TP_REPORT_ID_FW_UPDATE,
						buf_slice->data,
						buf_slice->len,
						fu_progress_get_child(progress),
						error)) {
		g_prefix_error_literal(error, "sending firmware data failed: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_himax_tp_hid_device_write_units(FuHimaxTpHidDevice *self,
				   GPtrArray *st_units,
				   GBytes *fw,
				   FuProgress *progress,
				   GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, st_units->len);

	for (guint i = 0; i < st_units->len; i++) {
		FuStructHimaxTpHidFwUnit *st_unit = g_ptr_array_index(st_units, i);
		if (!fu_himax_tp_hid_device_write_unit(self,
						       st_unit,
						       fw,
						       fu_progress_get_child(progress),
						       error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_himax_tp_hid_device_write_process(FuHimaxTpHidDevice *self,
				     GPtrArray *st_units,
				     guint8 start_cmd,
				     GBytes *fw,
				     FuProgress *progress,
				     GError **error)
{
	guint8 cmd = 0;
	g_autoptr(GError) error_local = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 5, "initial-handshake-unlock");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 85, "main");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 5, "commit");

	/* unlock flash if flash id exists, otherwise do nothing */
	if (!fu_himax_tp_hid_device_get_feature(self,
						FU_HIMAX_TP_REPORT_ID_FW_UPDATE_HANDSHAKING,
						&cmd,
						sizeof(cmd),
						error)) {
		g_prefix_error_literal(error, "failed to get initial handshake status: ");
		return FALSE;
	}
	if (!fu_himax_tp_hid_device_ensure_flash_id(self, &error_local)) {
		if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA)) {
			g_propagate_prefixed_error(error,
						   g_steal_pointer(&error_local),
						   "failed to get flash id: ");
			return FALSE;
		}
		g_debug("ignore invalid flash id: %s", error_local->message);
	} else {
		if (!fu_himax_tp_hid_device_unlock_flash(self, error))
			return FALSE;
	}
	fu_progress_step_done(progress);

	/* restart in bootloader mode and unlock again if locked before */
	if (!fu_himax_tp_hid_device_set_feature(self,
						FU_HIMAX_TP_REPORT_ID_FW_UPDATE_HANDSHAKING,
						&start_cmd,
						sizeof(start_cmd),
						fu_progress_get_child(progress),
						error)) {
		g_prefix_error_literal(error, "failed to send command to start firmware update: ");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), 100);
	if (self->flash_id > 0) {
		if (!fu_himax_tp_hid_device_unlock_flash(self, error))
			return FALSE;
	}
	fu_progress_step_done(progress);

	/* write each unit */
	if (!fu_himax_tp_hid_device_write_units(self,
						st_units,
						fw,
						fu_progress_get_child(progress),
						error))
		return FALSE;
	fu_progress_step_done(progress);

	/* wait for commit */
	if (!fu_himax_tp_hid_device_wait_fw_update_handshaking(self,
							       FU_HIMAX_TP_FW_STATUS_COMMIT,
							       3000,
							       error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), 500);
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_himax_tp_hid_device_bootloader_update(FuHimaxTpHidDevice *self,
					 GBytes *fw,
					 FuProgress *progress,
					 GError **error)
{
	g_autoptr(GPtrArray) st_units =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fu_struct_himax_tp_hid_fw_unit_unref);

	g_ptr_array_add(st_units, fu_struct_himax_tp_hid_info_get_bl_mapping(self->st_info));
	return fu_himax_tp_hid_device_write_process(self,
						    st_units,
						    FU_HIMAX_TP_HID_DEVICE_CMD_UPDATE_BL,
						    fw,
						    progress,
						    error);
}

static gboolean
fu_himax_tp_hid_device_main_update(FuHimaxTpHidDevice *self,
				   GBytes *fw,
				   FuProgress *progress,
				   GError **error)
{
	g_autoptr(GPtrArray) st_units =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fu_struct_himax_tp_hid_fw_unit_unref);

	/* update all with nonzero size */
	for (guint i = 0; i < FU_STRUCT_HIMAX_TP_HID_INFO_N_ELEMENTS_MAIN_MAPPING; i++) {
		g_autoptr(FuStructHimaxTpHidFwUnit) st_unit = NULL;
		st_unit = fu_struct_himax_tp_hid_info_get_main_mapping(self->st_info, i);
		if (fu_struct_himax_tp_hid_fw_unit_get_bin_size(st_unit) == 0)
			break;
		g_ptr_array_add(st_units, g_steal_pointer(&st_unit));
	}
	return fu_himax_tp_hid_device_write_process(self,
						    st_units,
						    FU_HIMAX_TP_HID_DEVICE_CMD_UPDATE_MAIN,
						    fw,
						    progress,
						    error);
}

static gboolean
fu_himax_tp_hid_device_probe(FuDevice *device, GError **error)
{
	/* check if interface valid */
	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "hidraw") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "is not correct subsystem=%s, expected hidraw",
			    fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_himax_tp_hid_device_setup(FuDevice *device, GError **error)
{
	FuHimaxTpHidDevice *self = FU_HIMAX_TP_HID_DEVICE(device);
	gsize cfg_sz = 0;
	g_autofree gchar *version_str = NULL;
	g_autoptr(FuHidDescriptor) hid_desc = NULL;
	g_autoptr(GByteArray) buf_hid = g_byte_array_new();
	g_autoptr(GPtrArray) reports = NULL;

	/* clear existing data */
	g_ptr_array_set_size(self->id_items, 0);

	hid_desc = fu_hidraw_device_parse_descriptor(FU_HIDRAW_DEVICE(self), error);
	if (hid_desc == NULL)
		return FALSE;
	reports = fu_firmware_get_images(FU_FIRMWARE(hid_desc));
	for (guint i = 0; i < reports->len; i++) {
		FuHidReport *report = g_ptr_array_index(reports, i);
		g_autoptr(FuFirmware) item = NULL;
		g_autofree FuHimaxTpHidDeviceIdItem *id_item = NULL;

		item = fu_firmware_get_image_by_id(FU_FIRMWARE(report), "report-id", NULL);
		if (item == NULL)
			continue;
		id_item = g_new0(FuHimaxTpHidDeviceIdItem, 1);
		id_item->report_id = fu_hid_report_item_get_value(FU_HID_REPORT_ITEM(item));
		if (id_item->report_id != FU_HIMAX_TP_REPORT_ID_CFG &&
		    id_item->report_id != FU_HIMAX_TP_REPORT_ID_REG_RW &&
		    id_item->report_id != FU_HIMAX_TP_REPORT_ID_FW_UPDATE &&
		    id_item->report_id != FU_HIMAX_TP_REPORT_ID_FW_UPDATE_HANDSHAKING &&
		    id_item->report_id != FU_HIMAX_TP_REPORT_ID_SELF_TEST)
			continue;
		if (!fu_himax_tp_hid_device_get_size_by_id(report,
							   id_item->report_id,
							   &id_item->size,
							   error))
			return FALSE;
		g_ptr_array_add(self->id_items, g_steal_pointer(&id_item));
	}

	if (!fu_himax_tp_hid_device_size_lookup(self, FU_HIMAX_TP_REPORT_ID_CFG, &cfg_sz, error))
		return FALSE;
	fu_byte_array_set_size(buf_hid, cfg_sz, 0x0);
	if (!fu_himax_tp_hid_device_get_feature(self,
						FU_HIMAX_TP_REPORT_ID_CFG,
						buf_hid->data,
						buf_hid->len,
						error)) {
		g_prefix_error_literal(error, "failed to get handshake status: ");
		return FALSE;
	}
	if (self->st_info != NULL)
		fu_struct_himax_tp_hid_info_unref(self->st_info);
	self->st_info = fu_struct_himax_tp_hid_info_parse(buf_hid->data, buf_hid->len, 0, error);
	if (self->st_info == NULL)
		return FALSE;

	/* define the extra instance IDs */
	fu_device_add_instance_u8(device,
				  "CID",
				  (fu_struct_himax_tp_hid_info_get_cid(self->st_info) & 0xFF00) >>
				      8);
	if (!fu_device_build_instance_id(device, error, "HIDRAW", "VEN", "DEV", "CID", NULL))
		return FALSE;

	/* version format : pid.cid (decimal) */
	version_str = g_strdup_printf("%u.%u",
				      fu_struct_himax_tp_hid_info_get_pid(self->st_info),
				      fu_struct_himax_tp_hid_info_get_cid(self->st_info));
	fu_device_set_version(device, version_str);

	/* success */
	return TRUE;
}

static gboolean
fu_himax_tp_hid_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuHimaxTpHidDevice *self = FU_HIMAX_TP_HID_DEVICE(device);
	const guint8 cmd = 0x01;

	/* check if reset function available */
	if (!fu_himax_tp_hid_device_size_lookup(self,
						FU_HIMAX_TP_REPORT_ID_SELF_TEST,
						NULL,
						NULL)) {
		fu_device_sleep(device, 500);
		return TRUE;
	}

	/* reset the device */
	if (!fu_himax_tp_hid_device_set_feature(self,
						FU_HIMAX_TP_REPORT_ID_SELF_TEST,
						&cmd,
						sizeof(cmd),
						progress,
						error)) {
		g_prefix_error_literal(error, "cannot reset device, and no fallback available: ");
		return FALSE;
	}

	/* success */
	fu_device_sleep(device, 500);
	return TRUE;
}

static gboolean
fu_himax_tp_hid_device_write_recovery(FuHimaxTpHidDevice *self,
				      GBytes *fw,
				      FuProgress *progress,
				      GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 15, "bootloader");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 85, "main");

	/* bootloader */
	if (!fu_himax_tp_hid_device_bootloader_update(self,
						      fw,
						      fu_progress_get_child(progress),
						      error)) {
		g_prefix_error_literal(error, "failed to update bootloader: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* main update */
	if (!fu_himax_tp_hid_device_main_update(self, fw, fu_progress_get_child(progress), error)) {
		g_prefix_error_literal(error, "failed to update main code: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_himax_tp_hid_device_write_firmware(FuDevice *device,
				      FuFirmware *firmware,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuHimaxTpHidDevice *self = FU_HIMAX_TP_HID_DEVICE(device);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error_local = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 85, "main");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 15, "bootloader");

	/* main update */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;
	if (!fu_himax_tp_hid_device_main_update(self,
						fw,
						fu_progress_get_child(progress),
						&error_local)) {
		if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
			g_propagate_prefixed_error(error,
						   g_steal_pointer(&error_local),
						   "firmware main update failed: ");
			return FALSE;
		}
		fu_progress_reset(progress);
		return fu_himax_tp_hid_device_write_recovery(self, fw, progress, error);
	}
	fu_progress_step_done(progress);

	/* bootloader */
	fu_device_sleep(device, 100);
	if (!fu_himax_tp_hid_device_bootloader_update(self,
						      fw,
						      fu_progress_get_child(progress),
						      error)) {
		g_prefix_error_literal(error, "failed to update firmware bootloader: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_himax_tp_hid_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 96, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 4, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static gboolean
fu_himax_tp_hid_device_check_firmware(FuDevice *device,
				      FuFirmware *firmware,
				      FuFirmwareParseFlags flags,
				      GError **error)
{
	FuHimaxTpHidDevice *self = FU_HIMAX_TP_HID_DEVICE(device);

	/* for coverage */
	if (flags & FU_FIRMWARE_PARSE_FLAG_IGNORE_VID_PID)
		return TRUE;

	/* check VID */
	if (fu_device_get_vid(device) !=
	    fu_himax_tp_firmware_get_vid(FU_HIMAX_TP_FIRMWARE(firmware))) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "firmware incompatible, VID is not valid");

		return FALSE;
	}

	/* check PID match */
	if (fu_device_get_pid(device) !=
	    fu_himax_tp_firmware_get_pid(FU_HIMAX_TP_FIRMWARE(firmware))) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "firmware incompatible, PID is not valid");
		return FALSE;
	}

	/* check CID high byte match */
	if (((fu_struct_himax_tp_hid_info_get_cid(self->st_info) & 0xFF00) >> 8) !=
	    (fu_himax_tp_firmware_get_cid(FU_HIMAX_TP_FIRMWARE(firmware)) >> 8)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "firmware incompatible, CID is not valid");

		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_himax_tp_hid_device_finalize(GObject *object)
{
	FuHimaxTpHidDevice *self = FU_HIMAX_TP_HID_DEVICE(object);
	if (self->st_info != NULL)
		fu_struct_himax_tp_hid_info_unref(self->st_info);
	g_ptr_array_unref(self->id_items);
	G_OBJECT_CLASS(fu_himax_tp_hid_device_parent_class)->finalize(object);
}

static void
fu_himax_tp_hid_device_init(FuHimaxTpHidDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_set_name(FU_DEVICE(self), "Touchscreen");
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_VIDEO_DISPLAY);
	fu_device_add_protocol(FU_DEVICE(self), "tw.com.himax.tp");
	fu_device_set_firmware_size_min(FU_DEVICE(self), 0x3FC00);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_HIMAX_TP_FIRMWARE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_NONBLOCK);
	self->id_items = g_ptr_array_new_with_free_func(g_free);
}

static void
fu_himax_tp_hid_device_class_init(FuHimaxTpHidDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_himax_tp_hid_device_finalize;
	device_class->to_string = fu_himax_tp_hid_device_to_string;
	device_class->attach = fu_himax_tp_hid_device_attach;
	device_class->setup = fu_himax_tp_hid_device_setup;
	device_class->reload = fu_himax_tp_hid_device_setup;
	device_class->write_firmware = fu_himax_tp_hid_device_write_firmware;
	device_class->check_firmware = fu_himax_tp_hid_device_check_firmware;
	device_class->probe = fu_himax_tp_hid_device_probe;
	device_class->set_progress = fu_himax_tp_hid_device_set_progress;
}
