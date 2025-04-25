/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuHeciDevice"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-heci-device.h"

/**
 * FuHeciDevice
 *
 * A HECI device.
 *
 * See also: #FuMeiDevice
 */

G_DEFINE_TYPE(FuHeciDevice, fu_heci_device, FU_TYPE_MEI_DEVICE)

#define FU_HECI_DEVICE_TIMEOUT 200 /* ms */

static gboolean
fu_heci_device_result_to_error(FuMkhiStatus result, GError **error)
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

/**
 * fu_heci_device_read_file:
 * @self: a #FuHeciDevice
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
fu_heci_device_read_file(FuHeciDevice *self, const gchar *filename, GError **error)
{
	guint32 data_size;
	guint datasz_req = 0x80;
	g_autoptr(GByteArray) bufout = g_byte_array_new();
	g_autoptr(GByteArray) buf_res = g_byte_array_new();
	g_autoptr(FuMkhiReadFileRequest) st_req = fu_mkhi_read_file_request_new();
	g_autoptr(FuMkhiReadFileResponse) st_res = NULL;

	g_return_val_if_fail(FU_IS_HECI_DEVICE(self), NULL);
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
				 FU_HECI_DEVICE_TIMEOUT,
				 error))
		return NULL;

	/* response */
	fu_byte_array_set_size(buf_res, FU_MKHI_READ_FILE_RESPONSE_SIZE + datasz_req, 0x0);
	if (!fu_mei_device_read(FU_MEI_DEVICE(self),
				buf_res->data,
				buf_res->len,
				NULL,
				FU_HECI_DEVICE_TIMEOUT,
				error))
		return NULL;
	st_res = fu_mkhi_read_file_response_parse(buf_res->data, buf_res->len, 0x0, error);
	if (st_res == NULL)
		return NULL;
	if (!fu_heci_device_result_to_error(fu_mkhi_read_file_response_get_result(st_res), error))
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
 * fu_heci_device_read_file_ex:
 * @self: a #FuHeciDevice
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
fu_heci_device_read_file_ex(FuHeciDevice *self,
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

	g_return_val_if_fail(FU_IS_HECI_DEVICE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* request */
	fu_mkhi_read_file_ex_request_set_file_id(st_req, file_id);
	fu_mkhi_read_file_ex_request_set_data_size(st_req, datasz_req);
	fu_mkhi_read_file_ex_request_set_flags(st_req, section);
	if (!fu_mei_device_write(FU_MEI_DEVICE(self),
				 st_req->data,
				 st_req->len,
				 FU_HECI_DEVICE_TIMEOUT,
				 error))
		return NULL;

	/* response */
	fu_byte_array_set_size(buf_res, FU_MKHI_READ_FILE_EX_REQUEST_SIZE + datasz_req, 0x0);
	if (!fu_mei_device_read(FU_MEI_DEVICE(self),
				buf_res->data,
				buf_res->len,
				NULL,
				FU_HECI_DEVICE_TIMEOUT,
				error))
		return NULL;
	st_res = fu_mkhi_read_file_ex_response_parse(buf_res->data, buf_res->len, 0x0, error);
	if (st_res == NULL)
		return NULL;
	if (!fu_heci_device_result_to_error(fu_mkhi_read_file_ex_response_get_result(st_res),
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

/**
 * fu_heci_device_arbh_svn_get_info:
 * @self: a #FuHeciDevice
 * @usage_id: usage ID, e.g. %FU_MKHI_ARBH_SVN_INFO_ENTRY_USAGE_ID_CSE_RBE
 * @executing: (out) (nullable): currently executing SVN
 * @min_allowed: (out) (nullable): minimal allowed SVN
 * @error: (nullable): optional return location for an error
 *
 * Reads the ARBH SVN for a specific usage ID.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.9
 **/
gboolean
fu_heci_device_arbh_svn_get_info(FuHeciDevice *self,
				 guint8 usage_id,
				 guint8 *executing,
				 guint8 *min_allowed,
				 GError **error)
{
	guint32 num_entries;
	gsize offset = 0;
	gboolean found_usage = FALSE;
	g_autoptr(FuMkhiArbhSvnGetInfoRequest) st_req = fu_mkhi_arbh_svn_get_info_request_new();
	g_autoptr(FuMkhiArbhSvnGetInfoResponse) st_res = NULL;
	g_autoptr(GByteArray) bufout = g_byte_array_new();
	g_autoptr(GByteArray) buf_res = g_byte_array_new();

	g_return_val_if_fail(FU_IS_HECI_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* request */
	if (!fu_mei_device_write(FU_MEI_DEVICE(self),
				 st_req->data,
				 st_req->len,
				 FU_HECI_DEVICE_TIMEOUT,
				 error))
		return FALSE;

	/* response */
	fu_byte_array_set_size(buf_res, fu_mei_device_get_max_msg_length(FU_MEI_DEVICE(self)), 0x0);
	if (!fu_mei_device_read(FU_MEI_DEVICE(self),
				buf_res->data,
				buf_res->len,
				NULL,
				FU_HECI_DEVICE_TIMEOUT,
				error))
		return FALSE;
	st_res = fu_mkhi_arbh_svn_get_info_response_parse(buf_res->data, buf_res->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;
	if (!fu_heci_device_result_to_error(fu_mkhi_arbh_svn_get_info_response_get_result(st_res),
					    error))
		return FALSE;

	/* verify we got what we asked for */
	num_entries = fu_mkhi_arbh_svn_get_info_response_get_num_entries(st_res);
	offset += st_res->len;
	for (guint32 i = 0; i < num_entries; i++) {
		g_autoptr(FuMkhiArbhSvnInfoEntry) st_entry = NULL;

		/* parse each entry */
		st_entry =
		    fu_mkhi_arbh_svn_info_entry_parse(buf_res->data, buf_res->len, offset, error);
		if (st_entry == NULL)
			return FALSE;

		/* matches */
		if (fu_mkhi_arbh_svn_info_entry_get_usage_id(st_entry) == usage_id) {
			found_usage = TRUE;
			if (executing != NULL)
				*executing = fu_mkhi_arbh_svn_info_entry_get_executing(st_entry);
			if (min_allowed != NULL)
				*min_allowed =
				    fu_mkhi_arbh_svn_info_entry_get_min_allowed(st_entry);
			break;
		}

		/* next */
		offset += st_entry->len;
	}

	/* did not find */
	if (!found_usage) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "no entry for usage ID 0x%x",
			    usage_id);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_heci_device_init(FuHeciDevice *self)
{
}

static void
fu_heci_device_class_init(FuHeciDeviceClass *klass)
{
}
