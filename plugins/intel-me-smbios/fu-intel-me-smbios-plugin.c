/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-intel-me-smbios-plugin.h"
#include "fu-intel-me-smbios-struct.h"

struct _FuIntelMeSmbiosPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuIntelMeSmbiosPlugin, fu_intel_me_smbios_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_intel_me_smbios_plugin_parse_fvi(FuPlugin *plugin,
				    FuIntelMeDevice *device,
				    FuStructMeFviData *st_fvi,
				    gboolean *found,
				    GError **error)
{
	g_autofree gchar *version = NULL;
	g_autoptr(FuStructMeVersion) st_ver = fu_struct_me_fvi_data_get_version(st_fvi);

	if (fu_struct_me_fvi_data_get_component_name(st_fvi) != FU_SMBIOS_FWSTS_COMPONENT_NAME_MEI3)
		return TRUE;
	version = g_strdup_printf("%u.%u.%u.%u",
				  fu_struct_me_version_get_major(st_ver),
				  fu_struct_me_version_get_minor(st_ver),
				  fu_struct_me_version_get_patch(st_ver),
				  fu_struct_me_version_get_build(st_ver));
	fu_device_set_version(FU_DEVICE(device), version);

	/* success */
	*found = TRUE;
	return TRUE;
}

static gboolean
fu_intel_me_smbios_plugin_parse_dd_table(FuPlugin *plugin,
					 FuIntelMeDevice *device,
					 GBytes *blob,
					 gboolean *found,
					 GError **error)
{
	gsize offset = 0;
	g_autoptr(FuStructMeFviHeader) st_fvi_hdr = NULL;
	g_autoptr(FuStructMeVersion) st_ver = NULL;
	g_autoptr(FuStructSmbiosStructure) st = NULL;

	st = fu_struct_smbios_structure_parse_bytes(blob, 0x0, error);
	if (st == NULL)
		return FALSE;

	/* fixme It doesn't always have to be type 219, it is BIOS build configuration-dependent.
	 * The software should check Type14 if it contains the $MEI signature and then check the
	 * handle it points to. In the example case it points to handle 0x31, so software should
	 * traverse whole SMBIOS tables in search for handle 0x31. */
	if (fu_struct_smbios_structure_get_handle(st) != FU_SMBIOS_DD_HANDLE_ME &&
	    fu_struct_smbios_structure_get_handle(st) != FU_SMBIOS_DD_HANDLE_ME2)
		return TRUE;

	/* parse as "Firmware Version Info" */
	offset += FU_STRUCT_SMBIOS_STRUCTURE_SIZE; /* unknown why */
	st_fvi_hdr = fu_struct_me_fvi_header_parse_bytes(blob, offset, error);
	if (st_fvi_hdr == NULL)
		return FALSE;
	offset += FU_STRUCT_ME_FVI_HEADER_SIZE;

	for (guint i = 0; i < fu_struct_me_fvi_header_get_count(st_fvi_hdr); i++) {
		g_autoptr(FuStructMeFviData) st_fvi = NULL;
		st_fvi = fu_struct_me_fvi_data_parse_bytes(blob, offset, error);
		if (st_fvi == NULL)
			return FALSE;
		if (!fu_intel_me_smbios_plugin_parse_fvi(plugin, device, st_fvi, found, error))
			return FALSE;
		offset += FU_STRUCT_ME_FVI_DATA_SIZE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_intel_me_smbios_plugin_parse_dd_tables(FuPlugin *plugin, FuIntelMeDevice *device, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autoptr(GPtrArray) oem_tables = NULL;

	oem_tables = fu_context_get_smbios_data(ctx, 0xDD, FU_SMBIOS_STRUCTURE_LENGTH_ANY, error);
	if (oem_tables == NULL)
		return FALSE;
	for (guint i = 0; i < oem_tables->len; i++) {
		GBytes *blob = g_ptr_array_index(oem_tables, i);
		gboolean found = FALSE;
		if (!fu_intel_me_smbios_plugin_parse_dd_table(plugin, device, blob, &found, error))
			return FALSE;
		if (found)
			return TRUE;
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no 0xDD ME SMBIOS table data");
	return FALSE;
}

static gboolean
fu_intel_me_smbios_plugin_parse_fwsts_record(FuPlugin *plugin,
					     FuIntelMeDevice *device,
					     GBytes *blob,
					     gsize offset,
					     gboolean *found,
					     GError **error)
{
	g_autoptr(FuStructSmbiosFwstsRecord) st = NULL;

	st = fu_struct_smbios_fwsts_record_parse_bytes(blob, offset, error);
	if (st == NULL)
		return FALSE;
	if (fu_struct_smbios_fwsts_record_get_component_name(st) !=
	    FU_SMBIOS_FWSTS_COMPONENT_NAME_MEI1)
		return TRUE;
	for (guint i = 0; i < FU_STRUCT_SMBIOS_FWSTS_RECORD_N_ELEMENTS_FWSTS; i++) {
		g_autoptr(FuStructIntelMeHfsts) st_hfsts =
		    fu_struct_smbios_fwsts_record_get_fwsts(st, i);
		fu_intel_me_device_set_hfsts(device, i + 1, st_hfsts);
	}
	*found = TRUE;
	return TRUE;
}

static gboolean
fu_intel_me_smbios_plugin_parse_db_table(FuPlugin *plugin,
					 FuIntelMeDevice *device,
					 GBytes *blob,
					 gboolean *found,
					 GError **error)
{
	gsize offset = 0;
	g_autoptr(FuStructSmbiosFwsts) st = NULL;

	st = fu_struct_smbios_fwsts_parse_bytes(blob, offset, error);
	if (st == NULL)
		return FALSE;
	offset += FU_STRUCT_SMBIOS_FWSTS_SIZE;
	for (guint i = 0; i < fu_struct_smbios_fwsts_get_count(st); i++) {
		if (!fu_intel_me_smbios_plugin_parse_fwsts_record(plugin,
								  device,
								  blob,
								  offset,
								  found,
								  error))
			return FALSE;
		if (*found)
			break;
		offset += FU_STRUCT_SMBIOS_FWSTS_RECORD_SIZE;
	}

	/* success, but not found */
	return TRUE;
}

static gboolean
fu_intel_me_smbios_plugin_parse_db_tables(FuPlugin *plugin, FuIntelMeDevice *device, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autoptr(GPtrArray) oem_tables = NULL;

	/* get the HFSTS registers */
	oem_tables = fu_context_get_smbios_data(ctx, 0xDB, FU_SMBIOS_STRUCTURE_LENGTH_ANY, error);
	if (oem_tables == NULL)
		return FALSE;
	for (guint i = 0; i < oem_tables->len; i++) {
		GBytes *blob = g_ptr_array_index(oem_tables, i);
		gboolean found = FALSE;
		if (!fu_intel_me_smbios_plugin_parse_db_table(plugin, device, blob, &found, error))
			return FALSE;
		if (found)
			return TRUE;
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no 0xDB ME1 SMBIOS table data");
	return FALSE;
}

static gboolean
fu_intel_me_smbios_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autoptr(FuIntelMeDevice) me_device = fu_intel_me_device_new(ctx);

	/* get the version */
	if (!fu_intel_me_smbios_plugin_parse_dd_tables(plugin, me_device, error))
		return FALSE;

	/* get the HFSTS registers */
	if (!fu_intel_me_smbios_plugin_parse_db_tables(plugin, me_device, error))
		return FALSE;

	/* success */
	fu_plugin_device_add(plugin, FU_DEVICE(me_device));
	return TRUE;
}

static void
fu_intel_me_smbios_plugin_init(FuIntelMeSmbiosPlugin *self)
{
}

static void
fu_intel_me_smbios_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_INTEL_ME_DEVICE);
}

static void
fu_intel_me_smbios_plugin_class_init(FuIntelMeSmbiosPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_intel_me_smbios_plugin_constructed;
	plugin_class->coldplug = fu_intel_me_smbios_plugin_coldplug;
}
