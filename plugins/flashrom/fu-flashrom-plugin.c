/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2019 9elements Agency GmbH <patrick.rudolph@9elements.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <libflashrom.h>
#include <string.h>

#include "fu-flashrom-device.h"

#define SELFCHECK_TRUE 1

struct FuPluginData {
	struct flashrom_flashctx *flashctx;
	struct flashrom_programmer *flashprog;
	gchar *guid; /* GUID from quirks that activated this plugin */
};

typedef FuPluginData FuFlashromPlugin;
#define FU_FLASHROM_PLUGIN(o) fu_plugin_get_data(FU_PLUGIN(o))

static void
fu_flashrom_plugin_to_string(FuPlugin *plugin, guint idt, GString *str)
{
	FuFlashromPlugin *self = FU_FLASHROM_PLUGIN(plugin);
	if (self->guid != NULL)
		fu_string_append(str, idt, "Guid", self->guid);
}

static int
fu_flashrom_plugin_debug_cb(enum flashrom_log_level lvl, const char *fmt, va_list args)
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
	g_autofree gchar *tmp = g_strdup_vprintf(fmt, args);
#pragma clang diagnostic pop
	g_autofree gchar *str = fu_strstrip(tmp);
	if (g_strcmp0(str, "OK.") == 0 || g_strcmp0(str, ".") == 0)
		return 0;
	switch (lvl) {
	case FLASHROM_MSG_ERROR:
	case FLASHROM_MSG_WARN:
		g_warning("%s", str);
		break;
	case FLASHROM_MSG_INFO:
		g_info("%s", str);
		break;
	case FLASHROM_MSG_DEBUG:
	case FLASHROM_MSG_DEBUG2:
		g_debug("%s", str);
		break;
	case FLASHROM_MSG_SPEW:
	default:
		break;
	}
	return 0;
}

static void
fu_flashrom_plugin_device_set_version(FuPlugin *plugin, FuDevice *device)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	const gchar *version;
	const gchar *version_major;
	const gchar *version_minor;

	/* as-is */
	version = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_BIOS_VERSION);
	if (version != NULL) {
		/* some Lenovo hardware requires a specific prefix for the EC,
		 * so strip it before we use ensure-semver */
		if (strlen(version) > 9 && g_str_has_prefix(version, "CBET"))
			version += 9;

		/* this may not "stick" if there are no numeric chars */
		fu_device_set_version(device, version);
		if (fu_device_get_version(device) != NULL)
			return;
	}

	/* component parts only */
	version_major = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_BIOS_MAJOR_RELEASE);
	version_minor = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_BIOS_MINOR_RELEASE);
	if (version_major != NULL && version_minor != NULL) {
		g_autofree gchar *tmp = g_strdup_printf("%s.%s.0", version_major, version_minor);
		fu_device_set_version(device, tmp);
		return;
	}
}

static gboolean
fu_flashrom_plugin_device_set_bios_info(FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	GBytes *bios_blob;
	const guint8 *buf;
	gsize bufsz;
	guint32 bios_char = 0x0;
	g_autoptr(GPtrArray) bios_tables = NULL;

	/* get SMBIOS info */
	bios_tables = fu_context_get_smbios_data(ctx, FU_SMBIOS_STRUCTURE_TYPE_BIOS, error);
	if (bios_tables == NULL)
		return FALSE;

	/* ROM size if not already been quirked */
	bios_blob = g_ptr_array_index(bios_tables, 0);
	buf = g_bytes_get_data(bios_blob, &bufsz);
	if (fu_device_get_firmware_size_max(device) == 0) {
		guint8 bios_sz = 0x0;
		if (fu_memread_uint8_safe(buf, bufsz, 0x9, &bios_sz, NULL)) {
			guint64 firmware_size = (bios_sz + 1) * 64 * 1024;
			fu_device_set_firmware_size_max(device, firmware_size);
		}
	}

	/* BIOS characteristics */
	if (fu_memread_uint32_safe(buf, bufsz, 0xa, &bios_char, G_LITTLE_ENDIAN, NULL)) {
		if ((bios_char & (1 << 11)) == 0) {
			fu_device_inhibit(device,
					  "bios-characteristics",
					  "Not supported from SMBIOS");
		}
	}
	return TRUE;
}

static void
fu_flashrom_plugin_device_set_hwids(FuPlugin *plugin, FuDevice *device)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	static const gchar *hwids[] = {
	    "HardwareID-3",
	    "HardwareID-4",
	    "HardwareID-5",
	    "HardwareID-6",
	    "HardwareID-10",
	    /* a more useful one for coreboot branch detection */
	    FU_HWIDS_KEY_MANUFACTURER "&" FU_HWIDS_KEY_FAMILY "&" FU_HWIDS_KEY_PRODUCT_NAME
				      "&" FU_HWIDS_KEY_PRODUCT_SKU "&" FU_HWIDS_KEY_BIOS_VENDOR,
	};
	/* don't include FU_HWIDS_KEY_BIOS_VERSION */
	for (guint i = 0; i < G_N_ELEMENTS(hwids); i++) {
		g_autofree gchar *str = NULL;
		str = fu_context_get_hwid_replace_value(ctx, hwids[i], NULL);
		if (str != NULL)
			fu_device_add_instance_id(device, str);
	}
}

static FuDevice *
fu_flashrom_plugin_add_device(FuPlugin *plugin,
			      const gchar *guid,
			      FuIfdRegion region,
			      GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	FuFlashromPlugin *self = FU_FLASHROM_PLUGIN(plugin);
	const gchar *dmi_vendor;
	const gchar *product = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_PRODUCT_NAME);
	const gchar *vendor = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_MANUFACTURER);
	const gchar *region_str = fu_ifd_region_to_string(region);
	g_autofree gchar *name = g_strdup_printf("%s (%s)", product, region_str);
	g_autoptr(FuDevice) device = fu_flashrom_device_new(ctx, self->flashctx, region);
	g_autoptr(GError) error_local = NULL;

	fu_device_set_name(device, name);
	fu_device_set_vendor(device, vendor);

	fu_device_add_instance_str(device, "VENDOR", vendor);
	fu_device_add_instance_str(device, "PRODUCT", product);
	fu_device_add_instance_strup(device, "REGION", region_str);
	if (!fu_device_build_instance_id(device,
					 error,
					 "FLASHROM",
					 "VENDOR",
					 "PRODUCT",
					 "REGION",
					 NULL))
		return NULL;

	/* add this so we can attach board-specific quirks */
	fu_device_add_instance_str(FU_DEVICE(device), "GUID", guid);
	if (!fu_device_build_instance_id(FU_DEVICE(device), error, "FLASHROM", "GUID", NULL))
		return NULL;

	/* use same VendorID logic as with UEFI */
	dmi_vendor = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_BIOS_VENDOR);
	if (dmi_vendor != NULL) {
		g_autofree gchar *vendor_id = g_strdup_printf("DMI:%s", dmi_vendor);
		fu_device_add_vendor_id(FU_DEVICE(device), vendor_id);
	}
	fu_flashrom_plugin_device_set_version(plugin, device);
	fu_flashrom_plugin_device_set_hwids(plugin, device);
	if (!fu_flashrom_plugin_device_set_bios_info(plugin, device, &error_local))
		g_warning("failed to set bios info: %s", error_local->message);
	if (!fu_device_setup(device, error))
		return NULL;

	/* success */
	fu_plugin_device_add(plugin, device);
	return g_steal_pointer(&device);
}

static void
fu_flashrom_plugin_device_registered(FuPlugin *plugin, FuDevice *device)
{
	g_autoptr(FuDevice) me_device = NULL;
	FuFlashromPlugin *self = FU_FLASHROM_PLUGIN(plugin);
	const gchar *me_region_str = fu_ifd_region_to_string(FU_IFD_REGION_ME);

	/* we're only interested in a device from intel-spi plugin that corresponds to ME
	 * region of IFD */
	if (g_strcmp0(fu_device_get_plugin(device), "intel_spi") != 0)
		return;
	if (g_strcmp0(fu_device_get_logical_id(device), me_region_str) != 0)
		return;

	me_device = fu_flashrom_plugin_add_device(plugin, self->guid, FU_IFD_REGION_ME, NULL);
	if (me_device == NULL)
		return;

	/* unlock operation requires device to be locked */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_LOCKED))
		fu_device_add_flag(me_device, FWUPD_DEVICE_FLAG_LOCKED);
}

static gboolean
fu_flashrom_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuFlashromPlugin *self = FU_FLASHROM_PLUGIN(plugin);
	g_autoptr(FuDevice) device =
	    fu_flashrom_plugin_add_device(plugin, self->guid, FU_IFD_REGION_BIOS, error);
	return (device != NULL);
}

/* finds GUID that activated this plugin */
static const gchar *
fu_flashrom_plugin_find_guid(FuPlugin *plugin, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	GPtrArray *hwids = fu_context_get_hwid_guids(ctx);

	for (guint i = 0; i < hwids->len; i++) {
		const gchar *guid = g_ptr_array_index(hwids, i);
		const gchar *plugin_name =
		    fu_context_lookup_quirk_by_id(ctx, guid, FU_QUIRKS_PLUGIN);
		if (g_strcmp0(plugin_name, "flashrom") == 0)
			return guid;
	}

	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no HwIDs found");
	return NULL;
}

static gboolean
fu_flashrom_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	gint rc;
	const gchar *guid;
	FuFlashromPlugin *self = FU_FLASHROM_PLUGIN(plugin);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 5, "find-guid");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 90, "init");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 5, "probe");

	guid = fu_flashrom_plugin_find_guid(plugin, error);
	if (guid == NULL)
		return FALSE;
	fu_progress_step_done(progress);

	self->guid = g_strdup(guid);

	if (flashrom_init(SELFCHECK_TRUE)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "flashrom initialization error");
		return FALSE;
	}
	flashrom_set_log_callback(fu_flashrom_plugin_debug_cb);
	fu_progress_step_done(progress);

	if (flashrom_programmer_init(&self->flashprog, "internal", NULL)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "programmer initialization failed");
		return FALSE;
	}

	rc = flashrom_flash_probe(&self->flashctx, self->flashprog, NULL);
	if (rc == 3) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "flash probe failed: multiple chips were found");
		return FALSE;
	}
	if (rc == 2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "flash probe failed: no chip was found");
		return FALSE;
	}
	if (rc != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "flash probe failed: unknown error");
		return FALSE;
	}
	fu_progress_step_done(progress);

	return TRUE;
}

static gboolean
fu_flashrom_plugin_unlock(FuPlugin *self, FuDevice *device, GError **error)
{
	return fu_flashrom_device_unlock(FU_FLASHROM_DEVICE(device), error);
}

static void
fu_flashrom_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);

	(void)fu_plugin_alloc_data(plugin, sizeof(FuFlashromPlugin));
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_METADATA_SOURCE, "linux_lockdown");
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_CONFLICTS, "coreboot"); /* obsoleted */
	fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_REQUIRE_HWID);
	fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_MEASURE_SYSTEM_INTEGRITY);
}

static void
fu_flashrom_plugin_finalize(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuFlashromPlugin *self = FU_FLASHROM_PLUGIN(plugin);
	if (self->flashctx != NULL)
		flashrom_flash_release(self->flashctx);
	if (self->flashprog != NULL)
		flashrom_programmer_shutdown(self->flashprog);
	g_free(self->guid);

	/* G_OBJECT_CLASS(fu_flashrom_plugin_parent_class)->finalize() not required as modular */
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->constructed = fu_flashrom_plugin_constructed;
	vfuncs->finalize = fu_flashrom_plugin_finalize;
	vfuncs->to_string = fu_flashrom_plugin_to_string;
	vfuncs->device_registered = fu_flashrom_plugin_device_registered;
	vfuncs->startup = fu_flashrom_plugin_startup;
	vfuncs->coldplug = fu_flashrom_plugin_coldplug;
	vfuncs->unlock = fu_flashrom_plugin_unlock;
}
