/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 * Copyright 2024 Advanced Micro Devices Inc.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * AMD Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include <string.h>

#include "fu-amd-kria-device.h"
#include "fu-amd-kria-som-eeprom.h"

typedef struct {
	FuVolume *esp;
	FuDeviceLocker *esp_locker;
	gchar *eeprom_address;
} FuAmdKriaDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuAmdKriaDevice, fu_amd_kria_device, FU_TYPE_I2C_DEVICE)

#define GET_PRIVATE(o) (fu_amd_kria_device_get_instance_private(o))

static void
fu_amd_kria_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuAmdKriaDevice *self = FU_AMD_KRIA_DEVICE(device);
	FuAmdKriaDevicePrivate *priv = GET_PRIVATE(self);
	fwupd_codec_string_append(str, idt, "AmdKriaEepromAddr", priv->eeprom_address);
}

static gboolean
fu_amd_kria_device_prepare(FuDevice *device,
			   FuProgress *progress,
			   FwupdInstallFlags flags,
			   GError **error)
{
	FuAmdKriaDevice *self = FU_AMD_KRIA_DEVICE(device);
	FuAmdKriaDevicePrivate *priv = GET_PRIVATE(self);

	priv->esp_locker = fu_volume_locker(priv->esp, error);
	if (priv->esp_locker == NULL)
		return FALSE;

	return TRUE;
}

static gboolean
fu_amd_kria_device_cleanup(FuDevice *device,
			   FuProgress *progress,
			   FwupdInstallFlags flags,
			   GError **error)
{
	FuAmdKriaDevice *self = FU_AMD_KRIA_DEVICE(device);
	FuAmdKriaDevicePrivate *priv = GET_PRIVATE(self);

	if (!fu_device_locker_close(priv->esp_locker, error))
		return FALSE;
	g_clear_object(&priv->esp_locker);

	return TRUE;
}

static gboolean
fu_amd_kria_device_write_firmware(FuDevice *device,
				  FuFirmware *firmware,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuAmdKriaDevice *self = FU_AMD_KRIA_DEVICE(device);
	FuAmdKriaDevicePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *cod_path = NULL;
	g_autoptr(GBytes) fw = NULL;

	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;
	cod_path = g_build_filename(fu_volume_get_mount_point(priv->esp),
				    "EFI",
				    "UpdateCapsule",
				    "fwupd.cap",
				    NULL);
	g_debug("using %s for capsule", cod_path);
	if (!fu_path_mkdir_parent(cod_path, error))
		return FALSE;
	if (!fu_bytes_set_contents(cod_path, fw, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_amd_kria_device_set_quirk_kv(FuDevice *device,
				const gchar *key,
				const gchar *value,
				GError **error)
{
	FuAmdKriaDevice *self = FU_AMD_KRIA_DEVICE(device);
	FuAmdKriaDevicePrivate *priv = GET_PRIVATE(self);

	if (g_strcmp0(key, "AmdKriaEepromAddr") == 0) {
		priv->eeprom_address = g_strdup(value);
		return TRUE;
	}

	return TRUE;
}

static gboolean
fu_amd_kria_device_probe(FuDevice *device, GError **error)
{
	FuAmdKriaDevice *self = FU_AMD_KRIA_DEVICE(device);
	FuAmdKriaDevicePrivate *priv = GET_PRIVATE(self);
	const gchar *tmp;
	g_auto(GStrv) of_name = NULL;

	/* FuI2cDevice->probe */
	if (!FU_DEVICE_CLASS(fu_amd_kria_device_parent_class)->probe(device, error))
		return FALSE;

	/*
	 * Fetch the OF_FULLNAME udev property and look for the I2C address in it
	 * sample format: OF_FULLNAME=/axi/i2c@ff030000/eeprom@50
	 */
	tmp = fu_device_get_physical_id(device);
	of_name = fu_strsplit(tmp, strlen(tmp), "@", -1);
	if (of_name == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no '@' found in %s",
			    tmp);
		return FALSE;
	}
	tmp = of_name[g_strv_length(of_name) - 1];

	if (g_strcmp0(priv->eeprom_address, tmp) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "invalid device");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_amd_kria_device_setup(FuDevice *device, GError **error)
{
	const gchar *devpath = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device));
	gsize bufsz = 0;
	g_autofree gchar *buf = NULL;
	g_autofree gchar *path = g_build_path("/", devpath, "eeprom", NULL);
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GError) error_esp = NULL;
	g_autoptr(GBytes) bytes = NULL;

	if (!g_file_get_contents(path, &buf, &bufsz, error))
		return FALSE;

	/* parse the eeprom */
	bytes = g_bytes_new(buf, bufsz);
	firmware = fu_amd_kria_som_eeprom_new();
	if (!fu_firmware_parse(firmware, bytes, FWUPD_INSTALL_FLAG_NONE, error))
		return FALSE;

	/* build instance IDs from EEPROM data */
	fu_device_set_vendor(
	    device,
	    fu_amd_kria_som_eeprom_get_manufacturer(FU_AMD_KRIA_SOM_EEPROM(firmware)));
	fu_device_build_vendor_id(device, "DMI", fu_device_get_vendor(device));
	fu_device_add_instance_str(device, "VENDOR", fu_device_get_vendor(device));
	fu_device_add_instance_str(
	    device,
	    "PRODUCT",
	    fu_amd_kria_som_eeprom_get_product_name(FU_AMD_KRIA_SOM_EEPROM(firmware)));
	fu_device_set_serial(
	    device,
	    fu_amd_kria_som_eeprom_get_serial_number(FU_AMD_KRIA_SOM_EEPROM(firmware)));
	if (!fu_device_build_instance_id(device, error, "UEFI", "VENDOR", NULL))
		return FALSE;
	if (!fu_device_build_instance_id(device, error, "UEFI", "VENDOR", "PRODUCT", NULL))
		return FALSE;

	return TRUE;
}

static void
fu_amd_kria_device_constructed(GObject *obj)
{
	FuAmdKriaDevice *self = FU_AMD_KRIA_DEVICE(obj);
	FuAmdKriaDevicePrivate *priv = GET_PRIVATE(self);
	FuContext *ctx;
	g_autoptr(GError) error_esp = NULL;

	/* setup the default ESP */
	ctx = fu_device_get_context(FU_DEVICE(obj));
	priv->esp = fu_context_get_default_esp(ctx, &error_esp);
	if (priv->esp == NULL)
		fu_device_inhibit(FU_DEVICE(obj), "no-esp", error_esp->message);
}

static void
fu_amd_kria_device_init(FuAmdKriaDevice *self)
{
	fu_device_set_name(FU_DEVICE(self), "System Firmware");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_logical_id(FU_DEVICE(self), "U-Boot");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_add_icon(FU_DEVICE(self), "computer");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_set_summary(FU_DEVICE(self), "AMD Kria device (Updated via capsule-on-disk)");
	fu_device_add_protocol(FU_DEVICE(self), "org.uefi.capsule");
}

static void
fu_amd_kria_device_finalize(GObject *object)
{
	FuAmdKriaDevice *self = FU_AMD_KRIA_DEVICE(object);
	FuAmdKriaDevicePrivate *priv = GET_PRIVATE(self);

	if (priv->esp != NULL)
		g_object_unref(priv->esp);
	if (priv->esp_locker != NULL)
		g_object_unref(priv->esp_locker);
	g_free(priv->eeprom_address);

	G_OBJECT_CLASS(fu_amd_kria_device_parent_class)->finalize(object);
}

static void
fu_amd_kria_device_class_init(FuAmdKriaDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	object_class->finalize = fu_amd_kria_device_finalize;
	object_class->constructed = fu_amd_kria_device_constructed;

	device_class->set_quirk_kv = fu_amd_kria_device_set_quirk_kv;
	device_class->setup = fu_amd_kria_device_setup;
	device_class->prepare = fu_amd_kria_device_prepare;
	device_class->cleanup = fu_amd_kria_device_cleanup;
	device_class->probe = fu_amd_kria_device_probe;
	device_class->write_firmware = fu_amd_kria_device_write_firmware;
	device_class->to_string = fu_amd_kria_device_to_string;
}
