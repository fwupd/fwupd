/*
 * Copyright 2025 Joe Hong <JoeHung@ilitek.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-ilitek-its-common.h"
#include "fu-ilitek-its-device.h"
#include "fu-ilitek-its-firmware.h"
#include "fu-ilitek-its-struct.h"

/* this can be set using Flags=example in the quirk file  */
#define FU_ILITEK_ITS_DEVICE_FLAG_EXAMPLE "example"

#define FU_ILITEK_ITS_AP_MODE 0x5A /* device in Application mode */
#define FU_ILITEK_ITS_BL_MODE 0x55 /* device in Bootloader mode */

struct _FuIlitekItsDevice {
	FuHidrawDevice parent_instance;
	gchar *ic_name;
	guint32 edid;

	/* start/end addr of first block */
	guint32 start_addr;
	guint32 end_addr;
};

G_DEFINE_TYPE(FuIlitekItsDevice, fu_ilitek_its_device, FU_TYPE_HIDRAW_DEVICE)

typedef struct {
	guint8 cmd;
	GByteArray *rbuf;
} FuIlitekItsHidCmdHelper;

static gboolean
fu_ilitek_its_device_read_input_cb(FuDevice *device, gpointer data, GError **error)
{
	FuIlitekItsDevice *self = FU_ILITEK_ITS_DEVICE(device);
	FuIlitekItsHidCmdHelper *helper = (FuIlitekItsHidCmdHelper *)data;
	guint8 rbuf[64] = {0};

	if (!fu_udev_device_pread(FU_UDEV_DEVICE(self), 0, rbuf, sizeof(rbuf), error)) {
		g_prefix_error_literal(error, "failed to read report: ");
		return FALSE;
	}

	fu_dump_raw(G_LOG_DOMAIN, "rbuf", rbuf, sizeof(rbuf));
	if (rbuf[0] != 0x03 || rbuf[1] != 0xA3 || rbuf[2] != helper->cmd) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "buffer invalid: ");
		return FALSE;
	}
	g_byte_array_append(helper->rbuf, rbuf, sizeof(rbuf));

	/* success */
	return TRUE;
}

static gboolean
fu_ilitek_its_device_read_ack_cb(FuDevice *device, gpointer data, GError **error)
{
	FuIlitekItsDevice *self = FU_ILITEK_ITS_DEVICE(device);
	FuIlitekItsHidCmdHelper *helper = (FuIlitekItsHidCmdHelper *)data;
	guint8 rbuf[64] = {0};

	if (!fu_hidraw_device_get_report(FU_HIDRAW_DEVICE(self), rbuf, sizeof(rbuf), 0, error)) {
		g_prefix_error_literal(error, "failed to read report: ");
		return FALSE;
	}
	fu_dump_raw(G_LOG_DOMAIN, "ack", rbuf, sizeof(rbuf));
	if (rbuf[0] != 0x03 || rbuf[1] != 0xA3 || rbuf[2] != helper->cmd || rbuf[4] != 0xac) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "buffer invalid: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_ilitek_its_device_send_cmd(FuIlitekItsDevice *self,
			   GByteArray *wbuf,
			   GByteArray *rbuf,
			   GError **error)
{
	FuIlitekItsHidCmdHelper helper = {
	    .rbuf = rbuf,
	};

	if (wbuf != NULL) {
		if (wbuf->data[0] == 0x03) { /* FIXME: magic value needs #define */
			fu_byte_array_set_size(wbuf, 64, 0x0);
			helper.cmd = wbuf->data[4];
		} else {
			fu_byte_array_set_size(wbuf, 1031, 0x0);
			helper.cmd = wbuf->data[7];
		}
		if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
						  wbuf->data,
						  wbuf->len,
						  FU_IOCTL_FLAG_RETRY,
						  error)) {
			g_prefix_error_literal(error, "failed to send hid cmd: ");
			return FALSE;
		}
	}
	if (rbuf != NULL) {
		fu_device_sleep(FU_DEVICE(self), 100);
		if (!fu_device_retry_full(FU_DEVICE(self),
					  fu_ilitek_its_device_read_input_cb,
					  50,
					  100,
					  &helper,
					  error)) {
			g_prefix_error_literal(error, "failed to recv hid packet: ");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_ilitek_its_device_send_cmd_then_wake_ack(FuIlitekItsDevice *self,
			   GByteArray *wbuf,
			   GError **error)
{
	FuIlitekItsHidCmdHelper helper = {
	    .rbuf = NULL,
	};

	if (wbuf->data[0] == 0x03) { /* FIXME: magic value needs #define */
		helper.cmd = wbuf->data[4];
	} else {
		helper.cmd = wbuf->data[6];
	}

	if (!fu_ilitek_its_device_send_cmd(self, wbuf, NULL, error))
		return FALSE;

	if (!fu_device_retry_full(FU_DEVICE(self),
					fu_ilitek_its_device_read_ack_cb,
					50,
					100,
					&helper,
					error)) {
		g_prefix_error_literal(error, "failed to recv ack: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_ilitek_its_device_get_block_crc(FuIlitekItsDevice *self,
				   gboolean need_recalculate,
				   guint32 start, guint32 end,
				   guint16 *crc,
				   GError **error)
{
	g_autoptr(FuStructIlitekItsHidCmd) wbuf = NULL;
	g_autoptr(GByteArray) rbuf = g_byte_array_new();

	if (need_recalculate) {
		wbuf = fu_struct_ilitek_its_hid_cmd_new();
		fu_struct_ilitek_its_hid_cmd_set_write_len(wbuf, 8);
		fu_struct_ilitek_its_hid_cmd_set_cmd(
			wbuf,
			FU_ILITEK_ITS_CMD_GET_BLOCK_CRC);
		fu_byte_array_append_uint8(wbuf, 0x0);
		fu_byte_array_append_uint24(wbuf, start, G_LITTLE_ENDIAN);
		fu_byte_array_append_uint24(wbuf, end, G_LITTLE_ENDIAN);

		if (!fu_ilitek_its_device_send_cmd_then_wake_ack(self, wbuf, error))
			return FALSE;
	}

	wbuf = fu_struct_ilitek_its_hid_cmd_new();
	fu_struct_ilitek_its_hid_cmd_set_write_len(wbuf, 2);
	fu_struct_ilitek_its_hid_cmd_set_read_len(wbuf, 2);
	fu_struct_ilitek_its_hid_cmd_set_cmd(
		wbuf,
		FU_ILITEK_ITS_CMD_GET_BLOCK_CRC);
	fu_byte_array_append_uint8(wbuf, 0x1);

	if (!fu_ilitek_its_device_send_cmd(self, wbuf, rbuf, error))
		return FALSE;

	return fu_memread_uint16_safe(rbuf->data,
				    rbuf->len,
				    4,
				    crc,
				    G_LITTLE_ENDIAN,
				    error);
}

static gboolean
fu_ilitek_its_device_flash_enable(FuIlitekItsDevice *self,
					  gboolean in_ap,
					  guint32 start,
					  guint32 end,
					  GError **error)
{
	g_autoptr(FuStructIlitekItsHidCmd) wbuf = fu_struct_ilitek_its_hid_cmd_new();

	fu_struct_ilitek_its_hid_cmd_set_write_len(wbuf, in_ap ? 3 : 9);
	fu_struct_ilitek_its_hid_cmd_set_cmd(
		wbuf,
		FU_ILITEK_ITS_CMD_FLASH_ENABLE);
	fu_byte_array_append_uint8(wbuf, 0x5A); /* FIXME: magic value needs #define */
	fu_byte_array_append_uint8(wbuf, 0xA5); /* FIXME: magic value needs #define */

	if (!in_ap) {
		fu_byte_array_append_uint24(wbuf, start, G_LITTLE_ENDIAN);
		fu_byte_array_append_uint24(wbuf, end, G_LITTLE_ENDIAN);
	}

	return fu_ilitek_its_device_send_cmd(self, wbuf, NULL, error);
}

static gboolean
fu_ilitek_its_device_set_ctrl_mode(FuIlitekItsDevice *self,
				   FuIlitekItsCtrlMode mode,
				   GError **error)
{
	g_autoptr(FuStructIlitekItsHidCmd) wbuf = fu_struct_ilitek_its_hid_cmd_new();

	fu_struct_ilitek_its_hid_cmd_set_write_len(wbuf, 3);
	fu_struct_ilitek_its_hid_cmd_set_cmd(
		wbuf,
		FU_ILITEK_ITS_CMD_SET_CTRL_MODE);
	fu_byte_array_append_uint8(wbuf, mode);
	fu_byte_array_append_uint8(wbuf, 0x0);

	if (!fu_ilitek_its_device_send_cmd(self, wbuf, NULL, error))
		return FALSE;

	/* success */
	fu_device_sleep(FU_DEVICE(self), 100);
	return TRUE;
}

static gboolean
fu_ilitek_its_device_ensure_fw_version(FuIlitekItsDevice *self, GError **error)
{
	guint64 version;
	g_autoptr(FuStructIlitekItsHidCmd) wbuf = fu_struct_ilitek_its_hid_cmd_new();
	g_autoptr(GByteArray) rbuf = g_byte_array_new();

	fu_struct_ilitek_its_hid_cmd_set_write_len(wbuf, 1);
	fu_struct_ilitek_its_hid_cmd_set_read_len(wbuf, 8);
	fu_struct_ilitek_its_hid_cmd_set_cmd(
		wbuf,
		FU_ILITEK_ITS_CMD_GET_FIRMWARE_VERSION);

	if (!fu_ilitek_its_device_send_cmd(self, wbuf, rbuf, error))
		return FALSE;

	if (!fu_memread_uint64_safe(rbuf->data, rbuf->len, 4, &version, G_BIG_ENDIAN, error))
		return FALSE;
	fu_device_set_version_raw(FU_DEVICE(self), version);
	return TRUE;
}

static gboolean
fu_ilitek_its_device_get_ic_mode(FuIlitekItsDevice *self,
			      guint8 *ic_mode,
			      GError **error)
{
	g_autoptr(FuStructIlitekItsHidCmd) wbuf = fu_struct_ilitek_its_hid_cmd_new();
	g_autoptr(GByteArray) rbuf = g_byte_array_new();

	fu_struct_ilitek_its_hid_cmd_set_write_len(wbuf, 1);
	fu_struct_ilitek_its_hid_cmd_set_read_len(wbuf, 2);
	fu_struct_ilitek_its_hid_cmd_set_cmd(
		wbuf,
		FU_ILITEK_ITS_CMD_GET_IC_MODE);

	if (!fu_ilitek_its_device_send_cmd(self, wbuf, rbuf, error))
		return FALSE;

	return fu_memread_uint8_safe(rbuf->data,
				    rbuf->len,
				    4,
				    ic_mode,
				    error);
}

static gboolean
fu_ilitek_its_device_ensure_ic_name(FuIlitekItsDevice *self, GError **error)
{
	g_autoptr(FuStructIlitekItsHidCmd) wbuf = fu_struct_ilitek_its_hid_cmd_new();
	g_autoptr(GByteArray) rbuf = g_byte_array_new();

	fu_struct_ilitek_its_hid_cmd_set_write_len(wbuf, 1);
	fu_struct_ilitek_its_hid_cmd_set_read_len(wbuf, 32);
	fu_struct_ilitek_its_hid_cmd_set_cmd(
		wbuf,
		FU_ILITEK_ITS_CMD_GET_IC_NAME);

	if (!fu_ilitek_its_device_send_cmd(self, wbuf, rbuf, error))
		return FALSE;

	g_free(self->ic_name);
	self->ic_name = fu_memstrsafe(rbuf->data, rbuf->len, 0x0, 4, error);
	if (self->ic_name == NULL)
		return FALSE;
	fu_device_set_name(FU_DEVICE(self), self->ic_name);
	return TRUE;
}

static gboolean
fu_ilitek_its_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuIlitekItsDevice *self = FU_ILITEK_ITS_DEVICE(device);
	g_autoptr(FuStructIlitekItsHidCmd) wbuf = fu_struct_ilitek_its_hid_cmd_new();
	guint8 ic_mode;

	if (!fu_ilitek_its_device_get_ic_mode(self, &ic_mode, error))
		return FALSE;
	if (ic_mode == FU_ILITEK_ITS_BL_MODE)
		return TRUE;

	if (!fu_ilitek_its_device_flash_enable(self, TRUE, 0, 0, error))
		return FALSE;

	fu_struct_ilitek_its_hid_cmd_set_write_len(wbuf, 1);
	fu_struct_ilitek_its_hid_cmd_set_cmd(
		wbuf,
		FU_ILITEK_ITS_CMD_SET_BL_MODE);
	if (!fu_ilitek_its_device_send_cmd(self, wbuf, NULL, error))
		return FALSE;

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_ilitek_its_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuIlitekItsDevice *self = FU_ILITEK_ITS_DEVICE(device);
	guint8 ic_mode;
	g_autoptr(FuStructIlitekItsHidCmd) wbuf = fu_struct_ilitek_its_hid_cmd_new();

	if (!fu_ilitek_its_device_get_ic_mode(self, &ic_mode, error))
		return FALSE;
	if (ic_mode == FU_ILITEK_ITS_AP_MODE)
		return TRUE;

	if (!fu_ilitek_its_device_flash_enable(self, FALSE, self->start_addr, self->end_addr, error))
		return FALSE;

	fu_struct_ilitek_its_hid_cmd_set_write_len(wbuf, 1);
	fu_struct_ilitek_its_hid_cmd_set_cmd(
		wbuf,
		FU_ILITEK_ITS_CMD_SET_AP_MODE);
	if (!fu_ilitek_its_device_send_cmd(self, wbuf, NULL, error))
		return FALSE;

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_ilitek_its_device_probe(FuDevice *device, GError **error)
{
	/* ignore unsupported subsystems */
	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "hidraw") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "is not correct subsystem: %s, expected hidraw",
			    fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_ilitek_its_device_setup(FuDevice *device, GError **error)
{
	FuIlitekItsDevice *self = FU_ILITEK_ITS_DEVICE(device);
	GPtrArray *children = fu_device_get_children(device);

	for (guint i = 0; i < children->len; i++) {
		FuDevice *child = g_ptr_array_index(children, i);
		FuEdid *edid = fu_drm_device_get_edid(FU_DRM_DEVICE(child));
		const gchar *pnp_id = NULL;
		gchar *pnp_id_mut = NULL;
		guint16 manu_id;

		if (edid == NULL)
			continue;

		pnp_id = fu_edid_get_pnp_id(edid);
		if (pnp_id == NULL || strlen(pnp_id) != 3)
			continue;

		pnp_id_mut[0] = pnp_id[0] - 'A' + 1;
		pnp_id_mut[1] = pnp_id[1] - 'A' + 1;
		pnp_id_mut[2] = pnp_id[2] - 'A' + 1;
		manu_id = ((((pnp_id_mut[1] & 0x7) << 5) + (pnp_id_mut[2] & 0x1f)) << 8) +
			  ((pnp_id_mut[0] << 2) & 0x7c) + ((pnp_id_mut[1] >> 3) & 0x3);
		g_debug("manu_id: 0x%04x, product_code: 0x%04x",
			manu_id,
			fu_edid_get_product_code(edid));

		self->edid = (manu_id << 16) + fu_edid_get_product_code(edid);
		break;
	}

	if (!fu_ilitek_its_device_ensure_ic_name(self, error))
		return FALSE;
	if (!fu_ilitek_its_device_ensure_fw_version(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static FuFirmware *
fu_ilitek_its_device_prepare_firmware(FuDevice *device,
				      GInputStream *stream,
				      FuProgress *progress,
				      FuFirmwareParseFlags flags,
				      GError **error)
{
	FuIlitekItsDevice *self = FU_ILITEK_ITS_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_ilitek_its_firmware_new();
	g_autoptr(FuFirmware) block_img = NULL;
	const gchar *fw_ic_name;

	/* check is compatible with hardware */
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;

	fw_ic_name = fu_ilitek_its_firmware_get_ic_name(FU_ILITEK_ITS_FIRMWARE(firmware));
	if (g_strcmp0(self->ic_name, fw_ic_name) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "firmware ic name %s does not match device ic name %s",
			    fw_ic_name, self->ic_name);
		return NULL;
	}

	block_img = fu_firmware_get_image_by_idx(firmware, 0, error);
	if (block_img == NULL)
		return NULL;
	self->start_addr = fu_firmware_get_addr(block_img);
	self->end_addr = self->start_addr + fu_firmware_get_size(block_img) - 1;

	/* success */
	return g_steal_pointer(&firmware);
}

static gboolean
fu_ilitek_its_device_write_block(FuIlitekItsDevice *self,
				 FuFirmware *block_img,
				 FuProgress *progress,
				 GError **error)
{
	guint16 crc;
	guint16 fw_crc;
	guint32 start;
	guint32 end;
	guint32 idx = fu_firmware_get_idx(block_img);
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GBytes) blob = NULL;

	start = fu_firmware_get_addr(block_img);
	end = start + fu_firmware_get_size(block_img) - 1;

	fw_crc = fu_ilitek_its_firmware_get_block_crc(
		FU_ILITEK_ITS_FIRMWARE(fu_firmware_get_parent(block_img)),
		idx);

	if (!fu_ilitek_its_device_get_block_crc(self, TRUE, start, end, &crc, error))
		return FALSE;

	g_debug("block[%u]: start/end addr: 0x%x/0x%x, ic/file crc: 0x%x/0x%x, need update: %s",
		idx,
		start,
		end,
		crc,
		fw_crc,
		crc == fw_crc ? "no" : "yes");

	/* no need to upgrade block if crc matched */
	if (crc == fw_crc)
		return TRUE;

	blob = fu_firmware_get_bytes(block_img, error);
	if (blob == NULL)
		return FALSE;
	chunks = fu_chunk_array_new_from_bytes(blob, 0, 0, 1024);
	if (chunks == NULL)
		return FALSE;

	if (!fu_ilitek_its_device_flash_enable(self, FALSE, start, end, error))
		return FALSE;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint32 i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chunk = NULL;
		g_autoptr(FuStructIlitekItsLongHidCmd) wbuf = fu_struct_ilitek_its_long_hid_cmd_new();
		g_autoptr(GBytes) chunk_bytes = NULL;
		g_autoptr(GBytes) chunk_bytes_padded = NULL;

		chunk = fu_chunk_array_index(chunks, i, error);
		if (chunk == NULL)
			return FALSE;

		fu_struct_ilitek_its_long_hid_cmd_set_write_len(wbuf, 1025);
		fu_struct_ilitek_its_long_hid_cmd_set_cmd(
			wbuf,
			FU_ILITEK_ITS_CMD_WRITE_DATA);

		chunk_bytes = fu_chunk_get_bytes(chunk);
		chunk_bytes_padded = fu_bytes_pad(chunk_bytes, 1024, 0xff);
		fu_byte_array_append_bytes(wbuf, chunk_bytes_padded);

		if (!fu_ilitek_its_device_send_cmd_then_wake_ack(self, wbuf, error))
			return FALSE;

		fu_progress_step_done(progress);
	}

	if (!fu_ilitek_its_device_get_block_crc(self, FALSE, 0, 0, &crc, error))
		return FALSE;

	g_debug("block[%u]: start/end addr: 0x%x/0x%x, ic/file crc: 0x%x/0x%x %s",
		idx,
		start,
		end,
		crc,
		fw_crc,
		crc == fw_crc ? "matched" : "not matched");

	if (crc != fw_crc) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "block crc mismatch: device 0x%04x, firmware 0x%04x",
			    crc, fw_crc);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_ilitek_its_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	FuIlitekItsDevice *self = FU_ILITEK_ITS_DEVICE(device);
	guint8 block_num;

	if (!fu_ilitek_its_device_set_ctrl_mode(self, FU_ILITEK_ITS_CTRL_MODE_SUSPEND, error))
		return FALSE;

	block_num = fu_ilitek_its_firmware_get_block_num(FU_ILITEK_ITS_FIRMWARE(firmware));

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, block_num);
	for (guint i = 0; i < block_num; i++) {
		g_autoptr(FuFirmware) block_img = NULL;

		block_img = fu_firmware_get_image_by_idx(firmware, i, error);
		if (block_img == NULL)
			return FALSE;
		if (!fu_ilitek_its_device_write_block(self,
						      block_img,
						      fu_progress_get_child(progress),
						      error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success! */
	return TRUE;
}

static void
fu_ilitek_its_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 10, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 80, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 10, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static gchar *
fu_ilitek_its_device_convert_version(FuDevice *device, guint64 version_raw)
{
	/* convert 8 byte version in to human readable format. e.g. convert 0x0700000101020304 into
	 * 0700.0001.0102.0304*/
	return g_strdup_printf("%02x%02x.%02x%02x.%02x%02x.%02x%02x",
			       (guint)((version_raw >> 56) & 0xFF),
			       (guint)((version_raw >> 48) & 0xFF),
			       (guint)((version_raw >> 40) & 0xFF),
			       (guint)((version_raw >> 32) & 0xFF),
			       (guint)((version_raw >> 24) & 0xFF),
			       (guint)((version_raw >> 16) & 0xFF),
			       (guint)((version_raw >> 8) & 0xFF),
			       (guint)((version_raw >> 0) & 0xFF));
}

static void
fu_ilitek_its_device_init(FuIlitekItsDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);

	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);

	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_VIDEO_DISPLAY);
	fu_device_add_protocol(FU_DEVICE(self), "com.ili.its");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);

	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_NONBLOCK);
}

static void
fu_ilitek_its_device_finalize(GObject *object)
{
	FuIlitekItsDevice *self = FU_ILITEK_ITS_DEVICE(object);
	g_free(self->ic_name);
	G_OBJECT_CLASS(fu_ilitek_its_device_parent_class)->finalize(object);
}

static void
fu_ilitek_its_device_class_init(FuIlitekItsDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_ilitek_its_device_finalize;
	device_class->probe = fu_ilitek_its_device_probe;
	device_class->setup = fu_ilitek_its_device_setup;
	device_class->attach = fu_ilitek_its_device_attach;
	device_class->detach = fu_ilitek_its_device_detach;
	device_class->prepare_firmware = fu_ilitek_its_device_prepare_firmware;
	device_class->write_firmware = fu_ilitek_its_device_write_firmware;
	device_class->set_progress = fu_ilitek_its_device_set_progress;
	device_class->convert_version = fu_ilitek_its_device_convert_version;
}
