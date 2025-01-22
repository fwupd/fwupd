/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-elan-kbd-common.h"
#include "fu-elan-kbd-device.h"
#include "fu-elan-kbd-firmware.h"
#include "fu-elan-kbd-struct.h"

struct _FuElanKbdDevice {
	FuUsbDevice parent_instance;
	guint16 ver_spec;
	FuElanKbdDevStatus status;
	FuElanKbdBootCond1 bootcond1;
};

G_DEFINE_TYPE(FuElanKbdDevice, fu_elan_kbd_device, FU_TYPE_USB_DEVICE)

#define FU_ELAN_KBD_DEVICE_EP_CMD_SIZE	4
#define FU_ELAN_KBD_DEVICE_EP_DATA_SIZE 64

static void
fu_elan_kbd_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuElanKbdDevice *self = FU_ELAN_KBD_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "VerSpec", self->ver_spec);
	fwupd_codec_string_append(str,
				  idt,
				  "Status",
				  fu_elan_kbd_dev_status_to_string(self->status));
	fwupd_codec_string_append(str,
				  idt,
				  "BootCond1",
				  fu_elan_kbd_boot_cond1_to_string(self->bootcond1));
}

static gboolean
fu_elan_kbd_device_status_check(FuElanKbdDevice *self, GByteArray *buf, GError **error)
{
	FuElanKbdStatus status;
	g_autoptr(FuStructElanKbdCmdStatusRes) st_res = NULL;

	st_res = fu_struct_elan_kbd_cmd_status_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;
	status = fu_struct_elan_kbd_cmd_status_res_get_value(st_res);
	if (status == FU_ELAN_KBD_STATUS_BUSY) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "busy");
		return FALSE;
	}
	if (status == FU_ELAN_KBD_STATUS_FAIL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed, with unknown error");
		return FALSE;
	}
	if (status == FU_ELAN_KBD_STATUS_ERROR) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed: %s",
			    fu_elan_kbd_error_to_string(
				fu_struct_elan_kbd_cmd_status_res_get_error(st_res)));
		return FALSE;
	}
	return TRUE;
}

static GByteArray *
fu_elan_kbd_device_cmd(FuElanKbdDevice *self, GByteArray *buf, GError **error)
{
	g_autoptr(GByteArray) buf_out = g_byte_array_new();

	fu_dump_raw(G_LOG_DOMAIN, "CmdReq", buf->data, buf->len);
	if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
					      FU_ELAN_KBD_EP_CMD,
					      buf->data,
					      buf->len,
					      NULL,
					      1000,
					      NULL,
					      error)) {
		return NULL;
	}
	fu_byte_array_set_size(buf_out, FU_ELAN_KBD_DEVICE_EP_CMD_SIZE, 0x0);
	if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
					      FU_ELAN_KBD_EP_STATUS,
					      buf_out->data,
					      buf_out->len,
					      NULL,
					      1000,
					      NULL,
					      error)) {
		return NULL;
	}
	fu_dump_raw(G_LOG_DOMAIN, "CmdRes", buf_out->data, buf_out->len);
	return g_steal_pointer(&buf_out);
}

static gboolean
fu_elan_kbd_device_read_data(FuElanKbdDevice *self,
			     guint8 *buf,
			     gsize bufsz,
			     gsize offset,
			     GError **error)
{
	g_autoptr(GByteArray) buf_mut = g_byte_array_new();

	fu_byte_array_set_size(buf_mut, FU_ELAN_KBD_DEVICE_EP_DATA_SIZE, 0x0);
	if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
					      FU_ELAN_KBD_EP_DATA_IN,
					      buf_mut->data,
					      buf_mut->len,
					      NULL,
					      1000,
					      NULL,
					      error)) {
		return FALSE;
	}
	fu_dump_raw(G_LOG_DOMAIN, "DataRes", buf_mut->data, buf_mut->len);
	if (!fu_memcpy_safe(buf,
			    bufsz,
			    offset,
			    buf_mut->data,
			    buf_mut->len,
			    0x0,
			    buf_mut->len,
			    error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_elan_kbd_device_write_data(FuElanKbdDevice *self,
			      const guint8 *buf,
			      gsize bufsz,
			      gsize offset,
			      GError **error)
{
	g_autoptr(GByteArray) buf_mut = g_byte_array_new();

	fu_byte_array_set_size(buf_mut, FU_ELAN_KBD_DEVICE_EP_DATA_SIZE, 0x0);
	if (!fu_memcpy_safe(buf_mut->data,
			    buf_mut->len,
			    0x0,
			    buf,
			    bufsz,
			    offset,
			    buf_mut->len,
			    error))
		return FALSE;
	fu_dump_raw(G_LOG_DOMAIN, "DataReq", buf_mut->data, buf_mut->len);
	if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
					      FU_ELAN_KBD_EP_DATA_OUT,
					      buf_mut->data,
					      buf_mut->len,
					      NULL,
					      1000,
					      NULL,
					      error)) {
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elan_kbd_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuElanKbdDevice *self = FU_ELAN_KBD_DEVICE(device);
	g_autoptr(FuStructElanKbdSoftwareResetReq) st_req =
	    fu_struct_elan_kbd_software_reset_req_new();
	g_autoptr(GByteArray) buf = NULL;

	buf = fu_elan_kbd_device_cmd(self, st_req, error);
	if (buf == NULL)
		return FALSE;
	if (!fu_elan_kbd_device_status_check(self, buf, error))
		return FALSE;

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_elan_kbd_device_ensure_ver_spec(FuElanKbdDevice *self, GError **error)
{
	g_autoptr(FuStructElanKbdGetVerSpecReq) st_req = fu_struct_elan_kbd_get_ver_spec_req_new();
	g_autoptr(FuStructElanKbdGetVerSpecRes) st_res = NULL;
	g_autoptr(GByteArray) buf = NULL;

	buf = fu_elan_kbd_device_cmd(self, st_req, error);
	if (buf == NULL)
		return FALSE;
	st_res = fu_struct_elan_kbd_get_ver_spec_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;
	self->ver_spec = fu_struct_elan_kbd_get_ver_spec_res_get_value(st_res);
	return TRUE;
}

static gboolean
fu_elan_kbd_device_ensure_ver_fw(FuElanKbdDevice *self, GError **error)
{
	g_autoptr(FuStructElanKbdGetVerFwReq) st_req = fu_struct_elan_kbd_get_ver_fw_req_new();
	g_autoptr(FuStructElanKbdGetVerFwRes) st_res = NULL;
	g_autofree gchar *version = NULL;
	g_autoptr(GByteArray) buf = NULL;

	buf = fu_elan_kbd_device_cmd(self, st_req, error);
	if (buf == NULL)
		return FALSE;
	st_res = fu_struct_elan_kbd_get_ver_fw_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;
	version = g_strdup_printf("%04x", fu_struct_elan_kbd_get_ver_fw_res_get_value(st_res));
	fu_device_set_version_bootloader(FU_DEVICE(self), version);
	return TRUE;
}

static gboolean
fu_elan_kbd_device_ensure_dev_status(FuElanKbdDevice *self, GError **error)
{
	g_autoptr(FuStructElanKbdGetStatusReq) st_req = fu_struct_elan_kbd_get_status_req_new();
	g_autoptr(FuStructElanKbdGetStatusRes) st_res = NULL;
	g_autoptr(GByteArray) buf = NULL;

	buf = fu_elan_kbd_device_cmd(self, st_req, error);
	if (buf == NULL)
		return FALSE;
	st_res = fu_struct_elan_kbd_get_status_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;
	self->status = fu_struct_elan_kbd_get_status_res_get_value(st_res);
	return TRUE;
}

static gboolean
fu_elan_kbd_device_ensure_boot_cond1(FuElanKbdDevice *self, GError **error)
{
	g_autoptr(FuStructElanKbdBootConditionReq) st_req =
	    fu_struct_elan_kbd_boot_condition_req_new();
	g_autoptr(FuStructElanKbdBootConditionRes) st_res = NULL;
	g_autoptr(GByteArray) buf = NULL;

	buf = fu_elan_kbd_device_cmd(self, st_req, error);
	if (buf == NULL)
		return FALSE;
	st_res = fu_struct_elan_kbd_boot_condition_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;
	self->bootcond1 = fu_struct_elan_kbd_boot_condition_res_get_value(st_res);
	return TRUE;
}

static gboolean
fu_elan_kbd_device_abort(FuElanKbdDevice *self, GError **error)
{
	g_autoptr(FuStructElanKbdAbortReq) st_req = fu_struct_elan_kbd_abort_req_new();
	g_autoptr(GByteArray) buf = NULL;

	buf = fu_elan_kbd_device_cmd(self, st_req, error);
	if (buf == NULL)
		return FALSE;
	return fu_elan_kbd_device_status_check(self, buf, error);
}

static gboolean
fu_elan_kbd_device_setup(FuDevice *device, GError **error)
{
	FuElanKbdDevice *self = FU_ELAN_KBD_DEVICE(device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_elan_kbd_device_parent_class)->setup(device, error))
		return FALSE;

	/* abort any transactions in-flight */
	if (!fu_elan_kbd_device_abort(self, error))
		return FALSE;

	/* get properties from the device while open */
	if (!fu_elan_kbd_device_ensure_ver_spec(self, error))
		return FALSE;
	if (!fu_elan_kbd_device_ensure_ver_fw(self, error))
		return FALSE;
	if (!fu_elan_kbd_device_ensure_dev_status(self, error))
		return FALSE;
	if (!fu_elan_kbd_device_ensure_boot_cond1(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_elan_kbd_device_cmd_read_rom_finished(FuElanKbdDevice *self, guint8 csum, GError **error)
{
	g_autoptr(FuStructElanKbdReadRomFinishedReq) st_req =
	    fu_struct_elan_kbd_read_rom_finished_req_new();
	g_autoptr(GByteArray) buf = NULL;

	fu_struct_elan_kbd_read_rom_finished_req_set_csum(st_req, csum);
	buf = fu_elan_kbd_device_cmd(self, st_req, error);
	if (buf == NULL)
		return FALSE;
	return fu_elan_kbd_device_status_check(self, buf, error);
}

static GBytes *
fu_elan_kbd_device_cmd_read_rom(FuElanKbdDevice *self,
				guint16 addr,
				guint16 len,
				FuProgress *progress,
				GError **error)
{
	g_autoptr(FuStructElanKbdReadRomReq) st_req = fu_struct_elan_kbd_read_rom_req_new();
	g_autoptr(GByteArray) buf = NULL;
	g_autofree guint8 *data = g_malloc0(len);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, len / FU_ELAN_KBD_DEVICE_EP_DATA_SIZE);

	/* set up read */
	fu_struct_elan_kbd_read_rom_req_set_addr(st_req, addr);
	fu_struct_elan_kbd_read_rom_req_set_len(st_req, len);
	buf = fu_elan_kbd_device_cmd(self, st_req, error);
	if (buf == NULL)
		return NULL;
	if (!fu_elan_kbd_device_status_check(self, buf, error))
		return NULL;
	for (gsize offset = 0; offset < len; offset += FU_ELAN_KBD_DEVICE_EP_DATA_SIZE) {
		if (!fu_elan_kbd_device_read_data(self, data, len, offset, error)) {
			g_prefix_error(error, "failed at 0x%x: ", (guint)offset);
			return NULL;
		}
		fu_progress_step_done(progress);
	}
	if (!fu_elan_kbd_device_cmd_read_rom_finished(self, fu_sum16(data, len), error))
		return NULL;
	return g_bytes_new_take(g_steal_pointer(&data), len);
}

static gboolean
fu_elan_kbd_device_cmd_read_option_finished(FuElanKbdDevice *self, guint8 csum, GError **error)
{
	g_autoptr(FuStructElanKbdReadOptionFinishedReq) st_req =
	    fu_struct_elan_kbd_read_option_finished_req_new();
	g_autoptr(GByteArray) buf = NULL;

	fu_struct_elan_kbd_read_option_finished_req_set_csum(st_req, csum);
	buf = fu_elan_kbd_device_cmd(self, st_req, error);
	if (buf == NULL)
		return FALSE;
	return fu_elan_kbd_device_status_check(self, buf, error);
}

static GBytes *
fu_elan_kbd_device_cmd_read_option(FuElanKbdDevice *self, FuProgress *progress, GError **error)
{
	const gsize len = FU_STRUCT_ELAN_KBD_READ_OPTION_REQ_DEFAULT_LEN;
	g_autoptr(FuStructElanKbdReadOptionReq) st_req = fu_struct_elan_kbd_read_option_req_new();
	g_autoptr(GByteArray) buf = NULL;
	g_autofree guint8 *data = g_malloc0(len);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, len / FU_ELAN_KBD_DEVICE_EP_DATA_SIZE);

	/* set up read */
	buf = fu_elan_kbd_device_cmd(self, st_req, error);
	if (buf == NULL)
		return NULL;
	if (!fu_elan_kbd_device_status_check(self, buf, error))
		return NULL;
	for (gsize offset = 0; offset < len; offset += FU_ELAN_KBD_DEVICE_EP_DATA_SIZE) {
		if (!fu_elan_kbd_device_read_data(self, data, len, offset, error)) {
			g_prefix_error(error, "failed at 0x%x: ", (guint)offset);
			return NULL;
		}
		fu_progress_step_done(progress);
	}
	if (!fu_elan_kbd_device_cmd_read_option_finished(self, fu_sum16(data, len), error))
		return NULL;
	return g_bytes_new_take(g_steal_pointer(&data), len);
}

static GBytes *
fu_elan_kbd_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuElanKbdDevice *self = FU_ELAN_KBD_DEVICE(device);
	return fu_elan_kbd_device_cmd_read_rom(self,
					       0x0,
					       FU_ELAN_KBD_DEVICE_SIZE_ROM,
					       progress,
					       error);
}

static FuFirmware *
fu_elan_kbd_device_read_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuElanKbdDevice *self = FU_ELAN_KBD_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_elan_kbd_firmware_new();
	g_autoptr(FuFirmware) img_app = NULL;
	g_autoptr(FuFirmware) img_bootloader = NULL;
	g_autoptr(FuFirmware) img_option = NULL;
	g_autoptr(GBytes) blob_app = NULL;
	g_autoptr(GBytes) blob_bootloader = NULL;
	g_autoptr(GBytes) blob_option = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 25, "bootloader");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 74, "app");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "option");

	/* bootloader */
	blob_bootloader = fu_elan_kbd_device_cmd_read_rom(self,
							  FU_ELAN_KBD_DEVICE_ADDR_BOOT,
							  FU_ELAN_KBD_DEVICE_SIZE_BOOT,
							  fu_progress_get_child(progress),
							  error);
	if (blob_bootloader == NULL) {
		g_prefix_error(error, "failed to read ROM: ");
		return NULL;
	}
	img_bootloader = fu_firmware_new_from_bytes(blob_bootloader);
	fu_firmware_set_id(img_bootloader, "bootloader");
	fu_firmware_add_image(firmware, img_bootloader);
	fu_progress_step_done(progress);

	/* app */
	blob_app = fu_elan_kbd_device_cmd_read_rom(self,
						   FU_ELAN_KBD_DEVICE_ADDR_APP,
						   FU_ELAN_KBD_DEVICE_SIZE_APP,
						   fu_progress_get_child(progress),
						   error);
	if (blob_app == NULL) {
		g_prefix_error(error, "failed to read ROM: ");
		return NULL;
	}
	img_app = fu_firmware_new_from_bytes(blob_app);
	fu_firmware_set_idx(img_app, FU_ELAN_KBD_FIRMWARE_IDX_APP);
	fu_firmware_add_image(firmware, img_app);
	fu_progress_step_done(progress);

	/* option */
	blob_option =
	    fu_elan_kbd_device_cmd_read_option(self, fu_progress_get_child(progress), error);
	if (blob_option == NULL) {
		g_prefix_error(error, "failed to read ROM: ");
		return NULL;
	}
	img_option = fu_firmware_new_from_bytes(blob_option);
	fu_firmware_set_idx(img_option, FU_ELAN_KBD_FIRMWARE_IDX_OPTION);
	fu_firmware_add_image(firmware, img_option);
	fu_progress_step_done(progress);

	/* success */
	return g_steal_pointer(&firmware);
}

static gboolean
fu_elan_kbd_device_cmd_get_auth_lock(FuElanKbdDevice *self, guint8 *key, GError **error)
{
	g_autoptr(FuStructElanKbdGetAuthLockReq) st_req =
	    fu_struct_elan_kbd_get_auth_lock_req_new();
	g_autoptr(FuStructElanKbdGetAuthLockRes) st_res = NULL;
	g_autoptr(GByteArray) buf = NULL;

	buf = fu_elan_kbd_device_cmd(self, st_req, error);
	if (buf == NULL)
		return FALSE;
	st_res = fu_struct_elan_kbd_get_auth_lock_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;
	*key = fu_struct_elan_kbd_get_auth_lock_res_get_key(st_res) ^ 0x24;
	return TRUE;
}

static gboolean
fu_elan_kbd_device_cmd_set_auth_lock(FuElanKbdDevice *self, guint8 key, GError **error)
{
	g_autoptr(FuStructElanKbdSetAuthLockReq) st_req =
	    fu_struct_elan_kbd_set_auth_lock_req_new();
	g_autoptr(GByteArray) buf = NULL;

	fu_struct_elan_kbd_set_auth_lock_req_set_key(st_req, key ^ 0x58);
	buf = fu_elan_kbd_device_cmd(self, st_req, error);
	if (buf == NULL)
		return FALSE;
	return fu_elan_kbd_device_status_check(self, buf, error);
}

static gboolean
fu_elan_kbd_device_cmd_unlock(FuElanKbdDevice *self, GError **error)
{
	guint8 key = 0x0;
	if (!fu_elan_kbd_device_cmd_get_auth_lock(self, &key, error))
		return FALSE;
	return fu_elan_kbd_device_cmd_set_auth_lock(self, key, error);
}

static gboolean
fu_elan_kbd_device_cmd_entry_iap(FuElanKbdDevice *self, GError **error)
{
	g_autoptr(FuStructElanKbdEntryIapReq) st_req = fu_struct_elan_kbd_entry_iap_req_new();
	g_autoptr(GByteArray) buf = NULL;

	buf = fu_elan_kbd_device_cmd(self, st_req, error);
	if (buf == NULL)
		return FALSE;
	return fu_elan_kbd_device_status_check(self, buf, error);
}

static gboolean
fu_elan_kbd_device_cmd_finished_iap(FuElanKbdDevice *self, GError **error)
{
	g_autoptr(FuStructElanKbdFinishedIapReq) st_req = fu_struct_elan_kbd_finished_iap_req_new();
	g_autoptr(GByteArray) buf = NULL;

	buf = fu_elan_kbd_device_cmd(self, st_req, error);
	if (buf == NULL)
		return FALSE;
	return fu_elan_kbd_device_status_check(self, buf, error);
}

static gboolean
fu_elan_kbd_device_cmd_write_rom_finished(FuElanKbdDevice *self, guint8 csum, GError **error)
{
	g_autoptr(FuStructElanKbdWriteRomFinishedReq) st_req =
	    fu_struct_elan_kbd_write_rom_finished_req_new();
	g_autoptr(GByteArray) buf = NULL;

	fu_struct_elan_kbd_write_rom_finished_req_set_csum(st_req, csum);
	buf = fu_elan_kbd_device_cmd(self, st_req, error);
	if (buf == NULL)
		return FALSE;
	return fu_elan_kbd_device_status_check(self, buf, error);
}

static gboolean
fu_elan_kbd_device_cmd_write_rom(FuElanKbdDevice *self,
				 guint16 addr,
				 GBytes *blob,
				 FuProgress *progress,
				 GError **error)
{
	gsize bufsz = 0;
	const guint8 *data = g_bytes_get_data(blob, &bufsz);
	g_autoptr(FuStructElanKbdWriteRomReq) st_req = fu_struct_elan_kbd_write_rom_req_new();
	g_autoptr(GByteArray) buf = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, bufsz / FU_ELAN_KBD_DEVICE_EP_DATA_SIZE);

	/* set up write */
	fu_struct_elan_kbd_write_rom_req_set_addr(st_req, addr);
	fu_struct_elan_kbd_write_rom_req_set_len(st_req, bufsz);
	buf = fu_elan_kbd_device_cmd(self, st_req, error);
	if (buf == NULL)
		return FALSE;
	if (!fu_elan_kbd_device_status_check(self, buf, error))
		return FALSE;
	for (gsize offset = 0; offset < bufsz; offset += FU_ELAN_KBD_DEVICE_EP_DATA_SIZE) {
		if (!fu_elan_kbd_device_write_data(self, data, bufsz, offset, error)) {
			g_prefix_error(error, "failed at 0x%x: ", (guint)offset);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}
	return fu_elan_kbd_device_cmd_write_rom_finished(self, fu_sum16_bytes(blob), error);
}

static gboolean
fu_elan_kbd_device_write_firmware(FuDevice *device,
				  FuFirmware *firmware,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuElanKbdDevice *self = FU_ELAN_KBD_DEVICE(device);
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) blob_verify = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "unlock");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "entry-iap");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 96, "write-rom");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "finished");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 1, "verify");

	/* unlock */
	if (!fu_elan_kbd_device_cmd_unlock(self, error)) {
		g_prefix_error(error, "failed to unlock: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* enter IAP */
	if (!fu_elan_kbd_device_cmd_entry_iap(self, error)) {
		g_prefix_error(error, "failed to entry IAP: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* write */
	blob = fu_firmware_get_image_by_idx_bytes(firmware, FU_ELAN_KBD_FIRMWARE_IDX_APP, error);
	if (blob == NULL)
		return FALSE;
	if (!fu_elan_kbd_device_cmd_write_rom(self,
					      FU_ELAN_KBD_DEVICE_ADDR_APP,
					      blob,
					      fu_progress_get_child(progress),
					      error)) {
		g_prefix_error(error, "failed to write ROM: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* finish IAP */
	if (!fu_elan_kbd_device_cmd_finished_iap(self, error)) {
		g_prefix_error(error, "failed to finish IAP: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* verify */
	blob_verify = fu_elan_kbd_device_cmd_read_rom(self,
						      FU_ELAN_KBD_DEVICE_ADDR_APP,
						      FU_ELAN_KBD_DEVICE_SIZE_APP,
						      fu_progress_get_child(progress),
						      error);
	if (blob_verify == NULL) {
		g_prefix_error(error, "failed to read ROM: ");
		return FALSE;
	}
	if (!fu_bytes_compare(blob, blob_verify, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_elan_kbd_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 56, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 38, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 6, "reload");
}

static void
fu_elan_kbd_device_init(FuElanKbdDevice *self)
{
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_NUMBER);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_name(FU_DEVICE(self), "ELAN USB Keyboard");
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_ELAN_KBD_FIRMWARE);
	fu_device_set_firmware_size_min(FU_DEVICE(self), FU_ELAN_KBD_DEVICE_EP_DATA_SIZE);
	fu_device_set_firmware_size_max(FU_DEVICE(self),
					FU_ELAN_KBD_DEVICE_SIZE_ROM +
					    FU_ELAN_KBD_DEVICE_SIZE_OPTION);
	fu_device_add_protocol(FU_DEVICE(self), "com.elan.kbd");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_icon(FU_DEVICE(self), "input-keyboard");
	fu_usb_device_add_interface(FU_USB_DEVICE(self), 0x01);
	fu_usb_device_add_interface(FU_USB_DEVICE(self), 0x02);
	fu_usb_device_add_interface(FU_USB_DEVICE(self), 0x03);
}

static void
fu_elan_kbd_device_class_init(FuElanKbdDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_elan_kbd_device_to_string;
	device_class->setup = fu_elan_kbd_device_setup;
	device_class->attach = fu_elan_kbd_device_attach;
	device_class->write_firmware = fu_elan_kbd_device_write_firmware;
	device_class->read_firmware = fu_elan_kbd_device_read_firmware;
	device_class->dump_firmware = fu_elan_kbd_device_dump_firmware;
	device_class->set_progress = fu_elan_kbd_device_set_progress;
}
