/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <cpuid.h>

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

struct FuPluginData {
	guint16		 total_width;
	guint16		 data_width;
};

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	const guint8 *buf;
	gsize bufsz = 0;
	g_autoptr(GBytes) blob = NULL;

	/* get blob */
	blob = fu_plugin_get_smbios_data (plugin, FU_SMBIOS_STRUCTURE_TYPE_MEMORY_DEVICE);
	if (blob == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "no SMBIOS MemoryDevice data");
		return FALSE;
	}

	/* parse */
	buf = g_bytes_get_data (blob, &bufsz);
	if (!fu_common_read_uint16_safe (buf, bufsz, 0x08,
					 &priv->total_width,
					 G_LITTLE_ENDIAN,
					 error))
		return FALSE;
	if (!fu_common_read_uint16_safe (buf, bufsz, 0x0a,
					 &priv->data_width,
					 G_LITTLE_ENDIAN,
					 error))
		return FALSE;
	if (g_getenv ("FWUPD_SMBIOS_MEMORY_VERBOSE") != NULL) {
		g_debug ("total_width: %u, data_width: %u",
			 priv->total_width, priv->data_width);
	}

	/* success */
	return TRUE;
}

void
fu_plugin_add_security_attrs (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_ECC_RAM);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_THEORETICAL);
	fu_security_attrs_append (attrs, attr);

	/* when ECC is enabled total_width > data_width */
	if (priv->total_width != 0xfff && priv->data_width != 0xfff &&
	    priv->total_width <= priv->data_width) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
}
