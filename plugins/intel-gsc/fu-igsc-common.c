/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-igsc-common.h"

static void
fu_igsc_fwdata_device_info_export_one(FuIgscFwdataDeviceInfo4 *st, XbBuilderNode *bn)
{
	fu_xmlb_builder_insert_kx(bn, "vendor_id", fu_igsc_fwdata_device_info4_get_vendor_id(st));
	fu_xmlb_builder_insert_kx(bn, "device_id", fu_igsc_fwdata_device_info4_get_device_id(st));
	fu_xmlb_builder_insert_kx(bn,
				  "subsys_vendor_id",
				  fu_igsc_fwdata_device_info4_get_subsys_vendor_id(st));
	fu_xmlb_builder_insert_kx(bn,
				  "subsys_device_id",
				  fu_igsc_fwdata_device_info4_get_subsys_device_id(st));
}

void
fu_igsc_fwdata_device_info_export(GPtrArray *device_infos, XbBuilderNode *bn)
{
	g_autoptr(XbBuilderNode) bc = NULL;

	if (device_infos->len == 0)
		return;
	bc = xb_builder_node_insert(bn, "device_infos", NULL);
	for (guint i = 0; i < device_infos->len; i++) {
		FuIgscFwdataDeviceInfo4 *st = g_ptr_array_index(device_infos, i);
		g_autoptr(XbBuilderNode) bm = xb_builder_node_insert(bc, "match", NULL);
		fu_igsc_fwdata_device_info_export_one(st, bm);
	}
}

static gboolean
fu_igsc_fwdata_device_info_parse_device_type(GPtrArray *device_infos,
					     GInputStream *stream,
					     GError **error)
{
	gsize streamsz = 0;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	for (gsize offset = 0; offset < streamsz; offset += FU_IGSC_FWDATA_DEVICE_INFO2_SIZE) {
		g_autoptr(FuIgscFwdataDeviceInfo2) st = NULL;
		g_autoptr(FuIgscFwdataDeviceInfo4) st4 = fu_igsc_fwdata_device_info4_new();
		st = fu_igsc_fwdata_device_info2_parse_stream(stream, offset, error);
		if (st == NULL)
			return FALSE;
		fu_igsc_fwdata_device_info4_set_vendor_id(st4, 0x0);
		fu_igsc_fwdata_device_info4_set_device_id(st4, 0x0);
		fu_igsc_fwdata_device_info4_set_subsys_vendor_id(
		    st4,
		    fu_igsc_fwdata_device_info2_get_subsys_vendor_id(st));
		fu_igsc_fwdata_device_info4_set_subsys_device_id(
		    st4,
		    fu_igsc_fwdata_device_info2_get_subsys_device_id(st));
		g_ptr_array_add(device_infos, g_steal_pointer(&st4));
	}

	/* success */
	return TRUE;
}

static gboolean
fu_igsc_fwdata_device_info_parse_device_id_array(GPtrArray *device_infos,
						 GInputStream *stream,
						 GError **error)
{
	gsize streamsz = 0;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	for (gsize offset = 0; offset < streamsz; offset += FU_IGSC_FWDATA_DEVICE_INFO4_SIZE) {
		g_autoptr(FuIgscFwdataDeviceInfo4) st = NULL;
		st = fu_igsc_fwdata_device_info4_parse_stream(stream, offset, error);
		if (st == NULL)
			return FALSE;
		g_ptr_array_add(device_infos, g_steal_pointer(&st));
	}

	/* success */
	return TRUE;
}

gboolean
fu_igsc_fwdata_device_info_parse(GPtrArray *device_infos, FuFirmware *fw, GError **error)
{
	FuIgscFwuExtType ext_type = fu_firmware_get_idx(fw);
	g_autoptr(GInputStream) stream = NULL;

	/* get data */
	stream = fu_firmware_get_stream(fw, error);
	if (stream == NULL)
		return FALSE;
	g_debug("found manifest extension: 0x%x [%s]",
		ext_type,
		fu_igsc_fwu_ext_type_to_string(ext_type));
	if (ext_type == FU_IGSC_FWU_EXT_TYPE_DEVICE_TYPE)
		return fu_igsc_fwdata_device_info_parse_device_type(device_infos, stream, error);
	if (ext_type == FU_IGSC_FWU_EXT_TYPE_DEVICE_ID_ARRAY)
		return fu_igsc_fwdata_device_info_parse_device_id_array(device_infos,
									stream,
									error);

	/* unknown is success */
	return TRUE;
}

gboolean
fu_igsc_heci_check_status(FuIgscFwuHeciStatus status, GError **error)
{
	if (status == FU_IGSC_FWU_HECI_STATUS_SUCCESS)
		return TRUE;
	if (status == FU_IGSC_FWU_HECI_STATUS_SIZE_ERROR) {
		g_set_error_literal(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "num of bytes to read/write/erase is bigger than partition size");
		return FALSE;
	}
	if (status == FU_IGSC_FWU_HECI_STATUS_UPDATE_OPROM_INVALID_STRUCTURE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "wrong oprom signature");
		return FALSE;
	}
	if (status == FU_IGSC_FWU_HECI_STATUS_UPDATE_OPROM_SECTION_NOT_EXIST) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "update oprom section does not exists on flash");
		return FALSE;
	}
	if (status == FU_IGSC_FWU_HECI_STATUS_INVALID_COMMAND) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "invalid HECI message sent");
		return FALSE;
	}
	if (status == FU_IGSC_FWU_HECI_STATUS_INVALID_PARAMS) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid command parameters");
		return FALSE;
	}
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "general firmware error");
	return FALSE;
}
