/*
 * Copyright 2022 Haowei Lo <haowei.lo@fingerprints.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-fpc-device.h"
#include "fu-fpc-ff2-firmware.h"
#include "fu-fpc-struct.h"

#define FPC_USB_INTERFACE	     0
#define FPC_USB_TRANSFER_TIMEOUT     1500 /* ms */
#define FPC_FLASH_BLOCK_SIZE_DEFAULT 2048 /* 2048 */
#define FPC_FLASH_BLOCK_SIZE_4096    4096 /* 4096 */

#define FPC_CMD_DFU_DETACH	  0x00
#define FPC_CMD_DFU_DNLOAD	  0x01
#define FPC_CMD_DFU_GETSTATUS	  0x03
#define FPC_CMD_DFU_CLRSTATUS	  0x04
#define FPC_CMD_DFU_GET_FW_STATUS 0x09
#define FPC_CMD_DFU_DNLOAD_FF2	  0x10

#define FPC_CMD_BOOT0		0x04
#define FPC_CMD_GET_STATE	0x0B
#define FPC_CMD_GET_STATE_LENFY 0x50

#define FPC_DEVICE_MOC_STATE_LEN     68
#define FPC_DEVICE_MOH_STATE_LEN     72
#define FPC_DEVICE_DFU_FW_STATUS_LEN 8
#define FPC_DFU_MAX_ATTEMPTS	     50

#define FPC_DEVICE_DFU_MODE_CLASS    0xFE
#define FPC_DEVICE_DFU_MODE_PORT     0x02
#define FPC_DEVICE_NORMAL_MODE_CLASS 0xFF
#define FPC_DEVICE_NORMAL_MODE_PORT  0xFF

#define FPC_FF2_BLK_SEC_LINK_LEN 100

#define FU_FPC_DEVICE_FLAG_MOH_DEVICE	"moh-device"
#define FU_FPC_DEVICE_FLAG_LEGACY_DFU	"legacy-dfu"
#define FU_FPC_DEVICE_FLAG_RTS_DEVICE	"rts"
#define FU_FPC_DEVICE_FLAG_LENFY_DEVICE "lenfy"

struct _FuFpcDevice {
	FuUsbDevice parent_instance;
	guint32 max_block_size;
};

G_DEFINE_TYPE(FuFpcDevice, fu_fpc_device, FU_TYPE_USB_DEVICE)

static FuFirmware *
fu_fpc_device_prepare_firmware(FuDevice *device,
			       GInputStream *stream,
			       FuProgress *progress,
			       FwupdInstallFlags flags,
			       GError **error)
{
	return fu_firmware_new_from_gtypes(stream,
					   0x0,
					   flags,
					   error,
					   FU_TYPE_FPC_FF2_FIRMWARE,
					   FU_TYPE_FIRMWARE,
					   G_TYPE_INVALID);
}

static gboolean
fu_fpc_device_dfu_cmd(FuFpcDevice *self,
		      guint8 request,
		      guint16 value,
		      guint8 *data,
		      gsize length,
		      gboolean device2host,
		      gboolean type_vendor,
		      GError **error)
{
	gsize actual_len = 0;

	if (!fu_usb_device_control_transfer(
		FU_USB_DEVICE(self),
		device2host ? FU_USB_DIRECTION_DEVICE_TO_HOST : FU_USB_DIRECTION_HOST_TO_DEVICE,
		type_vendor ? FU_USB_REQUEST_TYPE_VENDOR : FU_USB_REQUEST_TYPE_CLASS,
		FU_USB_RECIPIENT_INTERFACE,
		request,
		value,
		0x0000,
		data,
		length,
		length ? &actual_len : NULL,
		FPC_USB_TRANSFER_TIMEOUT,
		NULL,
		error)) {
		fu_error_convert(error);
		return FALSE;
	}
	if (actual_len != length) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "only sent 0x%04x of 0x%04x",
			    (guint)actual_len,
			    (guint)length);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_fpc_device_fw_cmd(FuFpcDevice *self,
		     guint8 request,
		     guint8 *data,
		     gsize length,
		     gboolean device2host,
		     GError **error)
{
	gsize actual_len = 0;

	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    device2host ? FU_USB_DIRECTION_DEVICE_TO_HOST
							: FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    request,
					    0x0000,
					    0x0000,
					    data,
					    length,
					    length ? &actual_len : NULL,
					    FPC_USB_TRANSFER_TIMEOUT,
					    NULL,
					    error)) {
		fu_error_convert(error);
		return FALSE;
	}
	if (actual_len != length) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "only sent 0x%04x of 0x%04x",
			    (guint)actual_len,
			    (guint)length);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_fpc_device_setup_mode(FuFpcDevice *self, GError **error)
{
	g_autoptr(GPtrArray) intfs = NULL;

	intfs = fu_usb_device_get_interfaces(FU_USB_DEVICE(self), error);
	if (intfs == NULL)
		return FALSE;
	for (guint i = 0; i < intfs->len; i++) {
		FuUsbInterface *intf = g_ptr_array_index(intfs, i);
		if (fu_usb_interface_get_class(intf) == FPC_DEVICE_DFU_MODE_CLASS &&
		    fu_usb_interface_get_protocol(intf) == FPC_DEVICE_DFU_MODE_PORT) {
			fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
			return TRUE;
		}
		if (fu_usb_interface_get_class(intf) == FPC_DEVICE_NORMAL_MODE_CLASS &&
		    fu_usb_interface_get_protocol(intf) == FPC_DEVICE_NORMAL_MODE_PORT) {
			fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
			return TRUE;
		}
	}
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no update interface found");
	return FALSE;
}

static gboolean
fu_fpc_device_setup_version(FuFpcDevice *self, GError **error)
{
	guint32 version = 0;
	gsize data_len = 0;
	FuEndianType endian_type = G_LITTLE_ENDIAN;
	g_autofree guint8 *data = NULL;
	guint32 cmd_id = FPC_CMD_GET_STATE;

	if (fu_device_has_private_flag(FU_DEVICE(self), FU_FPC_DEVICE_FLAG_RTS_DEVICE))
		endian_type = G_BIG_ENDIAN;

	if (!fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		if (fu_device_has_private_flag(FU_DEVICE(self), FU_FPC_DEVICE_FLAG_MOH_DEVICE)) {
			data_len = FPC_DEVICE_MOH_STATE_LEN;
		} else {
			data_len = FPC_DEVICE_MOC_STATE_LEN;
		}

		data = g_malloc0(data_len);
		if (fu_device_has_private_flag(FU_DEVICE(self), FU_FPC_DEVICE_FLAG_LENFY_DEVICE))
			cmd_id = FPC_CMD_GET_STATE_LENFY;
		if (!fu_fpc_device_fw_cmd(self, cmd_id, data, data_len, TRUE, error))
			return FALSE;

		if (!fu_memread_uint32_safe(data, data_len, 0, &version, endian_type, error))
			return FALSE;

	} else {
		if (!fu_device_has_private_flag(FU_DEVICE(self), FU_FPC_DEVICE_FLAG_LEGACY_DFU)) {
			if (!fu_fpc_device_dfu_cmd(self,
						   FPC_CMD_DFU_CLRSTATUS,
						   0x0000,
						   NULL,
						   0,
						   FALSE,
						   FALSE,
						   error)) {
				g_prefix_error(error, "fail to clear status in setup version");
				return FALSE;
			}
		}

		data = g_malloc0(FPC_DEVICE_DFU_FW_STATUS_LEN);
		if (!fu_fpc_device_dfu_cmd(self,
					   FPC_CMD_DFU_GET_FW_STATUS,
					   0x0000,
					   data,
					   FPC_DEVICE_DFU_FW_STATUS_LEN,
					   TRUE,
					   TRUE,
					   error)) {
			g_prefix_error(error, "fail to get fw status in setup version");
			return FALSE;
		}

		if (!fu_memread_uint32_safe(data,
					    FPC_DEVICE_DFU_FW_STATUS_LEN,
					    4,
					    &version,
					    endian_type,
					    error))
			return FALSE;
	}

	/* set display version */
	fu_device_set_version_raw(FU_DEVICE(self), version);
	return TRUE;
}

static gboolean
fu_fpc_device_check_dfu_status_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuFpcDevice *self = FU_FPC_DEVICE(device);
	g_autoptr(GByteArray) dfu_status = fu_struct_fpc_dfu_new();

	if (!fu_fpc_device_dfu_cmd(self,
				   FPC_CMD_DFU_GETSTATUS,
				   0x0000,
				   dfu_status->data,
				   dfu_status->len,
				   TRUE,
				   FALSE,
				   error)) {
		g_prefix_error(error, "failed to get status: ");
		return FALSE;
	}

	if (fu_struct_fpc_dfu_get_status(dfu_status) != 0 ||
	    fu_struct_fpc_dfu_get_state(dfu_status) == FU_FPC_DFU_STATE_DNBUSY) {
		/* device is not in correct status/state */
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "dfu status error [0x%x, 0x%x]",
			    fu_struct_fpc_dfu_get_status(dfu_status),
			    fu_struct_fpc_dfu_get_state(dfu_status));
		return FALSE;
	}

	if (fu_struct_fpc_dfu_get_max_payload_size(dfu_status) > 0 ||
	    fu_device_has_private_flag(FU_DEVICE(self), FU_FPC_DEVICE_FLAG_RTS_DEVICE))
		self->max_block_size = FPC_FLASH_BLOCK_SIZE_4096;
	else
		self->max_block_size = FPC_FLASH_BLOCK_SIZE_DEFAULT;

	return TRUE;
}

static gboolean
fu_fpc_device_update_init(FuFpcDevice *self, GError **error)
{
	if (!fu_device_has_private_flag(FU_DEVICE(self), FU_FPC_DEVICE_FLAG_LEGACY_DFU)) {
		if (!fu_fpc_device_dfu_cmd(self,
					   FPC_CMD_DFU_CLRSTATUS,
					   0x0000,
					   NULL,
					   0,
					   FALSE,
					   FALSE,
					   error)) {
			g_prefix_error(error, "failed to clear status: ");
			return FALSE;
		}
	}
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_fpc_device_check_dfu_status_cb,
				    FPC_DFU_MAX_ATTEMPTS,
				    20,
				    NULL,
				    error);
}

static void
fu_fpc_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuFpcDevice *self = FU_FPC_DEVICE(device);

	fwupd_codec_string_append_hex(str, idt, "MaxBlockSize", self->max_block_size);
	fwupd_codec_string_append_bool(
	    str,
	    idt,
	    "LegacyDfu",
	    fu_device_has_private_flag(device, FU_FPC_DEVICE_FLAG_LEGACY_DFU));
	fwupd_codec_string_append_bool(
	    str,
	    idt,
	    "MocDevice",
	    !fu_device_has_private_flag(device, FU_FPC_DEVICE_FLAG_MOH_DEVICE));
	if (fu_device_has_private_flag(device, FU_FPC_DEVICE_FLAG_MOH_DEVICE)) {
		fwupd_codec_string_append_bool(
		    str,
		    idt,
		    "RtsDevice",
		    fu_device_has_private_flag(device, FU_FPC_DEVICE_FLAG_RTS_DEVICE));
	}
}

static gboolean
fu_fpc_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuFpcDevice *self = FU_FPC_DEVICE(device);

	/* sanity check */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in runtime mode, skipping");
		return TRUE;
	}
	if (!fu_fpc_device_dfu_cmd(self, FPC_CMD_DFU_DETACH, 0x0000, NULL, 0, FALSE, FALSE, error))
		return FALSE;
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_fpc_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuFpcDevice *self = FU_FPC_DEVICE(device);

	/* sanity check */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in bootloader mode, skipping");
		return TRUE;
	}
	if (!fu_fpc_device_fw_cmd(self, FPC_CMD_BOOT0, NULL, 0, FALSE, error))
		return FALSE;
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_fpc_device_setup(FuDevice *device, GError **error)
{
	FuFpcDevice *self = FU_FPC_DEVICE(device);
	g_autofree gchar *name = NULL;

	/* setup */
	if (!FU_DEVICE_CLASS(fu_fpc_device_parent_class)->setup(device, error))
		return FALSE;

	/* remove the ' L:0001 FW:27.26.23.18' suffix */
	name = g_strdup(fu_device_get_name(device));
	if (name != NULL) {
		gchar *tmp = g_strstr_len(name, -1, " L:00");
		if (tmp != NULL)
			*tmp = '\0';
		fu_device_set_name(device, name);
	}

	if (!fu_fpc_device_setup_mode(self, error)) {
		g_prefix_error(error, "failed to get device mode: ");
		return FALSE;
	}

	/* ensure version */
	if (!fu_fpc_device_setup_version(self, error)) {
		g_prefix_error(error, "failed to get firmware version: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_fpc_device_write_ff2_blocks(FuFpcDevice *self, GInputStream *stream, GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;

	chunks = fu_chunk_array_new_from_stream(stream, 0x0, FPC_FLASH_BLOCK_SIZE_4096, error);
	if (chunks == NULL)
		return FALSE;
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_fpc_device_dfu_cmd(self,
					   FPC_CMD_DFU_DNLOAD_FF2,
					   0,
					   (guint8 *)fu_chunk_get_data(chk),
					   fu_chunk_get_data_sz(chk),
					   FALSE,
					   FALSE,
					   error)) {
			g_prefix_error(error,
				       "failed to write at 0x%x: ",
				       (guint)fu_chunk_get_address(chk));
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_fpc_device_write_ff2_firmware(FuFpcDevice *self,
				 FuFpcFf2Firmware *firmware,
				 FuProgress *progress,
				 FwupdInstallFlags flags,
				 GError **error)
{
	gsize offset = 0;
	guint32 blocks_num;
	g_autoptr(GInputStream) stream = NULL;

	stream = fu_firmware_get_stream(FU_FIRMWARE(firmware), error);
	if (stream == NULL)
		return FALSE;
	blocks_num = fu_fpc_ff2_firmware_get_blocks_num(firmware);
	offset += FU_STRUCT_FPC_FF2_HDR_SIZE;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, blocks_num);
	for (guint i = 0; i < blocks_num; i++) {
		FuFpcFf2BlockDir direction;
		g_autoptr(FuStructFpcFf2BlockHdr) st_blkhdr = NULL;
		g_autoptr(FuStructFpcFf2BlockSec) st_blksec = NULL;
		guint16 payload_len;

		/* parse dfu_meta_content_hdr_t */
		st_blkhdr = fu_struct_fpc_ff2_block_hdr_parse_stream(stream, offset, error);
		if (st_blkhdr == NULL)
			return FALSE;
		direction = fu_struct_fpc_ff2_block_hdr_get_dir(st_blkhdr);
		offset += st_blkhdr->len;

		/* validate dfu_sec_link_t and include the size in payload */
		st_blksec = fu_struct_fpc_ff2_block_sec_parse_stream(stream, offset, error);
		if (st_blksec == NULL)
			return FALSE;
		payload_len = fu_struct_fpc_ff2_block_sec_get_payload_len(st_blksec);
		payload_len += st_blksec->len;

		if (direction == FU_FPC_FF2_BLOCK_DIR_OUT) {
			g_autoptr(GInputStream) partial_stream = NULL;
			g_autoptr(GByteArray) buf_sec = NULL;

			/* write sec-link chunk? */
			buf_sec = fu_input_stream_read_byte_array(stream,
								  offset,
								  FPC_FF2_BLK_SEC_LINK_LEN,
								  fu_progress_get_child(progress),
								  error);
			if (buf_sec == NULL)
				return FALSE;
			if (!fu_fpc_device_dfu_cmd(self,
						   FPC_CMD_DFU_DNLOAD_FF2,
						   0,
						   buf_sec->data,
						   buf_sec->len,
						   FALSE,
						   FALSE,
						   error)) {
				g_prefix_error(error, "failed to write sec-link: ");
				return FALSE;
			}

			/* write data in 4k blocks */
			partial_stream =
			    fu_partial_input_stream_new(stream,
							offset + FPC_FF2_BLK_SEC_LINK_LEN,
							payload_len - FPC_FF2_BLK_SEC_LINK_LEN,
							error);
			if (partial_stream == NULL)
				return FALSE;
			if (!fu_fpc_device_write_ff2_blocks(self, partial_stream, error))
				return FALSE;
		} else if (direction == FU_FPC_FF2_BLOCK_DIR_IN) {
			if (!fu_device_retry_full(FU_DEVICE(self),
						  fu_fpc_device_check_dfu_status_cb,
						  FPC_DFU_MAX_ATTEMPTS,
						  20,
						  NULL,
						  error))
				return FALSE;
		} else {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "unsupported direction");
			return FALSE;
		}

		/* there is a block terminator of 0xFF */
		offset += payload_len + sizeof(guint8);
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_fpc_device_write_firmware(FuDevice *device,
			     FuFirmware *firmware,
			     FuProgress *progress,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuFpcDevice *self = FU_FPC_DEVICE(device);
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* new format */
	if (FU_IS_FPC_FF2_FIRMWARE(firmware)) {
		return fu_fpc_device_write_ff2_firmware(self,
							FU_FPC_FF2_FIRMWARE(firmware),
							progress,
							flags,
							error);
	}

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "init");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 95, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, "check");

	/*  write old fw format */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	/* don't auto-boot firmware */
	if (!fu_fpc_device_update_init(self, &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "failed to initial update: %s",
			    error_local->message);
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* build packets */
	chunks = fu_chunk_array_new_from_stream(stream, 0x00, self->max_block_size, error);
	if (chunks == NULL)
		return FALSE;

	/* write each block */
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GByteArray) req = g_byte_array_new();

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		g_byte_array_append(req, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));

		if (!fu_fpc_device_dfu_cmd(self,
					   FPC_CMD_DFU_DNLOAD,
					   (guint16)i,
					   req->data,
					   (gsize)req->len,
					   FALSE,
					   FALSE,
					   &error_local)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "failed to write: %s",
				    error_local->message);
			return FALSE;
		}

		if (!fu_device_retry_full(device,
					  fu_fpc_device_check_dfu_status_cb,
					  FPC_DFU_MAX_ATTEMPTS,
					  20,
					  NULL,
					  &error_local)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "failed to write: %s",
				    error_local->message);
			return FALSE;
		}

		/* update progress */
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						(gsize)i + 1,
						(gsize)fu_chunk_array_length(chunks));
	}

	if (!fu_device_has_private_flag(FU_DEVICE(self), FU_FPC_DEVICE_FLAG_LEGACY_DFU)) {
		/* exit fw download loop. send null package */
		if (!fu_fpc_device_dfu_cmd(self,
					   FPC_CMD_DFU_DNLOAD,
					   0,
					   NULL,
					   0,
					   FALSE,
					   FALSE,
					   error)) {
			g_prefix_error(error, "fail to exit dnload loop: ");
			return FALSE;
		}
	}
	fu_progress_step_done(progress);

	if (!fu_device_retry_full(device,
				  fu_fpc_device_check_dfu_status_cb,
				  FPC_DFU_MAX_ATTEMPTS,
				  20,
				  NULL,
				  error))
		return FALSE;

	fu_progress_step_done(progress);

	/* success! */
	return TRUE;
}

static void
fu_fpc_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static gchar *
fu_fpc_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32(version_raw, fu_device_get_version_format(device));
}

static void
fu_fpc_device_init(FuFpcDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_RUNTIME_VERSION);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_remove_delay(FU_DEVICE(self), 10000);
	fu_device_add_protocol(FU_DEVICE(self), "com.fingerprints.dfupc");
	fu_device_set_summary(FU_DEVICE(self), "FPC fingerprint sensor");
	fu_device_set_install_duration(FU_DEVICE(self), 15);
	fu_device_set_firmware_size_min(FU_DEVICE(self), 0x10000);
	fu_device_set_firmware_size_max(FU_DEVICE(self), 0x64000);
	fu_usb_device_add_interface(FU_USB_DEVICE(self), FPC_USB_INTERFACE);
	fu_device_register_private_flag(FU_DEVICE(self), FU_FPC_DEVICE_FLAG_MOH_DEVICE);
	fu_device_register_private_flag(FU_DEVICE(self), FU_FPC_DEVICE_FLAG_RTS_DEVICE);
	fu_device_register_private_flag(FU_DEVICE(self), FU_FPC_DEVICE_FLAG_LEGACY_DFU);
	fu_device_register_private_flag(FU_DEVICE(self), FU_FPC_DEVICE_FLAG_LENFY_DEVICE);
}

static void
fu_fpc_device_class_init(FuFpcDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_fpc_device_to_string;
	device_class->prepare_firmware = fu_fpc_device_prepare_firmware;
	device_class->write_firmware = fu_fpc_device_write_firmware;
	device_class->setup = fu_fpc_device_setup;
	device_class->reload = fu_fpc_device_setup;
	device_class->attach = fu_fpc_device_attach;
	device_class->detach = fu_fpc_device_detach;
	device_class->set_progress = fu_fpc_device_set_progress;
	device_class->convert_version = fu_fpc_device_convert_version;
}
