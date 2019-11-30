/*
 * Copyright (C) 2012 Intel Corporation.
 * Copyright (C) 2017 Google, Inc.
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/mei.h>
#include <string.h>
#include <sys/ioctl.h>

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

typedef struct {
	uuid_le guid;
	guint buf_size;
	guchar prot_ver;
	gint fd;
} mei_context;

static void
mei_context_free (mei_context *cl)
{
	if (cl->fd != -1)
		close(cl->fd);
	g_free (cl);
}

static gboolean
mei_context_new (mei_context *ctx,
		 const uuid_le *guid,
		 guchar req_protocol_version,
		 GError **error)
{
	gint result;
	struct mei_client *cl;
	struct mei_connect_client_data data;

	ctx->fd = open ("/dev/mei0", O_RDWR);
	if (ctx->fd == -1 && errno == ENOENT)
		ctx->fd = open ("/dev/mei", O_RDWR);
	if (ctx->fd == -1) {
		if (errno == ENOENT) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "Unable to find a ME interface");
		} else {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_FOUND,
					     "Cannot open /dev/mei0");
		}
		return FALSE;
	}
	memcpy (&ctx->guid, guid, sizeof(*guid));
	memset (&data, 0, sizeof(data));
	memcpy (&data.in_client_uuid, &ctx->guid, sizeof(ctx->guid));
	result = ioctl (ctx->fd, IOCTL_MEI_CONNECT_CLIENT, &data);
	if (result != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "ME refused connection");
		return FALSE;
	}
	cl = &data.out_client_properties;
	if ((req_protocol_version > 0) &&
	     (cl->protocol_version != req_protocol_version)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Intel MEI protocol version not supported %i",
			     cl->protocol_version);
		return FALSE;
	}

	ctx->buf_size = cl->max_msg_length;
	ctx->prot_ver = cl->protocol_version;
	return TRUE;
}

static gboolean
mei_recv_msg (mei_context *ctx, guchar *buffer,
	      gssize len, guint32 *readsz, unsigned long timeout, GError **error)
{
	gssize rc;
	rc = read (ctx->fd, buffer, len);
	if (rc < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "read failed with status %zd %s",
			     rc, strerror(errno));
		return FALSE;
	}
	if (readsz != NULL)
		*readsz = rc;
	return TRUE;
}

static gboolean
mei_send_msg (mei_context *ctx, const guchar *buffer,
	      gssize len, unsigned long timeout, GError **error)
{
	struct timeval tv;
	gssize written;
	gssize rc;
	fd_set set;

	tv.tv_sec = timeout / 1000;
	tv.tv_usec = (timeout % 1000) * 1000000;

	written = write (ctx->fd, buffer, len);
	if (written < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "write failed with status %zd %s",
			     written, strerror(errno));
		return FALSE;
	}
	if (written != len) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "only wrote %" G_GSSIZE_FORMAT " of %" G_GSSIZE_FORMAT,
			     written, len);
		return FALSE;
	}

	FD_ZERO(&set);
	FD_SET(ctx->fd, &set);
	rc = select (ctx->fd + 1 , &set, NULL, NULL, &tv);
	if (rc > 0 && FD_ISSET(ctx->fd, &set))
		return TRUE;

	/* timed out */
	if (rc == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "write failed on timeout with status");
		return FALSE;
	}

	/* rc < 0 */
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_WRITE,
		     "write failed on select with status %zd", rc);
	return FALSE;
}

/***************************************************************************
 * Intel Advanced Management Technology ME Client
 ***************************************************************************/

#define AMT_MAJOR_VERSION 1
#define AMT_MINOR_VERSION 1

#define AMT_STATUS_SUCCESS			0x0
#define AMT_STATUS_INTERNAL_ERROR		0x1
#define AMT_STATUS_NOT_READY			0x2
#define AMT_STATUS_INVALID_AMT_MODE		0x3
#define AMT_STATUS_INVALID_MESSAGE_LENGTH	0x4

#define AMT_STATUS_HOST_IF_EMPTY_RESPONSE	0x4000
#define AMT_STATUS_SDK_RESOURCES		0x1004


#define AMT_BIOS_VERSION_LEN			65
#define AMT_VERSIONS_NUMBER			50
#define AMT_UNICODE_STRING_LEN			20

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
	.length = 0
};

#define AMT_HOST_IF_PROVISIONING_MODE_REQUEST  0x04000008
#define AMT_HOST_IF_PROVISIONING_MODE_RESPONSE 0x04800008

const struct amt_host_if_msg_header PROVISIONING_MODE_REQUEST = {
	.version = {AMT_MAJOR_VERSION, AMT_MINOR_VERSION},
	._reserved = 0,
	.command = AMT_HOST_IF_PROVISIONING_MODE_REQUEST,
	.length = 0
};

#define AMT_HOST_IF_PROVISIONING_STATE_REQUEST  0x04000011
#define AMT_HOST_IF_PROVISIONING_STATE_RESPONSE 0x04800011

const struct amt_host_if_msg_header PROVISIONING_STATE_REQUEST = {
	.version = {AMT_MAJOR_VERSION, AMT_MINOR_VERSION},
	._reserved = 0,
	.command = AMT_HOST_IF_PROVISIONING_STATE_REQUEST,
	.length = 0
};

struct amt_host_if {
	mei_context mei_cl;
};

static gboolean
amt_verify_code_versions (const struct amt_host_if_resp_header *resp, GError **error)
{
	struct amt_code_versions *code_ver = (struct amt_code_versions *)resp->data;
	gsize code_ver_len = resp->header.length - sizeof(guint32);
	guint32 ver_type_cnt = code_ver_len -
					sizeof(code_ver->bios) -
					sizeof(code_ver->count);
	if (code_ver->count != ver_type_cnt / sizeof(struct amt_version_type)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "invalid offset");
		return FALSE;
	}
	for (guint32 i = 0; i < code_ver->count; i++) {
		guint32 len = code_ver->versions[i].description.length;
		if (len > AMT_UNICODE_STRING_LEN) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "string too large");
			return FALSE;
		}
		len = code_ver->versions[i].version.length;
		if (code_ver->versions[i].version.string[len] != '\0' ||
		    len != strlen(code_ver->versions[i].version.string)) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "string was invalid size");
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
amt_status_set_error (guint32 status, GError **error)
{
	if (status == AMT_STATUS_SUCCESS)
		return TRUE;
	if (status == AMT_STATUS_INTERNAL_ERROR) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "internal error");
		return FALSE;
	}
	if (status == AMT_STATUS_NOT_READY) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "not ready");
		return FALSE;
	}
	if (status == AMT_STATUS_INVALID_AMT_MODE) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "invalid AMT mode");
		return FALSE;
	}
	if (status == AMT_STATUS_INVALID_MESSAGE_LENGTH) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "invalid message length");
		return FALSE;
	}
	if (status == AMT_STATUS_HOST_IF_EMPTY_RESPONSE) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Intel AMT is disabled");
		return FALSE;
	}
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "unknown error");
	return FALSE;
}

static gboolean
amt_host_if_call (mei_context *mei_cl,
		  const guchar *command,
		  gssize command_sz,
		  guint8 **read_buf,
		  guint32 rcmd,
		  guint expected_sz,
		  unsigned long send_timeout,
		  GError **error)
{
	guint32 in_buf_sz;
	guint32 out_buf_sz;
	struct amt_host_if_resp_header *msg_hdr;

	in_buf_sz = mei_cl->buf_size;
	*read_buf = (guint8 *) g_malloc0 (in_buf_sz);
	msg_hdr = (struct amt_host_if_resp_header *) *read_buf;

	if (!mei_send_msg (mei_cl, command, command_sz, send_timeout, error))
		return FALSE;
	if (!mei_recv_msg (mei_cl, *read_buf, in_buf_sz, &out_buf_sz, 2000, error))
		return FALSE;
	if (out_buf_sz <= 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_READ,
				     "empty response");
		return FALSE;
	}
	if (expected_sz && expected_sz != out_buf_sz) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "expected %u but got %" G_GUINT32_FORMAT,
			     expected_sz, out_buf_sz);
		return FALSE;
	}
	if (!amt_status_set_error (msg_hdr->status, error))
		return FALSE;
	if (out_buf_sz < sizeof(struct amt_host_if_resp_header)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_READ,
				     "invalid response: too small");
		return FALSE;
	}
	if (out_buf_sz != (msg_hdr->header.length +
				sizeof(struct amt_host_if_msg_header))) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_READ,
				     "invalid response: headerlen");
		return FALSE;
	}
	if (msg_hdr->header.command != rcmd) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_READ,
				     "invalid response: rcmd");
		return FALSE;
	}
	if (msg_hdr->header._reserved != 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_READ,
				     "invalid response: reserved");
		return FALSE;
	}
	if (msg_hdr->header.version.major != AMT_MAJOR_VERSION ||
	    msg_hdr->header.version.minor < AMT_MINOR_VERSION) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_READ,
				     "invalid response: version");
		return FALSE;
	}
	return TRUE;
}

static gboolean
amt_get_provisioning_state (mei_context *mei_cl, guint8 *state, GError **error)
{
	g_autofree struct amt_host_if_resp_header *response = NULL;
	if (!amt_host_if_call (mei_cl,
			       (const guchar *)&PROVISIONING_STATE_REQUEST,
			       sizeof(PROVISIONING_STATE_REQUEST),
			       (guint8 **)&response,
			       AMT_HOST_IF_PROVISIONING_STATE_RESPONSE, 0,
			       5000, error)) {
		g_prefix_error (error, "unable to get provisioning state: ");
		return FALSE;
	}
	*state = response->data[0];
	return TRUE;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(mei_context, mei_context_free)
#pragma clang diagnostic pop

static FuDevice *
fu_plugin_amt_create_device (GError **error)
{
	guint8 state;
	struct amt_code_versions ver;
	fwupd_guid_t uu;
	g_autofree gchar *guid_buf = NULL;
	g_autofree struct amt_host_if_resp_header *response = NULL;
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(GString) version_bl = g_string_new (NULL);
	g_autoptr(GString) version_fw = g_string_new (NULL);
	g_autoptr(mei_context) ctx = g_new0 (mei_context, 1);

	const uuid_le MEI_IAMTHIF = UUID_LE(0x12f80028, 0xb4b7, 0x4b2d,  \
				0xac, 0xa8, 0x46, 0xe0, 0xff, 0x65, 0x81, 0x4c);

	/* create context */
	if (!mei_context_new (ctx, &MEI_IAMTHIF, 0, error))
		return NULL;

	/* check version */
	if (!amt_host_if_call (ctx,
			       (const guchar *) &CODE_VERSION_REQ,
			       sizeof(CODE_VERSION_REQ),
			       (guint8 **) &response,
			       AMT_HOST_IF_CODE_VERSIONS_RESPONSE, 0,
			       5000,
			       error)) {
		g_prefix_error (error, "Failed to check version: ");
		return NULL;
	}
	if (!amt_verify_code_versions (response, error)) {
		g_prefix_error (error, "failed to verify code versions: ");
		return NULL;
	}
	memcpy (&ver, response->data, sizeof(struct amt_code_versions));

	dev = fu_device_new ();
	fu_device_set_id (dev, "/dev/mei0");
	fu_device_set_vendor (dev, "Intel Corporation");
	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_icon (dev, "computer");
	fu_device_add_parent_guid (dev, "main-system-firmware");
	if (!amt_get_provisioning_state (ctx, &state, error))
		return NULL;
	switch (state) {
	case 0:
		fu_device_set_name (dev, "Intel AMT [unprovisioned]");
		break;
	case 1:
		fu_device_set_name (dev, "Intel AMT [being provisioned]");
		break;
	case 2:
		fu_device_set_name (dev, "Intel AMT [provisioned]");
		break;
	default:
		fu_device_set_name (dev, "Intel AMT [unknown]");
		break;
	}
	fu_device_set_summary (dev, "Hardware and firmware technology for remote "
				    "out-of-band management");

	/* add guid */
	memcpy (&uu, &ctx->guid, 16);
	guid_buf = fwupd_guid_to_string ((const fwupd_guid_t *) &uu, FWUPD_GUID_FLAG_NONE);
	fu_device_add_guid (dev, guid_buf);

	/* get version numbers */
	for (guint i = 0; i < ver.count; i++) {
		if (g_strcmp0 (ver.versions[i].description.string, "AMT") == 0) {
			g_string_append (version_fw, ver.versions[i].version.string);
			continue;
		}
		if (g_strcmp0 (ver.versions[i].description.string, "Recovery Version") == 0) {
			g_string_append (version_bl, ver.versions[i].version.string);
			continue;
		}
		if (g_strcmp0 (ver.versions[i].description.string, "Build Number") == 0) {
			g_string_append_printf (version_fw, ".%s",
						ver.versions[i].version.string);
			continue;
		}
		if (g_strcmp0 (ver.versions[i].description.string, "Recovery Build Num") == 0) {
			g_string_append_printf (version_bl, ".%s",
						ver.versions[i].version.string);
			continue;
		}
	}
	if (version_fw->len > 0)
		fu_device_set_version (dev, version_fw->str, FWUPD_VERSION_FORMAT_INTEL_ME);
	if (version_bl->len > 0)
		fu_device_set_version_bootloader (dev, version_bl->str);

	return g_steal_pointer (&dev);
}

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	g_autoptr(FuDevice) dev = NULL;
	dev = fu_plugin_amt_create_device (error);
	if (dev == NULL)
		return FALSE;
	fu_plugin_device_add (plugin, dev);
	return TRUE;
}
