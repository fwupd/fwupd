/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-cfu-device.h"
#include "fu-cfu-module.h"
#include "fu-cfu-struct.h"

typedef struct {
	guint8 op;
	guint8 id;
	guint8 ct;
} FuCfuDeviceMap;

struct _FuCfuDevice {
	FuHidDevice parent_instance;
	guint8 protocol_version;
	FuCfuDeviceMap version_get_report;
	FuCfuDeviceMap offer_set_report;
	FuCfuDeviceMap offer_get_report;
	FuCfuDeviceMap content_set_report;
	FuCfuDeviceMap content_get_report;
};

G_DEFINE_TYPE(FuCfuDevice, fu_cfu_device, FU_TYPE_HID_DEVICE)

#define FU_CFU_DEVICE_TIMEOUT 5000 /* ms */

#define FU_CFU_DEVICE_FLAG_SEND_OFFER_INFO (1 << 0)

static void
fu_cfu_device_map_to_string(GString *str, guint idt, FuCfuDeviceMap *map, const gchar *title)
{
	g_autofree gchar *title_op = g_strdup_printf("%sOp", title);
	g_autofree gchar *title_id = g_strdup_printf("%sId", title);
	g_autofree gchar *title_ct = g_strdup_printf("%sCt", title);
	fu_string_append_kx(str, idt, title_op, map->op);
	fu_string_append_kx(str, idt, title_id, map->id);
	fu_string_append_kx(str, idt, title_ct, map->ct);
}

static void
fu_cfu_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuCfuDevice *self = FU_CFU_DEVICE(device);

	/* FuUdevDevice->to_string */
	FU_DEVICE_CLASS(fu_cfu_device_parent_class)->to_string(device, idt, str);

	fu_string_append_kx(str, idt, "ProtocolVersion", self->protocol_version);
	fu_cfu_device_map_to_string(str, idt, &self->version_get_report, "VersionGetReport");
	fu_cfu_device_map_to_string(str, idt, &self->offer_set_report, "OfferSetReport");
	fu_cfu_device_map_to_string(str, idt, &self->offer_get_report, "OfferGetReport");
	fu_cfu_device_map_to_string(str, idt, &self->content_set_report, "ContentSetReport");
	fu_cfu_device_map_to_string(str, idt, &self->content_get_report, "ContentGetReport");
}

static gboolean
fu_cfu_device_send_offer_info(FuCfuDevice *self, FuCfuOfferInfoCode info_code, GError **error)
{
	g_autoptr(GByteArray) buf_in = g_byte_array_new();
	g_autoptr(GByteArray) buf_out = g_byte_array_new();
	g_autoptr(GByteArray) st_req = fu_struct_cfu_offer_info_req_new();
	g_autoptr(GByteArray) st_res = NULL;

	/* not all devices handle this */
	if (!fu_device_has_private_flag(FU_DEVICE(self), FU_CFU_DEVICE_FLAG_SEND_OFFER_INFO))
		return TRUE;

	/* SetReport */
	fu_struct_cfu_offer_info_req_set_code(st_req, info_code);
	fu_byte_array_append_uint8(buf_out, self->offer_set_report.id);
	g_byte_array_append(buf_out, st_req->data, st_req->len);
	fu_byte_array_set_size(buf_out, self->offer_set_report.ct, 0x0);
	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      self->offer_set_report.id,
				      buf_out->data,
				      buf_out->len,
				      FU_CFU_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		g_prefix_error(error, "failed to send offer info: ");
		return FALSE;
	}

	/* GetReport */
	fu_byte_array_append_uint8(buf_in, self->offer_get_report.id);
	fu_byte_array_set_size(buf_in, self->offer_get_report.ct + 0x1, 0x0);
	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      self->offer_get_report.id,
				      buf_in->data,
				      buf_in->len,
				      FU_CFU_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
				      error)) {
		g_prefix_error(error, "failed to send offer info: ");
		return FALSE;
	}
	st_res = fu_struct_cfu_offer_rsp_parse(buf_in->data, buf_in->len, 0x1, error);
	if (st_res == NULL)
		return FALSE;
	if (fu_struct_cfu_offer_rsp_get_token(st_res) !=
	    FU_STRUCT_CFU_OFFER_INFO_REQ_DEFAULT_TOKEN) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "token invalid: got 0x%x and expected 0x%x",
			    fu_struct_cfu_offer_rsp_get_token(st_res),
			    (guint)FU_STRUCT_CFU_OFFER_INFO_REQ_DEFAULT_TOKEN);
		return FALSE;
	}
	if (fu_struct_cfu_offer_rsp_get_status(st_res) != FU_CFU_OFFER_STATUS_ACCEPT) {
		g_set_error(
		    error,
		    G_IO_ERROR,
		    G_IO_ERROR_NOT_SUPPORTED,
		    "offer info %s not supported: %s",
		    fu_cfu_offer_info_code_to_string(info_code),
		    fu_cfu_offer_status_to_string(fu_struct_cfu_offer_rsp_get_status(st_res)));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cfu_device_send_offer(FuCfuDevice *self,
			 FuFirmware *firmware,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error)
{
	g_autoptr(GByteArray) buf_in = g_byte_array_new();
	g_autoptr(GByteArray) buf_out = g_byte_array_new();
	g_autoptr(GByteArray) st = NULL;
	g_autoptr(GBytes) blob = NULL;

	/* generate a offer blob */
	if (flags & FWUPD_INSTALL_FLAG_FORCE)
		fu_cfu_offer_set_force_ignore_version(FU_CFU_OFFER(firmware), TRUE);
	blob = fu_firmware_write(firmware, error);
	if (blob == NULL)
		return FALSE;

	/* SetReport */
	fu_byte_array_append_uint8(buf_out, self->offer_set_report.id);
	fu_byte_array_append_bytes(buf_out, blob);
	fu_byte_array_set_size(buf_out, self->offer_set_report.ct, 0x0);
	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      self->offer_set_report.id,
				      buf_out->data,
				      buf_out->len,
				      FU_CFU_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		g_prefix_error(error, "failed to send offer: ");
		return FALSE;
	}

	/* GetReport */
	fu_byte_array_append_uint8(buf_in, self->offer_get_report.id);
	fu_byte_array_set_size(buf_in, self->offer_get_report.ct + 0x1, 0x0);
	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      self->offer_get_report.id,
				      buf_in->data,
				      buf_in->len,
				      FU_CFU_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
				      error)) {
		g_prefix_error(error, "failed to get offer response: ");
		return FALSE;
	}
	st = fu_struct_cfu_offer_rsp_parse(buf_in->data, buf_in->len, 0x1, error);
	if (st == NULL)
		return FALSE;
	if (fu_struct_cfu_offer_rsp_get_token(st) !=
	    fu_cfu_offer_get_token(FU_CFU_OFFER(firmware))) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "offer token invalid: got %02x but expected %02x",
			    fu_struct_cfu_offer_rsp_get_token(st),
			    fu_cfu_offer_get_token(FU_CFU_OFFER(firmware)));
		return FALSE;
	}
	if (fu_struct_cfu_offer_rsp_get_status(st) != FU_CFU_OFFER_STATUS_ACCEPT) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "offer not supported: %s: %s",
			    fu_cfu_offer_status_to_string(fu_struct_cfu_offer_rsp_get_status(st)),
			    fu_cfu_rr_code_to_string(fu_struct_cfu_offer_rsp_get_rr_code(st)));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cfu_device_send_payload(FuCfuDevice *self,
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
		g_autoptr(GByteArray) buf_in = g_byte_array_new();
		g_autoptr(GByteArray) buf_out = g_byte_array_new();
		g_autoptr(GByteArray) st_req = fu_struct_cfu_content_req_new();
		g_autoptr(GByteArray) st_rsp = NULL;

		/* build */
		if (i == 0) {
			fu_struct_cfu_content_req_set_flags(st_req,
							    FU_CFU_CONTENT_FLAG_FIRST_BLOCK);
		} else if (i == chunks->len - 1) {
			fu_struct_cfu_content_req_set_flags(st_req, FU_CFU_CONTENT_FLAG_LAST_BLOCK);
		}
		fu_struct_cfu_content_req_set_data_length(st_req, fu_chunk_get_data_sz(chk));
		fu_struct_cfu_content_req_set_seq_number(st_req, i);
		fu_struct_cfu_content_req_set_address(st_req, fu_chunk_get_address(chk));

		fu_byte_array_append_uint8(buf_out, self->content_set_report.id);
		g_byte_array_append(buf_out, st_req->data, st_req->len);
		g_byte_array_append(buf_out, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
		fu_byte_array_set_size(buf_out, self->content_set_report.ct + 1, 0x0);

		/* SetReport */
		if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
					      self->content_set_report.id,
					      buf_out->data,
					      buf_out->len,
					      FU_CFU_DEVICE_TIMEOUT,
					      FU_HID_DEVICE_FLAG_NONE,
					      error)) {
			g_prefix_error(error, "failed to send payload: ");
			return FALSE;
		}

		/* GetReport */
		fu_byte_array_append_uint8(buf_in, self->content_get_report.id);
		fu_byte_array_set_size(buf_in, self->content_get_report.ct + 1, 0x0);
		if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
					      self->content_get_report.id,
					      buf_in->data,
					      buf_in->len,
					      FU_CFU_DEVICE_TIMEOUT,
					      FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
					      error)) {
			g_prefix_error(error, "failed to get payload response: ");
			return FALSE;
		}
		st_rsp = fu_struct_cfu_content_rsp_parse(buf_in->data, buf_in->len, 0x1, error);
		if (st_rsp == NULL)
			return FALSE;

		/* verify */
		if (fu_struct_cfu_content_rsp_get_seq_number(st_rsp) !=
		    fu_struct_cfu_content_req_get_seq_number(st_req)) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "sequence number invalid 0x%x: expected 0x%x",
				    fu_struct_cfu_content_rsp_get_seq_number(st_rsp),
				    fu_struct_cfu_content_req_get_seq_number(st_req));
			return FALSE;
		}
		if (fu_struct_cfu_content_rsp_get_status(st_rsp) != FU_CFU_CONTENT_STATUS_SUCCESS) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "failed to send chunk %u: %s",
				    i + 1,
				    fu_cfu_content_status_to_string(
					fu_struct_cfu_content_rsp_get_status(st_rsp)));
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
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "start-entire");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "start-offer");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "offer");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, "payload");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "end-offer");

	/* get both images */
	fw_offer = fu_firmware_get_image_by_id(firmware, FU_FIRMWARE_ID_HEADER, error);
	if (fw_offer == NULL)
		return FALSE;
	fw_payload = fu_firmware_get_image_by_id(firmware, FU_FIRMWARE_ID_PAYLOAD, error);
	if (fw_payload == NULL)
		return FALSE;

	/* host is now initialized */
	if (!fu_cfu_device_send_offer_info(self,
					   FU_CFU_OFFER_INFO_CODE_START_ENTIRE_TRANSACTION,
					   error))
		return FALSE;
	fu_progress_step_done(progress);

	/* send offer */
	if (!fu_cfu_device_send_offer_info(self, FU_CFU_OFFER_INFO_CODE_START_OFFER_LIST, error))
		return FALSE;
	fu_progress_step_done(progress);
	if (!fu_cfu_device_send_offer(self,
				      fw_offer,
				      fu_progress_get_child(progress),
				      flags,
				      error))
		return FALSE;
	fu_progress_step_done(progress);

	/* send payload */
	if (!fu_cfu_device_send_payload(self, fw_payload, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* all done */
	if (!fu_cfu_device_send_offer_info(self, FU_CFU_OFFER_INFO_CODE_END_OFFER_LIST, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

/* find report properties from usage */
static gboolean
fu_cfu_device_ensure_map_item(FuHidDescriptor *descriptor, FuCfuDeviceMap *map, GError **error)
{
	g_autoptr(FuFirmware) item_ct = NULL;
	g_autoptr(FuFirmware) item_id = NULL;
	g_autoptr(FuHidReport) report = NULL;

	report = fu_hid_descriptor_find_report(FU_HID_DESCRIPTOR(descriptor),
					       error,
					       "usage",
					       map->op,
					       NULL);
	if (report == NULL)
		return FALSE;
	item_id = fu_firmware_get_image_by_id(FU_FIRMWARE(report), "report-id", error);
	if (item_id == NULL)
		return FALSE;
	map->id = fu_hid_report_item_get_value(FU_HID_REPORT_ITEM(item_id));
	item_ct = fu_firmware_get_image_by_id(FU_FIRMWARE(report), "report-count", error);
	if (item_ct == NULL)
		return FALSE;
	map->ct = fu_hid_report_item_get_value(FU_HID_REPORT_ITEM(item_ct));
	return TRUE;
}

static gboolean
fu_cfu_device_setup(FuDevice *device, GError **error)
{
	FuCfuDevice *self = FU_CFU_DEVICE(device);
	guint8 component_cnt = 0;
	gsize offset = 0;
	g_autoptr(GHashTable) modules_by_cid = NULL;
	g_autoptr(GByteArray) st = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(FuHidDescriptor) descriptor = NULL;

	/* FuHidDevice->setup */
	if (!FU_DEVICE_CLASS(fu_cfu_device_parent_class)->setup(device, error))
		return FALSE;

	/* weirdly, use the in EP if out is missing */
	if (fu_hid_device_get_ep_addr_out(FU_HID_DEVICE(device)) == 0x0) {
		fu_hid_device_set_ep_addr_out(FU_HID_DEVICE(device),
					      fu_hid_device_get_ep_addr_in(FU_HID_DEVICE(device)));
	}

	descriptor = fu_hid_device_parse_descriptor(FU_HID_DEVICE(device), error);
	if (descriptor == NULL)
		return FALSE;
	if (!fu_cfu_device_ensure_map_item(descriptor, &self->version_get_report, error))
		return FALSE;
	if (!fu_cfu_device_ensure_map_item(descriptor, &self->offer_set_report, error))
		return FALSE;
	if (!fu_cfu_device_ensure_map_item(descriptor, &self->offer_get_report, error))
		return FALSE;
	if (!fu_cfu_device_ensure_map_item(descriptor, &self->content_set_report, error))
		return FALSE;
	if (!fu_cfu_device_ensure_map_item(descriptor, &self->content_get_report, error))
		return FALSE;

	/* get version */
	fu_byte_array_append_uint8(buf, self->version_get_report.id);
	fu_byte_array_set_size(buf, self->version_get_report.ct + 0x1, 0x0);
	if (!fu_hid_device_get_report(FU_HID_DEVICE(device),
				      self->version_get_report.id,
				      buf->data,
				      buf->len,
				      FU_CFU_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error))
		return FALSE;
	st = fu_struct_cfu_get_version_rsp_parse(buf->data, buf->len, 0x1, error);
	if (st == NULL)
		return FALSE;
	self->protocol_version = fu_struct_cfu_get_version_rsp_get_flags(st) & 0b1111;

	/* keep track of all modules so we can work out which are dual bank */
	modules_by_cid = g_hash_table_new(g_direct_hash, g_direct_equal);

	/* read each component module version */
	offset += 0x1 + st->len;
	component_cnt = fu_struct_cfu_get_version_rsp_get_component_cnt(st);
	for (guint i = 0; i < component_cnt; i++) {
		g_autoptr(FuCfuModule) module = fu_cfu_module_new(device);
		FuCfuModule *module_tmp;

		if (!fu_cfu_module_setup(module, buf->data, buf->len, offset, error))
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
		offset += FU_STRUCT_CFU_GET_VERSION_RSP_COMPONENT_SIZE;
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
		self->version_get_report.op = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "CfuOfferSetReport") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0x0, G_MAXUINT8, error))
			return FALSE;
		self->offer_set_report.op = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "CfuOfferGetReport") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0x0, G_MAXUINT8, error))
			return FALSE;
		self->offer_get_report.op = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "CfuContentSetReport") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0x0, G_MAXUINT8, error))
			return FALSE;
		self->content_set_report.op = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "CfuContentGetReport") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0x0, G_MAXUINT8, error))
			return FALSE;
		self->content_get_report.op = tmp;
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
	self->version_get_report.op = 0x62;
	self->offer_set_report.op = 0x8A;
	self->offer_get_report.op = 0x8E;
	self->content_set_report.op = 0x61;
	self->content_get_report.op = 0x66;
	fu_hid_device_add_flag(FU_HID_DEVICE(self), FU_HID_DEVICE_FLAG_AUTODETECT_EPS);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_CFU_DEVICE_FLAG_SEND_OFFER_INFO,
					"send-offer-info");
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
