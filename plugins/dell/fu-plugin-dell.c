/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario_limonciello@dell.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <fwup.h>
#include <appstream-glib.h>
#include <glib/gstdio.h>
#include <smbios_c/system_info.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "fu-plugin-dell.h"
#include "fu-plugin-vfuncs.h"
#include "fu-device-metadata.h"

/* These are used to indicate the status of a previous DELL flash */
#define DELL_SUCCESS			0x0000
#define DELL_CONSISTENCY_FAIL		0x0001
#define DELL_FLASH_MEMORY_FAIL		0x0002
#define DELL_FLASH_NOT_READY		0x0003
#define DELL_FLASH_DISABLED		0x0004
#define DELL_BATTERY_MISSING		0x0005
#define DELL_BATTERY_DEAD		0x0006
#define DELL_AC_MISSING			0x0007
#define DELL_CANT_SET_12V		0x0008
#define DELL_CANT_UNSET_12V		0x0009
#define DELL_FAILURE_BLOCK_ERASE	0x000A
#define DELL_GENERAL_FAILURE		0x000B
#define DELL_DATA_MISCOMPARE		0x000C
#define DELL_IMAGE_MISSING		0x000D
#define DELL_DID_NOTHING		0xFFFF

/* Delay for settling */
#define DELL_FLASH_MODE_DELAY		2

typedef struct _DOCK_DESCRIPTION
{
	const efi_guid_t	guid;
	const gchar *		query;
	const gchar *		desc;
} DOCK_DESCRIPTION;

/* These are for matching the components */
#define WD15_EC_STR		"2 0 2 2 0"
#define TB16_EC_STR		"2 0 2 1 0"
#define TB16_PC2_STR		"2 1 0 1 1"
#define TB16_PC1_STR		"2 1 0 1 0"
#define WD15_PC1_STR		"2 1 0 2 0"
#define LEGACY_CBL_STR		"2 2 2 1 0"
#define UNIV_CBL_STR		"2 2 2 2 0"
#define TBT_CBL_STR		"2 2 2 3 0"

/* supported dock related GUIDs */
#define DOCK_FLASH_GUID		EFI_GUID (0xE7CA1F36, 0xBF73, 0x4574, 0xAFE6, 0xA4, 0xCC, 0xAC, 0xAB, 0xF4, 0x79)
#define WD15_EC_GUID		EFI_GUID (0xE8445370, 0x0211, 0x449D, 0x9FAA, 0x10, 0x79, 0x06, 0xAB, 0x18, 0x9F)
#define TB16_EC_GUID		EFI_GUID (0x33CC8870, 0xB1FC, 0x4EC7, 0x948A, 0xC0, 0x74, 0x96, 0x87, 0x4F, 0xAF)
#define TB16_PC2_GUID		EFI_GUID (0x1B52C630, 0x86F6, 0x4AEE, 0x9F0C, 0x47, 0x4D, 0xC6, 0xBE, 0x49, 0xB6)
#define TB16_PC1_GUID		EFI_GUID (0x8FE183DA, 0xC94E, 0x4804, 0xB319, 0x0F, 0x1B, 0xA5, 0x45, 0x7A, 0x69)
#define WD15_PC1_GUID		EFI_GUID (0x8BA2B709, 0x6F97, 0x47FC, 0xB7E7, 0x6A, 0x87, 0xB5, 0x78, 0xFE, 0x25)
#define LEGACY_CBL_GUID		EFI_GUID (0xFECE1537, 0xD683, 0x4EA8, 0xB968, 0x15, 0x45, 0x30, 0xBB, 0x6F, 0x73)
#define UNIV_CBL_GUID		EFI_GUID (0xE2BF3AAD, 0x61A3, 0x44BF, 0x91EF, 0x34, 0x9B, 0x39, 0x51, 0x5D, 0x29)
#define TBT_CBL_GUID		EFI_GUID (0x6DC832FC, 0x5BB0, 0x4E63, 0xA2FF, 0x02, 0xAA, 0xBA, 0x5B, 0xC1, 0xDC)

#define EC_DESC			"EC"
#define PC1_DESC		"Port Controller 1"
#define PC2_DESC		"Port Controller 2"
#define LEGACY_CBL_DESC		"Passive Cable"
#define UNIV_CBL_DESC		"Universal Cable"
#define TBT_CBL_DESC		"Thunderbolt Cable"

/* supported host related GUIDs */
#define MST_GPIO_GUID		EFI_GUID (0xF24F9bE4, 0x2a13, 0x4344, 0xBC05, 0x01, 0xCE, 0xF7, 0xDA, 0xEF, 0x92)

/**
 * Devices that should allow modeswitching
 */
static guint16 tpm_switch_whitelist[] = {0x06F2, 0x06F3, 0x06DD, 0x06DE, 0x06DF,
					 0x06DB, 0x06DC, 0x06BB, 0x06C6, 0x06BA,
					 0x06B9, 0x05CA, 0x06C7, 0x06B7, 0x06E0,
					 0x06E5, 0x06D9, 0x06DA, 0x06E4, 0x0704,
					 0x0720, 0x0730, 0x0758, 0x0759, 0x075B,
					 0x07A0, 0x079F, 0x07A4, 0x07A5, 0x07A6,
					 0x07A7, 0x07A8, 0x07A9, 0x07AA, 0x07AB,
					 0x07B0, 0x07B1, 0x07B2, 0x07B4, 0x07B7,
					 0x07B8, 0x07B9, 0x07BE, 0x07BF, 0x077A,
					 0x07CF};
/**
  * Dell device types to run
  */
static guint8 enclosure_whitelist [] = { 0x03, /* desktop */
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
					 0x22, /* embedded PC */};

/**
  * System blacklist
  */
static guint16 system_blacklist [] =	{ 0x071E, /* latitude 5414 */
					  0x077A, /* xps 9365 */ };

/**
  * Systems containing host MST device
  */
static guint16 systems_host_mst [] =	{ 0x062d, /* Latitude E7250 */
					  0x062e, /* Latitude E7450 */
					  0x062a, /* Latitude E5250 */
					  0x062b, /* Latitude E5450 */
					  0x062c, /* Latitude E5550 */
					  0x06db, /* Latitude E7270 */
					  0x06dc, /* Latitude E7470 */
					  0x06dd, /* Latitude E5270 */
					  0x06de, /* Latitude E5470 */
					  0x06df, /* Latitude E5570 */
					  0x06e0, /* Precision 3510 */
					  0x071d, /* Latitude Rugged 7214 */
					  0x071e, /* Latitude Rugged 5414 */
					  0x071c, /* Latitude Rugged 7414 */};

static void
_fwup_resource_iter_free (fwup_resource_iter *iter)
{
	fwup_resource_iter_destroy (&iter);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (fwup_resource_iter, _fwup_resource_iter_free);

static guint16
fu_dell_get_system_id (FuPlugin *plugin)
{
	const gchar *system_id_str = NULL;
	guint16 system_id = 0;
	gchar *endptr = NULL;

	system_id_str = fu_plugin_get_dmi_value (plugin,
		FU_HWIDS_KEY_PRODUCT_SKU);
	if (system_id_str != NULL)
		system_id = g_ascii_strtoull (system_id_str, &endptr, 16);
	if (system_id == 0 || endptr == system_id_str)
		system_id = (guint16) sysinfo_get_dell_system_id ();

	return system_id;
}

static gboolean
fu_dell_host_mst_supported (FuPlugin *plugin)
{
	guint16 system_id;

	system_id = fu_dell_get_system_id (plugin);
	if (system_id == 0)
		return FALSE;
	for (guint i = 0; i < G_N_ELEMENTS (systems_host_mst); i++)
		if (systems_host_mst[i] == system_id)
			return TRUE;
	return FALSE;
}

static gboolean
fu_dell_supported (FuPlugin *plugin)
{
	GBytes *de_table = NULL;
	GBytes *enclosure = NULL;
	guint16 system_id = 0;
	const guint8 *value;
	gsize len;

	/* make sure that Dell SMBIOS methods are available */
	de_table = fu_plugin_get_smbios_data (plugin, 0xDE);
	if (de_table == NULL)
		return FALSE;
	value = g_bytes_get_data (de_table, &len);
	if (len == 0)
		return FALSE;
	if (*value != 0xDE)
		return FALSE;

	/* skip blacklisted hw */
	system_id = fu_dell_get_system_id (plugin);
	if (system_id == 0)
		return FALSE;
	for (guint i = 0; i < G_N_ELEMENTS (system_blacklist); i++) {
		if (system_blacklist[i] == system_id)
			return FALSE;
	}

	/* only run on intended Dell hw types */
	enclosure = fu_plugin_get_smbios_data (plugin,
					       FU_SMBIOS_STRUCTURE_TYPE_CHASSIS);
	if (enclosure == NULL)
		return FALSE;
	value = g_bytes_get_data (enclosure, &len);
	if (len == 0)
		return FALSE;
	for (guint i = 0; i < G_N_ELEMENTS (enclosure_whitelist); i++) {
		if (enclosure_whitelist[i] == value[0])
			return TRUE;
	}

	return FALSE;
}

static gboolean
fu_plugin_dell_match_dock_component (const gchar *query_str,
				     efi_guid_t *guid_out,
				     const gchar **name_out)
{
	const DOCK_DESCRIPTION list[] = {
		{WD15_EC_GUID, WD15_EC_STR, EC_DESC},
		{TB16_EC_GUID, TB16_EC_STR, EC_DESC},
		{WD15_PC1_GUID, WD15_PC1_STR, PC1_DESC},
		{TB16_PC1_GUID, TB16_PC1_STR, PC1_DESC},
		{TB16_PC2_GUID, TB16_PC2_STR, PC2_DESC},
		{TBT_CBL_GUID, TBT_CBL_STR, TBT_CBL_DESC},
		{UNIV_CBL_GUID, UNIV_CBL_STR, UNIV_CBL_DESC},
		{LEGACY_CBL_GUID, LEGACY_CBL_STR, LEGACY_CBL_DESC},
	};

	for (guint i = 0; i < G_N_ELEMENTS (list); i++) {
		if (g_strcmp0 (query_str,
			       list[i].query) == 0) {
			memcpy (guid_out, &list[i].guid, sizeof (efi_guid_t));
			*name_out = list[i].desc;
			return TRUE;
		}
	}
	return FALSE;
}

void
fu_plugin_dell_inject_fake_data (FuPlugin *plugin,
				 guint32 *output, guint16 vid, guint16 pid,
				 guint8 *buf, gboolean can_switch_modes)
{
	FuPluginData *data = fu_plugin_get_data (plugin);

	if (!data->smi_obj->fake_smbios)
		return;
	for (guint i = 0; i < 4; i++)
		data->smi_obj->output[i] = output[i];
	data->fake_vid = vid;
	data->fake_pid = pid;
	data->smi_obj->fake_buffer = buf;
	data->can_switch_modes = TRUE;
}

static AsVersionParseFlag
fu_plugin_dell_get_version_format (FuPlugin *plugin)
{
	const gchar *content;
	const gchar *quirk;

	content = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_MANUFACTURER);
	if (content == NULL)
		return AS_VERSION_PARSE_FLAG_USE_TRIPLET;

	/* any quirks match */
	quirk = fu_plugin_lookup_quirk_by_id (plugin,
					      FU_QUIRKS_UEFI_VERSION_FORMAT,
					      content);
	if (g_strcmp0 (quirk, "none") == 0)
		return AS_VERSION_PARSE_FLAG_NONE;

	/* fall back */
	return AS_VERSION_PARSE_FLAG_USE_TRIPLET;
}

static gchar *
fu_plugin_get_dock_key (FuPlugin *plugin,
			GUsbDevice *device, const gchar *guid)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	const gchar* platform_id;

	if (data->smi_obj->fake_smbios)
		platform_id = "fake";
	else
		platform_id = g_usb_device_get_platform_id (device);
	return g_strdup_printf ("%s_%s", platform_id, guid);
}

static gboolean
fu_plugin_dell_capsule_supported (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);

	return data->smi_obj->fake_smbios || data->capsule_supported;
}

static gboolean
fu_plugin_dock_node (FuPlugin *plugin, GUsbDevice *device,
		     guint8 type, const efi_guid_t *guid_raw,
		     const gchar *component_desc, const gchar *version)
{
	const gchar *dock_type;
	g_autofree gchar *dock_id = NULL;
	g_autofree gchar *guid_str = NULL;
	g_autofree gchar *dock_key = NULL;
	g_autofree gchar *dock_name = NULL;
	g_autoptr(FuDevice) dev = NULL;

	dock_type = fu_dell_get_dock_type (type);
	if (dock_type == NULL) {
		g_debug ("Unknown dock type %d", type);
		return FALSE;
	}

	guid_str = g_strdup ("00000000-0000-0000-0000-000000000000");
	if (efi_guid_to_str (guid_raw, &guid_str) < 0) {
		g_debug ("Failed to convert GUID.");
		return FALSE;
	}

	dock_key = fu_plugin_get_dock_key (plugin, device,
						  guid_str);
	if (fu_plugin_cache_lookup (plugin, dock_key) != NULL) {
		g_debug ("%s is already registered.", dock_key);
		return FALSE;
	}

	dev = fu_device_new ();
	dock_id = g_strdup_printf ("DELL-%s" G_GUINT64_FORMAT, guid_str);
	dock_name = g_strdup_printf ("Dell %s %s", dock_type,
				     component_desc);
	fu_device_set_id (dev, dock_id);
	fu_device_set_vendor (dev, "Dell Inc.");
	fu_device_set_name (dev, dock_name);
	if (type == DOCK_TYPE_TB16) {
		fu_device_set_summary (dev, "A Thunderbolt™ 3 docking station");
	} else if (type == DOCK_TYPE_WD15) {
		fu_device_set_summary (dev, "A USB type-C docking station");
	}
	fu_device_add_icon (dev, "computer");
	fu_device_add_guid (dev, guid_str);
	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_REQUIRE_AC);
	if (version != NULL) {
		fu_device_set_version (dev, version);
		if (fu_plugin_dell_capsule_supported (plugin)) {
			fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE);
			fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
		}
	}

	fu_plugin_device_add (plugin, dev);
	fu_plugin_cache_add (plugin, dock_key, dev);
	return TRUE;
}


void
fu_plugin_dell_device_added_cb (GUsbContext *ctx,
				GUsbDevice *device,
				FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	AsVersionParseFlag parse_flags;
	guint16 pid;
	guint16 vid;
	const gchar *query_str;
	const gchar *component_name = NULL;
	DOCK_UNION buf;
	DOCK_INFO *dock_info;
	efi_guid_t guid_raw;
	efi_guid_t tmpguid;
	gboolean old_ec = FALSE;

	/* don't look up immediately if a dock is connected as that would
	   mean a SMI on every USB device that showed up on the system */
	if (!data->smi_obj->fake_smbios) {
		vid = g_usb_device_get_vid (device);
		pid = g_usb_device_get_pid (device);
	} else {
		vid = data->fake_vid;
		pid = data->fake_pid;
	}

	/* we're going to match on the Realtek NIC in the dock */
	if (vid != DOCK_NIC_VID || pid != DOCK_NIC_PID)
		return;

	buf.buf = NULL;
	if (!fu_dell_query_dock (data->smi_obj, &buf)) {
		g_debug ("No dock detected.");
		return;
	}

	if (buf.record->dock_info_header.dir_version != 1) {
		g_debug ("Dock info header version unknown: %d",
			 buf.record->dock_info_header.dir_version);
		return;
	}

	dock_info = &buf.record->dock_info;
	g_debug ("Dock description: %s", dock_info->dock_description);
	/* Note: fw package version is deprecated, look at components instead */
	g_debug ("Dock flash pkg ver: 0x%x", dock_info->flash_pkg_version);
	if (dock_info->flash_pkg_version == 0x00ffffff)
		g_debug ("WARNING: dock flash package version invalid");
	g_debug ("Dock cable type: %" G_GUINT32_FORMAT, dock_info->cable_type);
	g_debug ("Dock location: %d", dock_info->location);
	g_debug ("Dock component count: %d", dock_info->component_count);
	parse_flags = fu_plugin_dell_get_version_format (plugin);

	for (guint i = 0; i < dock_info->component_count; i++) {
		g_autofree gchar *fw_str = NULL;
		if (i >= MAX_COMPONENTS) {
			g_debug ("Too many components.  Invalid: #%u", i);
			break;
		}
		g_debug ("Dock component %u: %s (version 0x%x)", i,
			 dock_info->components[i].description,
			 dock_info->components[i].fw_version);
		query_str = g_strrstr (dock_info->components[i].description,
				       "Query ");
		if (query_str == NULL) {
			g_debug ("Invalid dock component request");
			return;
		}
		if (!fu_plugin_dell_match_dock_component (query_str + 6,
							  &guid_raw,
							  &component_name)) {
			g_debug ("Unable to match dock component %s",
				query_str);
			return;
		}

		/* dock EC hasn't been updated for first time */
		if (dock_info->flash_pkg_version == 0x00ffffff) {
			old_ec = TRUE;
			dock_info->flash_pkg_version = 0;
			continue;
		}
		/* if invalid version, don't mark device for updates */
		else if (dock_info->components[i].fw_version == 0 ||
			 dock_info->components[i].fw_version == 0xffffffff) {
			old_ec = TRUE;
			continue;
		}

		fw_str = as_utils_version_from_uint32 (dock_info->components[i].fw_version,
						       parse_flags);
		if (!fu_plugin_dock_node (plugin,
						 device,
						 buf.record->dock_info_header.dock_type,
						 &guid_raw,
						 component_name,
						 fw_str)) {
			g_debug ("Failed to create %s", component_name);
			return;
		}
	}

	/* if an old EC or invalid EC version found, create updatable parent */
	if (old_ec) {
		g_autofree gchar *fw_str = NULL;
		tmpguid = DOCK_FLASH_GUID;
		fw_str = as_utils_version_from_uint32 (dock_info->flash_pkg_version,
						       parse_flags);
		if (!fu_plugin_dock_node (plugin,
						 device,
						 buf.record->dock_info_header.dock_type,
						 &tmpguid,
						 "",
						 fw_str)) {
			g_debug ("Failed to create top dock node");
			return;
		}
	}

#if defined (HAVE_SYNAPTICS)
	fu_plugin_recoldplug (plugin);
#endif
}

void
fu_plugin_dell_device_removed_cb (GUsbContext *ctx,
				  GUsbDevice *device,
				  FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	const efi_guid_t guids[] = { WD15_EC_GUID, TB16_EC_GUID, TB16_PC2_GUID,
				     TB16_PC1_GUID, WD15_PC1_GUID,
				     LEGACY_CBL_GUID, UNIV_CBL_GUID,
				     TBT_CBL_GUID, DOCK_FLASH_GUID};
	const efi_guid_t *guid_raw;
	guint16 pid;
	guint16 vid;
	FuDevice *dev = NULL;

	if (!data->smi_obj->fake_smbios) {
		vid = g_usb_device_get_vid (device);
		pid = g_usb_device_get_pid (device);
	} else {
		vid = data->fake_vid;
		pid = data->fake_pid;
	}

	/* we're going to match on the Realtek NIC in the dock */
	if (vid != DOCK_NIC_VID || pid != DOCK_NIC_PID)
		return;

	/* remove any components already in database? */
	for (guint i = 0; i < G_N_ELEMENTS (guids); i++) {
		g_autofree gchar *dock_key = NULL;
		g_autofree gchar *guid_str = NULL;
		guid_raw = &guids[i];
		guid_str = g_strdup ("00000000-0000-0000-0000-000000000000");
		efi_guid_to_str (guid_raw, &guid_str);
		dock_key = fu_plugin_get_dock_key (plugin, device,
							  guid_str);
		dev = fu_plugin_cache_lookup (plugin, dock_key);
		if (dev != NULL) {
			fu_plugin_device_remove (plugin,
						   dev);
			fu_plugin_cache_remove (plugin, dock_key);
		}
	}
#if defined (HAVE_SYNAPTICS)
	fu_plugin_recoldplug (plugin);
#endif
}

gboolean
fu_plugin_get_results (FuPlugin *plugin, FuDevice *device, GError **error)
{
	GBytes *de_table = NULL;
	const gchar *tmp = NULL;
	const guint16 *completion_code;
	gsize len;

	de_table = fu_plugin_get_smbios_data (plugin, 0xDE);
	completion_code = g_bytes_get_data (de_table, &len);
	if (len < 8) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "ERROR: Unable to read results of %s: %" G_GSIZE_FORMAT " < 8",
			     fu_device_get_name (device), len);
		return FALSE;
	}

	/* look at byte offset 0x06  for identifier meaning completion code */
	if (completion_code[3] == DELL_SUCCESS) {
		fu_device_set_update_state (device, FWUPD_UPDATE_STATE_SUCCESS);
	} else {
		fu_device_set_update_state (device, FWUPD_UPDATE_STATE_FAILED);
		switch (completion_code[3]) {
		case DELL_CONSISTENCY_FAIL:
			tmp = "The image failed one or more consistency checks.";
			break;
		case DELL_FLASH_MEMORY_FAIL:
			tmp = "The BIOS could not access the flash-memory device.";
			break;
		case DELL_FLASH_NOT_READY:
			tmp = "The flash-memory device was not ready when an erase was attempted.";
			break;
		case DELL_FLASH_DISABLED:
			tmp = "Flash programming is currently disabled on the system, or the voltage is low.";
			break;
		case DELL_BATTERY_MISSING:
			tmp = "A battery must be installed for the operation to complete.";
			break;
		case DELL_BATTERY_DEAD:
			tmp = "A fully-charged battery must be present for the operation to complete.";
			break;
		case DELL_AC_MISSING:
			tmp = "An external power adapter must be connected for the operation to complete.";
			break;
		case DELL_CANT_SET_12V:
			tmp = "The 12V required to program the flash-memory could not be set.";
			break;
		case DELL_CANT_UNSET_12V:
			tmp = "The 12V required to program the flash-memory could not be removed.";
			break;
		case DELL_FAILURE_BLOCK_ERASE :
			tmp = "A flash-memory failure occurred during a block-erase operation.";
			break;
		case DELL_GENERAL_FAILURE:
			tmp = "A general failure occurred during the flash programming.";
			break;
		case DELL_DATA_MISCOMPARE:
			tmp = "A data miscompare error occurred during the flash programming.";
			break;
		case DELL_IMAGE_MISSING:
			tmp = "The image could not be found in memory, i.e. the header could not be located.";
			break;
		case DELL_DID_NOTHING:
			tmp = "No update operation has been performed on the system.";
			break;
		default:
			break;
		}
		if (tmp != NULL)
			fu_device_set_update_error (device, tmp);
	}

	return TRUE;
}

gboolean
fu_plugin_dell_detect_tpm (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	const gchar *tpm_mode;
	const gchar *tpm_mode_alt;
	guint16 system_id = 0;
	gboolean can_switch_modes = FALSE;
	g_autofree gchar *pretty_tpm_name_alt = NULL;
	g_autofree gchar *pretty_tpm_name = NULL;
	g_autofree gchar *tpm_guid_raw_alt = NULL;
	g_autofree gchar *tpm_guid_alt = NULL;
	g_autofree gchar *tpm_guid = NULL;
	g_autofree gchar *tpm_guid_raw = NULL;
	g_autofree gchar *tpm_id_alt = NULL;
	g_autofree gchar *tpm_id = NULL;
	g_autofree gchar *version_str = NULL;
	struct tpm_status *out = NULL;
	g_autoptr (FuDevice) dev_alt = NULL;
	g_autoptr (FuDevice) dev = NULL;
	const gchar *product_name = NULL;

	fu_dell_clear_smi (data->smi_obj);
	out = (struct tpm_status *) data->smi_obj->output;

	/* execute TPM Status Query */
	data->smi_obj->input[0] = DACI_FLASH_ARG_TPM;
	if (!fu_dell_execute_simple_smi (data->smi_obj,
					 DACI_FLASH_INTERFACE_CLASS,
					 DACI_FLASH_INTERFACE_SELECT))
		return FALSE;

	if (out->ret != 0) {
		g_debug ("Failed to query system for TPM information: "
			 "(%" G_GUINT32_FORMAT ")", out->ret);
		return FALSE;
	}
	/* HW version is output in second /input/ arg
	 * it may be relevant as next gen TPM is enabled
	 */
	g_debug ("TPM HW version: 0x%x", data->smi_obj->input[1]);
	g_debug ("TPM Status: 0x%x", out->status);

	/* test TPM enabled (Bit 0) */
	if (!(out->status & TPM_EN_MASK)) {
		g_debug ("TPM not enabled (%x)", out->status);
		return FALSE;
	}

	/* test TPM mode to determine current mode */
	if (((out->status & TPM_TYPE_MASK) >> 8) == TPM_1_2_MODE) {
		tpm_mode = "1.2";
		tpm_mode_alt = "2.0";
	} else if (((out->status & TPM_TYPE_MASK) >> 8) == TPM_2_0_MODE) {
		tpm_mode = "2.0";
		tpm_mode_alt = "1.2";
	} else {
		g_debug ("Unable to determine TPM mode");
		return FALSE;
	}

	system_id = fu_dell_get_system_id (plugin);
	if (data->smi_obj->fake_smbios)
		can_switch_modes = data->can_switch_modes;
	else if (system_id == 0)
		return FALSE;

	for (guint i = 0; i < G_N_ELEMENTS (tpm_switch_whitelist); i++) {
		if (tpm_switch_whitelist[i] == system_id) {
			can_switch_modes = TRUE;
		}
	}

	tpm_guid_raw = g_strdup_printf ("%04x-%s", system_id, tpm_mode);
	tpm_guid = as_utils_guid_from_string (tpm_guid_raw);
	tpm_id = g_strdup_printf ("DELL-%s" G_GUINT64_FORMAT, tpm_guid);

	tpm_guid_raw_alt = g_strdup_printf ("%04x-%s", system_id, tpm_mode_alt);
	tpm_guid_alt = as_utils_guid_from_string (tpm_guid_raw_alt);
	tpm_id_alt = g_strdup_printf ("DELL-%s" G_GUINT64_FORMAT, tpm_guid_alt);

	g_debug ("Creating primary TPM GUID %s and secondary TPM GUID %s",
		 tpm_guid_raw, tpm_guid_raw_alt);
	version_str = as_utils_version_from_uint32 (out->fw_version,
						    AS_VERSION_PARSE_FLAG_NONE);

	/* make it clear that the TPM is a discrete device of the product */
	if (!data->smi_obj->fake_smbios) {
		product_name = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_PRODUCT_NAME);
	}
	pretty_tpm_name = g_strdup_printf ("%s TPM %s", product_name, tpm_mode);
	pretty_tpm_name_alt = g_strdup_printf ("%s TPM %s", product_name, tpm_mode_alt);

	/* build Standard device nodes */
	dev = fu_device_new ();
	fu_device_set_id (dev, tpm_id);
	fu_device_add_guid (dev, tpm_guid);
	fu_device_set_vendor (dev, "Dell Inc.");
	fu_device_set_name (dev, pretty_tpm_name);
	fu_device_set_summary (dev, "Platform TPM device");
	fu_device_set_version (dev, version_str);
	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_icon (dev, "computer");
	if (out->flashes_left > 0) {
		if (fu_plugin_dell_capsule_supported (plugin)) {
			fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE);
			fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
		}
		fu_device_set_flashes_left (dev, out->flashes_left);
	}
	fu_plugin_device_add (plugin, dev);

	/* build alternate device node */
	if (can_switch_modes) {
		dev_alt = fu_device_new ();
		fu_device_set_id (dev_alt, tpm_id_alt);
		fu_device_add_guid (dev_alt, tpm_guid_alt);
		fu_device_set_vendor (dev, "Dell Inc.");
		fu_device_set_name (dev_alt, pretty_tpm_name_alt);
		fu_device_set_summary (dev_alt, "Alternate mode for platform TPM device");
		fu_device_add_flag (dev_alt, FWUPD_DEVICE_FLAG_INTERNAL);
		fu_device_add_flag (dev_alt, FWUPD_DEVICE_FLAG_REQUIRE_AC);
		fu_device_add_flag (dev_alt, FWUPD_DEVICE_FLAG_LOCKED);
		fu_device_add_icon (dev_alt, "computer");
		fu_device_set_alternate (dev_alt, dev);

		/* If TPM is not owned and at least 1 flash left allow mode switching
		 *
		 * Mode switching is turned on by setting flashes left on alternate
		 * device.
		 */
		if (!((out->status) & TPM_OWN_MASK) &&
		    out->flashes_left > 0) {
			fu_device_set_flashes_left (dev_alt, out->flashes_left);
		} else {
			g_debug ("%s mode switch disabled due to TPM ownership",
				 pretty_tpm_name);
		}
		fu_plugin_device_add (plugin, dev_alt);
	}
	else
		g_debug ("System %04x does not offer TPM modeswitching",
			system_id);

	return TRUE;
}

gboolean
fu_plugin_unlock (FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuDevice *device_alt = NULL;
	FwupdDeviceFlags device_flags_alt = 0;
	guint flashes_left = 0;
	guint flashes_left_alt = 0;

	/* for unlocking TPM1.2 <-> TPM2.0 switching */
	g_debug ("Unlocking upgrades for: %s (%s)", fu_device_get_name (device),
		 fu_device_get_id (device));
	device_alt = fu_device_get_alternate (device);

	if (!device_alt)
		return FALSE;
	g_debug ("Preventing upgrades for: %s (%s)", fu_device_get_name (device_alt),
		 fu_device_get_id (device_alt));

	flashes_left = fu_device_get_flashes_left (device);
	flashes_left_alt = fu_device_get_flashes_left (device_alt);
	if (flashes_left == 0) {
		/* flashes left == 0 on both means no flashes left */
		if (flashes_left_alt == 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "ERROR: %s has no flashes left.",
				     fu_device_get_name (device));
		/* flashes left == 0 on just unlocking device is ownership */
		} else {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "ERROR: %s is currently OWNED. "
				     "Ownership must be removed to switch modes.",
				     fu_device_get_name (device_alt));
		}
		return FALSE;
	}


	/* clone the info from real device but prevent it from being flashed */
	device_flags_alt = fu_device_get_flags (device_alt);
	fu_device_set_flags (device, device_flags_alt);
	fu_device_set_flags (device_alt, device_flags_alt & ~FWUPD_DEVICE_FLAG_UPDATABLE);

	/* make sure that this unlocked device can be updated */
	fu_device_set_version (device, "0.0.0.0");

	return TRUE;
}

gboolean
fu_plugin_update (FuPlugin *plugin,
		  FuDevice *device,
		  GBytes *blob_fw,
		  FwupdInstallFlags flags,
		  GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autoptr (fwup_resource_iter) iter = NULL;
	fwup_resource *re = NULL;
	const gchar *name = NULL;
	gint rc;
	guint flashes_left;
	const gchar *guidstr = NULL;
	efi_guid_t guid;

	/* test the flash counter
	 * - devices with 0 left at setup aren't allowed offline updates
	 * - devices greater than 0 should show a warning when near 0
	 */
	flashes_left = fu_device_get_flashes_left (device);
	if (flashes_left > 0) {
		name = fu_device_get_name (device);
		g_debug ("%s has %u flashes left", name, flashes_left);
		if ((flags & FWUPD_INSTALL_FLAG_FORCE) == 0 &&
			   flashes_left <= 2) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "WARNING: %s only has %u flashes left. "
				     "See https://github.com/hughsie/fwupd/wiki/Dell-TPM:-flashes-left for more information.",
				     name, flashes_left);
			return FALSE;
		}
	}

	if (data->smi_obj->fake_smbios)
		return TRUE;

	/* perform the update */
	g_debug ("Performing capsule update");

	/* Stuff the payload into a different GUID
	 * - with fwup 0.5 this uses the ESRT GUID
	 * - with fwup 0.6 this uses the payload's GUID
	 * it's preferable to use payload GUID to avoid
	 * a corner case scenario of UEFI BIOS and non-ESRT
	 * update happening at same time
	 */
	fwup_resource_iter_create (&iter);
	fwup_resource_iter_next (iter, &re);
	guidstr = fu_device_get_guid_default (device);
	rc = efi_str_to_guid (guidstr, &guid);
	if (rc < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Failed to convert guid to string");
		return FALSE;
	}
	rc = fwup_set_guid (iter, &re, &guid);
	if (rc < 0 || re == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Failed to update GUID %s",
			     strerror (rc));
		return FALSE;
	}

	/* NOTE: if there are problems with this working, adjust the
	 * GUID in the capsule header to match something in ESRT.
	 * This won't actually cause any bad behavior because the real
	 * payload GUID is extracted later on.
	 */
	fu_plugin_set_status (plugin, FWUPD_STATUS_SCHEDULING);
	rc = fwup_set_up_update_with_buf (re, 0,
					  g_bytes_get_data (blob_fw, NULL),
					  g_bytes_get_size (blob_fw));
	if (rc < 0) {
		g_autoptr(GString) err_string = g_string_new ("Dell firmware update failed:\n");

		rc = 1;
		for (int i = 0; rc > 0; i++) {
			char *filename = NULL;
			char *function = NULL;
			char *message = NULL;
			int line = 0;
			int err = 0;

			rc = efi_error_get (i, &filename, &function, &line, &message, &err);
			if (rc <= 0)
				break;
			g_string_append_printf (err_string,
						"{error #%d} %s:%d %s(): %s: %s \n",
						i, filename, line, function, message, strerror(err));
		}
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "%s",
			     err_string->str);
		return FALSE;
	}
	return TRUE;
}

void
fu_plugin_device_registered (FuPlugin *plugin, FuDevice *device)
{
	/* thunderbolt plugin */
	if (g_strcmp0 (fu_device_get_plugin (device), "thunderbolt") == 0 &&
	    fu_device_has_flag (device, FWUPD_DEVICE_FLAG_INTERNAL)) {
		/* fix VID/DID of safe mode devices */
		if (fu_device_get_metadata_boolean (device, FU_DEVICE_METADATA_TBT_IS_SAFE_MODE)) {
			g_autofree gchar *vendor_id = NULL;
			g_autofree gchar *device_id = NULL;
			guint16 system_id = 0;

			vendor_id = g_strdup ("TBT:0x00D4");
			system_id = fu_dell_get_system_id (plugin);
			if (system_id == 0)
				return;
			/* the kernel returns lowercase in sysfs, need to match it */
			device_id = g_strdup_printf ("TBT-%04x%04x", 0x00d4u,
						     (unsigned) system_id);
			fu_device_set_vendor_id (device, vendor_id);
			fu_device_add_guid (device, device_id);
			fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);
		}
	}
}

static gboolean
fu_dell_toggle_flash (FuPlugin *plugin, FuDevice *device,
		      gboolean enable, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	gboolean has_host = fu_dell_host_mst_supported (plugin);
	gboolean has_dock;
	guint32 dock_location;
	const gchar *tmp;

	if (device) {
		if (!fu_device_has_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE))
			return TRUE;
		tmp = fu_device_get_plugin (device);
		if (g_strcmp0 (tmp, "synapticsmst") != 0)
			return TRUE;
		g_debug ("preparing/cleaning update for %s", tmp);
	}

	/* Dock MST Hub */
	has_dock = fu_dell_detect_dock (data->smi_obj, &dock_location);
	if (has_dock) {
		if (!fu_dell_toggle_dock_mode (data->smi_obj, enable,
					       dock_location, error))
			g_debug ("unable to change dock to %d", enable);
		else
			g_debug ("Toggled dock mode to %d", enable);
	}

	/* System MST hub */
	if (has_host) {
		if (!fu_dell_toggle_host_mode (data->smi_obj, MST_GPIO_GUID, enable))
			g_debug ("Unable to toggle MST hub GPIO to %d", enable);
		else
			g_debug ("Toggled MST hub GPIO to %d", enable);
	}

#if defined (HAVE_SYNAPTICS)
	/* set a delay to allow OS response to settling the GPIO change */
	if (enable && device == NULL && (has_dock || has_host))
		fu_plugin_set_coldplug_delay (plugin, DELL_FLASH_MODE_DELAY * 1000);
#endif
	return TRUE;
}

gboolean
fu_plugin_update_prepare (FuPlugin *plugin,
			  FuDevice *device,
			  GError **error)
{

	return fu_dell_toggle_flash (plugin, device, TRUE, error);
}

gboolean
fu_plugin_update_cleanup (FuPlugin *plugin,
			  FuDevice *device,
			  GError **error)
{
	return fu_dell_toggle_flash (plugin, device , FALSE, error);
}

gboolean
fu_plugin_coldplug_prepare (FuPlugin *plugin, GError **error)
{
	return fu_dell_toggle_flash (plugin, NULL, TRUE, error);
}

gboolean
fu_plugin_coldplug_cleanup (FuPlugin *plugin, GError **error)
{
	return fu_dell_toggle_flash (plugin, NULL, FALSE, error);
}

void
fu_plugin_init (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_alloc_data (plugin, sizeof (FuPluginData));

	data->smi_obj = g_malloc0 (sizeof (FuDellSmiObj));
	if (fu_dell_supported (plugin))
		data->smi_obj->smi = dell_smi_factory (DELL_SMI_DEFAULTS);
	data->smi_obj->fake_smbios = FALSE;
	if (g_getenv ("FWUPD_DELL_FAKE_SMBIOS") != NULL)
		data->smi_obj->fake_smbios = TRUE;
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	if (data->smi_obj->smi)
		dell_smi_obj_free (data->smi_obj->smi);
	g_free(data->smi_obj);
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	GUsbContext *usb_ctx = fu_plugin_get_usb_context (plugin);
	gint uefi_supported;

	if (data->smi_obj->fake_smbios) {
		g_debug ("Called with fake SMBIOS implementation. "
			 "We're ignoring test for SBMIOS table and ESRT. "
			 "Individual calls will need to be properly staged.");
		return TRUE;
	}

	if (!fu_dell_supported (plugin)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Firmware updating not supported");
		return FALSE;
	}

	/* If ESRT is not turned on, fwupd will have already created an
	 * unlock device (if compiled with support).
	 *
	 * Once unlocked, that will enable flashing capsules here too.
	 *
	 * that means we should only look for supported = 1
	 */
	uefi_supported = fwup_supported ();
	data->capsule_supported = (uefi_supported == 1);
	if (!data->capsule_supported) {
		g_debug ("UEFI capsule firmware updating not supported (%x)",
			 (guint) uefi_supported);
	}

	if (usb_ctx != NULL) {
		g_signal_connect (usb_ctx, "device-added",
				  G_CALLBACK (fu_plugin_dell_device_added_cb),
				  plugin);
		g_signal_connect (usb_ctx, "device-removed",
				  G_CALLBACK (fu_plugin_dell_device_removed_cb),
				  plugin);
	}

	return TRUE;
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	/* look for switchable TPM */
	if (!fu_plugin_dell_detect_tpm (plugin, error))
		g_debug ("No switchable TPM detected");
	return TRUE;
}
