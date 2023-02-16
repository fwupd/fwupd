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
};

G_DEFINE_TYPE(FuTpmV2Device, fu_tpm_v2_device, FU_TYPE_TPM_DEVICE)

static void
Esys_Finalize_autoptr_cleanup(ESYS_CONTEXT *esys_context)
{
	Esys_Finalize(&esys_context);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESYS_CONTEXT, Esys_Finalize_autoptr_cleanup)

static gboolean
fu_tpm_v2_device_probe(FuDevice *device, GError **error)
{
	return fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "tpm", error);
}

static gboolean
fu_tpm_v2_device_get_uint32(ESYS_CONTEXT *ctx, guint32 query, guint32 *val, GError **error)
{
	TSS2_RC rc;
	g_autofree TPMS_CAPABILITY_DATA *capability = NULL;

	g_return_val_if_fail(val != NULL, FALSE);

	rc = Esys_GetCapability(ctx,
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
fu_tpm_v2_device_get_string(ESYS_CONTEXT *ctx, guint32 query, GError **error)
{
	guint32 val_be = 0;
	guint32 val;
	gchar result[5] = {'\0'};

	/* return four bytes */
	if (!fu_tpm_v2_device_get_uint32(ctx, query, &val_be, error))
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
fu_tpm_v2_device_setup_pcrs(FuTpmV2Device *self, ESYS_CONTEXT *ctx, GError **error)
{
	TSS2_RC rc;
	g_autofree TPMS_CAPABILITY_DATA *capability_data = NULL;
	TPML_PCR_SELECTION pcr_selection_in = {
	    0,
	};
	g_autofree TPML_DIGEST *pcr_values = NULL;

	/* get hash algorithms supported by the TPM */
	rc = Esys_GetCapability(ctx,
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

	rc = Esys_PCR_Read(ctx,
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
	g_autoptr(ESYS_CONTEXT) ctx = NULL;

	/* suppress warning messages about missing TCTI libraries for tpm2-tss <2.3 */
	if (g_getenv("FWUPD_UEFI_VERBOSE") == NULL)
		(void)g_setenv("TSS2_LOG", "esys+none,tcti+none", FALSE);

	/* setup TSS */
	rc = Esys_Initialize(&ctx, NULL, NULL);
	if (rc != TSS2_RC_SUCCESS) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "failed to initialize TPM library");
		return FALSE;
	}
	rc = Esys_Startup(ctx, TPM2_SU_CLEAR);
	if (rc != TSS2_RC_SUCCESS) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "failed to initialize TPM");
		return FALSE;
	}

	/* lookup guaranteed details from TPM */
	family = fu_tpm_v2_device_get_string(ctx, TPM2_PT_FAMILY_INDICATOR, error);
	if (family == NULL) {
		g_prefix_error(error, "failed to read TPM family: ");
		return FALSE;
	}
	fu_tpm_device_set_family(FU_TPM_DEVICE(self), family);
	manufacturer = fu_tpm_v2_device_get_string(ctx, TPM2_PT_MANUFACTURER, error);
	if (manufacturer == NULL) {
		g_prefix_error(error, "failed to read TPM manufacturer: ");
		return FALSE;
	}
	model1 = fu_tpm_v2_device_get_string(ctx, TPM2_PT_VENDOR_STRING_1, error);
	if (model1 == NULL) {
		g_prefix_error(error, "failed to read TPM vendor string: ");
		return FALSE;
	}
	if (!fu_tpm_v2_device_get_uint32(ctx, TPM2_PT_VENDOR_TPM_TYPE, &tpm_type, error)) {
		g_prefix_error(error, "failed to read TPM type: ");
		return FALSE;
	}

	/* these are not guaranteed by spec and may be NULL */
	model2 = fu_tpm_v2_device_get_string(ctx, TPM2_PT_VENDOR_STRING_2, error);
	model3 = fu_tpm_v2_device_get_string(ctx, TPM2_PT_VENDOR_STRING_3, error);
	model4 = fu_tpm_v2_device_get_string(ctx, TPM2_PT_VENDOR_STRING_4, error);
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
	if (!fu_tpm_v2_device_get_uint32(ctx, TPM2_PT_FIRMWARE_VERSION_1, &version1, error))
		return FALSE;
	if (!fu_tpm_v2_device_get_uint32(ctx, TPM2_PT_FIRMWARE_VERSION_2, &version2, error))
		return FALSE;
	version_raw = ((guint64)version1) << 32 | ((guint64)version2);
	fu_device_set_version_from_uint64(device, version_raw);

	/* get PCRs */
	return fu_tpm_v2_device_setup_pcrs(self, ctx, error);
}

static void
fu_tpm_v2_device_init(FuTpmV2Device *self)
{
}

static void
fu_tpm_v2_device_class_init(FuTpmV2DeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->setup = fu_tpm_v2_device_setup;
	klass_device->probe = fu_tpm_v2_device_probe;
}

FuTpmDevice *
fu_tpm_v2_device_new(FuContext *ctx)
{
	FuTpmV2Device *self;
	self = g_object_new(FU_TYPE_TPM_V2_DEVICE, "context", ctx, NULL);
	return FU_TPM_DEVICE(self);
}
