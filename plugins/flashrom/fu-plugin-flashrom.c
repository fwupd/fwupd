/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2019 9elements Agency GmbH <patrick.rudolph@9elements.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <libflashrom.h>
#include <string.h>

#include "fu-flashrom-device.h"

#define SELFCHECK_TRUE 1

static void
fu_plugin_flashrom_init(FuPlugin *plugin)
{
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_METADATA_SOURCE, "linux_lockdown");
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_CONFLICTS, "coreboot"); /* obsoleted */
	fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_REQUIRE_HWID);
}

static int
fu_plugin_flashrom_debug_cb(enum flashrom_log_level lvl, const char *fmt, va_list args)
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
	g_autofree gchar *tmp = g_strdup_vprintf(fmt, args);
#pragma clang diagnostic pop
	g_autofree gchar *str = fu_common_strstrip(tmp);
	if (g_strcmp0(str, "OK.") == 0 || g_strcmp0(str, ".") == 0)
		return 0;
	switch (lvl) {
	case FLASHROM_MSG_ERROR:
	case FLASHROM_MSG_WARN:
		g_warning("%s", str);
		break;
	case FLASHROM_MSG_INFO:
		g_debug("%s", str);
		break;
	case FLASHROM_MSG_DEBUG:
	case FLASHROM_MSG_DEBUG2:
		if (g_getenv("FWUPD_FLASHROM_VERBOSE") != NULL)
			g_debug("%s", str);
		break;
	case FLASHROM_MSG_SPEW:
		break;
	default:
		break;
	}
	return 0;
}

static void
fu_plugin_flashrom_device_set_version(FuPlugin *plugin, FuDevice *device)
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
static void
fu_plugin_flashrom_device_set_bios_info(FuPlugin *plugin, FuDevice *device)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	const guint8 *buf;
	gsize bufsz;
	guint32 bios_char = 0x0;
	guint8 bios_sz = 0x0;
	g_autoptr(GBytes) bios_table = NULL;

	/* get SMBIOS info */
	bios_table = fu_context_get_smbios_data(ctx, FU_SMBIOS_STRUCTURE_TYPE_BIOS);
	if (bios_table == NULL)
		return;

	/* ROM size */
	buf = g_bytes_get_data(bios_table, &bufsz);
	if (fu_common_read_uint8_safe(buf, bufsz, 0x9, &bios_sz, NULL)) {
		guint64 firmware_size = (bios_sz + 1) * 64 * 1024;
		fu_device_set_firmware_size_max(device, firmware_size);
	}

	/* BIOS characteristics */
	if (fu_common_read_uint32_safe(buf, bufsz, 0xa, &bios_char, G_LITTLE_ENDIAN, NULL)) {
		if ((bios_char & (1 << 11)) == 0) {
			fu_device_inhibit(device,
					  "bios-characteristics",
					  "Not supported from SMBIOS");
		}
	}
}

static void
fu_plugin_flashrom_device_set_hwids(FuPlugin *plugin, FuDevice *device)
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
fu_plugin_flashrom_add_device(FuPlugin *plugin, FuIfdRegion region, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	const gchar *dmi_vendor;
	const gchar *product = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_PRODUCT_NAME);
	const gchar *vendor = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_MANUFACTURER);
	const gchar *region_str = fu_ifd_region_to_string(region);
	g_autofree gchar *name = g_strdup_printf("%s (%s)", product, region_str);
	g_autoptr(FuDevice) device = fu_flashrom_device_new(ctx, region);

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

	/* use same VendorID logic as with UEFI */
	dmi_vendor = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_BIOS_VENDOR);
	if (dmi_vendor != NULL) {
		g_autofree gchar *vendor_id = g_strdup_printf("DMI:%s", dmi_vendor);
		fu_device_add_vendor_id(FU_DEVICE(device), vendor_id);
	}
	fu_plugin_flashrom_device_set_version(plugin, device);
	fu_plugin_flashrom_device_set_hwids(plugin, device);
	fu_plugin_flashrom_device_set_bios_info(plugin, device);
	if (!fu_device_setup(device, error))
		return NULL;

	/* success */
	fu_plugin_device_add(plugin, device);

	g_object_ref(device);
	return device;
}

static void
fu_plugin_flashrom_device_registered(FuPlugin *plugin, FuDevice *device)
{
	g_autoptr(FuDevice) me_device = NULL;
	const gchar *me_region_str = fu_ifd_region_to_string(FU_IFD_REGION_ME);

	/* we're only interested in a device from intel-spi plugin that corresponds to ME
	 * region of IFD */
	if (g_strcmp0(fu_device_get_plugin(device), "intel_spi") != 0)
		return;
	if (g_strcmp0(fu_device_get_logical_id(device), me_region_str) != 0)
		return;

	me_device = fu_plugin_flashrom_add_device(plugin, FU_IFD_REGION_ME, NULL);
	if (me_device == NULL)
		return;

	/* unlock operation requires device to be locked */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_LOCKED))
		fu_device_add_flag(me_device, FWUPD_DEVICE_FLAG_LOCKED);
}

static gboolean
fu_plugin_flashrom_coldplug(FuPlugin *plugin, GError **error)
{
	g_autoptr(FuDevice) device =
	    fu_plugin_flashrom_add_device(plugin, FU_IFD_REGION_BIOS, error);
	return (device != NULL);
}

static gboolean
fu_plugin_flashrom_startup(FuPlugin *plugin, GError **error)
{
	if (flashrom_init(SELFCHECK_TRUE)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "flashrom initialization error");
		return FALSE;
	}
	flashrom_set_log_callback(fu_plugin_flashrom_debug_cb);
	return TRUE;
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_flashrom_init;
	vfuncs->device_registered = fu_plugin_flashrom_device_registered;
	vfuncs->startup = fu_plugin_flashrom_startup;
	vfuncs->coldplug = fu_plugin_flashrom_coldplug;
}
