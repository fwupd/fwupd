/*
 * Copyright (C) 2012 Intel Corporation.
 * Copyright (C) 2017 Google, Inc.
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-amt-device.h"

struct _FuAmtDevice {
	FuMeiDevice parent_instance;
};

G_DEFINE_TYPE(FuAmtDevice, fu_amt_device, FU_TYPE_MEI_DEVICE)

#define FU_AMT_DEVICE_MEI_IAMTHIF "2800f812-b7b4-2d4b-aca8-46e0ff65814c"

#define AMT_MAJOR_VERSION 1
#define AMT_MINOR_VERSION 1

#define AMT_STATUS_SUCCESS		  0x0
#define AMT_STATUS_INTERNAL_ERROR	  0x1
#define AMT_STATUS_NOT_READY		  0x2
#define AMT_STATUS_INVALID_AMT_MODE	  0x3
#define AMT_STATUS_INVALID_MESSAGE_LENGTH 0x4

#define AMT_STATUS_HOST_IF_EMPTY_RESPONSE 0x4000
#define AMT_STATUS_SDK_RESOURCES	  0x1004

#define AMT_BIOS_VERSION_LEN   65
#define AMT_VERSIONS_NUMBER    50
#define AMT_UNICODE_STRING_LEN 20

struct amt_unicode_string {
	guint16 length;
	char string[AMT_UNICODE_STRING_LEN];
} __attribute__((packed));

struct amt_version_type {
	struct amt_unicode_string description;
	struct amt_unicode_string version;
} __attribute__((packed));

struct amt_version {
	guint8 major;
	guint8 minor;
} __attribute__((packed));

struct amt_code_versions {
	guint8 bios[AMT_BIOS_VERSION_LEN];
	guint32 count;
	struct amt_version_type versions[AMT_VERSIONS_NUMBER];
} __attribute__((packed));

struct amt_provisioning_state {
	guint8 bios[AMT_BIOS_VERSION_LEN];
	guint32 count;
	guint8 state;
} __attribute__((packed));

/***************************************************************************
 * Intel Advanced Management Technology Host Interface
 ***************************************************************************/

struct amt_host_if_msg_header {
	struct amt_version version;
	guint16 _reserved;
	guint32 command;
	guint32 length;
} __attribute__((packed));

struct amt_host_if_resp_header {
	struct amt_host_if_msg_header header;
	guint32 status;
	guchar data[0];
} __attribute__((packed));

#define AMT_HOST_IF_CODE_VERSIONS_REQUEST  0x0400001A
#define AMT_HOST_IF_CODE_VERSIONS_RESPONSE 0x0480001A

const struct amt_host_if_msg_header CODE_VERSION_REQ = {
    .version = {AMT_MAJOR_VERSION, AMT_MINOR_VERSION},
    ._reserved = 0,
    .command = AMT_HOST_IF_CODE_VERSIONS_REQUEST,
    .length = 0};

#define AMT_HOST_IF_PROVISIONING_MODE_REQUEST  0x04000008
#define AMT_HOST_IF_PROVISIONING_MODE_RESPONSE 0x04800008

const struct amt_host_if_msg_header PROVISIONING_MODE_REQUEST = {
    .version = {AMT_MAJOR_VERSION, AMT_MINOR_VERSION},
    ._reserved = 0,
    .command = AMT_HOST_IF_PROVISIONING_MODE_REQUEST,
    .length = 0};

#define AMT_HOST_IF_PROVISIONING_STATE_REQUEST	0x04000011
#define AMT_HOST_IF_PROVISIONING_STATE_RESPONSE 0x04800011

const struct amt_host_if_msg_header PROVISIONING_STATE_REQUEST = {
    .version = {AMT_MAJOR_VERSION, AMT_MINOR_VERSION},
    ._reserved = 0,
    .command = AMT_HOST_IF_PROVISIONING_STATE_REQUEST,
    .length = 0};

struct amt_host_if {
	FuAmtDevice self;
};

static gboolean
fu_amt_device_verify_code_versions(const struct amt_host_if_resp_header *resp, GError **error)
{
	struct amt_code_versions *code_ver = (struct amt_code_versions *)resp->data;
	gsize code_ver_len = resp->header.length - sizeof(guint32);
	guint32 ver_type_cnt = code_ver_len - sizeof(code_ver->bios) - sizeof(code_ver->count);
	if (code_ver->count != ver_type_cnt / sizeof(struct amt_version_type)) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "invalid offset");
		return FALSE;
	}
	for (guint32 i = 0; i < code_ver->count; i++) {
		guint32 len = code_ver->versions[i].description.length;
		if (len > AMT_UNICODE_STRING_LEN) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "string too large");
			return FALSE;
		}
		len = code_ver->versions[i].version.length;
		if (code_ver->versions[i].version.string[len] != '\0' ||
		    len != strlen(code_ver->versions[i].version.string)) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "string was invalid size");
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_amt_device_status_set_error(guint32 status, GError **error)
{
	if (status == AMT_STATUS_SUCCESS)
		return TRUE;
	if (status == AMT_STATUS_INTERNAL_ERROR) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "internal error");
		return FALSE;
	}
	if (status == AMT_STATUS_NOT_READY) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "not ready");
		return FALSE;
	}
	if (status == AMT_STATUS_INVALID_AMT_MODE) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "invalid AMT mode");
		return FALSE;
	}
	if (status == AMT_STATUS_INVALID_MESSAGE_LENGTH) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "invalid message length");
		return FALSE;
	}
	if (status == AMT_STATUS_HOST_IF_EMPTY_RESPONSE) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "Intel AMT is disabled");
		return FALSE;
	}
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "unknown error");
	return FALSE;
}

static gboolean
fu_amt_device_host_if_call(FuAmtDevice *self,
			   const guchar *command,
			   gssize command_sz,
			   guint8 **read_buf,
			   guint32 rcmd,
			   guint expected_sz,
			   unsigned long send_timeout,
			   GError **error)
{
	gsize in_buf_sz = fu_mei_device_get_max_msg_length(FU_MEI_DEVICE(self));
	gsize out_buf_sz;
	struct amt_host_if_resp_header *msg_hdr;

	*read_buf = (guint8 *)g_malloc0(in_buf_sz);
	msg_hdr = (struct amt_host_if_resp_header *)*read_buf;

	if (!fu_mei_device_write(FU_MEI_DEVICE(self), command, command_sz, send_timeout, error))
		return FALSE;
	if (!fu_mei_device_read(FU_MEI_DEVICE(self),
				*read_buf,
				in_buf_sz,
				&out_buf_sz,
				2000,
				error))
		return FALSE;
	if (out_buf_sz <= 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_READ, "empty response");
		return FALSE;
	}
	if (expected_sz && expected_sz != out_buf_sz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "expected %u but got %u",
			    (guint)expected_sz,
			    (guint)out_buf_sz);
		return FALSE;
	}
	if (!fu_amt_device_status_set_error(msg_hdr->status, error))
		return FALSE;
	if (out_buf_sz < sizeof(struct amt_host_if_resp_header)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "invalid response: too small");
		return FALSE;
	}
	if (out_buf_sz != (msg_hdr->header.length + sizeof(struct amt_host_if_msg_header))) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "invalid response: headerlen");
		return FALSE;
	}
	if (msg_hdr->header.command != rcmd) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_READ, "invalid response: rcmd");
		return FALSE;
	}
	if (msg_hdr->header._reserved != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "invalid response: reserved");
		return FALSE;
	}
	if (msg_hdr->header.version.major != AMT_MAJOR_VERSION ||
	    msg_hdr->header.version.minor < AMT_MINOR_VERSION) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "invalid response: version");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_amt_device_get_provisioning_state(FuAmtDevice *self, guint8 *state, GError **error)
{
	g_autofree struct amt_host_if_resp_header *response = NULL;
	if (!fu_amt_device_host_if_call(self,
					(const guchar *)&PROVISIONING_STATE_REQUEST,
					sizeof(PROVISIONING_STATE_REQUEST),
					(guint8 **)&response,
					AMT_HOST_IF_PROVISIONING_STATE_RESPONSE,
					0,
					5000,
					error)) {
		g_prefix_error(error, "unable to get provisioning state: ");
		return FALSE;
	}
	*state = response->data[0];
	return TRUE;
}

static gboolean
fu_amt_device_setup(FuDevice *device, GError **error)
{
	FuAmtDevice *self = FU_AMT_DEVICE(device);
	guint8 state;
	struct amt_code_versions ver;
	g_autofree struct amt_host_if_resp_header *response = NULL;
	g_autoptr(GString) version_bl = g_string_new(NULL);
	g_autoptr(GString) version_fw = g_string_new(NULL);

	/* create context */
	if (!fu_mei_device_connect(FU_MEI_DEVICE(self), FU_AMT_DEVICE_MEI_IAMTHIF, 0, error))
		return FALSE;

	/* check version */
	if (!fu_amt_device_host_if_call(self,
					(const guchar *)&CODE_VERSION_REQ,
					sizeof(CODE_VERSION_REQ),
					(guint8 **)&response,
					AMT_HOST_IF_CODE_VERSIONS_RESPONSE,
					0,
					5000,
					error)) {
		g_prefix_error(error, "Failed to check version: ");
		return FALSE;
	}
	if (!fu_amt_device_verify_code_versions(response, error)) {
		g_prefix_error(error, "failed to verify code versions: ");
		return FALSE;
	}
	memcpy(&ver, response->data, sizeof(struct amt_code_versions));

	if (!fu_amt_device_get_provisioning_state(self, &state, error))
		return FALSE;
	switch (state) {
	case 0:
		fu_device_set_name(device, "AMT [unprovisioned]");
		break;
	case 1:
		fu_device_set_name(device, "AMT [being provisioned]");
		break;
	case 2:
		fu_device_set_name(device, "AMT [provisioned]");
		break;
	default:
		fu_device_set_name(device, "AMT [unknown]");
		break;
	}

	/* add guid */
	fu_device_add_guid(device, FU_AMT_DEVICE_MEI_IAMTHIF);
	fu_device_add_parent_guid(device, "main-system-firmware");

	/* get version numbers */
	for (guint i = 0; i < ver.count; i++) {
		if (g_strcmp0(ver.versions[i].description.string, "AMT") == 0) {
			g_string_append(version_fw, ver.versions[i].version.string);
			continue;
		}
		if (g_strcmp0(ver.versions[i].description.string, "Recovery Version") == 0) {
			g_string_append(version_bl, ver.versions[i].version.string);
			continue;
		}
		if (g_strcmp0(ver.versions[i].description.string, "Build Number") == 0) {
			g_string_append_printf(version_fw, ".%s", ver.versions[i].version.string);
			continue;
		}
		if (g_strcmp0(ver.versions[i].description.string, "Recovery Build Num") == 0) {
			g_string_append_printf(version_bl, ".%s", ver.versions[i].version.string);
			continue;
		}
	}
	if (version_fw->len > 0)
		fu_device_set_version(device, version_fw->str);
	if (version_bl->len > 0)
		fu_device_set_version_bootloader(device, version_bl->str);

	/* success */
	return TRUE;
}

static void
fu_amt_device_init(FuAmtDevice *self)
{
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_INTEL_ME);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_icon(FU_DEVICE(self), "computer");
	fu_device_set_summary(FU_DEVICE(self),
			      "Hardware and firmware technology for remote "
			      "out-of-band management");
}

static void
fu_amt_device_class_init(FuAmtDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->setup = fu_amt_device_setup;
}
