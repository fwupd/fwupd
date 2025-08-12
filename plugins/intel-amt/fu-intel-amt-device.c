/*
 * Copyright 2012 Intel Corporation.
 * Copyright 2017 Google, Inc.
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-intel-amt-device.h"
#include "fu-intel-amt-struct.h"

struct _FuIntelAmtDevice {
	FuMeiDevice parent_instance;
};

G_DEFINE_TYPE(FuIntelAmtDevice, fu_intel_amt_device, FU_TYPE_MEI_DEVICE)

#define FU_AMT_STATUS_HOST_IF_EMPTY_RESPONSE 0x4000

#define FU_INTEL_AMT_DEVICE_UUID "12f80028-b4b7-4b2d-aca8-46e0ff65814c"

static gboolean
fu_intel_amt_device_status_set_error(guint32 status, GError **error)
{
	if (status == FU_AMT_STATUS_SUCCESS)
		return TRUE;
	if (status == FU_AMT_STATUS_INTERNAL_ERROR) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "internal error");
		return FALSE;
	}
	if (status == FU_AMT_STATUS_NOT_READY) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "not ready");
		return FALSE;
	}
	if (status == FU_AMT_STATUS_INVALID_AMT_MODE) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "invalid AMT mode");
		return FALSE;
	}
	if (status == FU_AMT_STATUS_INVALID_MESSAGE_LENGTH) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "invalid message length");
		return FALSE;
	}
	if (status == FU_AMT_STATUS_HOST_IF_EMPTY_RESPONSE) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "Intel AMT is disabled");
		return FALSE;
	}
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "unknown error");
	return FALSE;
}

static GByteArray *
fu_intel_amt_device_host_if_call(FuIntelAmtDevice *self, GByteArray *inbuf, GError **error)
{
	gsize outbufsz;
	g_autoptr(GByteArray) outbuf = g_byte_array_new();

	fu_byte_array_set_size(outbuf, fu_mei_device_get_max_msg_length(FU_MEI_DEVICE(self)), 0x0);
	if (!fu_mei_device_write(FU_MEI_DEVICE(self), inbuf->data, inbuf->len, 5000, error))
		return NULL;
	if (!fu_mei_device_read(FU_MEI_DEVICE(self),
				outbuf->data,
				outbuf->len,
				&outbufsz,
				2000,
				error))
		return NULL;
	if (outbufsz <= 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_READ, "empty response");
		return NULL;
	}
	g_byte_array_set_size(outbuf, outbufsz);
	return g_steal_pointer(&outbuf);
}

static gboolean
fu_intel_amt_device_get_provisioning_state(FuIntelAmtDevice *self,
					   FuAmtProvisioningState *provisioning_state,
					   GError **error)
{
	g_autofree struct FuAmtHostIfRespHeader *response = NULL;
	g_autoptr(GByteArray) data = NULL;
	g_autoptr(FuAmtHostIfMsgProvisioningStateRequest) st_req =
	    fu_amt_host_if_msg_provisioning_state_request_new();
	g_autoptr(FuAmtHostIfMsgProvisioningStateResponse) st_res = NULL;

	data = fu_intel_amt_device_host_if_call(self, st_req, error);
	if (data == NULL)
		return FALSE;

	/* parse response */
	st_res =
	    fu_amt_host_if_msg_provisioning_state_response_parse(data->data, data->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;
	if (!fu_intel_amt_device_status_set_error(
		fu_amt_host_if_msg_provisioning_state_response_get_status(st_res),
		error))
		return FALSE;
	*provisioning_state =
	    fu_amt_host_if_msg_provisioning_state_response_get_provisioning_state(st_res);

	/* success */
	return TRUE;
}

static gboolean
fu_intel_amt_device_ensure_version(FuIntelAmtDevice *self, GError **error)
{
	guint32 version_count;
	g_autoptr(FuAmtHostIfMsgCodeVersionRequest) st_req =
	    fu_amt_host_if_msg_code_version_request_new();
	g_autoptr(FuAmtHostIfMsgCodeVersionResponse) st_res = NULL;
	g_autoptr(GByteArray) data = NULL;
	g_autoptr(GString) version_bl = g_string_new(NULL);
	g_autoptr(GString) version_fw = g_string_new(NULL);

	data = fu_intel_amt_device_host_if_call(self, st_req, error);
	if (data == NULL)
		return FALSE;

	/* parse response */
	st_res = fu_amt_host_if_msg_code_version_response_parse(data->data, data->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;
	if (!fu_intel_amt_device_status_set_error(
		fu_amt_host_if_msg_code_version_response_get_status(st_res),
		error))
		return FALSE;

	/* parse each version */
	version_count = fu_amt_host_if_msg_code_version_response_get_version_count(st_res);
	for (guint i = 0; i < version_count; i++) {
		g_autofree gchar *description = NULL;
		g_autofree gchar *version = NULL;
		g_autoptr(FuAmtUnicodeString) st_str = NULL;

		st_str = fu_amt_unicode_string_parse(data->data,
						     data->len,
						     st_res->len + (i * FU_AMT_UNICODE_STRING_SIZE),
						     error);
		if (st_str == NULL)
			return FALSE;

		/* get description */
		if (fu_amt_unicode_string_get_description_length(st_str) >
		    FU_AMT_UNICODE_STRING_SIZE_DESCRIPTION_STRING) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "description string too large");
			return FALSE;
		}
		description = fu_amt_unicode_string_get_description_string(st_str);

		/* get version */
		if (fu_amt_unicode_string_get_version_length(st_str) >
		    FU_AMT_UNICODE_STRING_SIZE_VERSION_STRING) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "version string too large");
			return FALSE;
		}
		version = fu_amt_unicode_string_get_version_string(st_str);

		/* build something suitable for fwupd */
		if (g_strcmp0(description, "AMT") == 0) {
			g_string_append(version_fw, version);
			continue;
		}
		if (g_strcmp0(description, "Recovery Version") == 0) {
			g_string_append(version_bl, version);
			continue;
		}
		if (g_strcmp0(description, "Build Number") == 0) {
			g_string_append_printf(version_fw, ".%s", version);
			continue;
		}
		if (g_strcmp0(description, "Recovery Build Num") == 0) {
			g_string_append_printf(version_bl, ".%s", version);
			continue;
		}
	}

	/* success */
	if (version_fw->len > 0)
		fu_device_set_version(FU_DEVICE(self), version_fw->str);
	if (version_bl->len > 0)
		fu_device_set_version_bootloader(FU_DEVICE(self), version_bl->str);
	return TRUE;
}

static gboolean
fu_intel_amt_device_setup(FuDevice *device, GError **error)
{
	FuIntelAmtDevice *self = FU_INTEL_AMT_DEVICE(device);
	FuAmtProvisioningState provisioning_state = FU_AMT_PROVISIONING_STATE_UNPROVISIONED;

	/* get versions */
	if (!fu_mei_device_connect(FU_MEI_DEVICE(device), FU_INTEL_AMT_DEVICE_UUID, 0, error)) {
		g_prefix_error_literal(error, "failed to connect: ");
		return FALSE;
	}
	if (!fu_intel_amt_device_ensure_version(self, error)) {
		g_prefix_error_literal(error, "failed to check version: ");
		return FALSE;
	}

	/* get provisioning state */
	if (!fu_intel_amt_device_get_provisioning_state(self, &provisioning_state, error)) {
		g_prefix_error_literal(error, "failed to get provisioning state: ");
		return FALSE;
	}
	if (provisioning_state < FU_AMT_PROVISIONING_STATE_LAST) {
		g_autofree gchar *name =
		    g_strdup_printf("AMT [%s]",
				    fu_amt_provisioning_state_to_string(provisioning_state));
		fu_device_set_name(device, name);
	}

	/* success */
	return TRUE;
}

static void
fu_intel_amt_device_init(FuIntelAmtDevice *self)
{
	fu_device_set_logical_id(FU_DEVICE(self), "AMT");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_INTEL_ME);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_HOST_FIRMWARE_CHILD);
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_COMPUTER);
	fu_device_set_name(FU_DEVICE(self), "AMT");
	fu_device_set_summary(FU_DEVICE(self),
			      "Hardware and firmware technology for remote "
			      "out-of-band management");
}

static void
fu_intel_amt_device_class_init(FuIntelAmtDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_intel_amt_device_setup;
}
