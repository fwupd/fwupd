/*
 * Copyright (C) 2022 Intel, Inc
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR Apache-2.0
 */

#include "config.h"

#include "fu-igsc-aux-device.h"
#include "fu-igsc-code-firmware.h"
#include "fu-igsc-device.h"
#include "fu-igsc-oprom-device.h"

struct _FuIgscDevice {
	FuMeiDevice parent_instance;
	gchar *project;
	guint32 hw_sku;
	guint16 subsystem_vendor;
	guint16 subsystem_model;
	gboolean oprom_code_devid_enforcement;
};

#define FU_IGSC_DEVICE_FLAG_HAS_AUX   (1 << 0)
#define FU_IGSC_DEVICE_FLAG_HAS_OPROM (1 << 1)

#define FU_IGSC_DEVICE_MEI_WRITE_TIMEOUT 60000	/* 60 sec */
#define FU_IGSC_DEVICE_MEI_READ_TIMEOUT	 480000 /* 480 sec */

G_DEFINE_TYPE(FuIgscDevice, fu_igsc_device, FU_TYPE_MEI_DEVICE)

struct igsc_fw_version {
	char project[4]; /* project code name */
	guint16 hotfix;
	guint16 build;
} __attribute__((packed));

#define GSC_FWU_STATUS_SUCCESS			      0x0
#define GSC_FWU_STATUS_SIZE_ERROR		      0x5
#define GSC_FWU_STATUS_UPDATE_OPROM_INVALID_STRUCTURE 0x1035
#define GSC_FWU_STATUS_UPDATE_OPROM_SECTION_NOT_EXIST 0x1032
#define GSC_FWU_STATUS_INVALID_COMMAND		      0x8D
#define GSC_FWU_STATUS_INVALID_PARAMS		      0x85
#define GSC_FWU_STATUS_FAILURE			      0x9E

#define GSC_FWU_GET_CONFIG_FORMAT_VERSION 0x1

static void
fu_igsc_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuIgscDevice *self = FU_IGSC_DEVICE(device);
	FU_DEVICE_CLASS(fu_igsc_device_parent_class)->to_string(device, idt, str);
	fu_string_append(str, idt, "Project", self->project);
	fu_string_append_kx(str, idt, "HwSku", self->hw_sku);
	fu_string_append_kx(str, idt, "SubsystemVendor", self->subsystem_vendor);
	fu_string_append_kx(str, idt, "SubsystemModel", self->subsystem_model);
	fu_string_append_kb(str,
			    idt,
			    "OpromCodeDevidEnforcement",
			    self->oprom_code_devid_enforcement);
}

gboolean
fu_igsc_device_get_oprom_code_devid_enforcement(FuIgscDevice *self)
{
	g_return_val_if_fail(FU_IS_IGSC_DEVICE(self), FALSE);
	return self->oprom_code_devid_enforcement;
}

guint16
fu_igsc_device_get_ssvid(FuIgscDevice *self)
{
	g_return_val_if_fail(FU_IS_IGSC_DEVICE(self), G_MAXUINT16);
	return self->subsystem_vendor;
}

guint16
fu_igsc_device_get_ssdid(FuIgscDevice *self)
{
	g_return_val_if_fail(FU_IS_IGSC_DEVICE(self), G_MAXUINT16);
	return self->subsystem_model;
}

static gboolean
fu_igsc_device_heci_validate_response_header(FuIgscDevice *self,
					     struct gsc_fwu_heci_response *resp_header,
					     enum gsc_fwu_heci_command_id command_id,
					     GError **error)
{
	if (resp_header->header.command_id != command_id) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid command ID (%d): ",
			    resp_header->header.command_id);
		return FALSE;
	}
	if (!resp_header->header.is_response) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "not a response");
		return FALSE;
	}
	if (resp_header->status != GSC_FWU_STATUS_SUCCESS) {
		const gchar *msg;
		switch (resp_header->status) {
		case GSC_FWU_STATUS_SIZE_ERROR:
			msg = "num of bytes to read/write/erase is bigger than partition size";
			break;
		case GSC_FWU_STATUS_UPDATE_OPROM_INVALID_STRUCTURE:
			msg = "wrong oprom signature";
			break;
		case GSC_FWU_STATUS_UPDATE_OPROM_SECTION_NOT_EXIST:
			msg = "update oprom section does not exists on flash";
			break;
		case GSC_FWU_STATUS_INVALID_COMMAND:
			msg = "invalid HECI message sent";
			break;
		case GSC_FWU_STATUS_INVALID_PARAMS:
			msg = "invalid command parameters";
			break;
		case GSC_FWU_STATUS_FAILURE:
		/* fall through */
		default:
			msg = "general firmware error";
			break;
		}
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "HECI message failed: %s [0x%x]: ",
			    msg,
			    resp_header->status);
		return FALSE;
	}
	if (resp_header->reserved != 0) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "HECI message response is leaking data");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_igsc_device_command(FuIgscDevice *self,
		       const guint8 *req_buf,
		       gsize req_bufsz,
		       guint8 *resp_buf,
		       gsize resp_bufsz,
		       GError **error)
{
	gsize resp_readsz = 0;
	if (!fu_mei_device_write(FU_MEI_DEVICE(self),
				 req_buf,
				 req_bufsz,
				 FU_IGSC_DEVICE_MEI_WRITE_TIMEOUT,
				 error))
		return FALSE;
	if (!fu_mei_device_read(FU_MEI_DEVICE(self),
				resp_buf,
				resp_bufsz,
				&resp_readsz,
				FU_IGSC_DEVICE_MEI_READ_TIMEOUT,
				error))
		return FALSE;
	if (resp_readsz != resp_bufsz) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "read 0x%x bytes but expected 0x%x",
			    (guint)resp_readsz,
			    (guint)resp_bufsz);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_igsc_device_get_version_raw(FuIgscDevice *self,
			       enum gsc_fwu_heci_partition_version partition,
			       guint8 *buf,
			       gsize bufsz,
			       GError **error)
{
	struct gsc_fwu_heci_version_req req = {.header.command_id =
						   GSC_FWU_HECI_COMMAND_ID_GET_IP_VERSION,
					       .partition = partition};
	guint8 res_buf[100];
	struct gsc_fwu_heci_version_resp *res = (struct gsc_fwu_heci_version_resp *)res_buf;

	if (!fu_igsc_device_command(self,
				    (const guint8 *)&req,
				    sizeof(req),
				    res_buf,
				    sizeof(struct gsc_fwu_heci_version_resp) + bufsz,
				    error)) {
		g_prefix_error(error, "invalid HECI message response: ");
		return FALSE;
	}
	if (!fu_igsc_device_heci_validate_response_header(self,
							  &res->response,
							  req.header.command_id,
							  error))
		return FALSE;
	if (res->partition != partition) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid HECI message response payload: 0x%x: ",
			    res->partition);
		return FALSE;
	}
	if (bufsz > 0 && res->version_length != bufsz) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid HECI message response version_length: 0x%x, expected 0x%x: ",
			    res->version_length,
			    (guint)bufsz);
		return FALSE;
	}
	if (buf != NULL) {
		if (!fu_memcpy_safe(buf,
				    bufsz,
				    0x0, /* dst */
				    res->version,
				    res->version_length,
				    0x0, /* src*/
				    res->version_length,
				    error)) {
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

gboolean
fu_igsc_device_get_aux_version(FuIgscDevice *self,
			       guint32 *oem_version,
			       guint16 *major_version,
			       guint16 *major_vcn,
			       GError **error)
{
	struct gsc_fw_data_heci_version_req req = {
	    .header.command_id = GSC_FWU_HECI_COMMAND_ID_GET_GFX_DATA_UPDATE_INFO};
	struct gsc_fw_data_heci_version_resp res = {0x0};

	if (!fu_igsc_device_command(self,
				    (const guint8 *)&req,
				    sizeof(req),
				    (guint8 *)&res,
				    sizeof(res),
				    error))
		return FALSE;
	if (!fu_igsc_device_heci_validate_response_header(self,
							  &res.response,
							  req.header.command_id,
							  error))
		return FALSE;

	/* success */
	*major_vcn = res.major_vcn;
	*major_version = res.major_version;
	if (res.oem_version_fitb_valid) {
		*oem_version = res.oem_version_fitb;
	} else {
		*oem_version = res.oem_version_nvm;
	}
	return TRUE;
}

static gboolean
fu_igsc_device_get_subsystem_ids(FuIgscDevice *self, GError **error)
{
	struct gsc_fwu_heci_get_subsystem_ids_message_req req = {
	    .header.command_id = GSC_FWU_HECI_COMMAND_ID_GET_SUBSYSTEM_IDS};
	struct gsc_fwu_heci_get_subsystem_ids_message_resp res = {0x0};

	if (!fu_igsc_device_command(self,
				    (const guint8 *)&req,
				    sizeof(req),
				    (guint8 *)&res,
				    sizeof(res),
				    error))
		return FALSE;
	if (!fu_igsc_device_heci_validate_response_header(self,
							  &res.response,
							  req.header.command_id,
							  error))
		return FALSE;

	/* success */
	self->subsystem_vendor = res.ssvid;
	self->subsystem_model = res.ssdid;
	return TRUE;
}

#define GSC_IFWI_TAG_SOC2_SKU_BIT 0x1
#define GSC_IFWI_TAG_SOC3_SKU_BIT 0x2
#define GSC_IFWI_TAG_SOC1_SKU_BIT 0x4

#define GSC_DG2_SKUID_SOC1 0
#define GSC_DG2_SKUID_SOC2 1
#define GSC_DG2_SKUID_SOC3 2

static gboolean
fu_igsc_device_get_config(FuIgscDevice *self, GError **error)
{
	struct gsc_fwu_heci_get_config_message_req req = {
	    .header.command_id = GSC_FWU_HECI_COMMAND_ID_GET_CONFIG,
	};
	struct gsc_fwu_heci_get_config_message_resp res = {0x0};

	if (!fu_igsc_device_command(self,
				    (const guint8 *)&req,
				    sizeof(req),
				    (guint8 *)&res,
				    sizeof(res),
				    error)) {
		g_prefix_error(error, "invalid HECI message response: ");
		return FALSE;
	}
	if (!fu_igsc_device_heci_validate_response_header(self,
							  &res.response,
							  req.header.command_id,
							  error))
		return FALSE;
	if (res.format_version != GSC_FWU_GET_CONFIG_FORMAT_VERSION) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid config version 0x%x, expected 0x%x",
			    res.format_version,
			    (guint)GSC_FWU_GET_CONFIG_FORMAT_VERSION);
		return FALSE;
	}

	/* convert to firmware bit mask for easier comparison */
	if (res.hw_sku == GSC_DG2_SKUID_SOC1) {
		self->hw_sku = GSC_IFWI_TAG_SOC1_SKU_BIT;
	} else if (res.hw_sku == GSC_DG2_SKUID_SOC3) {
		self->hw_sku = GSC_IFWI_TAG_SOC3_SKU_BIT;
	} else if (res.hw_sku == GSC_DG2_SKUID_SOC2) {
		self->hw_sku = GSC_IFWI_TAG_SOC2_SKU_BIT;
	} else {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid hw sku 0x%x, expected 0..2",
			    res.hw_sku);
		return FALSE;
	}

	self->oprom_code_devid_enforcement = res.oprom_code_devid_enforcement;

	/* success */
	return TRUE;
}

static gboolean
fu_igsc_device_open(FuDevice *device, GError **error)
{
	/* open then create context */
	if (!FU_DEVICE_CLASS(fu_igsc_device_parent_class)->open(device, error))
		return FALSE;
	return fu_mei_device_connect(FU_MEI_DEVICE(device), 0, error);
}

static gboolean
fu_igsc_device_setup(FuDevice *device, GError **error)
{
	FuIgscDevice *self = FU_IGSC_DEVICE(device);
	FuContext *ctx = fu_device_get_context(FU_DEVICE(self));
	g_autofree gchar *version = NULL;
	struct igsc_fw_version fw_code_version;

	/* get current version */
	if (!fu_igsc_device_get_version_raw(self,
					    GSC_FWU_HECI_PART_VERSION_GFX_FW,
					    (guint8 *)&fw_code_version,
					    sizeof(fw_code_version),
					    error)) {
		g_prefix_error(error, "cannot cannot get fw version: ");
		return FALSE;
	}
	self->project = g_strdup_printf("%c%c%c%c",
					fw_code_version.project[0],
					fw_code_version.project[1],
					fw_code_version.project[2],
					fw_code_version.project[3]);
	version = g_strdup_printf("%u.%u", fw_code_version.hotfix, fw_code_version.build);
	fu_device_set_version(device, version);

	/* get hardware SKU if supported */
	if (g_strcmp0(self->project, "DG02") == 0) {
		if (!fu_igsc_device_get_config(self, error)) {
			g_prefix_error(error, "cannot cannot get SKU: ");
			return FALSE;
		}
	}

	/* allow vendors to differentiate their products */
	if (!fu_igsc_device_get_subsystem_ids(self, error))
		return FALSE;
	if (self->subsystem_vendor != 0x0 && self->subsystem_model != 0x0) {
		g_autofree gchar *subsys =
		    g_strdup_printf("%04X%04X", self->subsystem_vendor, self->subsystem_model);
		fu_device_add_instance_str(device, "SUBSYS", subsys);
	}

	/* some devices have children */
	if (fu_device_has_private_flag(device, FU_IGSC_DEVICE_FLAG_HAS_AUX)) {
		g_autoptr(FuIgscAuxDevice) device_child = fu_igsc_aux_device_new(ctx);
		fu_device_add_child(FU_DEVICE(self), FU_DEVICE(device_child));
	}
	if (fu_device_has_private_flag(device, FU_IGSC_DEVICE_FLAG_HAS_OPROM)) {
		g_autoptr(FuIgscOpromDevice) device_code = NULL;
		g_autoptr(FuIgscOpromDevice) device_data = NULL;
		device_code = fu_igsc_oprom_device_new(ctx, GSC_FWU_HECI_PAYLOAD_TYPE_OPROM_CODE);
		device_data = fu_igsc_oprom_device_new(ctx, GSC_FWU_HECI_PAYLOAD_TYPE_OPROM_DATA);
		fu_device_add_child(FU_DEVICE(self), FU_DEVICE(device_code));
		fu_device_add_child(FU_DEVICE(self), FU_DEVICE(device_data));
	}

	/* success */
	return TRUE;
}

/* @line is indexed from 1 */
static gboolean
fu_igsc_device_get_fw_status(FuIgscDevice *self, guint line, guint32 *fw_status, GError **error)
{
	guint64 tmp64 = 0;
	g_autofree gchar *tmp = NULL;
	g_autofree gchar *hex = NULL;

	/* read value and convert to hex */
	tmp = fu_mei_device_get_fw_status(FU_MEI_DEVICE(self), line, error);
	if (tmp == NULL) {
		g_prefix_error(error, "device is corrupted: ");
		return FALSE;
	}
	hex = g_strdup_printf("0x%s", tmp);
	if (!fu_strtoull(hex, &tmp64, 0x1, G_MAXUINT32 - 0x1, error)) {
		g_prefix_error(error, "fw_status %s is invalid: ", tmp);
		return FALSE;
	}

	/* success */
	if (fw_status != NULL)
		*fw_status = tmp64;
	return TRUE;
}

static gboolean
fu_igsc_device_probe(FuDevice *device, GError **error)
{
	FuIgscDevice *self = FU_IGSC_DEVICE(device);

	/* check firmware status */
	if (!fu_igsc_device_get_fw_status(self, 1, NULL, error))
		return FALSE;

	/* add extra instance IDs */
	fu_device_add_instance_str(device, "PART", "FWCODE");
	if (!fu_device_build_instance_id(device, error, "MEI", "VEN", "DEV", "PART", NULL))
		return FALSE;
	return fu_device_build_instance_id(device,
					   error,
					   "MEI",
					   "VEN",
					   "DEV",
					   "SUBSYS",
					   "PART",
					   NULL);
}

static FuFirmware *
fu_igsc_device_prepare_firmware(FuDevice *device,
				GBytes *fw,
				FwupdInstallFlags flags,
				GError **error)
{
	FuIgscDevice *self = FU_IGSC_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_igsc_code_firmware_new();

	/* check project code */
	if (!fu_firmware_parse(firmware, fw, flags, error))
		return NULL;
	if (g_strcmp0(self->project, fu_firmware_get_id(firmware)) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "firmware is for a different project, got %s, expected %s",
			    fu_firmware_get_id(firmware),
			    self->project);
		return NULL;
	}
	if (self->hw_sku != fu_igsc_code_firmware_get_hw_sku(FU_IGSC_CODE_FIRMWARE(firmware))) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "firmware is for a different SKU, got 0x%x, expected 0x%x",
			    fu_igsc_code_firmware_get_hw_sku(FU_IGSC_CODE_FIRMWARE(firmware)),
			    self->hw_sku);
		return NULL;
	}

	/* success */
	return g_steal_pointer(&firmware);
}

static gboolean
fu_igsc_device_update_end(FuIgscDevice *self, GError **error)
{
	struct gsc_fwu_heci_end_req req = {.header.command_id = GSC_FWU_HECI_COMMAND_ID_END};
	return fu_mei_device_write(FU_MEI_DEVICE(self),
				   (const guint8 *)&req,
				   sizeof(req),
				   FU_IGSC_DEVICE_MEI_WRITE_TIMEOUT,
				   error);
}

static gboolean
fu_igsc_device_update_data(FuIgscDevice *self, const guint8 *data, guint32 length, GError **error)
{
	struct gsc_fwu_heci_data_req req = {.header.command_id = GSC_FWU_HECI_COMMAND_ID_DATA,
					    .data_length = length};
	struct gsc_fwu_heci_data_resp res = {0x0};
	g_autoptr(GByteArray) buf = g_byte_array_new();

	g_byte_array_append(buf, (const guint8 *)&req, sizeof(req));
	g_byte_array_append(buf, data, length);
	if (!fu_igsc_device_command(self, buf->data, buf->len, (guint8 *)&res, sizeof(res), error))
		return FALSE;
	return fu_igsc_device_heci_validate_response_header(self,
							    &res.response,
							    req.header.command_id,
							    error);
}

static gboolean
fu_igsc_device_update_start(FuIgscDevice *self,
			    guint32 payload_type,
			    GBytes *fw_info,
			    GBytes *fw,
			    GError **error)
{
	struct gsc_fwu_heci_start_req req = {.header.command_id = GSC_FWU_HECI_COMMAND_ID_START,
					     .payload_type = payload_type,
					     .update_img_length = g_bytes_get_size(fw),
					     .flags = {0}};
	struct gsc_fwu_heci_start_resp res = {0x0};
	g_autoptr(GByteArray) buf = g_byte_array_new();

	g_byte_array_append(buf, (const guint8 *)&req, sizeof(req));
	if (fw_info != NULL)
		fu_byte_array_append_bytes(buf, fw_info);
	if (!fu_igsc_device_command(self, buf->data, buf->len, (guint8 *)&res, sizeof(res), error))
		return FALSE;
	return fu_igsc_device_heci_validate_response_header(self,
							    &res.response,
							    req.header.command_id,
							    error);
}

static gboolean
fu_igsc_device_no_update(FuIgscDevice *self, GError **error)
{
	struct gsc_fwu_heci_no_update_req req = {.header.command_id =
						     GSC_FWU_HECI_COMMAND_ID_NO_UPDATE};
	return fu_mei_device_write(FU_MEI_DEVICE(self),
				   (const guint8 *)&req,
				   sizeof(req),
				   FU_IGSC_DEVICE_MEI_WRITE_TIMEOUT,
				   error);
}

static gboolean
fu_igsc_device_write_chunks(FuIgscDevice *self,
			    GPtrArray *chunks,
			    FuProgress *progress,
			    GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_igsc_device_update_data(self,
						fu_chunk_get_data(chk),
						fu_chunk_get_data_sz(chk),
						error)) {
			g_prefix_error(error,
				       "failed on chunk %u (@0x%x): ",
				       i,
				       fu_chunk_get_address(chk));
			return FALSE;
		}
		fu_progress_step_done(progress);
	}
	return TRUE;
}

/* the expectation is that it will fail eventually */
static gboolean
fu_igsc_device_wait_for_reset(FuIgscDevice *self, GError **error)
{
	struct igsc_fw_version fw_code_version;
	for (guint i = 0; i < 20; i++) {
		if (!fu_igsc_device_get_version_raw(self,
						    GSC_FWU_HECI_PART_VERSION_GFX_FW,
						    (guint8 *)&fw_code_version,
						    sizeof(fw_code_version),
						    NULL))
			return TRUE;
		g_usleep(1000 * 100);
	}
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "device did not reset");
	return FALSE;
}

static gboolean
fu_igsc_device_reconnect_cb(FuDevice *self, gpointer user_data, GError **error)
{
	return fu_mei_device_connect(FU_MEI_DEVICE(self), 0, error);
}

// FIXME we want to retry this on failure
gboolean
fu_igsc_device_write_blob(FuIgscDevice *self,
			  enum gsc_fwu_heci_payload_type payload_type,
			  GBytes *fw_info,
			  GBytes *fw,
			  FuProgress *progress,
			  GError **error)
{
	gboolean cp_mode;
	guint32 sts5 = 0;
	gsize payloadsz = fu_mei_device_get_max_msg_length(FU_MEI_DEVICE(self)) -
			  sizeof(struct gsc_fwu_heci_data_req);
	g_autoptr(GPtrArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "get-status");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "update-start");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "write-chunks");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 5, "update-end");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 65, "wait-for-reboot");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 65, "reconnect");

	/* need to get the new version in a loop? */
	if (!fu_igsc_device_get_fw_status(self, 5, &sts5, error))
		return FALSE;
	cp_mode = (sts5 & HECI1_CSE_FS_MODE_MASK) == HECI1_CSE_FS_CP_MODE;
	fu_progress_step_done(progress);

	/* start */
	if (!fu_igsc_device_update_start(self, payload_type, fw_info, fw, error)) {
		g_prefix_error(error, "failed to start: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* data */
	chunks = fu_chunk_array_new_from_bytes(fw, 0x0, 0x0, payloadsz);
	if (!fu_igsc_device_write_chunks(self, chunks, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* stop */
	if (!fu_igsc_device_update_end(self, error)) {
		g_prefix_error(error, "failed to end: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* detect a firmware reboot */
	if (payload_type == GSC_FWU_HECI_PAYLOAD_TYPE_GFX_FW ||
	    payload_type == GSC_FWU_HECI_PAYLOAD_TYPE_FWDATA) {
		if (!fu_igsc_device_wait_for_reset(self, error))
			return FALSE;
	}
	fu_progress_step_done(progress);

	/* after Gfx FW update there is a FW reset so driver reconnect is needed */
	if (payload_type == GSC_FWU_HECI_PAYLOAD_TYPE_GFX_FW) {
		if (cp_mode) {
			if (!fu_igsc_device_wait_for_reset(self, error))
				return FALSE;
		}
		if (!fu_device_retry_full(FU_DEVICE(self),
					  fu_igsc_device_reconnect_cb,
					  200,
					  300 /* ms */,
					  NULL,
					  error))
			return FALSE;
		if (!fu_igsc_device_no_update(self, error)) {
			g_prefix_error(error, "failed to send no-update: ");
			return FALSE;
		}
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_igsc_device_write_firmware(FuDevice *device,
			      FuFirmware *firmware,
			      FuProgress *progress,
			      FwupdInstallFlags flags,
			      GError **error)
{
	FuIgscDevice *self = FU_IGSC_DEVICE(device);
	g_autoptr(GBytes) fw_info = NULL;
	g_autoptr(GBytes) fw_payload = NULL;

	/* get image, and install on ourself */
	fw_info =
	    fu_firmware_get_image_by_idx_bytes(firmware, FU_IFWI_FPT_FIRMWARE_IDX_INFO, error);
	if (fw_info == NULL)
		return FALSE;
	fw_payload =
	    fu_firmware_get_image_by_idx_bytes(firmware, FU_IFWI_FPT_FIRMWARE_IDX_FWIM, error);
	if (fw_payload == NULL)
		return FALSE;
	return fu_igsc_device_write_blob(self,
					 GSC_FWU_HECI_PAYLOAD_TYPE_GFX_FW,
					 fw_info,
					 fw_payload,
					 progress,
					 error);
}

static gboolean
fu_igsc_device_set_pci_power_policy(FuIgscDevice *self, const gchar *val, GError **error)
{
	g_autoptr(FuUdevDevice) parent = NULL;

	/* get PCI parent */
	parent = fu_udev_device_get_parent_with_subsystem(FU_UDEV_DEVICE(self), "pci");
	if (parent == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no PCI parent");
		return FALSE;
	}
	return fu_udev_device_write_sysfs(parent, "power/control", val, error);
}

static gboolean
fu_igsc_device_prepare(FuDevice *device,
		       FuProgress *progress,
		       FwupdInstallFlags flags,
		       GError **error)
{
	FuIgscDevice *self = FU_IGSC_DEVICE(device);
	return fu_igsc_device_set_pci_power_policy(self, "on", error);
}

static gboolean
fu_igsc_device_cleanup(FuDevice *device,
		       FuProgress *progress,
		       FwupdInstallFlags flags,
		       GError **error)
{
	FuIgscDevice *self = FU_IGSC_DEVICE(device);
	return fu_igsc_device_set_pci_power_policy(self, "auto", error);
}

static void
fu_igsc_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 88, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 10, "reload");
}

static void
fu_igsc_device_init(FuIgscDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_set_vendor(FU_DEVICE(self), "Intel");
	fu_device_set_name(FU_DEVICE(self), "Graphics Card");
	fu_device_set_summary(FU_DEVICE(self), "Discrete Graphics Card");
	fu_device_add_icon(FU_DEVICE(self), "video-display");
	fu_device_add_protocol(FU_DEVICE(self), "com.intel.gsc");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_register_private_flag(FU_DEVICE(self), FU_IGSC_DEVICE_FLAG_HAS_AUX, "has-aux");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_IGSC_DEVICE_FLAG_HAS_OPROM,
					"has-oprom");
}

static void
fu_igsc_device_finalize(GObject *object)
{
	FuIgscDevice *self = FU_IGSC_DEVICE(object);

	g_free(self->project);

	G_OBJECT_CLASS(fu_igsc_device_parent_class)->finalize(object);
}

static void
fu_igsc_device_class_init(FuIgscDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_igsc_device_finalize;
	klass_device->set_progress = fu_igsc_device_set_progress;
	klass_device->to_string = fu_igsc_device_to_string;
	klass_device->open = fu_igsc_device_open;
	klass_device->setup = fu_igsc_device_setup;
	klass_device->probe = fu_igsc_device_probe;
	klass_device->prepare = fu_igsc_device_prepare;
	klass_device->cleanup = fu_igsc_device_cleanup;
	klass_device->prepare_firmware = fu_igsc_device_prepare_firmware;
	klass_device->write_firmware = fu_igsc_device_write_firmware;
}
