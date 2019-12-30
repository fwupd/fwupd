/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <glib/gstdio.h>
#include <smbios_c/system_info.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <tss2/tss2_esys.h>

#include "fwupd-common.h"
#include "fu-plugin-dell.h"
#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"
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
	const gchar *		guid;
	const gchar *		query;
	const gchar *		desc;
} DOCK_DESCRIPTION;

struct da_structure {
	guint8			 type;
	guint8			 length;
	guint16			 handle;
	guint16			 cmd_address;
	guint8			 cmd_code;
	guint32			 supported_cmds;
	guint8			*tokens;
} __attribute__((packed));

/* These are for matching the components */
#define WD15_EC_STR		"2 0 2 2 0"
#define TB16_EC_STR		"2 0 2 1 0"
#define TB16_PC2_STR		"2 1 0 1 1"
#define TB16_PC1_STR		"2 1 0 1 0"
#define WD15_PC1_STR		"2 1 0 2 0"
#define LEGACY_CBL_STR		"2 2 2 1 0"
#define UNIV_CBL_STR		"2 2 2 2 0"
#define TBT_CBL_STR		"2 2 2 3 0"
#define FUTURE_EC_STR		"3 0 2 4 0"
#define FUTURE_EC_STR2		"4 0 2 4 0"

/* supported dock related GUIDs */
#define DOCK_FLASH_GUID		"e7ca1f36-bf73-4574-afe6-a4ccacabf479"
#define WD15_EC_GUID		"e8445370-0211-449d-9faa-107906ab189f"
#define TB16_EC_GUID		"33cc8870-b1fc-4ec7-948a-c07496874faf"
#define TB16_PC2_GUID		"1b52c630-86f6-4aee-9f0c-474dc6be49b6"
#define TB16_PC1_GUID		"8fe183da-c94e-4804-b319-0f1ba5457a69"
#define WD15_PC1_GUID		"8ba2b709-6f97-47fc-b7e7-6a87b578fe25"
#define LEGACY_CBL_GUID		"fece1537-d683-4ea8-b968-154530bb6f73"
#define UNIV_CBL_GUID		"e2bf3aad-61a3-44bf-91ef-349b39515d29"
#define TBT_CBL_GUID		"6dc832fc-5bb0-4e63-a2ff-02aaba5bc1dc"

#define EC_DESC			"EC"
#define PC1_DESC		"Port Controller 1"
#define PC2_DESC		"Port Controller 2"
#define LEGACY_CBL_DESC		"Passive Cable"
#define UNIV_CBL_DESC		"Universal Cable"
#define TBT_CBL_DESC		"Thunderbolt Cable"

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

static guint16
fu_dell_get_system_id (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	const gchar *system_id_str = NULL;
	guint16 system_id = 0;
	gchar *endptr = NULL;

	/* don't care for test suite */
	if (data->smi_obj->fake_smbios)
		return 0;

	system_id_str = fu_plugin_get_dmi_value (plugin,
		FU_HWIDS_KEY_PRODUCT_SKU);
	if (system_id_str != NULL)
		system_id = g_ascii_strtoull (system_id_str, &endptr, 16);
	if (system_id == 0 || endptr == system_id_str)
		system_id = (guint16) sysinfo_get_dell_system_id ();

	return system_id;
}

static gboolean
fu_dell_supported (FuPlugin *plugin)
{
	g_autoptr(GBytes) de_table = NULL;
	g_autoptr(GBytes) da_table = NULL;
	g_autoptr(GBytes) enclosure = NULL;
	const guint8 *value;
	const struct da_structure *da_values;
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
	da_table = fu_plugin_get_smbios_data (plugin, 0xDA);
	if (da_table == NULL)
		return FALSE;
	da_values = (struct da_structure *) g_bytes_get_data (da_table, &len);
	if (len == 0)
		return FALSE;
	if (!(da_values->supported_cmds & (1 << DACI_FLASH_INTERFACE_CLASS))) {
		g_debug ("unable to access flash interface. supported commands: 0x%x",
			 da_values->supported_cmds);
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
				     const gchar **guid_out,
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
		{NULL, FUTURE_EC_STR, NULL},
		{NULL, FUTURE_EC_STR2, NULL},
	};

	for (guint i = 0; i < G_N_ELEMENTS (list); i++) {
		if (g_strcmp0 (query_str,
			       list[i].query) == 0) {
			*guid_out = list[i].guid;
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

static FwupdVersionFormat
fu_plugin_dell_get_version_format (FuPlugin *plugin)
{
	const gchar *content;
	const gchar *quirk;
	g_autofree gchar *group = NULL;

	content = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_MANUFACTURER);
	if (content == NULL)
		return FWUPD_VERSION_FORMAT_TRIPLET;

	/* any quirks match */
	group = g_strdup_printf ("SmbiosManufacturer=%s", content);
	quirk = fu_plugin_lookup_quirk_by_id (plugin, group,
					      FU_QUIRKS_UEFI_VERSION_FORMAT);
	if (quirk == NULL)
		return FWUPD_VERSION_FORMAT_TRIPLET;
	return fwupd_version_format_from_string (quirk);
}

static gboolean
fu_plugin_dell_capsule_supported (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);

	return data->smi_obj->fake_smbios || data->capsule_supported;
}

static gboolean
fu_plugin_dock_node (FuPlugin *plugin, const gchar *platform,
		     guint8 type, const gchar *component_guid,
		     const gchar *component_desc, const gchar *version,
		     FwupdVersionFormat version_format)
{
	const gchar *dock_type;
	g_autofree gchar *dock_name = NULL;
	g_autoptr(FuDevice) dev = NULL;

	dock_type = fu_dell_get_dock_type (type);
	if (dock_type == NULL) {
		g_debug ("Unknown dock type %d", type);
		return FALSE;
	}

	dev = fu_device_new ();
	fu_device_set_physical_id (dev, platform);
	fu_device_set_logical_id (dev, component_guid);
	if (component_desc != NULL) {
		dock_name = g_strdup_printf ("Dell %s %s", dock_type,
					     component_desc);
		fu_device_add_parent_guid (dev, DOCK_FLASH_GUID);
	} else {
		dock_name = g_strdup_printf ("Dell %s", dock_type);
	}
	fu_device_set_vendor (dev, "Dell Inc.");
	fu_device_set_vendor_id (dev, "PCI:0x1028");
	fu_device_set_name (dev, dock_name);
	fu_device_set_metadata (dev, FU_DEVICE_METADATA_UEFI_DEVICE_KIND, "device-firmware");
	if (type == DOCK_TYPE_TB16) {
		fu_device_set_summary (dev, "A Thunderbolt™ 3 docking station");
	} else if (type == DOCK_TYPE_WD15) {
		fu_device_set_summary (dev, "A USB type-C docking station");
	}
	fu_device_add_icon (dev, "computer");
	fu_device_add_guid (dev, component_guid);
	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_REQUIRE_AC);
	if (version != NULL) {
		fu_device_set_version (dev, version, version_format);
		if (fu_plugin_dell_capsule_supported (plugin)) {
			fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE);
			fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
		} else {
			fu_device_set_update_error (dev,
						    "UEFI capsule updates turned off in BIOS setup");
		}
	}

	fu_plugin_device_register (plugin, dev);
	return TRUE;
}

gboolean
fu_plugin_usb_device_added (FuPlugin *plugin,
			    FuUsbDevice *device,
			    GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	FwupdVersionFormat version_format;
	guint16 pid;
	guint16 vid;
	const gchar *query_str;
	const gchar *component_guid = NULL;
	const gchar *component_name = NULL;
	const gchar *platform;
	DOCK_UNION buf;
	DOCK_INFO *dock_info;
	gboolean old_ec = FALSE;
	g_autofree gchar *flash_ver_str = NULL;

	/* don't look up immediately if a dock is connected as that would
	   mean a SMI on every USB device that showed up on the system */
	if (!data->smi_obj->fake_smbios) {
		vid = fu_usb_device_get_vid (device);
		pid = fu_usb_device_get_pid (device);
		platform = fu_device_get_physical_id (FU_DEVICE (device));
	} else {
		vid = data->fake_vid;
		pid = data->fake_pid;
		platform = "fake";
	}

	/* we're going to match on the Realtek NIC in the dock */
	if (vid != DOCK_NIC_VID || pid != DOCK_NIC_PID) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
			     "wrong VID/PID %04x:%04x", vid, pid);
		return FALSE;
	}

	buf.buf = NULL;
	if (!fu_dell_query_dock (data->smi_obj, &buf)) {
		g_debug ("no dock detected");
		return TRUE;
	}

	if (buf.record->dock_info_header.dir_version != 1) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
			     "dock info header version unknown %d",
			     buf.record->dock_info_header.dir_version);
		return FALSE;
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
	version_format = fu_plugin_dell_get_version_format (plugin);

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
			g_set_error_literal (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
					     "invalid dock component request");
			return FALSE;
		}
		if (!fu_plugin_dell_match_dock_component (query_str + 6,
							  &component_guid,
							  &component_name)) {
			g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
				     "invalid dock component request %s", query_str);
			return FALSE;
		}
		if (component_guid == NULL || component_name == NULL) {
			g_debug ("%s is supported by another plugin", query_str);
			return TRUE;
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

		fw_str = fu_common_version_from_uint32 (dock_info->components[i].fw_version,
						       version_format);
		if (!fu_plugin_dock_node (plugin,
					  platform,
					  buf.record->dock_info_header.dock_type,
					  component_guid,
					  component_name,
					  fw_str,
					  version_format)) {
			g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
				     "failed to create %s", component_name);
			return FALSE;
		}
	}

	/* if an old EC or invalid EC version found, create updatable parent */
	if (old_ec)
		flash_ver_str = fu_common_version_from_uint32 (dock_info->flash_pkg_version,
							       version_format);
	if (!fu_plugin_dock_node (plugin,
				  platform,
				  buf.record->dock_info_header.dock_type,
				  DOCK_FLASH_GUID,
				  NULL,
				  flash_ver_str,
				  version_format)) {
		g_set_error_literal (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
				    "failed to create top dock node");

		return FALSE;
	}

	return TRUE;
}

gboolean
fu_plugin_get_results (FuPlugin *plugin, FuDevice *device, GError **error)
{
	g_autoptr(GBytes) de_table = NULL;
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
		FwupdUpdateState update_state = FWUPD_UPDATE_STATE_FAILED;
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
			update_state = FWUPD_UPDATE_STATE_FAILED_TRANSIENT;
			break;
		case DELL_BATTERY_DEAD:
			tmp = "A fully-charged battery must be present for the operation to complete.";
			update_state = FWUPD_UPDATE_STATE_FAILED_TRANSIENT;
			break;
		case DELL_AC_MISSING:
			tmp = "An external power adapter must be connected for the operation to complete.";
			update_state = FWUPD_UPDATE_STATE_FAILED_TRANSIENT;
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
		fu_device_set_update_state (device, update_state);
		if (tmp != NULL)
			fu_device_set_update_error (device, tmp);
	}

	return TRUE;
}

static void Esys_Finalize_autoptr_cleanup (ESYS_CONTEXT *esys_context)
{
	Esys_Finalize (&esys_context);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ESYS_CONTEXT, Esys_Finalize_autoptr_cleanup)

static gchar *
fu_plugin_dell_get_tpm_capability (ESYS_CONTEXT *ctx, guint32 query)
{
	TSS2_RC rc;
	guint32 val;
	gchar result[5] = {'\0'};
	g_autofree TPMS_CAPABILITY_DATA *capability = NULL;
	rc = Esys_GetCapability (ctx, ESYS_TR_NONE, ESYS_TR_NONE, ESYS_TR_NONE,
	                         TPM2_CAP_TPM_PROPERTIES, query, 1, NULL, &capability);
	if (rc != TSS2_RC_SUCCESS) {
		g_debug ("capability request failed for query %x", query);
		return NULL;
	}
	if (capability->data.tpmProperties.count == 0) {
		g_debug ("no properties returned for query %x", query);
		return NULL;
	}
	if (capability->data.tpmProperties.tpmProperty[0].property != query) {
		g_debug ("wrong query returned (got %x expected %x)",
			 capability->data.tpmProperties.tpmProperty[0].property,
			 query);
		return NULL;
	}

	val = GUINT32_FROM_BE (capability->data.tpmProperties.tpmProperty[0].value);
	memcpy (result, (gchar *) &val, 4);

	/* convert non-ASCII into spaces */
	for (guint i = 0; i < 4; i++) {
		if (!g_ascii_isgraph (result[i]) && result[i] != '\0')
			result[i] = 0x20;
	}

	return fu_common_strstrip (result);
}

static gboolean
fu_plugin_dell_add_tpm_model (FuDevice *dev, GError **error)
{
	TSS2_RC rc;
	const gchar *base = "DELL-TPM";
	g_autoptr(ESYS_CONTEXT) ctx = NULL;
	g_autofree gchar *family = NULL;
	g_autofree gchar *manufacturer = NULL;
	g_autofree gchar *vendor1 = NULL;
	g_autofree gchar *vendor2 = NULL;
	g_autofree gchar *vendor3 = NULL;
	g_autofree gchar *vendor4 = NULL;
	g_autofree gchar *v1 = NULL;
	g_autofree gchar *v1_v2 = NULL;
	g_autofree gchar *v1_v2_v3 = NULL;
	g_autofree gchar *v1_v2_v3_v4 = NULL;

	rc = Esys_Initialize (&ctx, NULL, NULL);
	if (rc != TSS2_RC_SUCCESS) {
		g_set_error_literal (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND,
		                     "failed to initialize TPM library");
		return FALSE;
	}
	rc = Esys_Startup (ctx, TPM2_SU_CLEAR);
	if (rc != TSS2_RC_SUCCESS) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		                     "failed to initialize TPM");
		return FALSE;
	}

	/* lookup guaranteed details from TPM */
	family = fu_plugin_dell_get_tpm_capability (ctx,
						    TPM2_PT_FAMILY_INDICATOR);
	if (family == NULL) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		                     "failed to read TPM family");
		return FALSE;
	}
	manufacturer = fu_plugin_dell_get_tpm_capability (ctx, TPM2_PT_MANUFACTURER);
	if (manufacturer == NULL) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		                     "failed to read TPM manufacturer");
		return FALSE;
	}
	vendor1 = fu_plugin_dell_get_tpm_capability (ctx, TPM2_PT_VENDOR_STRING_1);
	if (vendor1 == NULL) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		                     "failed to read TPM vendor string");
		return FALSE;
	}

	/* these are not guaranteed by spec and may be NULL */
	vendor2 = fu_plugin_dell_get_tpm_capability (ctx, TPM2_PT_VENDOR_STRING_2);
	vendor3 = fu_plugin_dell_get_tpm_capability (ctx, TPM2_PT_VENDOR_STRING_3);
	vendor4 = fu_plugin_dell_get_tpm_capability (ctx, TPM2_PT_VENDOR_STRING_4);

	/* add GUIDs to daemon */
	v1 = g_strjoin ("-", base, family, manufacturer, vendor1, NULL);
	v1_v2 = g_strconcat (v1, vendor2, NULL);
	v1_v2_v3 = g_strconcat (v1_v2, vendor3, NULL);
	v1_v2_v3_v4 = g_strconcat (v1_v2_v3, vendor4, NULL);
	fu_device_add_instance_id (dev, v1);
	fu_device_add_instance_id (dev, v1_v2);
	fu_device_add_instance_id (dev, v1_v2_v3);
	fu_device_add_instance_id (dev, v1_v2_v3_v4);

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
	g_autofree gchar *version_str = NULL;
	struct tpm_status *out = NULL;
	g_autoptr (FuDevice) dev_alt = NULL;
	g_autoptr (FuDevice) dev = NULL;
	g_autoptr(GError) error_tss = NULL;

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
	tpm_guid = fwupd_guid_hash_string (tpm_guid_raw);

	tpm_guid_raw_alt = g_strdup_printf ("%04x-%s", system_id, tpm_mode_alt);
	tpm_guid_alt = fwupd_guid_hash_string (tpm_guid_raw_alt);
	tpm_id_alt = g_strdup_printf ("DELL-%s" G_GUINT64_FORMAT, tpm_guid_alt);

	g_debug ("Creating primary TPM GUID %s and secondary TPM GUID %s",
		 tpm_guid_raw, tpm_guid_raw_alt);
	version_str = fu_common_version_from_uint32 (out->fw_version,
						     FWUPD_VERSION_FORMAT_QUAD);

	/* make it clear that the TPM is a discrete device of the product */
	pretty_tpm_name = g_strdup_printf ("TPM %s", tpm_mode);
	pretty_tpm_name_alt = g_strdup_printf ("TPM %s", tpm_mode_alt);

	/* build Standard device nodes */
	dev = fu_device_new ();
	fu_device_set_physical_id (dev, "DEVNAME=/dev/tpm0");
	fu_device_add_instance_id (dev, tpm_guid_raw);
	fu_device_add_instance_id (dev, "system-tpm");
	fu_device_set_vendor (dev, "Dell Inc.");
	fu_device_set_vendor_id (dev, "PCI:0x1028");
	fu_device_set_name (dev, pretty_tpm_name);
	fu_device_set_summary (dev, "Platform TPM device");
	fu_device_set_version (dev, version_str, FWUPD_VERSION_FORMAT_QUAD);
	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_icon (dev, "computer");
	fu_device_set_metadata (dev, FU_DEVICE_METADATA_UEFI_DEVICE_KIND, "dell-tpm-firmware");
	if ((out->status & TPM_OWN_MASK) == 0 && out->flashes_left > 0) {
		if (fu_plugin_dell_capsule_supported (plugin)) {
			fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE);
			fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
		} else {
			fu_device_set_update_error (dev,
						    "UEFI capsule updates turned off in BIOS setup");
		}
		fu_device_set_flashes_left (dev, out->flashes_left);
	} else {
		fu_device_set_update_error (dev,
					    "Updating disabled due to TPM ownership");
	}
	/* build GUIDs from TSS strings */
	if (!fu_plugin_dell_add_tpm_model (dev, &error_tss))
		g_debug ("could not build instances: %s", error_tss->message);

	if (!fu_device_setup (dev, error))
		return FALSE;
	fu_plugin_device_register (plugin, dev);

	/* build alternate device node */
	if (can_switch_modes) {
		dev_alt = fu_device_new ();
		fu_device_set_id (dev_alt, tpm_id_alt);
		fu_device_add_instance_id (dev_alt, tpm_guid_raw_alt);
		fu_device_set_vendor (dev, "Dell Inc.");
		fu_device_set_vendor_id (dev, "PCI:0x1028");
		fu_device_set_name (dev_alt, pretty_tpm_name_alt);
		fu_device_set_summary (dev_alt, "Alternate mode for platform TPM device");
		fu_device_add_flag (dev_alt, FWUPD_DEVICE_FLAG_INTERNAL);
		fu_device_add_flag (dev_alt, FWUPD_DEVICE_FLAG_REQUIRE_AC);
		fu_device_add_flag (dev_alt, FWUPD_DEVICE_FLAG_LOCKED);
		fu_device_add_icon (dev_alt, "computer");
		fu_device_set_alternate_id (dev_alt, fu_device_get_id (dev));
		fu_device_set_metadata (dev_alt, FU_DEVICE_METADATA_UEFI_DEVICE_KIND, "dell-tpm-firmware");
		fu_device_add_parent_guid (dev_alt, tpm_guid);

		/* If TPM is not owned and at least 1 flash left allow mode switching
		 *
		 * Mode switching is turned on by setting flashes left on alternate
		 * device.
		 */
		if ((out->status & TPM_OWN_MASK) == 0 && out->flashes_left > 0) {
			fu_device_set_flashes_left (dev_alt, out->flashes_left);
		} else {
			fu_device_set_update_error (dev_alt, "mode switch disabled due to TPM ownership");
		}
		if (!fu_device_setup (dev_alt, error))
			return FALSE;
		fu_plugin_device_register (plugin, dev_alt);
	}
	else
		g_debug ("System %04x does not offer TPM modeswitching",
			system_id);

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
			fu_device_add_instance_id (device, device_id);
			fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);
		}
	}
}

void
fu_plugin_init (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	g_autofree gchar *tmp = NULL;

	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	tmp = g_strdup_printf ("%d.%d",
			       smbios_get_library_version_major(),
			       smbios_get_library_version_minor());
	fu_plugin_add_runtime_version (plugin, "com.dell.libsmbios", tmp);
	g_debug ("Using libsmbios %s", tmp);

	data->smi_obj = g_malloc0 (sizeof (FuDellSmiObj));
	if (g_getenv ("FWUPD_DELL_VERBOSE") != NULL)
		g_setenv ("LIBSMBIOS_C_DEBUG_OUTPUT_ALL", "1", TRUE);
	else
		g_setenv ("TSS2_LOG", "esys+error,tcti+none", FALSE);
	if (fu_dell_supported (plugin))
		data->smi_obj->smi = dell_smi_factory (DELL_SMI_DEFAULTS);
	data->smi_obj->fake_smbios = FALSE;
	if (g_getenv ("FWUPD_DELL_FAKE_SMBIOS") != NULL)
		data->smi_obj->fake_smbios = TRUE;

	/* make sure that UEFI plugin is ready to receive devices */
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_RUN_AFTER, "uefi");

	/* our TPM device is upgradable! */
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_BETTER_THAN, "tpm");
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
	g_autofree gchar *sysfsfwdir = NULL;
	g_autofree gchar *esrtdir = NULL;

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

	if (data->smi_obj->smi == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to initialize libsmbios library");
		return FALSE;
	}

	/* If ESRT is not turned on, fwupd will have already created an
	 * unlock device.
	 *
	 * Once unlocked, that will enable flashing capsules here too.
	 */
	sysfsfwdir = fu_common_get_path (FU_PATH_KIND_SYSFSDIR_FW);
	esrtdir = g_build_filename (sysfsfwdir, "efi", "esrt", NULL);
	if (g_file_test (esrtdir, G_FILE_TEST_EXISTS)) {
		data->capsule_supported = TRUE;
	} else {
		g_debug ("UEFI capsule firmware updating not supported");
	}

	return TRUE;
}

static gboolean
fu_plugin_dell_coldplug (FuPlugin *plugin, GError **error)
{
	/* look for switchable TPM */
	if (!fu_plugin_dell_detect_tpm (plugin, error))
		g_debug ("No switchable TPM detected");
	return TRUE;
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	return fu_plugin_dell_coldplug (plugin, error);
}
