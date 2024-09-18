/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

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

GByteArray *
fu_intel_me_heci_device_read_file(FuIntelMeHeciDevice *self, const gchar *filename, GError **error)
{
	guint32 data_size;
	guint datasz_req = 0x80;
	g_autoptr(GByteArray) bufout = g_byte_array_new();
	g_autoptr(GByteArray) buf_res = g_byte_array_new();
	g_autoptr(FuMkhiReadFileRequest) st_req = fu_mkhi_read_file_request_new();
	g_autoptr(FuMkhiReadFileResponse) st_res = NULL;

	/* request */
	if (!fu_mkhi_read_file_request_set_filename(st_req, filename, error))
		return NULL;
	fu_mkhi_read_file_request_set_data_size(st_req, datasz_req);
	fu_mkhi_read_file_request_set_flags(st_req, (1 << 3)); /* ?? */
	if (!fu_mei_device_write(FU_MEI_DEVICE(self),
				 st_req->data,
				 st_req->len,
				 FU_INTEL_ME_HECI_DEVICE_TIMEOUT,
				 error))
		return NULL;

	/* response */
	fu_byte_array_set_size(buf_res, FU_MKHI_READ_FILE_RESPONSE_SIZE + datasz_req, 0x0);
	if (!fu_mei_device_read(FU_MEI_DEVICE(self),
				buf_res->data,
				buf_res->len,
				NULL,
				FU_INTEL_ME_HECI_DEVICE_TIMEOUT,
				error))
		return NULL;
	st_res = fu_mkhi_read_file_response_parse(buf_res->data, buf_res->len, 0x0, error);
	if (st_res == NULL)
		return NULL;
	if (!fu_intel_me_mkhi_result_to_error(fu_mkhi_read_file_response_get_result(st_res), error))
		return NULL;

	/* verify we got what we asked for */
	data_size = fu_mkhi_read_file_response_get_data_size(st_res);
	if (data_size > datasz_req) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid response data size, requested 0x%x and got 0x%x",
			    datasz_req,
			    data_size);
		return NULL;
	}

	/* success */
	g_byte_array_append(bufout, &buf_res->data[st_res->len], data_size);
	return g_steal_pointer(&bufout);
}

GByteArray *
fu_intel_me_heci_device_read_file_ex(FuIntelMeHeciDevice *self,
				     guint32 file_id,
				     guint32 section,
				     guint32 datasz_req,
				     GError **error)
{
	guint32 data_size;
	g_autoptr(FuMkhiReadFileExRequest) st_req = fu_mkhi_read_file_ex_request_new();
	g_autoptr(FuMkhiReadFileExResponse) st_res = NULL;
	g_autoptr(GByteArray) bufout = g_byte_array_new();
	g_autoptr(GByteArray) buf_res = g_byte_array_new();

	/* request */
	fu_mkhi_read_file_ex_request_set_file_id(st_req, file_id);
	fu_mkhi_read_file_ex_request_set_data_size(st_req, datasz_req);
	fu_mkhi_read_file_ex_request_set_flags(st_req, section);
	if (!fu_mei_device_write(FU_MEI_DEVICE(self),
				 st_req->data,
				 st_req->len,
				 FU_INTEL_ME_HECI_DEVICE_TIMEOUT,
				 error))
		return NULL;

	/* response */
	fu_byte_array_set_size(buf_res, FU_MKHI_READ_FILE_EX_REQUEST_SIZE + datasz_req, 0x0);
	if (!fu_mei_device_read(FU_MEI_DEVICE(self),
				buf_res->data,
				buf_res->len,
				NULL,
				FU_INTEL_ME_HECI_DEVICE_TIMEOUT,
				error))
		return NULL;
	st_res = fu_mkhi_read_file_ex_response_parse(buf_res->data, buf_res->len, 0x0, error);
	if (st_res == NULL)
		return NULL;
	if (!fu_intel_me_mkhi_result_to_error(fu_mkhi_read_file_ex_response_get_result(st_res),
					      error))
		return NULL;

	/* verify we got what we asked for */
	data_size = fu_mkhi_read_file_ex_response_get_data_size(st_res);
	if (data_size > datasz_req) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid response data size, requested 0x%x and got 0x%x",
			    datasz_req,
			    data_size);
		return NULL;
	}

	/* success */
	g_byte_array_append(bufout, &buf_res->data[st_res->len], data_size);
	return g_steal_pointer(&bufout);
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
	fu_device_register_private_flag(FU_DEVICE(self), FU_INTEL_ME_HECI_DEVICE_FLAG_LEAKED_KM);
	g_signal_connect(FWUPD_DEVICE(self),
			 "notify::private-flags",
			 G_CALLBACK(fu_intel_me_heci_device_version_notify_cb),
			 NULL);
}

static void
fu_intel_me_heci_device_class_init(FuIntelMeHeciDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->open = fu_intel_me_heci_device_open;
}
