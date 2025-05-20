/*
 * Copyright 2024 Mario Limonciello <superm1@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-legion-hid2-child-device.h"
#include "fu-legion-hid2-device.h"
#include "fu-legion-hid2-firmware.h"
#include "fu-legion-hid2-struct.h"

struct _FuLegionHid2Device {
	FuHidDevice parent_instance;
};

G_DEFINE_TYPE(FuLegionHid2Device, fu_legion_hid2_device, FU_TYPE_HID_DEVICE)

#define FU_LEGION_HID2_DEVICE_TIMEOUT 200 /* ms */

static gboolean
fu_legion_hid2_device_transfer(FuLegionHid2Device *self,
			       GByteArray *req,
			       GByteArray *res,
			       GError **error)
{
	if (req != NULL) {
		if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
					      req->data[0],
					      req->data,
					      req->len,
					      FU_LEGION_HID2_DEVICE_TIMEOUT,
					      FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
					      error)) {
			g_prefix_error(error, "failed to send packet: ");
			return FALSE;
		}
	}
	if (res != NULL) {
		if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
					      res->data[0],
					      res->data,
					      res->len,
					      FU_LEGION_HID2_DEVICE_TIMEOUT,
					      FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
					      error)) {
			g_prefix_error(error, "failed to receive packet: ");
			return FALSE;
		}
	}

	return TRUE;
}

static gchar *
fu_legion_hid2_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32(version_raw, fu_device_get_version_format(device));
}

static void
fu_legion_hid2_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 6, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 76, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 17, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static gboolean
fu_legion_hid2_device_ensure_version(FuDevice *device, GError **error)
{
	guint32 version;
	g_autoptr(GByteArray) cmd = fu_struct_legion_get_version_new();
	g_autoptr(GByteArray) result = fu_struct_legion_version_new();

	if (!fu_legion_hid2_device_transfer(FU_LEGION_HID2_DEVICE(device), cmd, result, error))
		return FALSE;
	version = fu_struct_legion_version_get_version(result);
	fu_device_set_version_raw(device, version);

	return TRUE;
}

static gboolean
fu_legion_hid2_device_ensure_mcu_id(FuDevice *device, GError **error)
{
	g_autoptr(GByteArray) cmd = fu_struct_legion_get_mcu_id_new();
	g_autoptr(GByteArray) result = fu_struct_legion_mcu_id_new();

	if (!fu_legion_hid2_device_transfer(FU_LEGION_HID2_DEVICE(device), cmd, result, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_legion_hid2_device_probe(FuDevice *device, GError **error)
{
	if (fu_device_has_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		fu_hid_device_set_interface(FU_HID_DEVICE(device), 0);
		fu_hid_device_set_ep_addr_in(FU_HID_DEVICE(device), 0x81);
		fu_hid_device_set_ep_addr_out(FU_HID_DEVICE(device), 1);
	} else {
		fu_hid_device_set_interface(FU_HID_DEVICE(device), 3);
		fu_hid_device_set_ep_addr_in(FU_HID_DEVICE(device), 0x84);
		fu_hid_device_set_ep_addr_out(FU_HID_DEVICE(device), 0x4);
	}

	/* FuHidDevice->probe */
	if (!FU_DEVICE_CLASS(fu_legion_hid2_device_parent_class)->probe(device, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_legion_hid2_device_setup_child(FuDevice *device)
{
	g_autoptr(FuDevice) child = fu_legion_hid2_child_device_new(device);
	g_autoptr(GError) error_child = NULL;

	if (!fu_device_probe(child, &error_child)) {
		g_info("%s", error_child->message);
		return;
	}

	if (!fu_device_setup(child, &error_child)) {
		g_info("%s", error_child->message);
		return;
	}

	fu_device_add_child(device, child);
}

static gboolean
fu_legion_hid2_device_setup(FuDevice *device, GError **error)
{
	/* HidDevice->setup */
	if (!FU_DEVICE_CLASS(fu_legion_hid2_device_parent_class)->setup(device, error))
		return FALSE;

	/* can't use anything but write and reset in IAP mode */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	if (!fu_legion_hid2_device_ensure_version(device, error))
		return FALSE;

	if (!fu_legion_hid2_device_ensure_mcu_id(device, error))
		return FALSE;

	fu_legion_hid2_device_setup_child(device);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_legion_hid2_device_prepare_firmware(FuDevice *device,
				       GInputStream *stream,
				       FuProgress *progress,
				       FuFirmwareParseFlags flags,
				       GError **error)
{
	guint32 version;
	g_autofree gchar *version_str = NULL;
	g_autoptr(FuFirmware) firmware = fu_legion_hid2_firmware_new();

	/* sanity check */
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;

	version = fu_legion_hid2_firmware_get_version(firmware);
	if (fu_device_get_version_raw(device) > version) {
		version_str = fu_version_from_uint32(version, FWUPD_VERSION_FORMAT_QUAD);
		if ((flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "downgrading from %s to %s is not supported",
				    fu_device_get_version(device),
				    version_str);
			return NULL;
		}
		g_warning("firmware %s is a downgrade but is being force installed anyway",
			  version_str);
	}

	return g_steal_pointer(&firmware);
}

static GByteArray *
fu_legion_hid2_device_tlv(FuLegionHid2Device *self, GByteArray *cmd, GError **error)
{
	g_autoptr(GByteArray) result = fu_struct_legion_iap_tlv_new();
	const guint8 *value;
	guint8 expected;
	guint16 tag;

	if (fu_struct_legion_iap_tlv_get_tag(cmd) == FU_LEGION_IAP_HOST_TAG_IAP_UPDATE)
		expected = FU_LEGION_IAP_ERROR_IAP_CERTIFIED;
	else
		expected = FU_LEGION_IAP_ERROR_IAP_OK;

	if (!fu_legion_hid2_device_transfer(self, cmd, result, error))
		return NULL;

	tag = fu_struct_legion_iap_tlv_get_tag(result);
	if (tag != FU_LEGION_IAP_DEVICE_TAG_IAP_ACK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "failed to transmit TLV, result: %u",
			    tag);
		return NULL;
	}
	value = fu_struct_legion_iap_tlv_get_value(result, NULL);
	if (value[0] != expected) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "failed to transmit TLV, data: %u",
			    value[0]);
		return NULL;
	}

	return g_steal_pointer(&result);
}

static gboolean
fu_legion_hid2_device_unlock_flash(FuLegionHid2Device *self, GError **error)
{
	g_autoptr(GByteArray) cmd = fu_struct_legion_iap_tlv_new();
	g_autoptr(GByteArray) result = NULL;

	fu_struct_legion_iap_tlv_set_tag(cmd, FU_LEGION_IAP_HOST_TAG_IAP_UNLOCK);

	result = fu_legion_hid2_device_tlv(self, cmd, error);
	if (result == NULL) {
		g_prefix_error(error, "failed to unlock: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_legion_hid2_device_verify_signature(FuLegionHid2Device *self, GError **error)
{
	g_autoptr(GByteArray) cmd = fu_struct_legion_iap_tlv_new();
	g_autoptr(GByteArray) result = NULL;

	fu_struct_legion_iap_tlv_set_tag(cmd, FU_LEGION_IAP_HOST_TAG_IAP_UPDATE);

	result = fu_legion_hid2_device_tlv(self, cmd, error);
	if (result == NULL) {
		g_prefix_error(error, "failed to verify signature: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_legion_hid2_device_verify_code(FuLegionHid2Device *self, GError **error)
{
	g_autoptr(GByteArray) cmd = fu_struct_legion_iap_tlv_new();
	g_autoptr(GByteArray) result = NULL;

	fu_struct_legion_iap_tlv_set_tag(cmd, FU_LEGION_IAP_HOST_TAG_IAP_VERIFY);

	result = fu_legion_hid2_device_tlv(self, cmd, error);
	if (result == NULL) {
		g_prefix_error(error, "failed to verify code: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_legion_hid2_device_write_data_chunks(FuLegionHid2Device *self,
					FuChunkArray *chunks,
					FuProgress *progress,
					guint16 tag,
					GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GByteArray) req = fu_struct_legion_iap_tlv_new();
		g_autoptr(GByteArray) res = NULL;

		fu_struct_legion_iap_tlv_set_tag(req, tag);

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		if (!fu_struct_legion_iap_tlv_set_value(req,
							fu_chunk_get_data(chk),
							fu_chunk_get_data_sz(chk),
							error))
			return FALSE;

		fu_struct_legion_iap_tlv_set_length(req, fu_chunk_get_data_sz(chk));

		res = fu_legion_hid2_device_tlv(self, req, error);
		if (res == NULL) {
			g_prefix_error(error, "failed to write data chunks: ");
			return FALSE;
		}

		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_legion_hid2_device_wait_for_complete_cb(FuDevice *device, gpointer user_data, GError **error)
{
	g_autoptr(GByteArray) cmd = fu_struct_legion_iap_tlv_new();
	g_autoptr(GByteArray) result = NULL;
	const guint8 *value;

	fu_struct_legion_iap_tlv_set_tag(cmd, FU_LEGION_IAP_HOST_TAG_IAP_CARRY);

	result = fu_legion_hid2_device_tlv(FU_LEGION_HID2_DEVICE(device), cmd, error);
	if (result == NULL) {
		g_prefix_error(error, "failed to verify code: ");
		return FALSE;
	}
	value = fu_struct_legion_iap_tlv_get_value(result, NULL);
	if (value[1] < 100) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BUSY,
			    "device is %d percent done",
			    value[1]);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_legion_hid2_device_write_data(FuLegionHid2Device *self,
				 FuFirmware *firmware,
				 FuProgress *progress,
				 GError **error)
{
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	stream = fu_firmware_get_image_by_id_stream(firmware, FU_FIRMWARE_ID_PAYLOAD, error);
	if (stream == NULL)
		return FALSE;

	chunks = fu_chunk_array_new_from_stream(stream,
						FU_CHUNK_ADDR_OFFSET_NONE,
						FU_CHUNK_PAGESZ_NONE,
						FU_STRUCT_LEGION_IAP_TLV_SIZE_VALUE,
						error);
	if (chunks == NULL)
		return FALSE;
	if (!fu_legion_hid2_device_write_data_chunks(self,
						     chunks,
						     progress,
						     FU_LEGION_IAP_HOST_TAG_IAP_DATA,
						     error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_legion_hid2_device_write_sig(FuLegionHid2Device *self,
				FuFirmware *firmware,
				FuProgress *progress,
				GError **error)
{
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	stream = fu_firmware_get_image_by_id_stream(firmware, FU_FIRMWARE_ID_SIGNATURE, error);
	if (stream == NULL)
		return FALSE;

	chunks = fu_chunk_array_new_from_stream(stream,
						FU_CHUNK_ADDR_OFFSET_NONE,
						FU_CHUNK_PAGESZ_NONE,
						FU_STRUCT_LEGION_IAP_TLV_SIZE_VALUE,
						error);
	if (chunks == NULL)
		return FALSE;
	if (!fu_legion_hid2_device_write_data_chunks(self,
						     chunks,
						     progress,
						     FU_LEGION_IAP_HOST_TAG_IAP_SIGNATURE,
						     error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_legion_hid2_device_write_firmware(FuDevice *device,
				     FuFirmware *firmware,
				     FuProgress *progress,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuLegionHid2Device *self = FU_LEGION_HID2_DEVICE(device);

	g_return_val_if_fail(device != NULL, FALSE);
	g_return_val_if_fail(FU_IS_FIRMWARE(firmware), FALSE);

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 29, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 29, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 19, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 19, NULL);

	if (!fu_legion_hid2_device_unlock_flash(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	if (!fu_legion_hid2_device_write_data(self,
					      firmware,
					      fu_progress_get_child(progress),
					      error))
		return FALSE;
	fu_progress_step_done(progress);

	if (!fu_legion_hid2_device_write_sig(self,
					     firmware,
					     fu_progress_get_child(progress),
					     error))
		return FALSE;
	fu_progress_step_done(progress);

	if (!fu_legion_hid2_device_verify_signature(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	if (!fu_device_retry_full(device,
				  fu_legion_hid2_device_wait_for_complete_cb,
				  50,
				  200,
				  NULL,
				  error))
		return FALSE;
	fu_progress_step_done(progress);

	if (!fu_legion_hid2_device_verify_code(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* restart dev is moved to attach command! */

	return TRUE;
}

static gboolean
fu_legion_hid2_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	g_autoptr(GByteArray) cmd = NULL;
	g_autoptr(GByteArray) result = NULL;
	guint ret;

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	cmd = fu_struct_legion_start_iap_new();
	result = fu_struct_legion_iap_result_new();

	if (!fu_legion_hid2_device_transfer(FU_LEGION_HID2_DEVICE(device), cmd, result, error))
		return FALSE;

	ret = fu_struct_legion_iap_result_get_ret(result);
	if (ret != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "failed to enable IAP, result: %u",
			    ret);
		return FALSE;
	}

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	return TRUE;
}

static gboolean
fu_legion_hid2_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	g_autoptr(GByteArray) cmd = NULL;
	g_autoptr(GByteArray) result = NULL;
	g_autoptr(GError) error_attach = NULL;

	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	cmd = fu_struct_legion_iap_tlv_new();

	fu_struct_legion_iap_tlv_set_tag(cmd, FU_LEGION_IAP_HOST_TAG_IAP_RESTART);

	result = fu_legion_hid2_device_tlv(FU_LEGION_HID2_DEVICE(device), cmd, &error_attach);
	if (result == NULL)
		g_debug("failed to attach: %s", error_attach->message);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	return TRUE;
}

static void
fu_legion_hid2_device_init(FuLegionHid2Device *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_protocol(FU_DEVICE(self), "com.lenovo.legion-hid2");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
}

static void
fu_legion_hid2_device_class_init(FuLegionHid2DeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_legion_hid2_device_setup;
	device_class->probe = fu_legion_hid2_device_probe;
	device_class->prepare_firmware = fu_legion_hid2_device_prepare_firmware;
	device_class->convert_version = fu_legion_hid2_device_convert_version;
	device_class->write_firmware = fu_legion_hid2_device_write_firmware;
	device_class->detach = fu_legion_hid2_device_detach;
	device_class->attach = fu_legion_hid2_device_attach;
	device_class->set_progress = fu_legion_hid2_device_set_progress;
}
