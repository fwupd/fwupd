/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-intel-me-common.h"
#include "fu-intel-me-heci-device.h"

G_DEFINE_TYPE(FuIntelMeHeciDevice, fu_intel_me_heci_device, FU_TYPE_MEI_DEVICE)

#define FU_INTEL_ME_HECI_DEVICE_TIMEOUT 200 /* ms */

static gboolean
fu_intel_me_heci_device_open(FuDevice *device, GError **error)
{
	/* open then create context */
	if (!FU_DEVICE_CLASS(fu_intel_me_heci_device_parent_class)->open(device, error))
		return FALSE;
	return fu_mei_device_connect(FU_MEI_DEVICE(device), 0, error);
}

static GByteArray *
fu_intel_me_heci_device_read_file_response(GByteArray *buf_res, guint32 datasz_req, GError **error)
{
	guint32 datasz_res = 0;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* verify payload size */
	if (!fu_memread_uint32_safe(buf_res->data,
				    buf_res->len,
				    sizeof(FuMkhiHeader),
				    &datasz_res,
				    G_LITTLE_ENDIAN,
				    error))
		return NULL;
	if (datasz_res > datasz_req || datasz_res == 0x0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid response data size, requested 0x%x and got 0x%x",
			    datasz_req,
			    datasz_res);
		return NULL;
	}

	/* copy out payload */
	for (gsize i = 0; i < datasz_res; i++) {
		guint8 tmp = 0;
		if (!fu_memread_uint8_safe(buf_res->data,
					   buf_res->len,
					   sizeof(FuMkhiHeader) + sizeof(guint32) + i,
					   &tmp,
					   error))
			return NULL;
		fu_byte_array_append_uint8(buf, tmp);
	}

	/* success */
	return g_steal_pointer(&buf);
}

GByteArray *
fu_intel_me_heci_device_read_file(FuIntelMeHeciDevice *self, const gchar *filename, GError **error)
{
	FuMkhiHeader hdr_res = {0};
	gsize filenamesz = strlen(filename);
	guint datasz_req = 0x80;
	g_autoptr(GByteArray) buf_fn = g_byte_array_new();
	g_autoptr(GByteArray) buf_req = g_byte_array_new();
	g_autoptr(GByteArray) buf_res = g_byte_array_new();
	const FuMkhiHeader hdr_req = {.group_id = MKHI_GROUP_ID_MCA, .command = MCA_READ_FILE_EX};

	/* filename must be a specific size */
	fu_byte_array_set_size(buf_fn, 0x40, 0x0);
	if (!fu_memcpy_safe(buf_fn->data,
			    buf_fn->len - 1,
			    0x0,
			    (const guint8 *)filename,
			    filenamesz,
			    0x0,
			    filenamesz,
			    error))
		return NULL;

	/* request */
	g_byte_array_append(buf_req, (const guint8 *)&hdr_req, sizeof(hdr_req));
	g_byte_array_append(buf_req, buf_fn->data, buf_fn->len);	   /* Filename */
	fu_byte_array_append_uint32(buf_req, 0x0, G_LITTLE_ENDIAN);	   /* Offset */
	fu_byte_array_append_uint32(buf_req, datasz_req, G_LITTLE_ENDIAN); /* DataSize */
	fu_byte_array_append_uint8(buf_req, (1 << 3));			   /* Flags?? */
	if (!fu_mei_device_write(FU_MEI_DEVICE(self),
				 buf_req->data,
				 buf_req->len,
				 FU_INTEL_ME_HECI_DEVICE_TIMEOUT,
				 error))
		return NULL;

	/* response */
	fu_byte_array_set_size(buf_res, sizeof(hdr_res) + sizeof(guint32) + datasz_req, 0x0);
	if (!fu_mei_device_read(FU_MEI_DEVICE(self),
				buf_res->data,
				buf_res->len,
				NULL,
				FU_INTEL_ME_HECI_DEVICE_TIMEOUT,
				error))
		return NULL;

	/* verify header */
	if (!fu_memcpy_safe((guint8 *)&hdr_res,
			    sizeof(hdr_res),
			    0x0, /* dst */
			    buf_res->data,
			    buf_res->len,
			    0x0, /* src */
			    sizeof(hdr_req),
			    error))
		return NULL;
	if (!fu_intel_me_mkhi_verify_header(&hdr_req, &hdr_res, error))
		return NULL;
	return fu_intel_me_heci_device_read_file_response(buf_res, datasz_req, error);
}

GByteArray *
fu_intel_me_heci_device_read_file_ex(FuIntelMeHeciDevice *self,
				     guint32 file_id,
				     guint32 section,
				     guint32 datasz_req,
				     GError **error)
{
	FuMkhiHeader hdr_res = {0};
	g_autoptr(GByteArray) buf_req = g_byte_array_new();
	g_autoptr(GByteArray) buf_res = g_byte_array_new();
	const FuMkhiHeader hdr_req = {.group_id = MKHI_GROUP_ID_MCA,
				      .command = MCA_READ_FILE_EX_CMD};

	/* request */
	g_byte_array_append(buf_req, (const guint8 *)&hdr_req, sizeof(hdr_req));
	fu_byte_array_append_uint32(buf_req, file_id, G_LITTLE_ENDIAN);	   /* FileId */
	fu_byte_array_append_uint32(buf_req, 0x0, G_LITTLE_ENDIAN);	   /* Offset */
	fu_byte_array_append_uint32(buf_req, datasz_req, G_LITTLE_ENDIAN); /* DataSize */
	fu_byte_array_append_uint8(buf_req, section);			   /* Flags */
	if (!fu_mei_device_write(FU_MEI_DEVICE(self),
				 buf_req->data,
				 buf_req->len,
				 FU_INTEL_ME_HECI_DEVICE_TIMEOUT,
				 error))
		return NULL;

	/* response */
	fu_byte_array_set_size(buf_res, sizeof(hdr_res) + sizeof(guint32) + datasz_req, 0x0);
	if (!fu_mei_device_read(FU_MEI_DEVICE(self),
				buf_res->data,
				buf_res->len,
				NULL,
				FU_INTEL_ME_HECI_DEVICE_TIMEOUT,
				error))
		return NULL;

	/* verify header */
	if (!fu_memcpy_safe((guint8 *)&hdr_res,
			    sizeof(hdr_res),
			    0x0, /* dst */
			    buf_res->data,
			    buf_res->len,
			    0x0, /* src */
			    sizeof(hdr_req),
			    error))
		return NULL;
	if (!fu_intel_me_mkhi_verify_header(&hdr_req, &hdr_res, error))
		return NULL;
	return fu_intel_me_heci_device_read_file_response(buf_res, datasz_req, error);
}

static void
fu_intel_me_heci_device_version_notify_cb(FuDevice *device, GParamSpec *pspec, gpointer user_data)
{
	if (fu_device_has_private_flag(device, FU_INTEL_ME_HECI_DEVICE_FLAG_LEAKED_KM))
		fu_device_inhibit(device, "leaked-km", "Provisioned with a leaked private key");
}

static void
fu_intel_me_heci_device_init(FuIntelMeHeciDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_icon(FU_DEVICE(self), "computer");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_INTEL_ME_HECI_DEVICE_FLAG_LEAKED_KM,
					"leaked-km");
	g_signal_connect(FWUPD_DEVICE(self),
			 "notify::private-flags",
			 G_CALLBACK(fu_intel_me_heci_device_version_notify_cb),
			 NULL);
}

static void
fu_intel_me_heci_device_class_init(FuIntelMeHeciDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->open = fu_intel_me_heci_device_open;
}
