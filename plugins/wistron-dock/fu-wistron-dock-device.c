/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 * Copyright 2022 Wistron <Felix_F_Chen@wistron.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-wistron-dock-common.h"
#include "fu-wistron-dock-device.h"
#include "fu-wistron-dock-struct.h"

struct _FuWistronDockDevice {
	FuHidDevice parent_instance;
	guint8 component_idx;
	guint8 update_phase;
	guint8 status_code;
	guint8 imgmode;
	gchar *icp_bbinfo;
	gchar *icp_userinfo;
	guint device_insert_id;
};

G_DEFINE_TYPE(FuWistronDockDevice, fu_wistron_dock_device, FU_TYPE_HID_DEVICE)

#define FU_WISTRON_DOCK_TRANSFER_BLOCK_SIZE  512  /* bytes */
#define FU_WISTRON_DOCK_TRANSFER_TIMEOUT     5000 /* ms */
#define FU_WISTRON_DOCK_TRANSFER_RETRY_COUNT 5
#define FU_WISTRON_DOCK_TRANSFER_RETRY_DELAY 100 /* ms */

#define FU_WISTRON_DOCK_ID_USB_CONTROL	  0x06 /* 7 bytes */
#define FU_WISTRON_DOCK_ID_USB_BLOCK	  0x07 /* 512 bytes */
#define FU_WISTRON_DOCK_ID_IMG_CONTROL	  0x16 /* 7 bytes */
#define FU_WISTRON_DOCK_ID_DOCK_IMG_DATA  0x17 /* 512 bytes */
#define FU_WISTRON_DOCK_ID_DOCK_WDIT	  0x20 /* 512 bytes */
#define FU_WISTRON_DOCK_ID_DOCK_WDFL_SIG  0x21 /* 256 bytes */
#define FU_WISTRON_DOCK_ID_DOCK_WDFL_DATA 0x22 /* 1440 bytes */
#define FU_WISTRON_DOCK_ID_DOCK_SN	  0x23 /* 32 bytes */

static void
fu_wistron_dock_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuWistronDockDevice *self = FU_WISTRON_DOCK_DEVICE(device);
	fwupd_codec_string_append(str,
				  idt,
				  "ComponentIdx",
				  fu_wistron_dock_component_idx_to_string(self->component_idx));
	fwupd_codec_string_append(str,
				  idt,
				  "UpdatePhase",
				  fu_wistron_dock_update_phase_to_string(self->update_phase));
	fwupd_codec_string_append(str,
				  idt,
				  "StatusCode",
				  fu_wistron_dock_status_code_to_string(self->status_code));
	fwupd_codec_string_append_hex(str, idt, "ImgMode", self->imgmode);
	fwupd_codec_string_append(str, idt, "IcpBbInfo", self->icp_bbinfo);
	fwupd_codec_string_append(str, idt, "IcpUserInfo", self->icp_userinfo);
}

typedef struct {
	guint8 *cmd;
	gsize cmdsz;
	guint8 *buf;
	gsize bufsz;
	gboolean check_result;
} FuWistronDockHelper;

static gboolean
fu_wistron_dock_device_control_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuWistronDockHelper *helper = (FuWistronDockHelper *)user_data;
	if (!fu_hid_device_set_report(FU_HID_DEVICE(device),
				      helper->cmd[0], /* value */
				      helper->cmd,
				      helper->cmdsz,
				      FU_WISTRON_DOCK_TRANSFER_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error))
		return FALSE;
	if (helper->check_result) {
		if (!fu_hid_device_get_report(FU_HID_DEVICE(device),
					      helper->buf[0], /* value */
					      helper->buf,
					      helper->bufsz,
					      FU_WISTRON_DOCK_TRANSFER_TIMEOUT,
					      FU_HID_DEVICE_FLAG_IS_FEATURE,
					      error))
			return FALSE;
		if (helper->buf[7] != FU_WISTRON_DOCK_CMD_ICP_DONE) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "not icp-done, got 0x%02x",
				    helper->cmd[7]);
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_wistron_dock_device_control_write(FuWistronDockDevice *self,
				     guint8 *cmd,
				     gsize cmdsz,
				     gboolean check_result,
				     GError **error)
{
	FuWistronDockHelper helper = {.cmd = cmd,
				      .cmdsz = cmdsz,
				      .buf = cmd,
				      .bufsz = cmdsz,
				      .check_result = check_result};
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_wistron_dock_device_control_cb,
				    FU_WISTRON_DOCK_TRANSFER_RETRY_COUNT,
				    FU_WISTRON_DOCK_TRANSFER_RETRY_DELAY,
				    &helper,
				    error);
}

static gboolean
fu_wistron_dock_device_control_read(FuWistronDockDevice *self,
				    guint8 *cmd,
				    gsize cmdsz,
				    guint8 *buf,
				    gsize bufsz,
				    gboolean check_result,
				    GError **error)
{
	FuWistronDockHelper helper = {.cmd = cmd,
				      .cmdsz = cmdsz,
				      .buf = buf,
				      .bufsz = bufsz,
				      .check_result = check_result};
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_wistron_dock_device_control_cb,
				    FU_WISTRON_DOCK_TRANSFER_RETRY_COUNT,
				    FU_WISTRON_DOCK_TRANSFER_RETRY_DELAY,
				    &helper,
				    error);
}

static gboolean
fu_wistron_dock_device_data_write_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuWistronDockHelper *helper = (FuWistronDockHelper *)user_data;
	if (!fu_hid_device_set_report(FU_HID_DEVICE(device),
				      helper->cmd[0], /* value */
				      helper->cmd,
				      helper->cmdsz,
				      FU_WISTRON_DOCK_TRANSFER_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error))
		return FALSE;
	if (!fu_hid_device_set_report(FU_HID_DEVICE(device),
				      helper->buf[0], /* value */
				      helper->buf,
				      helper->bufsz,
				      FU_WISTRON_DOCK_TRANSFER_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error))
		return FALSE;
	if (!fu_hid_device_get_report(FU_HID_DEVICE(device),
				      helper->cmd[0], /* value */
				      helper->cmd,
				      helper->cmdsz,
				      FU_WISTRON_DOCK_TRANSFER_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error))
		return FALSE;
	if (helper->cmd[7] != FU_WISTRON_DOCK_CMD_ICP_DONE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "not icp-done, got 0x%02x",
			    helper->cmd[7]);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_wistron_dock_device_data_write(FuWistronDockDevice *self,
				  guint8 *cmd,
				  gsize cmdsz,
				  guint8 *buf,
				  gsize bufsz,
				  GError **error)
{
	FuWistronDockHelper helper = {.cmd = cmd, .cmdsz = cmdsz, .buf = buf, .bufsz = bufsz};
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_wistron_dock_device_data_write_cb,
				    FU_WISTRON_DOCK_TRANSFER_RETRY_COUNT,
				    FU_WISTRON_DOCK_TRANSFER_RETRY_DELAY,
				    &helper,
				    error);
}

static gboolean
fu_wistron_dock_device_write_wdfl_sig(FuWistronDockDevice *self,
				      const guint8 *buf,
				      gsize bufsz,
				      GError **error)
{
	guint8 cmd[8] = {FU_WISTRON_DOCK_ID_IMG_CONTROL, FU_WISTRON_DOCK_CMD_DFU_WRITE_WDFL_SIG};
	guint8 towrite[FU_WISTRON_DOCK_WDFL_SIG_SIZE + 1] = {FU_WISTRON_DOCK_ID_DOCK_WDFL_SIG};

	if (!fu_memcpy_safe(towrite,
			    sizeof(towrite),
			    0x1, /* dst */
			    buf,
			    bufsz,
			    0x0, /* src */
			    bufsz,
			    error))
		return FALSE;
	return fu_wistron_dock_device_data_write(self,
						 cmd,
						 sizeof(cmd),
						 towrite,
						 sizeof(towrite),
						 error);
}

static gboolean
fu_wistron_dock_device_write_wdfl_data(FuWistronDockDevice *self,
				       const guint8 *buf,
				       gsize bufsz,
				       GError **error)
{
	guint8 cmd[8] = {FU_WISTRON_DOCK_ID_IMG_CONTROL, FU_WISTRON_DOCK_CMD_DFU_WRITE_WDFL_DATA};
	guint8 towrite[FU_WISTRON_DOCK_WDFL_DATA_SIZE + 1] = {FU_WISTRON_DOCK_ID_DOCK_WDFL_DATA};
	if (!fu_memcpy_safe(towrite,
			    sizeof(towrite),
			    0x1, /* dst */
			    buf,
			    bufsz,
			    0x0, /* src */
			    bufsz,
			    error))
		return FALSE;
	return fu_wistron_dock_device_data_write(self,
						 cmd,
						 sizeof(cmd),
						 towrite,
						 sizeof(towrite),
						 error);
}

static gboolean
fu_wistron_dock_device_set_img_address(FuWistronDockDevice *self, guint32 addr, GError **error)
{
	guint8 cmd[8] = {FU_WISTRON_DOCK_ID_IMG_CONTROL, FU_WISTRON_DOCK_CMD_DFU_ADDRESS};
	fu_memwrite_uint32(cmd + 2, addr, G_BIG_ENDIAN);
	return fu_wistron_dock_device_control_write(self, cmd, sizeof(cmd), TRUE, error);
}

static gboolean
fu_wistron_dock_device_write_img_data(FuWistronDockDevice *self,
				      const guint8 *buf,
				      gsize bufsz,
				      GError **error)
{
	guint8 cmd[8] = {FU_WISTRON_DOCK_ID_IMG_CONTROL, FU_WISTRON_DOCK_CMD_DFU_WRITEIMG_BLOCK};
	guint8 towrite[FU_WISTRON_DOCK_TRANSFER_BLOCK_SIZE + 1] = {
	    FU_WISTRON_DOCK_ID_DOCK_IMG_DATA};
	if (!fu_memcpy_safe(towrite,
			    sizeof(towrite),
			    0x1, /* dst */
			    buf,
			    bufsz,
			    0x0, /* src */
			    bufsz,
			    error))
		return FALSE;
	return fu_wistron_dock_device_data_write(self,
						 cmd,
						 sizeof(cmd),
						 towrite,
						 sizeof(towrite),
						 error);
}

static gboolean
fu_wistron_dock_device_write_blocks(FuWistronDockDevice *self,
				    FuChunkArray *chunks,
				    FuProgress *progress,
				    GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		/* set address */
		if (!fu_wistron_dock_device_set_img_address(self,
							    fu_chunk_get_address(chk),
							    error)) {
			g_prefix_error(error,
				       "failed to set img address 0x%x",
				       (guint)fu_chunk_get_address(chk));
			return FALSE;
		}

		/* write */
		if (!fu_wistron_dock_device_write_img_data(self,
							   fu_chunk_get_data(chk),
							   fu_chunk_get_data_sz(chk),
							   error)) {
			g_prefix_error(error,
				       "failed to write img data 0x%x",
				       (guint)fu_chunk_get_address(chk));
			return FALSE;
		}

		/* update progress */
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static FuFirmware *
fu_wistron_dock_device_prepare_firmware(FuDevice *device,
					GInputStream *stream,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_archive_firmware_new();
	g_autoptr(FuFirmware) fw_cbin = NULL;
	g_autoptr(FuFirmware) fw_new = fu_firmware_new();
	g_autoptr(FuFirmware) fw_wdfl = NULL;
	g_autoptr(FuFirmware) fw_wsig = NULL;

	/* unzip and get images */
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	fw_wsig = fu_archive_firmware_get_image_fnmatch(FU_ARCHIVE_FIRMWARE(firmware),
							"*.wdfl.sig",
							error);
	if (fw_wsig == NULL)
		return NULL;
	fw_wdfl =
	    fu_archive_firmware_get_image_fnmatch(FU_ARCHIVE_FIRMWARE(firmware), "*.wdfl", error);
	if (fw_wdfl == NULL)
		return NULL;
	fw_cbin =
	    fu_archive_firmware_get_image_fnmatch(FU_ARCHIVE_FIRMWARE(firmware), "*.bin", error);
	if (fw_cbin == NULL)
		return NULL;

	/* sanity check sizes */
	if (fu_firmware_get_size(fw_wsig) < FU_WISTRON_DOCK_WDFL_SIG_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "WDFL signature size invalid, got 0x%x, expected >= 0x%x",
			    (guint)fu_firmware_get_size(fw_wsig),
			    (guint)FU_WISTRON_DOCK_WDFL_SIG_SIZE);
		return NULL;
	}
	if (fu_firmware_get_size(fw_wdfl) != FU_WISTRON_DOCK_WDFL_DATA_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "WDFL size invalid, got 0x%x, expected 0x%x",
			    (guint)fu_firmware_get_size(fw_wdfl),
			    (guint)FU_WISTRON_DOCK_WDFL_DATA_SIZE);
		return NULL;
	}

	/* success */
	fu_firmware_set_id(fw_wsig, FU_FIRMWARE_ID_SIGNATURE);
	fu_firmware_add_image(fw_new, fw_wsig);
	fu_firmware_set_id(fw_wdfl, FU_FIRMWARE_ID_HEADER);
	fu_firmware_add_image(fw_new, fw_wdfl);
	fu_firmware_set_id(fw_cbin, FU_FIRMWARE_ID_PAYLOAD);
	fu_firmware_add_image(fw_new, fw_cbin);
	return g_steal_pointer(&fw_new);
}

static gboolean
fu_wistron_dock_device_write_firmware(FuDevice *device,
				      FuFirmware *firmware,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuWistronDockDevice *self = FU_WISTRON_DOCK_DEVICE(device);
	g_autoptr(GInputStream) stream_cbin = NULL;
	g_autoptr(GBytes) fw_wdfl = NULL;
	g_autoptr(GBytes) fw_wsig = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "write-wdfl-signature");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "write-wdfl-data");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, "write-payload");

	/* write WDFL signature */
	fw_wsig = fu_firmware_get_image_by_id_bytes(firmware, FU_FIRMWARE_ID_SIGNATURE, error);
	if (fw_wsig == NULL)
		return FALSE;
	if (!fu_wistron_dock_device_write_wdfl_sig(self,
						   g_bytes_get_data(fw_wsig, NULL),
						   g_bytes_get_size(fw_wsig),
						   error)) {
		g_prefix_error(error, "failed to write WDFL signature: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* write WDFL data */
	fw_wdfl = fu_firmware_get_image_by_id_bytes(firmware, FU_FIRMWARE_ID_HEADER, error);
	if (fw_wdfl == NULL)
		return FALSE;
	if (!fu_wistron_dock_device_write_wdfl_data(self,
						    g_bytes_get_data(fw_wdfl, NULL),
						    g_bytes_get_size(fw_wdfl),
						    error)) {
		g_prefix_error(error, "failed to write WDFL data: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* write each block */
	stream_cbin = fu_firmware_get_image_by_id_stream(firmware, FU_FIRMWARE_ID_PAYLOAD, error);
	if (stream_cbin == NULL)
		return FALSE;
	chunks = fu_chunk_array_new_from_stream(stream_cbin,
						0x0,
						FU_WISTRON_DOCK_TRANSFER_BLOCK_SIZE,
						error);
	if (chunks == NULL)
		return FALSE;
	if (!fu_wistron_dock_device_write_blocks(self,
						 chunks,
						 fu_progress_get_child(progress),
						 error)) {
		g_prefix_error(error, "failed to write payload: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success! */
	return TRUE;
}

static gboolean
fu_wistron_dock_device_ensure_mcuid(FuWistronDockDevice *self, GError **error)
{
	guint8 cmd[8] = {FU_WISTRON_DOCK_ID_USB_CONTROL, FU_WISTRON_DOCK_CMD_ICP_MCUID};
	guint8 buf[8] = {FU_WISTRON_DOCK_ID_USB_CONTROL};
	g_autofree gchar *tmp = NULL;

	if (!fu_wistron_dock_device_control_read(self,
						 cmd,
						 sizeof(cmd),
						 buf,
						 sizeof(buf),
						 TRUE,
						 error))
		return FALSE;
	tmp = fu_memstrsafe(buf, sizeof(buf), 2, 5, error);
	if (tmp == NULL)
		return FALSE;
	fu_device_add_instance_str(FU_DEVICE(self), "MCUID", tmp);
	return fu_device_build_instance_id(FU_DEVICE(self),
					   error,
					   "USB",
					   "VID",
					   "PID",
					   "MCUID",
					   NULL);
}

static gboolean
fu_wistron_dock_device_ensure_bbinfo(FuWistronDockDevice *self, GError **error)
{
	guint8 cmd[8] = {FU_WISTRON_DOCK_ID_USB_CONTROL, FU_WISTRON_DOCK_CMD_ICP_BBINFO};
	guint8 buf[8] = {FU_WISTRON_DOCK_ID_USB_CONTROL};

	if (!fu_wistron_dock_device_control_read(self,
						 cmd,
						 sizeof(cmd),
						 buf,
						 sizeof(buf),
						 TRUE,
						 error))
		return FALSE;
	g_free(self->icp_bbinfo);
	self->icp_bbinfo = g_strdup_printf("%u.%u.%u", buf[2], buf[3], buf[4]);
	return TRUE;
}

static gboolean
fu_wistron_dock_device_ensure_userinfo(FuWistronDockDevice *self, GError **error)
{
	guint8 cmd[8] = {FU_WISTRON_DOCK_ID_USB_CONTROL, FU_WISTRON_DOCK_CMD_ICP_USERINFO};
	guint8 buf[8] = {FU_WISTRON_DOCK_ID_USB_CONTROL};

	if (!fu_wistron_dock_device_control_read(self,
						 cmd,
						 sizeof(cmd),
						 buf,
						 sizeof(buf),
						 TRUE,
						 error))
		return FALSE;
	g_free(self->icp_userinfo);
	self->icp_userinfo = g_strdup_printf("%u.%u.%u", buf[2], buf[3], buf[4]);
	return TRUE;
}

static gboolean
fu_wistron_dock_device_parse_wdit_img(FuWistronDockDevice *self,
				      const guint8 *buf,
				      gsize bufsz,
				      gsize offset,
				      guint8 device_cnt,
				      GError **error)
{
	for (guint j = 0; j < device_cnt; j++) {
		guint32 version_raw = 0;
		guint8 status;
		g_autofree gchar *name = NULL;
		g_autofree gchar *version0 = NULL;
		g_autofree gchar *version1 = NULL;
		g_autofree gchar *version2 = NULL;
		g_autoptr(GByteArray) st = NULL;

		/* parse */
		st = fu_struct_wistron_dock_wdit_img_parse(buf, bufsz, offset, error);
		if (st == NULL)
			return FALSE;

		/* versions */
		version_raw = fu_struct_wistron_dock_wdit_img_get_version_build(st);
		if (version_raw != 0)
			version0 = fu_version_from_uint32(version_raw, FWUPD_VERSION_FORMAT_QUAD);
		version_raw = fu_struct_wistron_dock_wdit_img_get_version1(st);
		if (version_raw != 0)
			version1 = fu_version_from_uint32(version_raw, FWUPD_VERSION_FORMAT_QUAD);
		version_raw = fu_struct_wistron_dock_wdit_img_get_version2(st);
		if (version_raw != 0)
			version2 = fu_version_from_uint32(version_raw, FWUPD_VERSION_FORMAT_QUAD);

		/* name */
		name = fu_struct_wistron_dock_wdit_img_get_name(st);
		status = fu_struct_wistron_dock_wdit_img_get_status(st);
		g_debug("%s: bld:%s, img1:%s, img2:%s", name, version0, version1, version2);
		g_debug(" - comp-id:%u, mode:%u, status:%u/%u",
			fu_struct_wistron_dock_wdit_img_get_comp_id(st),
			fu_struct_wistron_dock_wdit_img_get_mode(st),
			(guint)status & 0x0F,
			(guint)(status & 0xF0) >> 4);

		offset += st->len;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_wistron_dock_device_ensure_wdit(FuWistronDockDevice *self, GError **error)
{
	guint8 update_state = 0x0;
	guint8 buf[FU_WISTRON_DOCK_WDIT_SIZE + 1] = {FU_WISTRON_DOCK_ID_DOCK_WDIT};
	g_autoptr(GByteArray) st = NULL;

	/* get WDIT */
	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      buf[0], /* value */
				      buf,
				      sizeof(buf),
				      FU_WISTRON_DOCK_TRANSFER_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE |
					  FU_HID_DEVICE_FLAG_RETRY_FAILURE |
					  FU_HID_DEVICE_FLAG_ALLOW_TRUNC,
				      error))
		return FALSE;

	/* unpack */
	st = fu_struct_wistron_dock_wdit_parse(buf, sizeof(buf), 0x0, error);
	if (st == NULL)
		return FALSE;
	if (fu_struct_wistron_dock_wdit_get_tag_id(st) != FU_WISTRON_DOCK_WDIT_TAG_ID) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "WDIT tag invalid, expected 0x%x, got 0x%x",
			    (guint)FU_WISTRON_DOCK_WDIT_TAG_ID,
			    fu_struct_wistron_dock_wdit_get_tag_id(st));
		return FALSE;
	}

	/* verify VID & PID */
	if (fu_struct_wistron_dock_wdit_get_vid(st) != fu_device_get_vid(FU_DEVICE(self)) ||
	    fu_struct_wistron_dock_wdit_get_pid(st) != fu_device_get_pid(FU_DEVICE(self))) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "USB VID:PID invalid, expected %04X:%04X, got %04X:%04X",
			    (guint)fu_device_get_vid(FU_DEVICE(self)),
			    (guint)fu_device_get_pid(FU_DEVICE(self)),
			    fu_struct_wistron_dock_wdit_get_vid(st),
			    fu_struct_wistron_dock_wdit_get_pid(st));
		return FALSE;
	}

	/* image mode */
	self->imgmode = fu_struct_wistron_dock_wdit_get_imgmode(st);
	if (self->imgmode == 0)
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	else if (self->imgmode == 1)
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);

	/* update state */
	update_state = fu_struct_wistron_dock_wdit_get_update_state(st);
	self->update_phase = (update_state & 0xF0) >> 4;
	if (self->update_phase == FU_WISTRON_DOCK_UPDATE_PHASE_DOWNLOAD)
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	else
		fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	if (fu_wistron_dock_update_phase_to_string(self->update_phase) == NULL)
		g_warning("unknown update_phase 0x%02x", self->update_phase);
	self->component_idx = update_state & 0xF;
	if (fu_wistron_dock_component_idx_to_string(self->component_idx) == NULL)
		g_warning("unknown component_idx 0x%02x", self->component_idx);

	/* status code */
	self->status_code = fu_struct_wistron_dock_wdit_get_status_code(st);
	if (fu_wistron_dock_status_code_to_string(self->status_code) == NULL)
		g_warning("unknown status_code 0x%02x", self->status_code);

	/* composite version */
	fu_device_set_version_raw(FU_DEVICE(self),
				  fu_struct_wistron_dock_wdit_get_composite_version(st));

	/* for debugging only */
	if (!fu_wistron_dock_device_parse_wdit_img(
		self,
		buf,
		sizeof(buf),
		st->len + 0x1,
		MIN(fu_struct_wistron_dock_wdit_get_device_cnt(st), 32),
		error)) {
		g_prefix_error(error, "failed to parse imgs: ");
		return FALSE;
	}

	/* adding the MCU while flashing the device, ignore until it comes back in runtime mode */
	if (self->update_phase == FU_WISTRON_DOCK_UPDATE_PHASE_DEPLOY &&
	    self->status_code == FU_WISTRON_DOCK_STATUS_CODE_UPDATING) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "ignoring device in MCU mode");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_wistron_dock_device_setup(FuDevice *device, GError **error)
{
	FuWistronDockDevice *self = FU_WISTRON_DOCK_DEVICE(device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_wistron_dock_device_parent_class)->setup(device, error))
		return FALSE;

	if (!fu_wistron_dock_device_ensure_mcuid(self, error)) {
		g_prefix_error(error, "failed to get MCUID: ");
		return FALSE;
	}
	if (!fu_wistron_dock_device_ensure_bbinfo(self, error)) {
		g_prefix_error(error, "failed to get BBINFO: ");
		return FALSE;
	}
	if (!fu_wistron_dock_device_ensure_userinfo(self, error)) {
		g_prefix_error(error, "failed to get USERINFO: ");
		return FALSE;
	}
	if (!fu_wistron_dock_device_ensure_wdit(self, error)) {
		g_prefix_error(error, "failed to get WDIT: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_wistron_dock_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuWistronDockDevice *self = FU_WISTRON_DOCK_DEVICE(device);
	guint8 cmd[8] = {FU_WISTRON_DOCK_ID_IMG_CONTROL, FU_WISTRON_DOCK_CMD_DFU_ENTER};

	/* sanity check */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in bootloader mode, skipping");
		return TRUE;
	}
	if (!fu_wistron_dock_device_control_write(self, cmd, sizeof(cmd), FALSE, error))
		return FALSE;
	return fu_wistron_dock_device_ensure_wdit(self, error);
}

static gboolean
fu_wistron_dock_device_insert_cb(gpointer user_data)
{
	FuWistronDockDevice *self = FU_WISTRON_DOCK_DEVICE(user_data);
	g_autoptr(FwupdRequest) request = fwupd_request_new();
	g_autoptr(GError) error_local = NULL;

	/* interactive request to start the SPI write */
	fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_IMMEDIATE);
	fwupd_request_set_id(request, FWUPD_REQUEST_ID_INSERT_USB_CABLE);
	fwupd_request_add_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	if (!fu_device_emit_request(FU_DEVICE(self), request, NULL, &error_local))
		g_warning("%s", error_local->message);

	/* success */
	self->device_insert_id = 0;
	return G_SOURCE_REMOVE;
}

static gboolean
fu_wistron_dock_device_cleanup(FuDevice *device,
			       FuProgress *progress,
			       FwupdInstallFlags flags,
			       GError **error)
{
	FuWistronDockDevice *self = FU_WISTRON_DOCK_DEVICE(device);

	/* ensure the timeout has been cleared, even on error */
	if (self->device_insert_id != 0) {
		g_source_remove(self->device_insert_id);
		self->device_insert_id = 0;
	}
	return TRUE;
}

static gboolean
fu_wistron_dock_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuWistronDockDevice *self = FU_WISTRON_DOCK_DEVICE(device);
	guint8 cmd[8] = {FU_WISTRON_DOCK_ID_IMG_CONTROL, FU_WISTRON_DOCK_CMD_DFU_EXIT};
	g_autoptr(FwupdRequest) request = fwupd_request_new();

	/* sanity check */
	if (!fu_wistron_dock_device_ensure_wdit(self, error))
		return FALSE;
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in runtime mode, skipping");
		return TRUE;
	}
	if (!fu_wistron_dock_device_control_write(self, cmd, sizeof(cmd), FALSE, error))
		return FALSE;
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* the user has to remove the USB cable, wait 15 seconds, then re-insert it */
	fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_IMMEDIATE);
	fwupd_request_set_id(request, FWUPD_REQUEST_ID_REMOVE_USB_CABLE);
	fwupd_request_add_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	if (!fu_device_emit_request(device, request, progress, error))
		return FALSE;

	/* set a timeout, which will trigger as we're waiting for the device --
	 * no sync sleep is possible as the device will re-enumerate one more time */
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_BUSY);
	self->device_insert_id = g_timeout_add_seconds(20, fu_wistron_dock_device_insert_cb, self);

	/* success */
	return TRUE;
}

static void
fu_wistron_dock_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 20, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 75, "reload");
}

static gchar *
fu_wistron_dock_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32(version_raw, fu_device_get_version_format(device));
}

static void
fu_wistron_dock_device_init(FuWistronDockDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.wistron.dock");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ONLY_WAIT_FOR_REPLUG);
	fu_device_add_request_flag(FU_DEVICE(self), FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	fu_device_set_remove_delay(FU_DEVICE(self), 5 * 60 * 1000);
}

static void
fu_wistron_dock_device_finalize(GObject *object)
{
	FuWistronDockDevice *self = FU_WISTRON_DOCK_DEVICE(object);
	if (self->device_insert_id != 0)
		g_source_remove(self->device_insert_id);
	g_free(self->icp_bbinfo);
	g_free(self->icp_userinfo);
	G_OBJECT_CLASS(fu_wistron_dock_device_parent_class)->finalize(object);
}

static void
fu_wistron_dock_device_class_init(FuWistronDockDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_wistron_dock_device_finalize;
	device_class->to_string = fu_wistron_dock_device_to_string;
	device_class->prepare_firmware = fu_wistron_dock_device_prepare_firmware;
	device_class->write_firmware = fu_wistron_dock_device_write_firmware;
	device_class->attach = fu_wistron_dock_device_attach;
	device_class->detach = fu_wistron_dock_device_detach;
	device_class->setup = fu_wistron_dock_device_setup;
	device_class->cleanup = fu_wistron_dock_device_cleanup;
	device_class->set_progress = fu_wistron_dock_device_set_progress;
	device_class->convert_version = fu_wistron_dock_device_convert_version;
}
