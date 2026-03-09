/*
 * Copyright 2022 Andrii Dushko <andrii.dushko@developex.net>
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-corsair-common.h"
#include "fu-corsair-device.h"
#include "fu-corsair-subdevice.h"

struct _FuCorsairSubdevice {
	FuDevice parent_instance;
};

G_DEFINE_TYPE(FuCorsairSubdevice, fu_corsair_subdevice, FU_TYPE_DEVICE)

static gchar *
fu_corsair_subdevice_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_corsair_version_from_uint32(version_raw);
}

static gboolean
fu_corsair_subdevice_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuDevice *proxy;

	proxy = fu_device_get_proxy(device, error);
	if (proxy == NULL)
		return FALSE;
	if (!fu_corsair_device_reconnect_subdevice(FU_CORSAIR_DEVICE(proxy), error))
		return FALSE;
	return fu_corsair_device_write_firmware_full(FU_CORSAIR_DEVICE(proxy),
						     FU_CORSAIR_DESTINATION_SUBDEVICE,
						     firmware,
						     progress,
						     error);
}

static gboolean
fu_corsair_subdevice_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevice *proxy;

	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;
	proxy = fu_device_get_proxy(device, error);
	if (proxy == NULL)
		return FALSE;
	if (fu_device_has_private_flag(proxy, FU_CORSAIR_DEVICE_FLAG_LEGACY_ATTACH)) {
		if (!fu_corsair_device_legacy_attach(FU_CORSAIR_DEVICE(proxy),
						     FU_CORSAIR_DESTINATION_SUBDEVICE,
						     error))
			return FALSE;
	} else {
		if (!fu_corsair_device_set_mode(FU_CORSAIR_DEVICE(proxy),
						FU_CORSAIR_DESTINATION_SUBDEVICE,
						FU_CORSAIR_DEVICE_MODE_APPLICATION,
						error))
			return FALSE;
	}
	return fu_corsair_device_reconnect_subdevice(FU_CORSAIR_DEVICE(proxy), error);
}

static gboolean
fu_corsair_subdevice_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevice *proxy;
	g_autoptr(GError) error_local = NULL;

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;
	proxy = fu_device_get_proxy(device, error);
	if (proxy == NULL)
		return FALSE;
	if (!fu_corsair_device_set_mode(FU_CORSAIR_DEVICE(proxy),
					FU_CORSAIR_DESTINATION_SUBDEVICE,
					FU_CORSAIR_DEVICE_MODE_BOOTLOADER,
					&error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_READ)) {
			g_debug("ignoring: %s", error_local->message);
		} else {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
	}

	/* success */
	fu_device_sleep(device, 4000);
	return TRUE;
}

static gboolean
fu_corsair_subdevice_ensure_mode(FuCorsairSubdevice *self, GError **error)
{
	FuDevice *proxy;
	guint32 mode = 0;

	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;
	if (!fu_corsair_device_get_property(FU_CORSAIR_DEVICE(proxy),
					    FU_CORSAIR_DESTINATION_SUBDEVICE,
					    FU_CORSAIR_DEVICE_PROPERTY_MODE,
					    &mode,
					    error))
		return FALSE;
	if (mode == FU_CORSAIR_DEVICE_MODE_BOOTLOADER)
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static gboolean
fu_corsair_subdevice_ensure_version(FuCorsairSubdevice *self, GError **error)
{
	FuDevice *proxy;
	guint32 version_raw = 0;

	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;
	if (!fu_corsair_device_get_property(FU_CORSAIR_DEVICE(proxy),
					    FU_CORSAIR_DESTINATION_SUBDEVICE,
					    FU_CORSAIR_DEVICE_PROPERTY_VERSION,
					    &version_raw,
					    error)) {
		g_prefix_error_literal(error, "cannot get version: ");
		return FALSE;
	}
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		if (version_raw == G_MAXUINT32)
			version_raw = 0;
	}

	/* success */
	fu_device_set_version_raw(FU_DEVICE(self), version_raw);
	return TRUE;
}

static gboolean
fu_corsair_subdevice_ensure_battery_level(FuCorsairSubdevice *self, GError **error)
{
	FuDevice *proxy;
	guint32 battery_level = 0;

	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;
	if (!fu_corsair_device_get_property(FU_CORSAIR_DEVICE(proxy),
					    FU_CORSAIR_DESTINATION_SUBDEVICE,
					    FU_CORSAIR_DEVICE_PROPERTY_BATTERY_LEVEL,
					    &battery_level,
					    error)) {
		g_prefix_error_literal(error, "cannot get battery level: ");
		return FALSE;
	}
	fu_device_set_battery_level(FU_DEVICE(self), battery_level / 10);
	return TRUE;
}

static gboolean
fu_corsair_subdevice_ensure_bootloader_version(FuCorsairSubdevice *self, GError **error)
{
	FuDevice *proxy;
	guint32 version_raw = 0;
	g_autofree gchar *version_str = NULL;

	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;
	if (!fu_corsair_device_get_property(FU_CORSAIR_DEVICE(proxy),
					    FU_CORSAIR_DESTINATION_SUBDEVICE,
					    FU_CORSAIR_DEVICE_PROPERTY_BOOTLOADER_VERSION,
					    &version_raw,
					    error)) {
		g_prefix_error_literal(error, "cannot get bootloader version: ");
		return FALSE;
	}

	version_str = fu_corsair_subdevice_convert_version(FU_DEVICE(self), version_raw);
	fu_device_set_version_bootloader(FU_DEVICE(self), version_str);
	return TRUE;
}

static gboolean
fu_corsair_subdevice_setup(FuDevice *device, GError **error)
{
	FuCorsairSubdevice *self = FU_CORSAIR_SUBDEVICE(device);
	FuDevice *proxy;
	g_autoptr(GString) name = NULL;

	/* use the receiver name with a tweak */
	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;
	name = g_string_new(fu_device_get_name(proxy));
	g_string_replace(name, "Dongle", "Mouse", -1);
	fu_device_set_name(device, name->str);

	/* this is non-standard */
	fu_device_add_instance_str(device, "DEV", "MOUSE");
	if (!fu_device_build_instance_id(device, error, "USB", "VID", "PID", "DEV", NULL))
		return FALSE;

	/* for the subdevice only */
	if (!fu_corsair_subdevice_ensure_mode(self, error))
		return FALSE;
	if (!fu_corsair_subdevice_ensure_version(self, error))
		return FALSE;
	if (!fu_corsair_subdevice_ensure_bootloader_version(self, error))
		return FALSE;
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		if (!fu_corsair_subdevice_ensure_battery_level(self, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_corsair_subdevice_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 4, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 92, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 4, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
}

static void
fu_corsair_subdevice_init(FuCorsairSubdevice *self)
{
	fu_device_set_logical_id(FU_DEVICE(self), "subdevice");
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_INPUT_MOUSE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FOR_OPEN);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_proxy_gtype(FU_DEVICE(self), FU_TYPE_CORSAIR_DEVICE);
	fu_device_set_battery_threshold(FU_DEVICE(self), 30);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_EMULATION_TAG);
	fu_device_add_protocol(FU_DEVICE(self), "com.corsair.bp");
}

static void
fu_corsair_subdevice_class_init(FuCorsairSubdeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_corsair_subdevice_setup;
	device_class->attach = fu_corsair_subdevice_attach;
	device_class->detach = fu_corsair_subdevice_detach;
	device_class->write_firmware = fu_corsair_subdevice_write_firmware;
	device_class->set_progress = fu_corsair_subdevice_set_progress;
	device_class->convert_version = fu_corsair_subdevice_convert_version;
}

FuCorsairSubdevice *
fu_corsair_subdevice_new(FuDevice *proxy)
{
	return g_object_new(FU_TYPE_CORSAIR_SUBDEVICE, "proxy", proxy, NULL);
}
