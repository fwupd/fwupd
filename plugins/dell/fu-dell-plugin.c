/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fcntl.h>
#include <glib/gstdio.h>
#include <smbios_c/system_info.h>
#include <string.h>
#include <tss2/tss2_esys.h>
#include <unistd.h>

#include "fu-dell-plugin.h"
#include "fu-plugin-dell.h"

#define BIOS_SETTING_BIOS_DOWNGRADE "com.dell-wmi-sysman.AllowBiosDowngrade"

struct _FuDellPlugin {
	FuPlugin parent_instance;
	FuDellSmiObj *smi_obj;
	guint16 fake_vid;
	guint16 fake_pid;
	gboolean capsule_supported;
};

G_DEFINE_TYPE(FuDellPlugin, fu_dell_plugin, FU_TYPE_PLUGIN)

/* These are used to indicate the status of a previous DELL flash */
#define DELL_SUCCESS		 0x0000
#define DELL_CONSISTENCY_FAIL	 0x0001
#define DELL_FLASH_MEMORY_FAIL	 0x0002
#define DELL_FLASH_NOT_READY	 0x0003
#define DELL_FLASH_DISABLED	 0x0004
#define DELL_BATTERY_MISSING	 0x0005
#define DELL_BATTERY_DEAD	 0x0006
#define DELL_AC_MISSING		 0x0007
#define DELL_CANT_SET_12V	 0x0008
#define DELL_CANT_UNSET_12V	 0x0009
#define DELL_FAILURE_BLOCK_ERASE 0x000A
#define DELL_GENERAL_FAILURE	 0x000B
#define DELL_DATA_MISCOMPARE	 0x000C
#define DELL_IMAGE_MISSING	 0x000D
#define DELL_DID_NOTHING	 0xFFFF

struct da_structure {
	guint8 type;
	guint8 length;
	guint16 handle;
	guint16 cmd_address;
	guint8 cmd_code;
	guint32 supported_cmds;
	guint8 *tokens;
} __attribute__((packed));

/**
 * Dell device types to run
 */
static guint8 enclosure_allowlist[] = {0x03, /* desktop */
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
fu_dell_get_system_id(FuPlugin *plugin)
{
	FuDellPlugin *self = FU_DELL_PLUGIN(plugin);
	FuContext *ctx = fu_plugin_get_context(plugin);
	const gchar *system_id_str = NULL;
	guint16 system_id = 0;
	gchar *endptr = NULL;

	/* don't care for test suite */
	if (self->smi_obj->fake_smbios)
		return 0;

	system_id_str = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_PRODUCT_SKU);
	if (system_id_str != NULL)
		system_id = g_ascii_strtoull(system_id_str, &endptr, 16);

	return system_id;
}

static gboolean
fu_dell_supported(FuPlugin *plugin, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	FuSmbiosChassisKind chassis_kind = fu_context_get_chassis_kind(ctx);
	g_autoptr(GBytes) de_table = NULL;
	g_autoptr(GBytes) da_table = NULL;
	guint8 value = 0;
	struct da_structure da_values = {0x0};

	/* make sure that Dell SMBIOS methods are available */
	de_table = fu_context_get_smbios_data(ctx, 0xDE, error);
	if (de_table == NULL)
		return FALSE;
	if (!fu_memread_uint8_safe(g_bytes_get_data(de_table, NULL),
				   g_bytes_get_size(de_table),
				   0x0,
				   &value,
				   error)) {
		g_prefix_error(error, "invalid DE data: ");
		return FALSE;
	}
	if (value != 0xDE) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid DE data");
		return FALSE;
	}

	da_table = fu_context_get_smbios_data(ctx, 0xDA, error);
	if (da_table == NULL)
		return FALSE;
	if (!fu_memcpy_safe((guint8 *)&da_values,
			    sizeof(da_values),
			    0x0, /* dst */
			    g_bytes_get_data(da_table, NULL),
			    g_bytes_get_size(da_table),
			    0x0, /* src */
			    sizeof(da_values),
			    error)) {
		g_prefix_error(error, "unable to access flash interface: ");
		return FALSE;
	}
	if (!(da_values.supported_cmds & (1 << DACI_FLASH_INTERFACE_CLASS))) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
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
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "chassis invalid");
	return FALSE;
}

void
fu_dell_plugin_inject_fake_data(FuPlugin *plugin,
				guint32 *output,
				guint16 vid,
				guint16 pid,
				guint8 *buf)
{
	FuDellPlugin *self = FU_DELL_PLUGIN(plugin);
	if (!self->smi_obj->fake_smbios)
		return;
	for (guint i = 0; i < 4; i++)
		self->smi_obj->output[i] = output[i];
	self->fake_vid = vid;
	self->fake_pid = pid;
	self->smi_obj->fake_buffer = buf;
}

static gboolean
fu_dell_plugin_capsule_supported(FuPlugin *plugin)
{
	FuDellPlugin *self = FU_DELL_PLUGIN(plugin);
	return self->smi_obj->fake_smbios || self->capsule_supported;
}

static gboolean
fu_dell_plugin_get_results(FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autoptr(GBytes) de_table = NULL;
	const gchar *tmp = NULL;
	guint16 completion_code = 0;

	de_table = fu_context_get_smbios_data(ctx, 0xDE, error);
	if (de_table == NULL)
		return FALSE;

	/* look at byte offset 0x06  for identifier meaning completion code */
	if (!fu_memread_uint16_safe(g_bytes_get_data(de_table, NULL),
				    g_bytes_get_size(de_table),
				    0x06,
				    &completion_code,
				    G_LITTLE_ENDIAN,
				    error)) {
		g_prefix_error(error, "unable to read results of %s: ", fu_device_get_name(device));
		return FALSE;
	}
	if (completion_code == DELL_SUCCESS) {
		fu_device_set_update_state(device, FWUPD_UPDATE_STATE_SUCCESS);
	} else {
		FwupdUpdateState update_state = FWUPD_UPDATE_STATE_FAILED;
		switch (completion_code) {
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
			tmp = "Flash programming is currently disabled on the system, or the "
			      "voltage is low.";
			break;
		case DELL_BATTERY_MISSING:
			tmp = "A battery must be installed for the operation to complete.";
			update_state = FWUPD_UPDATE_STATE_FAILED_TRANSIENT;
			break;
		case DELL_BATTERY_DEAD:
			tmp = "A fully-charged battery must be present for the operation to "
			      "complete.";
			update_state = FWUPD_UPDATE_STATE_FAILED_TRANSIENT;
			break;
		case DELL_AC_MISSING:
			tmp = "An external power adapter must be connected for the operation to "
			      "complete.";
			update_state = FWUPD_UPDATE_STATE_FAILED_TRANSIENT;
			break;
		case DELL_CANT_SET_12V:
			tmp = "The 12V required to program the flash-memory could not be set.";
			break;
		case DELL_CANT_UNSET_12V:
			tmp = "The 12V required to program the flash-memory could not be removed.";
			break;
		case DELL_FAILURE_BLOCK_ERASE:
			tmp = "A flash-memory failure occurred during a block-erase operation.";
			break;
		case DELL_GENERAL_FAILURE:
			tmp = "A general failure occurred during the flash programming.";
			break;
		case DELL_DATA_MISCOMPARE:
			tmp = "A data miscompare error occurred during the flash programming.";
			break;
		case DELL_IMAGE_MISSING:
			tmp = "The image could not be found in memory, i.e. the header could not "
			      "be located.";
			break;
		case DELL_DID_NOTHING:
			tmp = "No update operation has been performed on the system.";
			break;
		default:
			break;
		}
		fu_device_set_update_state(device, update_state);
		if (tmp != NULL)
			fu_device_set_update_error(device, tmp);
	}

	return TRUE;
}

static void
Esys_Finalize_autoptr_cleanup(ESYS_CONTEXT *esys_context)
{
	Esys_Finalize(&esys_context);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESYS_CONTEXT, Esys_Finalize_autoptr_cleanup)

static gchar *
fu_dell_plugin_get_tpm_capability(ESYS_CONTEXT *ctx, guint32 query)
{
	TSS2_RC rc;
	guint32 val;
	gchar result[5] = {'\0'};
	g_autofree TPMS_CAPABILITY_DATA *capability = NULL;
	rc = Esys_GetCapability(ctx,
				ESYS_TR_NONE,
				ESYS_TR_NONE,
				ESYS_TR_NONE,
				TPM2_CAP_TPM_PROPERTIES,
				query,
				1,
				NULL,
				&capability);
	if (rc != TSS2_RC_SUCCESS) {
		g_debug("capability request failed for query %x", query);
		return NULL;
	}
	if (capability->data.tpmProperties.count == 0) {
		g_debug("no properties returned for query %x", query);
		return NULL;
	}
	if (capability->data.tpmProperties.tpmProperty[0].property != query) {
		g_debug("wrong query returned (got %x expected %x)",
			capability->data.tpmProperties.tpmProperty[0].property,
			query);
		return NULL;
	}

	val = GUINT32_FROM_BE(capability->data.tpmProperties.tpmProperty[0].value);
	memcpy(result, (gchar *)&val, 4);

	/* convert non-ASCII into spaces */
	for (guint i = 0; i < 4; i++) {
		if (!g_ascii_isgraph(result[i]) && result[i] != '\0')
			result[i] = 0x20;
	}

	return fu_strstrip(result);
}

static gboolean
fu_dell_plugin_add_tpm_model(FuDevice *dev, GError **error)
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

	rc = Esys_Initialize(&ctx, NULL, NULL);
	if (rc != TSS2_RC_SUCCESS) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "failed to initialize TPM library");
		return FALSE;
	}
	rc = Esys_Startup(ctx, TPM2_SU_CLEAR);
	if (rc != TSS2_RC_SUCCESS) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "failed to initialize TPM");
		return FALSE;
	}

	/* lookup guaranteed details from TPM */
	family = fu_dell_plugin_get_tpm_capability(ctx, TPM2_PT_FAMILY_INDICATOR);
	if (family == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "failed to read TPM family");
		return FALSE;
	}
	manufacturer = fu_dell_plugin_get_tpm_capability(ctx, TPM2_PT_MANUFACTURER);
	if (manufacturer == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "failed to read TPM manufacturer");
		return FALSE;
	}
	vendor1 = fu_dell_plugin_get_tpm_capability(ctx, TPM2_PT_VENDOR_STRING_1);
	if (vendor1 == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "failed to read TPM vendor string");
		return FALSE;
	}
	fu_device_set_metadata(dev, "TpmFamily", family);

	/* these are not guaranteed by spec and may be NULL */
	vendor2 = fu_dell_plugin_get_tpm_capability(ctx, TPM2_PT_VENDOR_STRING_2);
	vendor3 = fu_dell_plugin_get_tpm_capability(ctx, TPM2_PT_VENDOR_STRING_3);
	vendor4 = fu_dell_plugin_get_tpm_capability(ctx, TPM2_PT_VENDOR_STRING_4);

	/* add GUIDs to daemon */
	v1 = g_strjoin("-", base, family, manufacturer, vendor1, NULL);
	v1_v2 = g_strconcat(v1, vendor2, NULL);
	v1_v2_v3 = g_strconcat(v1_v2, vendor3, NULL);
	v1_v2_v3_v4 = g_strconcat(v1_v2_v3, vendor4, NULL);
	fu_device_add_instance_id(dev, v1);
	fu_device_add_instance_id(dev, v1_v2);
	fu_device_add_instance_id(dev, v1_v2_v3);
	fu_device_add_instance_id(dev, v1_v2_v3_v4);

	return TRUE;
}

gboolean
fu_dell_plugin_detect_tpm(FuPlugin *plugin, GError **error)
{
	FuDellPlugin *self = FU_DELL_PLUGIN(plugin);
	FuContext *ctx = fu_plugin_get_context(plugin);
	const gchar *tpm_mode;
	guint16 system_id = 0;
	g_autofree gchar *pretty_tpm_name = NULL;
	g_autofree gchar *tpm_guid = NULL;
	g_autofree gchar *tpm_guid_raw = NULL;
	g_autofree gchar *version_str = NULL;
	struct tpm_status *out = NULL;
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(GError) error_tss = NULL;

	fu_dell_clear_smi(self->smi_obj);
	out = (struct tpm_status *)self->smi_obj->output;

	/* execute TPM Status Query */
	self->smi_obj->input[0] = DACI_FLASH_ARG_TPM;
	if (!fu_dell_execute_simple_smi(self->smi_obj,
					DACI_FLASH_INTERFACE_CLASS,
					DACI_FLASH_INTERFACE_SELECT)) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "cannot query");
		return FALSE;
	}

	if (out->ret != 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "failed to query system for TPM information: 0x%x",
			    (guint)out->ret);
		return FALSE;
	}
	/* HW version is output in second /input/ arg
	 * it may be relevant as next gen TPM is enabled
	 */
	g_debug("TPM HW version: 0x%x", self->smi_obj->input[1]);
	g_debug("TPM Status: 0x%x", out->status);

	/* test TPM enabled (Bit 0) */
	if (!(out->status & TPM_EN_MASK)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "TPM not enabled: 0x%x",
			    out->status);
		return FALSE;
	}

	/* test TPM mode to determine current mode */
	if (((out->status & TPM_TYPE_MASK) >> 8) == TPM_1_2_MODE) {
		tpm_mode = "1.2";
	} else if (((out->status & TPM_TYPE_MASK) >> 8) == TPM_2_0_MODE) {
		tpm_mode = "2.0";
	} else {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "unable to determine TPM mode");
		return FALSE;
	}

	system_id = fu_dell_get_system_id(plugin);
	if (!self->smi_obj->fake_smbios && system_id == 0) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "no system ID");
		return FALSE;
	}

	tpm_guid_raw = g_strdup_printf("%04x-%s", system_id, tpm_mode);
	tpm_guid = fwupd_guid_hash_string(tpm_guid_raw);

	g_debug("Creating TPM GUID %s", tpm_guid_raw);
	version_str = fu_version_from_uint32(out->fw_version, FWUPD_VERSION_FORMAT_QUAD);

	/* make it clear that the TPM is a discrete device of the product */
	pretty_tpm_name = g_strdup_printf("TPM %s", tpm_mode);

	/* build Standard device nodes */
	dev = fu_device_new(ctx);
	fu_device_set_physical_id(dev, "DEVNAME=/dev/tpm0");
	fu_device_set_logical_id(dev, "UEFI");
	fu_device_add_instance_id(dev, tpm_guid_raw);
	fu_device_add_instance_id(dev, "system-tpm");
	fu_device_set_vendor(dev, "Dell Inc.");
	fu_device_add_vendor_id(dev, "PCI:0x1028");
	fu_device_set_name(dev, pretty_tpm_name);
	fu_device_set_summary(dev, "Platform TPM device");
	fu_device_set_version_format(dev, FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_version(dev, version_str);
	fu_device_add_flag(dev, FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(dev, FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_icon(dev, "computer");
	fu_device_set_metadata(dev, FU_DEVICE_METADATA_UEFI_DEVICE_KIND, "dell-tpm-firmware");
	if ((out->status & TPM_OWN_MASK) == 0 && out->flashes_left > 0) {
		if (fu_dell_plugin_capsule_supported(plugin)) {
			fu_device_add_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE);
			fu_device_add_flag(dev, FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
		}
		fu_device_set_flashes_left(dev, out->flashes_left);
	} else {
		fu_device_set_update_error(dev, "Updating disabled due to TPM ownership");
	}
	/* build GUIDs from TSS strings */
	if (!fu_dell_plugin_add_tpm_model(dev, &error_tss))
		g_debug("could not build instances: %s", error_tss->message);

	if (!fu_device_setup(dev, error))
		return FALSE;
	fu_plugin_device_register(plugin, dev);
	fu_plugin_add_report_metadata(plugin,
				      "TpmFamily",
				      fu_device_get_metadata(dev, "TpmFamily"));

	return TRUE;
}

static void
fu_dell_plugin_device_registered(FuPlugin *plugin, FuDevice *device)
{
	/* thunderbolt plugin */
	if (g_strcmp0(fu_device_get_plugin(device), "thunderbolt") == 0 &&
	    fu_device_has_flag(device, FWUPD_DEVICE_FLAG_INTERNAL)) {
		/* fix VID/DID of safe mode devices */
		if (fu_device_get_metadata_boolean(device, FU_DEVICE_METADATA_TBT_IS_SAFE_MODE)) {
			g_autofree gchar *vendor_id = NULL;
			g_autofree gchar *device_id = NULL;
			guint16 system_id = 0;

			vendor_id = g_strdup("TBT:0x00D4");
			system_id = fu_dell_get_system_id(plugin);
			if (system_id == 0)
				return;
			/* the kernel returns lowercase in sysfs, need to match it */
			device_id = g_strdup_printf("TBT-%04x%04x", 0x00d4u, (unsigned)system_id);
			fu_device_add_vendor_id(device, vendor_id);
			fu_device_add_instance_id(device, device_id);
			fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
		}
	}
}

static void
fu_dell_plugin_to_string(FuPlugin *plugin, guint idt, GString *str)
{
	FuDellPlugin *self = FU_DELL_PLUGIN(plugin);
	fu_string_append_kb(str, idt, "CapsuleSupported", self->capsule_supported);
}

static gboolean
fu_dell_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuDellPlugin *self = FU_DELL_PLUGIN(plugin);
	g_autofree gchar *sysfsfwdir = NULL;
	g_autofree gchar *esrtdir = NULL;

	if (self->smi_obj->fake_smbios) {
		g_debug("Called with fake SMBIOS implementation. "
			"We're ignoring test for SMBIOS table and ESRT. "
			"Individual calls will need to be properly staged.");
		return TRUE;
	}

	if (!fu_dell_supported(plugin, error)) {
		g_prefix_error(error, "firmware updating not supported: ");
		return FALSE;
	}

	self->smi_obj->smi = dell_smi_factory(DELL_SMI_DEFAULTS);
	if (self->smi_obj->smi == NULL) {
		g_set_error(error,
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
	sysfsfwdir = fu_path_from_kind(FU_PATH_KIND_SYSFSDIR_FW);
	esrtdir = g_build_filename(sysfsfwdir, "efi", "esrt", NULL);
	if (g_file_test(esrtdir, G_FILE_TEST_EXISTS))
		self->capsule_supported = TRUE;

	/* capsules not supported */
	if (!fu_dell_plugin_capsule_supported(plugin)) {
		fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_USER_WARNING);
		fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE);
		fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_CAPSULES_UNSUPPORTED);
	}

	return TRUE;
}

static gboolean
fu_dell_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	g_autoptr(GError) error_local = NULL;

	/* look for switchable TPM */
	if (!fu_dell_plugin_detect_tpm(plugin, &error_local))
		g_debug("no switchable TPM detected: %s", error_local->message);

	/* always success */
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
	self->smi_obj = g_malloc0(sizeof(FuDellSmiObj));
	if (g_getenv("FWUPD_DELL_VERBOSE") != NULL)
		(void)g_setenv("LIBSMBIOS_C_DEBUG_OUTPUT_ALL", "1", TRUE);
	else
		(void)g_setenv("TSS2_LOG", "esys+none,tcti+none", FALSE);
}

static void
fu_dell_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	FuDellPlugin *self = FU_DELL_PLUGIN(plugin);
	g_autofree gchar *tmp = NULL;

	tmp = g_strdup_printf("%d.%d",
			      smbios_get_library_version_major(),
			      smbios_get_library_version_minor());
	fu_context_add_runtime_version(ctx, "com.dell.libsmbios", tmp);
	g_debug("Using libsmbios %s", tmp);

	self->smi_obj->fake_smbios = FALSE;
	if (g_getenv("FWUPD_DELL_FAKE_SMBIOS") != NULL)
		self->smi_obj->fake_smbios = TRUE;

	/* make sure that UEFI plugin is ready to receive devices */
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_RUN_AFTER, "uefi_capsule");

	/* our TPM device is upgradable! */
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_BETTER_THAN, "tpm");
}

static void
fu_dell_finalize(GObject *obj)
{
	FuDellPlugin *self = FU_DELL_PLUGIN(obj);
	if (self->smi_obj->smi)
		dell_smi_obj_free(self->smi_obj->smi);
	g_free(self->smi_obj);
	G_OBJECT_CLASS(fu_dell_plugin_parent_class)->finalize(obj);
}

static void
fu_dell_plugin_class_init(FuDellPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_dell_finalize;
	plugin_class->constructed = fu_dell_plugin_constructed;
	plugin_class->to_string = fu_dell_plugin_to_string;
	plugin_class->startup = fu_dell_plugin_startup;
	plugin_class->coldplug = fu_dell_plugin_coldplug;
	plugin_class->device_registered = fu_dell_plugin_device_registered;
	plugin_class->get_results = fu_dell_plugin_get_results;
	plugin_class->add_security_attrs = fu_dell_plugin_add_security_attrs;
}
