/*
 * Copyright 2024 B&R Industrial Automation GmbH
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupdplugin.h"

#include "fu-bnr-dp-common.h"
#include "fu-bnr-dp-struct.h"

gchar *
fu_bnr_dp_version_to_string(guint64 version)
{
	guint64 major = version / 100;
	guint64 minor = version % 100;

	return g_strdup_printf("%" G_GUINT64_FORMAT ".%02" G_GUINT64_FORMAT, major, minor);
}

/* read, convert and validate the version from `header` to an integer */
gboolean
fu_bnr_dp_version_from_header(const FuStructBnrDpPayloadHeader *st_header,
			      guint64 *version,
			      GError **error)
{
	g_autofree gchar *tmp = fu_struct_bnr_dp_payload_header_get_version(st_header);

	return fu_strtoull(tmp, version, 0, 9999, FU_INTEGER_BASE_10, error);
}

guint32
fu_bnr_dp_effective_product_num(const FuStructBnrDpFactoryData *st_factory_data)
{
	guint32 parent = fu_struct_bnr_dp_factory_data_get_parent_product_num(st_factory_data);

	return (parent != 0 && parent != G_MAXUINT32)
		   ? parent
		   : fu_struct_bnr_dp_factory_data_get_product_num(st_factory_data);
}

guint16
fu_bnr_dp_effective_compat_id(const FuStructBnrDpFactoryData *st_factory_data)
{
	guint16 parent = fu_struct_bnr_dp_factory_data_get_parent_compat_id(st_factory_data);

	return (parent != 0 && parent != G_MAXUINT16)
		   ? parent
		   : fu_struct_bnr_dp_factory_data_get_compat_id(st_factory_data);
}
