/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <tss2/tss2_esys.h>

#include "fu-tpm-v2-device.h"

struct _FuTpmV2Device {
	FuTpmDevice parent_instance;
	ESYS_CONTEXT *esys_context;
};

G_DEFINE_TYPE(FuTpmV2Device, fu_tpm_v2_device, FU_TYPE_TPM_DEVICE)

static gboolean
fu_tpm_v2_device_probe(FuDevice *device, GError **error)
{
	return fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "tpm", error);
}

static gboolean
fu_tpm_v2_device_get_uint32(FuTpmV2Device *self, guint32 query, guint32 *val, GError **error)
{
	TSS2_RC rc;
	g_autofree TPMS_CAPABILITY_DATA *capability = NULL;

	g_return_val_if_fail(val != NULL, FALSE);

	rc = Esys_GetCapability(self->esys_context,
				ESYS_TR_NONE,
				ESYS_TR_NONE,
				ESYS_TR_NONE,
				TPM2_CAP_TPM_PROPERTIES,
				query,
				1,
				NULL,
				&capability);
	if (rc != TSS2_RC_SUCCESS) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "capability request failed for query %x",
			    query);
		return FALSE;
	}
	if (capability->data.tpmProperties.count == 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "no properties returned for query %x",
			    query);
		return FALSE;
	}
	if (capability->data.tpmProperties.tpmProperty[0].property != query) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "wrong query returned (got %x expected %x)",
			    capability->data.tpmProperties.tpmProperty[0].property,
			    query);
		return FALSE;
	}

	*val = capability->data.tpmProperties.tpmProperty[0].value;
	return TRUE;
}

static gchar *
fu_tpm_v2_device_get_string(FuTpmV2Device *self, guint32 query, GError **error)
{
	guint32 val_be = 0;
	guint32 val;
	gchar result[5] = {'\0'};

	/* return four bytes */
	if (!fu_tpm_v2_device_get_uint32(self, query, &val_be, error))
		return NULL;
	val = GUINT32_FROM_BE(val_be);
	memcpy(result, (gchar *)&val, 4);

	/* convert non-ASCII into spaces */
	for (guint i = 0; i < 4; i++) {
		if (!g_ascii_isgraph(result[i]))
			result[i] = 0x20;
	}

	return fu_strstrip(result);
}

/* taken from TCG-TPM-Vendor-ID-Registry-Version-1.01-Revision-1.00.pdf */
static const gchar *
fu_tpm_v2_device_convert_manufacturer(const gchar *manufacturer)
{
	if (g_strcmp0(manufacturer, "AMD") == 0)
		return "Advanced Micro Devices, Inc.";
	if (g_strcmp0(manufacturer, "ATML") == 0)
		return "Atmel";
	if (g_strcmp0(manufacturer, "BRCM") == 0)
		return "Broadcom";
	if (g_strcmp0(manufacturer, "HPE") == 0)
		return "HPE";
	if (g_strcmp0(manufacturer, "IBM") == 0)
		return "IBM";
	if (g_strcmp0(manufacturer, "IFX") == 0)
		return "Infineon";
	if (g_strcmp0(manufacturer, "INTC") == 0)
		return "Intel";
	if (g_strcmp0(manufacturer, "LEN") == 0)
		return "Lenovo";
	if (g_strcmp0(manufacturer, "MSFT") == 0)
		return "Microsoft";
	if (g_strcmp0(manufacturer, "NSM") == 0)
		return "National Semiconductor";
	if (g_strcmp0(manufacturer, "NTZ") == 0)
		return "Nationz";
	if (g_strcmp0(manufacturer, "NTC") == 0)
		return "Nuvoton Technology";
	if (g_strcmp0(manufacturer, "QCOM") == 0)
		return "Qualcomm";
	if (g_strcmp0(manufacturer, "SMSC") == 0)
		return "SMSC";
	if (g_strcmp0(manufacturer, "STM") == 0)
		return "ST Microelectronics";
	if (g_strcmp0(manufacturer, "SMSN") == 0)
		return "Samsung";
	if (g_strcmp0(manufacturer, "SNS") == 0)
		return "Sinosun";
	if (g_strcmp0(manufacturer, "TXN") == 0)
		return "Texas Instruments";
	if (g_strcmp0(manufacturer, "WEC") == 0)
		return "Winbond";
	if (g_strcmp0(manufacturer, "ROCC") == 0)
		return "Fuzhou Rockchip";
	if (g_strcmp0(manufacturer, "GOOG") == 0)
		return "Google";
	return NULL;
}

static gboolean
fu_tpm_v2_device_setup_pcrs(FuTpmV2Device *self, GError **error)
{
	TSS2_RC rc;
	g_autofree TPMS_CAPABILITY_DATA *capability_data = NULL;
	TPML_PCR_SELECTION pcr_selection_in = {
	    0,
	};
	g_autofree TPML_DIGEST *pcr_values = NULL;

	/* get hash algorithms supported by the TPM */
	rc = Esys_GetCapability(self->esys_context,
				ESYS_TR_NONE,
				ESYS_TR_NONE,
				ESYS_TR_NONE,
				TPM2_CAP_PCRS,
				0,
				1,
				NULL,
				&capability_data);
	if (rc != TSS2_RC_SUCCESS) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "failed to get hash algorithms supported by TPM");
		return FALSE;
	}

	/* fetch PCR 0 for every supported hash algorithm */
	pcr_selection_in.count = capability_data->data.assignedPCR.count;
	for (guint i = 0; i < pcr_selection_in.count; i++) {
		pcr_selection_in.pcrSelections[i].hash =
		    capability_data->data.assignedPCR.pcrSelections[i].hash;
		pcr_selection_in.pcrSelections[i].sizeofSelect =
		    capability_data->data.assignedPCR.pcrSelections[i].sizeofSelect;
		pcr_selection_in.pcrSelections[i].pcrSelect[0] = 0b00000001;
	}

	rc = Esys_PCR_Read(self->esys_context,
			   ESYS_TR_NONE,
			   ESYS_TR_NONE,
			   ESYS_TR_NONE,
			   &pcr_selection_in,
			   NULL,
			   NULL,
			   &pcr_values);
	if (rc != TSS2_RC_SUCCESS) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "failed to read PCR values from TPM");
		return FALSE;
	}

	for (guint i = 0; i < pcr_values->count; i++) {
		g_autoptr(GString) str = NULL;
		gboolean valid = FALSE;

		str = g_string_new(NULL);
		for (guint j = 0; j < pcr_values->digests[i].size; j++) {
			gint64 val = pcr_values->digests[i].buffer[j];
			if (val > 0)
				valid = TRUE;
			g_string_append_printf(str, "%02x", pcr_values->digests[i].buffer[j]);
		}
		if (valid) {
			/* constant PCR index 0, since we only read this single PCR */
			fu_tpm_device_add_checksum(FU_TPM_DEVICE(self), 0, str->str);
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_tpm_v2_device_ensure_commands(FuTpmV2Device *self, GError **error)
{
	gboolean seen_upgrade_data = FALSE;
	gboolean seen_upgrade_start = FALSE;
	TSS2_RC rc;
	g_autofree TPMS_CAPABILITY_DATA *capability = NULL;
	g_autoptr(GString) str = g_string_new(NULL);

	rc = Esys_GetCapability(self->esys_context,
				ESYS_TR_NONE,
				ESYS_TR_NONE,
				ESYS_TR_NONE,
				TPM2_CAP_COMMANDS,
				TPM2_CC_FIRST,
				TPM2_MAX_CAP_CC,
				NULL,
				&capability);
	if (rc != TSS2_RC_SUCCESS) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "capability request failed for TPM2_CAP_COMMANDS");
		return FALSE;
	}
	for (guint i = 0; i < capability->data.ppCommands.count; i++) {
		guint cap_cmd = capability->data.ppCommands.commandCodes[i] & 0xFFFF;

		if (str->len > 0)
			g_string_append(str, ", ");
		g_string_append_printf(str, "0x%04x", cap_cmd);

		/* ones we care about */
		if (cap_cmd == TPM2_CC_FieldUpgradeStart) {
			seen_upgrade_start = TRUE;
		} else if (cap_cmd == TPM2_CC_FieldUpgradeData) {
			seen_upgrade_data = TRUE;
		} else if (cap_cmd == TPM2_CC_FirmwareRead) {
			fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
		}
	}
	g_debug("CAP_COMMANDS: %s", str->str);

	/* both available */
	if (seen_upgrade_start && seen_upgrade_data)
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);

	/* success */
	return TRUE;
}

static gboolean
fu_tpm_v2_device_setup(FuDevice *device, GError **error)
{
	FuTpmV2Device *self = FU_TPM_V2_DEVICE(device);
	TSS2_RC rc;
	const gchar *tmp;
	guint32 tpm_type = 0;
	guint32 version1 = 0;
	guint32 version2 = 0;
	guint64 version_raw;
	g_autofree gchar *manufacturer = NULL;
	g_autofree gchar *model1 = NULL;
	g_autofree gchar *model2 = NULL;
	g_autofree gchar *model3 = NULL;
	g_autofree gchar *model4 = NULL;
	g_autofree gchar *model = NULL;
	g_autofree gchar *vendor_id = NULL;
	g_autofree gchar *family = NULL;

	/* suppress warning messages about missing TCTI libraries for tpm2-tss <2.3 */
	if (g_getenv("FWUPD_UEFI_VERBOSE") == NULL)
		(void)g_setenv("TSS2_LOG", "esys+none,tcti+none", FALSE);

	rc = Esys_Startup(self->esys_context, TPM2_SU_CLEAR);
	if (rc != TSS2_RC_SUCCESS) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "failed to initialize TPM");
		return FALSE;
	}

	/* lookup guaranteed details from TPM */
	family = fu_tpm_v2_device_get_string(self, TPM2_PT_FAMILY_INDICATOR, error);
	if (family == NULL) {
		g_prefix_error(error, "failed to read TPM family: ");
		return FALSE;
	}
	fu_tpm_device_set_family(FU_TPM_DEVICE(self), family);
	manufacturer = fu_tpm_v2_device_get_string(self, TPM2_PT_MANUFACTURER, error);
	if (manufacturer == NULL) {
		g_prefix_error(error, "failed to read TPM manufacturer: ");
		return FALSE;
	}
	model1 = fu_tpm_v2_device_get_string(self, TPM2_PT_VENDOR_STRING_1, error);
	if (model1 == NULL) {
		g_prefix_error(error, "failed to read TPM vendor string: ");
		return FALSE;
	}
	if (!fu_tpm_v2_device_get_uint32(self, TPM2_PT_VENDOR_TPM_TYPE, &tpm_type, error)) {
		g_prefix_error(error, "failed to read TPM type: ");
		return FALSE;
	}

	/* these are not guaranteed by spec and may be NULL */
	model2 = fu_tpm_v2_device_get_string(self, TPM2_PT_VENDOR_STRING_2, error);
	model3 = fu_tpm_v2_device_get_string(self, TPM2_PT_VENDOR_STRING_3, error);
	model4 = fu_tpm_v2_device_get_string(self, TPM2_PT_VENDOR_STRING_4, error);
	model = g_strjoin("", model1, model2, model3, model4, NULL);

	/* add GUIDs to daemon */
	fu_device_add_instance_str(device, "VEN", manufacturer);
	fu_device_add_instance_u16(device, "DEV", tpm_type);
	fu_device_add_instance_str(device, "MOD", model);
	fu_device_add_instance_str(device, "VER", family);
	fu_device_build_instance_id(device, NULL, "TPM", "VEN", "DEV", NULL);
	fu_device_build_instance_id(device, NULL, "TPM", "VEN", "MOD", NULL);
	fu_device_build_instance_id(device, NULL, "TPM", "VEN", "DEV", "VER", NULL);
	fu_device_build_instance_id(device, NULL, "TPM", "VEN", "MOD", "VER", NULL);

	/* enforce vendors can only ship updates for their own hardware */
	vendor_id = g_strdup_printf("TPM:%s", manufacturer);
	fu_device_add_vendor_id(device, vendor_id);
	tmp = fu_tpm_v2_device_convert_manufacturer(manufacturer);
	fu_device_set_vendor(device, tmp != NULL ? tmp : manufacturer);

	/* get version */
	if (!fu_tpm_v2_device_get_uint32(self, TPM2_PT_FIRMWARE_VERSION_1, &version1, error))
		return FALSE;
	if (!fu_tpm_v2_device_get_uint32(self, TPM2_PT_FIRMWARE_VERSION_2, &version2, error))
		return FALSE;
	version_raw = ((guint64)version1) << 32 | ((guint64)version2);
	fu_device_set_version_from_uint64(device, version_raw);

	/* get capabilities */
	if (!fu_tpm_v2_device_ensure_commands(self, error))
		return FALSE;

	/* get PCRs */
	return fu_tpm_v2_device_setup_pcrs(self, error);
}

static gboolean
fu_tpm_v2_device_upgrade_data(FuTpmV2Device *self, GBytes *fw, FuProgress *progress, GError **error)
{
	TPMT_HA *first_digest;
	TPMT_HA *next_digest;
	TSS2_RC rc;
	g_autoptr(GPtrArray) chunks = NULL;

	chunks = fu_chunk_array_new_from_bytes(fw, 0x0, 0x0, TPM2_MAX_DIGEST_BUFFER);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		TPM2B_MAX_BUFFER data = {.size = g_bytes_get_size(fw)};
		if (!fu_memcpy_safe((guint8 *)data.buffer,
				    sizeof(data.buffer),
				    0x0, /* dst */
				    fu_chunk_get_data(chk),
				    fu_chunk_get_data_sz(chk),
				    0x0, /* src */
				    fu_chunk_get_data_sz(chk),
				    error))
			return FALSE;

		rc = Esys_FieldUpgradeData(self->esys_context,
					   ESYS_TR_NONE,
					   ESYS_TR_NONE,
					   ESYS_TR_NONE,
					   &data,
					   &next_digest,
					   &first_digest);
		if (rc == TPM2_RC_COMMAND_CODE ||
		    (rc == (TPM2_RC_COMMAND_CODE | TSS2_RESMGR_RC_LAYER)) ||
		    (rc == (TPM2_RC_COMMAND_CODE | TSS2_RESMGR_TPM_RC_LAYER))) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "TPM2_FieldUpgradeData not supported: 0x%x",
				    rc);
			return FALSE;
		}

		/* update progress */
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_tpm_v2_device_write_firmware(FuDevice *device,
				FuFirmware *firmware,
				FuProgress *progress,
				FwupdInstallFlags flags,
				GError **error)
{
	FuTpmV2Device *self = FU_TPM_V2_DEVICE(device);
	TPM2B_DIGEST digest = {0x0};
	TSS2_RC rc;
	g_autoptr(GBytes) fw = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 99, NULL);

	/* validate the signature and that the authorization is valid */
	rc = Esys_FieldUpgradeStart(self->esys_context,
				    ESYS_TR_NONE, /* TODO: authorization */
				    ESYS_TR_NONE, /* TODO: keyHandle */
				    ESYS_TR_PASSWORD,
				    ESYS_TR_NONE,
				    ESYS_TR_NONE,
				    &digest,
				    NULL);
	if (rc == TPM2_RC_SIGNATURE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "the signature check failed");
		return FALSE;
	}
	if (rc == TPM2_RC_COMMAND_CODE || (rc == (TPM2_RC_COMMAND_CODE | TSS2_RESMGR_RC_LAYER)) ||
	    (rc == (TPM2_RC_COMMAND_CODE | TSS2_RESMGR_TPM_RC_LAYER))) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "TPM2_FieldUpgradeStart not supported: 0x%x",
			    rc);
		return FALSE;
	}

	/* deploy data to device */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;
	if (!fu_tpm_v2_device_upgrade_data(self, fw, fu_progress_get_child(progress), error))
		return FALSE;

	/* success! */
	return TRUE;
}

static GBytes *
fu_tpm_v2_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuTpmV2Device *self = FU_TPM_V2_DEVICE(device);
	TSS2_RC rc;
	guint chunks_max = fu_device_get_firmware_size_max(device) / TPM2_MAX_DIGEST_BUFFER;
	g_autoptr(GByteArray) buf = NULL;

	/* keep reading chunks until we get a zero sized response */
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);
	for (guint i = 0; i < chunks_max; i++) {
		g_autofree TPM2B_MAX_BUFFER **data = g_new0(TPM2B_MAX_BUFFER *, 1);

		g_debug("getting firmware chunk 0x%x", i);
		rc = Esys_FirmwareRead(self->esys_context,
				       ESYS_TR_NONE,
				       ESYS_TR_NONE,
				       ESYS_TR_NONE,
				       i /* seqnum */,
				       (TPM2B_MAX_BUFFER **)&data);
		if (rc == TPM2_RC_COMMAND_CODE ||
		    (rc == (TPM2_RC_COMMAND_CODE | TSS2_RESMGR_RC_LAYER)) ||
		    (rc == (TPM2_RC_COMMAND_CODE | TSS2_RESMGR_TPM_RC_LAYER))) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "TPM2_FirmwareRead not supported: 0x%x",
				    rc);
			return NULL;
		}
		if (data[0] == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "no data returned");
			return NULL;
		}
		if (data[0]->size == 0)
			break;

		/* yes, the blocks are returned in reverse order */
		g_byte_array_prepend(buf, data[0]->buffer, data[0]->size);
		Esys_Free(data[0]);
	}

	/* success */
	return g_bytes_new(buf->data, buf->len);
}

static gboolean
fu_tpm_v2_device_open(FuDevice *device, GError **error)
{
	FuTpmV2Device *self = FU_TPM_V2_DEVICE(device);
	TSS2_RC rc;

	/* setup TSS */
	rc = Esys_Initialize(&self->esys_context, NULL, NULL);
	if (rc != TSS2_RC_SUCCESS) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "failed to initialize TPM library");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_tpm_v2_device_close(FuDevice *device, GError **error)
{
	FuTpmV2Device *self = FU_TPM_V2_DEVICE(device);
	Esys_Finalize(&self->esys_context);
	return TRUE;
}

static void
fu_tpm_v2_device_init(FuTpmV2Device *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "org.trustedcomputinggroup.tpm2");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_AFFECTS_FDE);
	fu_device_set_firmware_size_max(FU_DEVICE(self), 32 * 1024 * 1024);
}

static void
fu_tpm_v2_device_class_init(FuTpmV2DeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->setup = fu_tpm_v2_device_setup;
	klass_device->probe = fu_tpm_v2_device_probe;
	klass_device->open = fu_tpm_v2_device_open;
	klass_device->close = fu_tpm_v2_device_close;
	klass_device->write_firmware = fu_tpm_v2_device_write_firmware;
	klass_device->dump_firmware = fu_tpm_v2_device_dump_firmware;
}

FuTpmDevice *
fu_tpm_v2_device_new(FuContext *ctx)
{
	FuTpmV2Device *self;
	self = g_object_new(FU_TYPE_TPM_V2_DEVICE, "context", ctx, NULL);
	return FU_TPM_DEVICE(self);
}
