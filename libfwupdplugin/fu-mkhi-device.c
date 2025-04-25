/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuMkhiDevice"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-mkhi-device.h"

/**
 * FuMkhiDevice
 *
 * A MKHI device, typically accessed over HECI.
 *
 * See also: #FuMeiDevice
 */

G_DEFINE_TYPE(FuMkhiDevice, fu_mkhi_device, FU_TYPE_MEI_DEVICE)

#define FU_MKHI_DEVICE_TIMEOUT 200 /* ms */

static gboolean
fu_mkhi_device_result_to_error(FuMkhiStatus result, GError **error)
{
	if (result == FU_MKHI_STATUS_SUCCESS)
		return TRUE;

	switch (result) {
	case FU_MKHI_STATUS_NOT_SUPPORTED:
	case FU_MKHI_STATUS_NOT_AVAILABLE:
	case FU_MKHI_STATUS_NOT_SET:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "not supported [0x%x]",
			    result);
		break;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "generic failure [0x%x]",
			    result);
		break;
	}
	return FALSE;
}

static gboolean
fu_mkhi_device_open(FuDevice *device, GError **error)
{
	/* open then create context */
	if (!FU_DEVICE_CLASS(fu_mkhi_device_parent_class)->open(device, error))
		return FALSE;
	return fu_mei_device_connect(FU_MEI_DEVICE(device), 0, error);
}

/**
 * fu_mkhi_device_read_file:
 * @self: a #FuMkhiDevice
 * @filename: (not nullable): MFS filename
 * @error: (nullable): optional return location for an error
 *
 * Reads a file from the MFS.
 *
 * Returns: (transfer container): file data
 *
 * Since: 2.0.9
 **/
GByteArray *
fu_mkhi_device_read_file(FuMkhiDevice *self, const gchar *filename, GError **error)
{
	guint32 data_size;
	guint datasz_req = 0x80;
	g_autoptr(GByteArray) bufout = g_byte_array_new();
	g_autoptr(GByteArray) buf_res = g_byte_array_new();
	g_autoptr(FuMkhiReadFileRequest) st_req = fu_mkhi_read_file_request_new();
	g_autoptr(FuMkhiReadFileResponse) st_res = NULL;

	g_return_val_if_fail(FU_IS_MKHI_DEVICE(self), NULL);
	g_return_val_if_fail(filename != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* request */
	if (!fu_mkhi_read_file_request_set_filename(st_req, filename, error))
		return NULL;
	fu_mkhi_read_file_request_set_data_size(st_req, datasz_req);
	fu_mkhi_read_file_request_set_flags(st_req, (1 << 3)); /* ?? */
	if (!fu_mei_device_write(FU_MEI_DEVICE(self),
				 st_req->data,
				 st_req->len,
				 FU_MKHI_DEVICE_TIMEOUT,
				 error))
		return NULL;

	/* response */
	fu_byte_array_set_size(buf_res, FU_MKHI_READ_FILE_RESPONSE_SIZE + datasz_req, 0x0);
	if (!fu_mei_device_read(FU_MEI_DEVICE(self),
				buf_res->data,
				buf_res->len,
				NULL,
				FU_MKHI_DEVICE_TIMEOUT,
				error))
		return NULL;
	st_res = fu_mkhi_read_file_response_parse(buf_res->data, buf_res->len, 0x0, error);
	if (st_res == NULL)
		return NULL;
	if (!fu_mkhi_device_result_to_error(fu_mkhi_read_file_response_get_result(st_res), error))
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

/**
 * fu_mkhi_device_read_file_ex:
 * @self: a #FuMkhiDevice
 * @file_id: MFS file ID
 * @section: MFS section
 * @datasz_req: the maximum size of data to request
 * @error: (nullable): optional return location for an error
 *
 * Reads a file from the MFS.
 *
 * Returns: (transfer container): file data
 *
 * Since: 2.0.9
 **/
GByteArray *
fu_mkhi_device_read_file_ex(FuMkhiDevice *self,
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

	g_return_val_if_fail(FU_IS_MKHI_DEVICE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* request */
	fu_mkhi_read_file_ex_request_set_file_id(st_req, file_id);
	fu_mkhi_read_file_ex_request_set_data_size(st_req, datasz_req);
	fu_mkhi_read_file_ex_request_set_flags(st_req, section);
	if (!fu_mei_device_write(FU_MEI_DEVICE(self),
				 st_req->data,
				 st_req->len,
				 FU_MKHI_DEVICE_TIMEOUT,
				 error))
		return NULL;

	/* response */
	fu_byte_array_set_size(buf_res, FU_MKHI_READ_FILE_EX_REQUEST_SIZE + datasz_req, 0x0);
	if (!fu_mei_device_read(FU_MEI_DEVICE(self),
				buf_res->data,
				buf_res->len,
				NULL,
				FU_MKHI_DEVICE_TIMEOUT,
				error))
		return NULL;
	st_res = fu_mkhi_read_file_ex_response_parse(buf_res->data, buf_res->len, 0x0, error);
	if (st_res == NULL)
		return NULL;
	if (!fu_mkhi_device_result_to_error(fu_mkhi_read_file_ex_response_get_result(st_res),
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
fu_mkhi_device_init(FuMkhiDevice *self)
{
}

static void
fu_mkhi_device_class_init(FuMkhiDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->open = fu_mkhi_device_open;
}
