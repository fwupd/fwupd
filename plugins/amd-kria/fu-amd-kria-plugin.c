/*
 * Copyright 2024 Advanced Micro Devices Inc.
 * All rights reserved.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * AMD Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-amd-kria-device.h"
#include "fu-amd-kria-image-firmware.h"
#include "fu-amd-kria-persistent-firmware.h"
#include "fu-amd-kria-plugin.h"
#include "fu-amd-kria-som-eeprom.h"

struct _FuAmdKriaPlugin {
	FuPlugin parent_instance;
	gchar *version_a;
	gchar *version_b;
	const gchar *active;
};

G_DEFINE_TYPE(FuAmdKriaPlugin, fu_amd_kria_plugin, FU_TYPE_PLUGIN)

static void
fu_amd_kria_plugin_init(FuAmdKriaPlugin *self)
{
}

static gboolean
fu_amd_kria_plugin_process_image(FuPlugin *plugin, FuDevice *dev, GError **error)
{
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);

	bytes = fu_device_dump_firmware(dev, progress, error);
	if (bytes == NULL)
		return FALSE;
	firmware = fu_amd_kria_image_firmware_new();

	if (!fu_firmware_parse(firmware, bytes, FWUPD_INSTALL_FLAG_NONE, error))
		return FALSE;

	fu_device_set_version(dev, fu_firmware_get_version(firmware));

	return TRUE;
}

static gboolean
fu_amd_kria_plugin_process_persistent(FuPlugin *plugin, FuDevice *dev, GError **error)
{
	FuAmdKriaPlugin *self = FU_AMD_KRIA_PLUGIN(plugin);
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);

	bytes = fu_device_dump_firmware(dev, progress, error);
	if (bytes == NULL)
		return FALSE;
	firmware = fu_amd_kria_persistent_firmware_new();

	if (!fu_firmware_parse(firmware, bytes, FWUPD_INSTALL_FLAG_NONE, error))
		return FALSE;

	if (fu_amd_kria_persistent_firmware_booted_image_a(
		FU_AMD_KRIA_PERSISTENT_FIRMWARE(firmware)))
		self->active = "A";
	else
		self->active = "B";

	return TRUE;
}

static void
fu_amd_kria_plugin_device_registered(FuPlugin *plugin, FuDevice *dev)
{
	FuAmdKriaPlugin *self;
	const gchar *name;
	g_autoptr(GError) error_local = NULL;

	if (g_strcmp0(fu_device_get_plugin(dev), "mtd") != 0)
		return;

	self = FU_AMD_KRIA_PLUGIN(plugin);
	name = fu_device_get_name(dev);
	if (g_strcmp0(name, "Image A") == 0) {
		if (!fu_amd_kria_plugin_process_image(plugin, dev, &error_local))
			g_warning("%s", error_local->message);
		self->version_a = g_strdup(fu_device_get_version(dev));
	} else if (g_strcmp0(name, "Image B") == 0) {
		if (!fu_amd_kria_plugin_process_image(plugin, dev, &error_local))
			g_warning("%s", error_local->message);
		self->version_b = g_strdup(fu_device_get_version(dev));
	} else if (g_strcmp0(name, "Persistent Register") == 0) {
		if (!fu_amd_kria_plugin_process_persistent(plugin, dev, &error_local))
			g_warning("%s", error_local->message);
	}

	/* mark the active partition version on the created KRIA device */
	if (fu_device_get_parent(dev) != NULL && fu_device_get_version(dev) != NULL) {
		if (g_strcmp0(self->active, "A") == 0 && self->version_a != NULL)
			fu_device_set_version(fu_device_get_parent(dev), self->version_a);
		else if (g_strcmp0(self->active, "B") == 0 && self->version_b != NULL)
			fu_device_set_version(fu_device_get_parent(dev), self->version_b);
		fu_device_add_flag(fu_device_get_parent(dev), FWUPD_DEVICE_FLAG_UPDATABLE);
	}

	fu_device_remove_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE);
}

static void
fu_amd_kria_plugin_to_string(FuPlugin *plugin, guint idt, GString *str)
{
	FuAmdKriaPlugin *self = FU_AMD_KRIA_PLUGIN(plugin);

	fwupd_codec_string_append(str, idt, "VersionA", self->version_a);
	fwupd_codec_string_append(str, idt, "VersionB", self->version_b);
	fwupd_codec_string_append(str, idt, "Activeimage", self->active);
}

static gboolean
fu_amd_kria_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
#ifdef __aarch64__
	g_autofree gchar *sysfsfwdir = fu_path_from_kind(FU_PATH_KIND_SYSFSDIR_FW);
	g_autofree gchar *esrt_path = g_build_filename(sysfsfwdir, "efi", "esrt", NULL);

	/* if there is an ESRT use that instead and disable the plugin */
	if (g_file_test(esrt_path, G_FILE_TEST_IS_DIR)) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "system uses UEFI ESRT");
		return FALSE;
	}
	return TRUE;
#else
	g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "only for aarch64");
	return FALSE;
#endif
}

static void
fu_amd_kria_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);

	/* for parsing QSPI in registered callback */
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_AMD_KRIA_IMAGE_FIRMWARE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_AMD_KRIA_PERSISTENT_FIRMWARE);

	/* for reading FRU inventory */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_AMD_KRIA_DEVICE);
	fu_plugin_add_udev_subsystem(plugin, "i2c");
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_AMD_KRIA_SOM_EEPROM);
}

static void
fu_amd_kria_plugin_finalize(GObject *obj)
{
	FuAmdKriaPlugin *self = FU_AMD_KRIA_PLUGIN(obj);

	g_free(self->version_a);
	g_free(self->version_b);
	G_OBJECT_CLASS(fu_amd_kria_plugin_parent_class)->finalize(obj);
}

static void
fu_amd_kria_plugin_class_init(FuAmdKriaPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_amd_kria_plugin_finalize;

	plugin_class->startup = fu_amd_kria_plugin_startup;
	plugin_class->device_registered = fu_amd_kria_plugin_device_registered;
	plugin_class->constructed = fu_amd_kria_plugin_constructed;
	plugin_class->to_string = fu_amd_kria_plugin_to_string;
}
