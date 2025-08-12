/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 * Copyright 2016 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

#include "fu-dell-plugin.h"

#define DACI_FLASH_INTERFACE_CLASS  7
#define DACI_FLASH_INTERFACE_SELECT 3
#define BIOS_SETTING_BIOS_DOWNGRADE "com.dell-wmi-sysman.AllowBiosDowngrade"

struct _FuDellPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuDellPlugin, fu_dell_plugin, FU_TYPE_PLUGIN)

struct da_structure {
	guint8 type;
	guint8 length;
	guint16 handle;
	guint16 cmd_address;
	guint8 cmd_code;
	guint32 supported_cmds;
	guint8 *tokens;
} __attribute__((packed)); /* nocheck:blocked */

/**
 * Dell device types to run
 */
const guint8 enclosure_allowlist[] = {0x03, /* desktop */
				      0x04, /* low profile desktop */
				      0x06, /* mini tower */
				      0x07, /* tower */
				      0x08, /* portable */
				      0x09, /* laptop */
				      0x0A, /* notebook */
				      0x0D, /* AIO */
				      0x1E, /* tablet */
				      0x1F, /* convertible */
				      0x21, /* IoT gateway */
				      0x22,
				      /* embedded PC */};

static guint16
fu_dell_plugin_get_system_id(FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	const gchar *system_id_str = NULL;
	guint64 system_id_val = 0;

	system_id_str = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_PRODUCT_SKU);
	if (system_id_str != NULL) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_strtoull(system_id_str,
				 &system_id_val,
				 0,
				 G_MAXUINT16,
				 FU_INTEGER_BASE_16,
				 &error_local)) {
			g_warning("failed to parse system ID: %s", error_local->message);
		}
	}

	return (guint16)system_id_val;
}

static gboolean
fu_dell_plugin_supported(FuPlugin *plugin, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	FuSmbiosChassisKind chassis_kind = fu_context_get_chassis_kind(ctx);
	GBytes *de_blob = NULL;
	GBytes *da_blob = NULL;
	g_autoptr(GPtrArray) de_tables = NULL;
	g_autoptr(GPtrArray) da_tables = NULL;
	guint8 value = 0;
	struct da_structure da_values = {0x0};

	/* make sure that Dell SMBIOS methods are available */
	de_tables = fu_context_get_smbios_data(ctx, 0xDE, FU_SMBIOS_STRUCTURE_LENGTH_ANY, error);
	if (de_tables == NULL)
		return FALSE;
	de_blob = g_ptr_array_index(de_tables, 0);
	if (!fu_memread_uint8_safe(g_bytes_get_data(de_blob, NULL),
				   g_bytes_get_size(de_blob),
				   0x0,
				   &value,
				   error)) {
		g_prefix_error_literal(error, "invalid DE data: ");
		return FALSE;
	}
	if (value != 0xDE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid DE data");
		return FALSE;
	}

	da_tables = fu_context_get_smbios_data(ctx, 0xDA, FU_SMBIOS_STRUCTURE_LENGTH_ANY, error);
	if (da_tables == NULL)
		return FALSE;
	da_blob = g_ptr_array_index(da_tables, 0);
	if (!fu_memcpy_safe((guint8 *)&da_values,
			    sizeof(da_values),
			    0x0, /* dst */
			    g_bytes_get_data(da_blob, NULL),
			    g_bytes_get_size(da_blob),
			    0x0, /* src */
			    sizeof(da_values),
			    error)) {
		g_prefix_error_literal(error, "unable to access flash interface: ");
		return FALSE;
	}
	if (!(da_values.supported_cmds & (1 << DACI_FLASH_INTERFACE_CLASS))) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "unable to access flash interface. supported commands: 0x%x",
			    da_values.supported_cmds);
		return FALSE;
	}

	/* only run on intended Dell hw types */
	for (guint i = 0; i < G_N_ELEMENTS(enclosure_allowlist); i++) {
		if (enclosure_allowlist[i] == chassis_kind)
			return TRUE;
	}

	/* failed */
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "chassis invalid");
	return FALSE;
}

static void
fu_dell_plugin_device_registered(FuPlugin *plugin, FuDevice *device)
{
	/* thunderbolt plugin */
	if (g_strcmp0(fu_device_get_plugin(device), "thunderbolt") == 0 &&
	    fu_device_has_flag(device, FWUPD_DEVICE_FLAG_INTERNAL)) {
		/* fix VID/DID of safe mode devices */
		if (fu_device_get_metadata_boolean(device, FU_DEVICE_METADATA_TBT_IS_SAFE_MODE)) {
			g_autofree gchar *device_id = NULL;
			guint16 system_id = 0;

			system_id = fu_dell_plugin_get_system_id(plugin);
			if (system_id == 0)
				return;
			/* the kernel returns lowercase in sysfs, need to match it */
			device_id = g_strdup_printf("TBT-%04x%04x", 0x00d4u, (unsigned)system_id);
			fu_device_build_vendor_id_u16(device, "TBT", 0x00D4);
			fu_device_add_instance_id(device, device_id);
			fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
		}
	}
	/* tpm plugin */
	if (g_strcmp0(fu_device_get_plugin(device), "tpm") == 0) {
		guint16 system_id = fu_dell_plugin_get_system_id(plugin);
		g_autofree gchar *tpm_guid_raw = NULL;

		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
		fu_device_set_metadata(device,
				       FU_DEVICE_METADATA_UEFI_DEVICE_KIND,
				       "dell-tpm-firmware");
		tpm_guid_raw = g_strdup_printf("%04x-2.0", system_id);
		fu_device_add_instance_id(device, tpm_guid_raw);
		fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_QUAD);
	}
}

static gboolean
fu_dell_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	g_autofree gchar *sysfsfwdir = NULL;
	g_autofree gchar *esrtdir = NULL;

	if (!fu_dell_plugin_supported(plugin, error)) {
		g_prefix_error_literal(error, "firmware updating not supported: ");
		return FALSE;
	}

	/* If ESRT is not turned on, fwupd will have already created an
	 * unlock device.
	 *
	 * Once unlocked, that will enable flashing capsules here too.
	 */
	sysfsfwdir = fu_path_from_kind(FU_PATH_KIND_SYSFSDIR_FW);
	esrtdir = g_build_filename(sysfsfwdir, "efi", "esrt", NULL);
	if (!g_file_test(esrtdir, G_FILE_TEST_EXISTS)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "capsule support disabled in BIOS");
		return FALSE;
	}

	return TRUE;
}

static void
fu_dell_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FwupdBiosSetting *bios_attr;
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	bios_attr = fu_context_get_bios_setting(ctx, BIOS_SETTING_BIOS_DOWNGRADE);
	if (bios_attr == NULL) {
		g_debug("failed to find %s in cache", BIOS_SETTING_BIOS_DOWNGRADE);
		return;
	}

	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_BIOS_ROLLBACK_PROTECTION);
	fu_security_attr_add_bios_target_value(attr, BIOS_SETTING_BIOS_DOWNGRADE, "Disabled");
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fu_security_attrs_append(attrs, attr);

	if (g_strcmp0(fwupd_bios_setting_get_current_value(bios_attr), "Enabled") == 0) {
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_dell_plugin_init(FuDellPlugin *self)
{
}

static void
fu_dell_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);

	/* make sure that UEFI plugin is ready to receive devices */
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_RUN_AFTER, "uefi_capsule");
}

static void
fu_dell_plugin_class_init(FuDellPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_dell_plugin_constructed;
	plugin_class->startup = fu_dell_plugin_startup;
	plugin_class->device_registered = fu_dell_plugin_device_registered;
	plugin_class->add_security_attrs = fu_dell_plugin_add_security_attrs;
}
