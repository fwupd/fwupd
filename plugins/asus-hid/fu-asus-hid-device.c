/*
 * Copyright 2024 Mario Limonciello <superm1@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-asus-hid-child-device.h"
#include "fu-asus-hid-device.h"
#include "fu-asus-hid-firmware.h"
#include "fu-asus-hid-struct.h"

struct _FuAsusHidDevice {
	FuHidrawDevice parent_instance;
	guint8 num_mcu;
	gulong child_added_id;
};

G_DEFINE_TYPE(FuAsusHidDevice, fu_asus_hid_device, FU_TYPE_HIDRAW_DEVICE)

#define FU_ASUS_HID_DEVICE_TIMEOUT	    200 /* ms */
#define FU_ASUS_HID_DEVICE_PRE_UPDATE_DELAY 50	/* ms */
#define FU_ASUS_HID_DEVICE_RETRIES	    50
#define FU_ASUS_HID_DEVICE_ERASE_DELAY	    200 /* ms */
#define FU_ASUS_HID_DEVICE_BLOCK_START_DELAY 5	/* ms */
#define FU_ASUS_HID_DEVICE_CHUNK_DELAY	    2	/* ms */
#define FU_ASUS_HID_DEVICE_COMMIT_DELAY	    50	/* ms */
#define FU_ASUS_HID_DEVICE_BLOCK_SIZE	    0x400

static gboolean
fu_asus_hid_device_transfer_feature(FuAsusHidDevice *self,
				    GByteArray *req,
				    GByteArray *res,
				    guint8 report,
				    GError **error)
{
	if (req != NULL) {
		if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
						  req->data,
						  req->len,
						  FU_IOCTL_FLAG_NONE,
						  error)) {
			g_prefix_error_literal(error, "failed to send packet: ");
			return FALSE;
		}
	}
	if (res != NULL) {
		if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
						  res->data,
						  res->len,
						  FU_IOCTL_FLAG_NONE,
						  error)) {
			g_prefix_error_literal(error, "failed to receive packet: ");
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
fu_asus_hid_device_init_seq(FuAsusHidDevice *self, GError **error)
{
	g_autoptr(FuStructAsusHidCommand) st = fu_struct_asus_hid_command_new();

	fu_struct_asus_hid_command_set_cmd(st, FU_ASUS_HID_COMMAND_INIT_SEQUENCE);

	if (!fu_asus_hid_device_transfer_feature(self,
						 st->buf,
						 NULL,
						 FU_ASUS_HID_REPORT_ID_INFO,
						 error)) {
		g_prefix_error_literal(error, "failed to initialize device: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_asus_hid_device_ensure_manufacturer_cb(FuDevice *device, gpointer user_data, GError **error)
{
	g_autofree gchar *data = NULL;
	g_autoptr(FuStructAsusManCommand) st = fu_struct_asus_man_command_new();
	g_autoptr(FuStructAsusManResult) st_result = fu_struct_asus_man_result_new();

	if (!fu_asus_hid_device_transfer_feature(FU_ASUS_HID_DEVICE(device),
						 st->buf,
						 st_result->buf,
						 FU_ASUS_HID_REPORT_ID_INFO,
						 error))
		return FALSE;
	data = fu_struct_asus_man_result_get_data(st_result);
	if (g_strcmp0(data, "ASUS Tech.Inc.") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "device did not echo the manufacturer, got %s",
			    data);
		return FALSE;
	}

	/* success */
	return TRUE;
}

/* the query returns a byte that the MCU expects to see masked and sent straight
 * back to it, and it refuses to leave runtime mode if that does not happen */
static gboolean
fu_asus_hid_device_pre_update_pair(FuAsusHidDevice *self,
				   FuAsusHidCommand cmd_query,
				   FuAsusHidCommand cmd_apply,
				   guint8 mask,
				   GError **error)
{
	const guint8 *buf;
	gsize bufsz = 0;
	guint8 value = 0;
	g_autoptr(FuStructAsusHidPreUpdateCommand) st = fu_struct_asus_hid_pre_update_command_new();
	g_autoptr(FuStructAsusHidResult) st_result = fu_struct_asus_hid_result_new();

	fu_struct_asus_hid_pre_update_command_set_cmd(st, cmd_query);
	fu_struct_asus_hid_pre_update_command_set_length(st, 0x1);
	if (!fu_asus_hid_device_transfer_feature(self,
						 st->buf,
						 st_result->buf,
						 FU_ASUS_HID_REPORT_ID_INFO,
						 error))
		return FALSE;
	buf = fu_struct_asus_hid_result_get_data(st_result, &bufsz);
	if (!fu_memread_uint8_safe(buf, bufsz, 0x5, &value, error))
		return FALSE;
	value |= mask;

	fu_struct_asus_hid_pre_update_command_set_cmd(st, cmd_apply);
	fu_struct_asus_hid_pre_update_command_set_length(st, 0x1);
	if (!fu_struct_asus_hid_pre_update_command_set_data(st, &value, sizeof(value), error))
		return FALSE;
	if (!fu_asus_hid_device_transfer_feature(self,
						 st->buf,
						 NULL,
						 FU_ASUS_HID_REPORT_ID_INFO,
						 error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), FU_ASUS_HID_DEVICE_PRE_UPDATE_DELAY);

	/* success */
	return TRUE;
}

static gboolean
fu_asus_hid_device_read_info(FuAsusHidDevice *self, FuAsusHidCommand cmd, GError **error)
{
	g_autoptr(FuStructAsusHidPreUpdateCommand) st = fu_struct_asus_hid_pre_update_command_new();
	g_autoptr(FuStructAsusHidResult) st_result = fu_struct_asus_hid_result_new();

	fu_struct_asus_hid_pre_update_command_set_cmd(st, cmd);
	fu_struct_asus_hid_pre_update_command_set_length(st, FU_STRUCT_ASUS_HID_RESULT_SIZE);
	return fu_asus_hid_device_transfer_feature(self,
						   st->buf,
						   st_result->buf,
						   FU_ASUS_HID_REPORT_ID_INFO,
						   error);
}

static void
fu_asus_hid_device_child_added_cb(FuDevice *device, FuDevice *child, gpointer user_data)
{
	g_debug("child %s added to parent %s updating proxy",
		fu_device_get_id(child),
		fu_device_get_id(device));
	fu_device_set_proxy(child, device);
}

static gboolean
fu_asus_hid_device_validate_descriptor(FuAsusHidDevice *self, GError **error)
{
	g_autoptr(FuHidDescriptor) descriptor = NULL;
	g_autoptr(FuHidReport) report = NULL;

	descriptor = fu_hidraw_device_parse_descriptor(FU_HIDRAW_DEVICE(self), error);
	if (descriptor == NULL)
		return FALSE;
	report = fu_hid_descriptor_find_report(descriptor,
					       error,
					       "usage-page",
					       0xFF31,
					       "usage",
					       0x76,
					       "collection",
					       0x01,
					       NULL);
	if (report == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_asus_hid_device_probe(FuDevice *device, GError **error)
{
	FuAsusHidDevice *self = FU_ASUS_HID_DEVICE(device);

	for (guint i = 0; i < self->num_mcu; i++) {
		g_autoptr(FuDevice) dev_tmp = fu_asus_hid_child_device_new(device, i);
		fu_device_add_child(device, dev_tmp);
	}

	return TRUE;
}

/* the vendor tool flashes the part as a whole rather than per-microcontroller,
 * so the version of the primary one is what gets compared against the release */
static gboolean
fu_asus_hid_device_ensure_version(FuAsusHidDevice *self, GError **error)
{
	g_autofree gchar *version = NULL;
	g_autoptr(FuStructAsusHidCommand) st = fu_struct_asus_hid_command_new();
	g_autoptr(FuStructAsusHidDesc) st_desc = NULL;
	g_autoptr(FuStructAsusHidFwInfo) st_result = fu_struct_asus_hid_fw_info_new();

	fu_struct_asus_hid_command_set_cmd(st, FU_ASUS_HID_COMMAND_FW_VERSION);
	fu_struct_asus_hid_command_set_length(st, FU_STRUCT_ASUS_HID_RESULT_SIZE);
	if (!fu_asus_hid_device_transfer_feature(self,
						 st->buf,
						 st_result->buf,
						 FU_ASUS_HID_REPORT_ID_INFO,
						 error))
		return FALSE;
	st_desc = fu_struct_asus_hid_fw_info_get_description(st_result);
	version = fu_struct_asus_hid_desc_get_version(st_desc);
	fu_device_set_version(FU_DEVICE(self), version);

	/* success */
	return TRUE;
}

static gboolean
fu_asus_hid_device_setup(FuDevice *device, GError **error)
{
	FuAsusHidDevice *self = FU_ASUS_HID_DEVICE(device);

	/* bootloader mode won't know about children */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	if (!fu_asus_hid_device_validate_descriptor(self, error))
		return FALSE;

	if (!fu_asus_hid_device_init_seq(self, error))
		return FALSE;

	if (!fu_asus_hid_device_ensure_version(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_asus_hid_device_reload(FuDevice *device, GError **error)
{
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;
	return fu_asus_hid_device_ensure_version(FU_ASUS_HID_DEVICE(device), error);
}

static gboolean
fu_asus_hid_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	g_autoptr(FuStructAsusFlashReset) st = fu_struct_asus_flash_reset_new();

	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	if (!fu_asus_hid_device_transfer_feature(FU_ASUS_HID_DEVICE(device),
						 st->buf,
						 NULL,
						 FU_ASUS_HID_REPORT_ID_FLASHING,
						 error)) {
		g_prefix_error_literal(error, "failed to reset device: ");
		return FALSE;
	}

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	return TRUE;
}

static gboolean
fu_asus_hid_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuAsusHidDevice *self = FU_ASUS_HID_DEVICE(device);
	g_autoptr(FuStructAsusHidPreUpdateCommand) st = fu_struct_asus_hid_pre_update_command_new();

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	/* everything below is ignored until the MCU has echoed this back */
	if (!fu_device_retry_full(device,
				  fu_asus_hid_device_ensure_manufacturer_cb,
				  FU_ASUS_HID_DEVICE_RETRIES,
				  FU_ASUS_HID_DEVICE_PRE_UPDATE_DELAY,
				  NULL,
				  error))
		return FALSE;

	/* the vendor tool reads these back before starting, and the MCU stays in
	 * runtime mode if they are skipped */
	if (!fu_asus_hid_device_read_info(self, FU_ASUS_HID_COMMAND_FW_VERSION, error))
		return FALSE;
	if (!fu_asus_hid_device_read_info(self, FU_ASUS_HID_COMMAND_GET_FW_CONFIG, error))
		return FALSE;
	if (!fu_asus_hid_device_read_info(self, FU_ASUS_HID_COMMAND_PRE_UPDATE, error))
		return FALSE;

	if (!fu_asus_hid_device_pre_update_pair(self,
						FU_ASUS_HID_COMMAND_PRE_UPDATE2,
						FU_ASUS_HID_COMMAND_PRE_UPDATE3,
						0x0F,
						error))
		return FALSE;
	if (!fu_asus_hid_device_pre_update_pair(self,
						FU_ASUS_HID_COMMAND_PRE_UPDATE4,
						FU_ASUS_HID_COMMAND_PRE_UPDATE5,
						0x02,
						error))
		return FALSE;

	/* this is what actually switches to the bootloader, and it is not
	 * acknowledged -- the device drops off the bus straight away and takes
	 * several seconds to enumerate again with the counterpart GUID */
	fu_struct_asus_hid_pre_update_command_set_cmd(st, FU_ASUS_HID_COMMAND_PRE_UPDATE6);
	fu_struct_asus_hid_pre_update_command_set_length(st, 0x0);
	if (!fu_asus_hid_device_transfer_feature(self,
						 st->buf,
						 NULL,
						 FU_ASUS_HID_REPORT_ID_INFO,
						 error))
		return FALSE;

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	return TRUE;
}

static GBytes *
fu_asus_hid_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuAsusHidDevice *self = FU_ASUS_HID_DEVICE(device);
	g_autoptr(GByteArray) fw = g_byte_array_new();
	g_autoptr(GPtrArray) blocks = NULL;

	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "device is not in bootloader mode");
		return NULL;
	}

	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);
	fu_byte_array_set_size(fw, fu_device_get_firmware_size_max(device), 0x00);
	blocks = fu_chunk_array_mutable_new(fw->data,
					    fw->len,
					    0x0,
					    4 * FU_KB,
					    FU_STRUCT_ASUS_READ_FLASH_COMMAND_SIZE_DATA,
					    error);
	if (blocks == NULL)
		return NULL;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, blocks->len);
	for (guint i = 0, offset = 0; i < blocks->len; i++) {
		FuChunk *chk = g_ptr_array_index(blocks, i);
		const guint8 *buf;
		gsize bufsz = 0;
		g_autoptr(FuStructAsusReadFlashCommand) st =
		    fu_struct_asus_read_flash_command_new();
		g_autoptr(FuStructAsusReadFlashCommand) st_result =
		    fu_struct_asus_read_flash_command_new();

		fu_struct_asus_read_flash_command_set_offset(st, offset);
		fu_struct_asus_read_flash_command_set_datasz(st, fu_chunk_get_data_sz(chk));

		if (!fu_asus_hid_device_transfer_feature(self,
							 st->buf,
							 st_result->buf,
							 FU_ASUS_HID_REPORT_ID_FLASHING,
							 error))
			return NULL;
		buf = fu_struct_asus_read_flash_command_get_data(st_result, &bufsz);
		if (!fu_memcpy_safe(fu_chunk_get_data_out(chk),
				    fu_chunk_get_data_sz(chk),
				    0x0,
				    buf,
				    bufsz,
				    0x0,
				    fu_struct_asus_read_flash_command_get_datasz(st_result),
				    error))
			return NULL;
		offset += fu_chunk_get_data_sz(chk);
		fu_progress_step_done(progress);
	}
	return g_bytes_new(fw->data, fw->len);
}

static gboolean
fu_asus_hid_device_write_block(FuAsusHidDevice *self,
			       guint32 address,
			       const guint8 *buf,
			       gsize bufsz,
			       GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(FuStructAsusCommitFlashCommand) st_commit =
	    fu_struct_asus_commit_flash_command_new();
	g_autoptr(FuStructAsusFlashBlockStart) st_start = fu_struct_asus_flash_block_start_new();
	g_autoptr(GBytes) blob = g_bytes_new_static(buf, bufsz);

	if (!fu_asus_hid_device_transfer_feature(self,
						 st_start->buf,
						 NULL,
						 FU_ASUS_HID_REPORT_ID_FLASHING,
						 error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), FU_ASUS_HID_DEVICE_BLOCK_START_DELAY);

	/* the block is filled a fragment at a time before being committed */
	chunks = fu_chunk_array_new_from_bytes(blob,
					       0x0,
					       0x0,
					       FU_STRUCT_ASUS_WRITE_FLASH_COMMAND_SIZE_DATA,
					       error);
	if (chunks == NULL)
		return FALSE;
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(FuStructAsusWriteFlashCommand) st =
		    fu_struct_asus_write_flash_command_new();

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		fu_struct_asus_write_flash_command_set_offset(st, fu_chunk_get_address(chk));
		fu_struct_asus_write_flash_command_set_datasz(st, fu_chunk_get_data_sz(chk));
		if (!fu_struct_asus_write_flash_command_set_data(st,
								 fu_chunk_get_data(chk),
								 fu_chunk_get_data_sz(chk),
								 error))
			return FALSE;
		if (!fu_asus_hid_device_transfer_feature(self,
							 st->buf,
							 NULL,
							 FU_ASUS_HID_REPORT_ID_FLASHING,
							 error))
			return FALSE;
		fu_device_sleep(FU_DEVICE(self), FU_ASUS_HID_DEVICE_CHUNK_DELAY);
	}

	fu_struct_asus_commit_flash_command_set_offset(st_commit, address);
	fu_struct_asus_commit_flash_command_set_datasz(st_commit, bufsz);
	if (!fu_asus_hid_device_transfer_feature(self,
						 st_commit->buf,
						 NULL,
						 FU_ASUS_HID_REPORT_ID_FLASHING,
						 error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), FU_ASUS_HID_DEVICE_COMMIT_DELAY);

	/* success */
	return TRUE;
}

static gboolean
fu_asus_hid_device_write_firmware(FuDevice *device,
				  FuFirmware *firmware,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuAsusHidDevice *self = FU_ASUS_HID_DEVICE(device);
	const guint8 *buf;
	gsize bufsz = 0;
	guint32 address;
	guint blocks;
	g_autoptr(FuFirmware) img = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(FuStructAsusEraseFlashCommand) st_erase = fu_struct_asus_erase_flash_command_new();

	img = fu_firmware_get_image_by_id(firmware, FU_FIRMWARE_ID_PAYLOAD, error);
	if (img == NULL)
		return FALSE;
	blob = fu_firmware_get_bytes(img, error);
	if (blob == NULL)
		return FALSE;
	buf = g_bytes_get_data(blob, &bufsz);
	address = fu_firmware_get_addr(img);
	if (bufsz % FU_ASUS_HID_DEVICE_BLOCK_SIZE != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "payload of 0x%x bytes is not a multiple of the 0x%x block size",
			    (guint)bufsz,
			    (guint)FU_ASUS_HID_DEVICE_BLOCK_SIZE);
		return FALSE;
	}
	if (address % FU_ASUS_HID_DEVICE_BLOCK_SIZE != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "payload address 0x%x is not aligned to the 0x%x block size",
			    address,
			    (guint)FU_ASUS_HID_DEVICE_BLOCK_SIZE);
		return FALSE;
	}
	blocks = bufsz / FU_ASUS_HID_DEVICE_BLOCK_SIZE;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 5, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 95, NULL);

	/* the whole region goes in one command, and the count is in blocks */
	fu_struct_asus_erase_flash_command_set_offset(st_erase, address);
	fu_struct_asus_erase_flash_command_set_blocks(st_erase, blocks);
	if (!fu_asus_hid_device_transfer_feature(self,
						 st_erase->buf,
						 NULL,
						 FU_ASUS_HID_REPORT_ID_FLASHING,
						 error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), FU_ASUS_HID_DEVICE_ERASE_DELAY);
	fu_progress_step_done(progress);

	fu_progress_set_id(fu_progress_get_child(progress), G_STRLOC);
	fu_progress_set_steps(fu_progress_get_child(progress), blocks);
	for (guint i = 0; i < blocks; i++) {
		if (!fu_asus_hid_device_write_block(self,
						    address + (i * FU_ASUS_HID_DEVICE_BLOCK_SIZE),
						    buf + (i * FU_ASUS_HID_DEVICE_BLOCK_SIZE),
						    FU_ASUS_HID_DEVICE_BLOCK_SIZE,
						    error))
			return FALSE;
		fu_progress_step_done(fu_progress_get_child(progress));
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_asus_hid_device_set_quirk_kv(FuDevice *device,
				const gchar *key,
				const gchar *value,
				GError **error)
{
	FuAsusHidDevice *self = FU_ASUS_HID_DEVICE(device);

	if (g_strcmp0(key, "AsusHidNumMcu") == 0) {
		guint64 tmp;

		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->num_mcu = tmp;
		return TRUE;
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_asus_hid_device_dispose(GObject *object)
{
	FuAsusHidDevice *self = FU_ASUS_HID_DEVICE(object);

	if (self->child_added_id != 0) {
		g_signal_handler_disconnect(FU_DEVICE(self), self->child_added_id);
		self->child_added_id = 0;
	}

	G_OBJECT_CLASS(fu_asus_hid_device_parent_class)->dispose(object);
}

static void
fu_asus_hid_device_init(FuAsusHidDevice *self)
{
	/* TODO: automatic backup */
	/* fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_BACKUP_BEFORE_INSTALL); */
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_protocol(FU_DEVICE(self), "com.asus.hid");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_ASUS_HID_FIRMWARE);
	/* the device comes back with the counterpart GUID rather than the one
	 * it left with, so the replug is only matched with this */
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	/* switching modes takes about eight seconds on a RC73XA, in either
	 * direction, so ten was not enough of a margin */
	fu_device_set_remove_delay(FU_DEVICE(self), 30000);
	self->child_added_id = g_signal_connect(FU_DEVICE(self),
						"child-added",
						G_CALLBACK(fu_asus_hid_device_child_added_cb),
						self);
}

static void
fu_asus_hid_device_class_init(FuAsusHidDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = fu_asus_hid_device_dispose;
	device_class->setup = fu_asus_hid_device_setup;
	device_class->reload = fu_asus_hid_device_reload;
	device_class->probe = fu_asus_hid_device_probe;
	device_class->set_quirk_kv = fu_asus_hid_device_set_quirk_kv;
	device_class->detach = fu_asus_hid_device_detach;
	device_class->attach = fu_asus_hid_device_attach;
	device_class->dump_firmware = fu_asus_hid_device_dump_firmware;
	device_class->write_firmware = fu_asus_hid_device_write_firmware;
}
