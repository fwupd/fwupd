/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-cfu-device.h"
#include "fu-cfu-module.h"
#include "fu-cfu-struct.h"

struct _FuCfuDevice {
	FuHidDevice parent_instance;
	guint8 protocol_version;
	guint8 version_get_report;
	guint8 offer_set_report;
	guint8 offer_get_report;
	guint8 content_set_report;
	guint8 content_get_report;
};

G_DEFINE_TYPE(FuCfuDevice, fu_cfu_device, FU_TYPE_HID_DEVICE)

#define FU_CFU_DEVICE_TIMEOUT 5000 /* ms */
#define FU_CFU_FEATURE_SIZE   60   /* bytes */

static void
fu_cfu_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuCfuDevice *self = FU_CFU_DEVICE(device);

	/* FuUdevDevice->to_string */
	FU_DEVICE_CLASS(fu_cfu_device_parent_class)->to_string(device, idt, str);

	fu_string_append_kx(str, idt, "ProtocolVersion", self->protocol_version);
	fu_string_append_kx(str, idt, "VersionGetReport", self->version_get_report);
	fu_string_append_kx(str, idt, "OfferSetReport", self->offer_set_report);
	fu_string_append_kx(str, idt, "OfferGetReport", self->offer_get_report);
	fu_string_append_kx(str, idt, "ContentSetReport", self->content_set_report);
	fu_string_append_kx(str, idt, "ContentGetReport", self->content_get_report);
}

static gboolean
fu_cfu_device_write_offer(FuCfuDevice *self,
			  FuFirmware *firmware,
			  FuProgress *progress,
			  FwupdInstallFlags flags,
			  GError **error)
{
	const guint8 *buf;
	gsize bufsz = 0;
	guint8 buf2[FU_CFU_FEATURE_SIZE] = {0};
	g_autofree guint8 *buf_tmp = NULL;
	g_autoptr(GByteArray) st = NULL;
	g_autoptr(GBytes) blob = NULL;

	/* generate a offer blob */
	if (flags & FWUPD_INSTALL_FLAG_FORCE)
		fu_cfu_offer_set_force_ignore_version(FU_CFU_OFFER(firmware), TRUE);
	blob = fu_firmware_write(firmware, error);
	if (blob == NULL)
		return FALSE;

	/* send it to the hardware */
	buf = g_bytes_get_data(blob, &bufsz);
	buf_tmp = fu_memdup_safe(buf, bufsz, error);
	if (buf_tmp == NULL)
		return FALSE;
	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      self->offer_set_report,
				      buf_tmp,
				      bufsz,
				      FU_CFU_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error)) {
		g_prefix_error(error, "failed to send offer: ");
		return FALSE;
	}
	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      self->offer_get_report,
				      buf2,
				      sizeof(buf2),
				      FU_CFU_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error)) {
		return FALSE;
	}
	st = fu_struct_cfu_rsp_firmware_update_offer_parse(buf2, sizeof(buf2), 0x0, error);
	if (st == NULL)
		return FALSE;
	if (fu_struct_cfu_rsp_firmware_update_offer_get_status(st) != FU_CFU_DEVICE_OFFER_ACCEPT) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "not supported: %s",
			    fu_cfu_device_offer_to_string(
				fu_struct_cfu_rsp_firmware_update_offer_get_status(st)));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cfu_device_write_payload(FuCfuDevice *self,
			    FuFirmware *firmware,
			    FuProgress *progress,
			    GError **error)
{
	g_autoptr(GPtrArray) chunks = NULL;

	/* write each chunk */
	chunks = fu_firmware_get_chunks(firmware, error);
	if (chunks == NULL)
		return FALSE;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		g_autoptr(GByteArray) st_req = fu_struct_cfu_req_firmware_update_content_new();
		g_autoptr(GByteArray) st_rsp = fu_struct_cfu_rsp_firmware_update_content_new();

		/* build */
		if (i == 0) {
			fu_struct_cfu_req_firmware_update_content_set_flags(
			    st_req,
			    FU_CFU_DEVICE_FLAG_FIRST_BLOCK);
		} else if (i == chunks->len - 1) {
			fu_struct_cfu_req_firmware_update_content_set_flags(
			    st_req,
			    FU_CFU_DEVICE_FLAG_LAST_BLOCK);
		}
		fu_struct_cfu_req_firmware_update_content_set_data_length(
		    st_req,
		    fu_chunk_get_data_sz(chk));
		fu_struct_cfu_req_firmware_update_content_set_seq_number(st_req, i + 1);
		fu_struct_cfu_req_firmware_update_content_set_address(st_req,
								      fu_chunk_get_address(chk));
		g_byte_array_append(st_req, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));

		/* transfer */
		if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
					      self->content_set_report,
					      st_req->data,
					      st_req->len,
					      FU_CFU_DEVICE_TIMEOUT,
					      FU_HID_DEVICE_FLAG_IS_FEATURE,
					      error)) {
			g_prefix_error(error, "failed to send payload: ");
			return FALSE;
		}
		if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
					      self->content_get_report,
					      st_rsp->data,
					      st_rsp->len,
					      FU_CFU_DEVICE_TIMEOUT,
					      FU_HID_DEVICE_FLAG_IS_FEATURE,
					      error)) {
			return FALSE;
		}

		/* verify */
		if (fu_struct_cfu_rsp_firmware_update_content_get_seq_number(st_rsp) !=
		    fu_struct_cfu_req_firmware_update_content_get_seq_number(st_req)) {
			g_set_error(
			    error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "sequence number invalid 0x%x: expected 0x%x",
			    fu_struct_cfu_rsp_firmware_update_content_get_seq_number(st_rsp),
			    fu_struct_cfu_req_firmware_update_content_get_seq_number(st_req));
			return FALSE;
		}
		if (fu_struct_cfu_rsp_firmware_update_content_get_status(st_rsp) !=
		    FU_CFU_DEVICE_STATUS_SUCCESS) {
			g_set_error(
			    error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "failed to send chunk %u: %s",
			    i + 1,
			    fu_cfu_device_status_to_string(
				fu_struct_cfu_rsp_firmware_update_content_get_status(st_rsp)));
			return FALSE;
		}

		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cfu_device_write_firmware(FuDevice *device,
			     FuFirmware *firmware,
			     FuProgress *progress,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuCfuDevice *self = FU_CFU_DEVICE(device);
	g_autoptr(FuFirmware) fw_offer = NULL;
	g_autoptr(FuFirmware) fw_payload = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "offer");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, "payload");

	/* get both images */
	fw_offer = fu_archive_firmware_get_image_fnmatch(FU_ARCHIVE_FIRMWARE(firmware),
							 "*.offer.bin",
							 error);
	if (fw_offer == NULL)
		return FALSE;
	fw_payload = fu_archive_firmware_get_image_fnmatch(FU_ARCHIVE_FIRMWARE(firmware),
							   "*.payload.bin",
							   error);
	if (fw_payload == NULL)
		return FALSE;

	/* send offer */
	if (!fu_cfu_device_write_offer(self,
				       fw_offer,
				       fu_progress_get_child(progress),
				       flags,
				       error))
		return FALSE;
	fu_progress_step_done(progress);

	/* send payload */
	if (!fu_cfu_device_write_payload(self, fw_payload, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_cfu_device_setup(FuDevice *device, GError **error)
{
	FuCfuDevice *self = FU_CFU_DEVICE(device);
	guint8 buf[FU_CFU_FEATURE_SIZE] = {0};
	guint8 component_cnt = 0;
	gsize offset = 0;
	g_autoptr(GHashTable) modules_by_cid = NULL;
	g_autoptr(GByteArray) st = NULL;

	/* FuHidDevice->setup */
	if (!FU_DEVICE_CLASS(fu_cfu_device_parent_class)->setup(device, error))
		return FALSE;

	/* get version */
	if (!fu_hid_device_get_report(FU_HID_DEVICE(device),
				      self->version_get_report,
				      buf,
				      sizeof(buf),
				      FU_CFU_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error)) {
		return FALSE;
	}
	st = fu_struct_cfu_rsp_get_firmware_version_parse(buf, sizeof(buf), 0x0, error);
	if (st == NULL)
		return FALSE;
	self->protocol_version = fu_struct_cfu_rsp_get_firmware_version_get_flags(st) & 0b1111;

	/* keep track of all modules so we can work out which are dual bank */
	modules_by_cid = g_hash_table_new(g_int_hash, g_int_equal);

	/* read each component module version */
	offset += st->len;
	component_cnt = fu_struct_cfu_rsp_get_firmware_version_get_component_cnt(st);
	for (guint i = 0; i < component_cnt; i++) {
		g_autoptr(FuCfuModule) module = fu_cfu_module_new(device);
		FuCfuModule *module_tmp;

		if (!fu_cfu_module_setup(module, buf, sizeof(buf), offset, error))
			return FALSE;
		fu_device_add_child(device, FU_DEVICE(module));

		/* same module already exists, so mark both as being dual bank */
		module_tmp =
		    g_hash_table_lookup(modules_by_cid,
					GINT_TO_POINTER(fu_cfu_module_get_component_id(module)));
		if (module_tmp != NULL) {
			fu_device_add_flag(FU_DEVICE(module), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
			fu_device_add_flag(FU_DEVICE(module_tmp), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
		} else {
			g_hash_table_insert(modules_by_cid,
					    GINT_TO_POINTER(fu_cfu_module_get_component_id(module)),
					    module);
		}

		/* done */
		offset += 0x8;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cfu_device_set_quirk_kv(FuDevice *device, const gchar *key, const gchar *value, GError **error)
{
	FuCfuDevice *self = FU_CFU_DEVICE(device);

	/* load from quirks */
	if (g_strcmp0(key, "CfuVersionGetReport") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0x0, G_MAXUINT8, error))
			return FALSE;
		self->version_get_report = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "CfuOfferSetReport") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0x0, G_MAXUINT8, error))
			return FALSE;
		self->offer_set_report = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "CfuOfferGetReport") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0x0, G_MAXUINT8, error))
			return FALSE;
		self->offer_get_report = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "CfuContentSetReport") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0x0, G_MAXUINT8, error))
			return FALSE;
		self->content_set_report = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "CfuContentGetReport") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0x0, G_MAXUINT8, error))
			return FALSE;
		self->content_get_report = tmp;
		return TRUE;
	}

	/* failed */
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "quirk key not supported");
	return FALSE;
}

static void
fu_cfu_device_init(FuCfuDevice *self)
{
	/* values taken from CFU/Tools/ComponentFirmwareUpdateStandAloneToolSample/README.md */
	self->version_get_report = 0x62;
	self->offer_set_report = 0x8A;
	self->offer_get_report = 0x8E;
	self->content_set_report = 0x61;
	self->content_get_report = 0x66;
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_MD_SET_SIGNED);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_ARCHIVE_FIRMWARE);
}

static void
fu_cfu_device_class_init(FuCfuDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->setup = fu_cfu_device_setup;
	klass_device->to_string = fu_cfu_device_to_string;
	klass_device->write_firmware = fu_cfu_device_write_firmware;
	klass_device->set_quirk_kv = fu_cfu_device_set_quirk_kv;
}
