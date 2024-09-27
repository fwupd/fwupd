/*
 * Copyright 2024 Maciej Borzecki <maciej.borzecki@canonical.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <time.h>

#include "fwupd-enums.h"
#include "fwupd-error.h"

#include "fu-firmware.h"
#include "fu-progress.h"
#include "fu-secure-boot-device.h"
#include "fu-snapd-error.h"
#include "fu-snapd-observer.h"
#include "fu-snapd-plugin.h"
#include "fu-snapd-snap.h"
#include "glib.h"

struct _FuSnapdPlugin {
	FuPlugin parent_instance;

	FuSnapdObserver *snapd_observer;
};

G_DEFINE_TYPE(FuSnapdPlugin, fu_snapd_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_snapd_plugin_notify_init(FuSnapdPlugin *self, GError **error);

static void
fu_snapd_plugin_init(FuSnapdPlugin *self)
{
}

static void
fu_snapd_plugin_finalize(GObject *object)
{
	FuSnapdPlugin *self = FU_SNAPD_PLUGIN(object);
	if (self->snapd_observer != NULL) {
		g_object_unref(self->snapd_observer);
		self->snapd_observer = NULL;
	}
}

static gboolean
fu_snapd_notify_secure_boot_dbx_write(FuSecureBootDevice *dev,
				      FuFirmware *fw,
				      gpointer user_data,
				      GError **error);

static gboolean
fu_snapd_plugin_composite_prepare(FuPlugin *plugin, GPtrArray *devices, GError **error)
{
	FuSnapdPlugin *self = FU_SNAPD_PLUGIN(plugin);

	g_debug("composite prepare");

	g_return_val_if_fail(self->snapd_observer != NULL, TRUE);

	/* TODO which devices are part of the update */
	/* TODO if found UEFI DBX device, then register a callback */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index(devices, i);
		if (FU_IS_SECURE_BOOT_DEVICE(dev)) {
			FuSecureBootDevice *sbdev = FU_SECURE_BOOT_DEVICE(dev);
			FuSecureBootDeviceKind kind = fu_secure_boot_device_get_kind(sbdev);
			if (kind != FU_SECURE_BOOT_DEVICE_KIND_UEFI_DBX) {
				continue;
			}

			g_debug("found DBX device");
			if (!fu_secure_boot_device_set_firmware_write_observe(
				sbdev,
				fu_snapd_notify_secure_boot_dbx_write,
				(gpointer)plugin)) {
				g_warning("cannot install firmware write observer");
				return FALSE;
			}
		}
	}

	return TRUE;
}

static gboolean
fu_snapd_plugin_composite_cleanup(FuPlugin *plugin, GPtrArray *devices, GError **error)
{
	FuSnapdPlugin *self = FU_SNAPD_PLUGIN(plugin);
	g_autoptr(GError) snapd_cleanup_error = NULL;
	gboolean dbx_device_found = FALSE;

	g_debug("composite cleanup");

	g_return_val_if_fail(self->snapd_observer != NULL, TRUE);

	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index(devices, i);
		if (FU_IS_SECURE_BOOT_DEVICE(dev)) {
			FuSecureBootDevice *sbdev = FU_SECURE_BOOT_DEVICE(dev);
			FuSecureBootDeviceKind kind = fu_secure_boot_device_get_kind(sbdev);
			if (kind != FU_SECURE_BOOT_DEVICE_KIND_UEFI_DBX) {
				continue;
			}

			g_debug("found DBX device");
			dbx_device_found = TRUE;
			fu_secure_boot_device_reset_firmware_write_observe(sbdev);
		}
	}

	if (dbx_device_found) {
		if (!fu_snapd_observer_notify_secureboot_db_update_cleanup(self->snapd_observer,
									   &snapd_cleanup_error)) {
			g_warning("snapd cleanup failed: %s", snapd_cleanup_error->message);
			/* TODO do we care about failures here? */
		}
	}
	return TRUE;
}

static gboolean
fu_snapd_plugin_startup(FuPlugin *obj, FuProgress *progress, GError **error)
{
	GError *snapd_error = NULL;
	gboolean res = FALSE;

	/* TODO check if SecureBoot is enabled, see uefi-sbat plugin */

	if (!fu_snapd_is_in_snap()) {
		g_debug("disabling snapd integration in non-snap scenario");
		g_set_error_literal(error,
				    FU_SNAPD_ERROR,
				    FU_SNAPD_ERROR_UNSUPPORTED,
				    "snapd integration outside of snap is not supported");
		return FALSE;
	}

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 100, "snapd-probe");

	g_debug("snapd startup");

	res = fu_snapd_plugin_notify_init(FU_SNAPD_PLUGIN(obj), &snapd_error);

	fu_progress_step_done(progress);

	if (!res) {
		/* TODO inspect the error, maybe simply unavailable */
		g_debug("snapd integration error: %s",
			(snapd_error != NULL) ? snapd_error->message : "(unknown)");

		if (g_error_matches(snapd_error, FU_SNAPD_ERROR, FU_SNAPD_ERROR_UNSUPPORTED)) {
			g_warning("snapd integration not supported");
		}
		g_propagate_error(error, snapd_error);
		g_prefix_error(error, "cannot initialize snapd integration: ");
		/* plugin becomes disabled */
		return FALSE;
	}

	g_debug("snapd integration enabled ");

	return TRUE;
}

static void
fu_snapd_plugin_class_init(FuSnapdPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);

	plugin_class->startup = fu_snapd_plugin_startup;
	plugin_class->finalize = fu_snapd_plugin_finalize;
	plugin_class->composite_prepare = fu_snapd_plugin_composite_prepare;
	plugin_class->composite_cleanup = fu_snapd_plugin_composite_cleanup;
}

static gboolean
fu_snapd_plugin_notify_init(FuSnapdPlugin *self, GError **error)
{
	g_autoptr(FuSnapdObserver) obs = fu_snapd_observer_new();

	if (!fu_snapd_observer_notify_secureboot_manager_startup(obs, error)) {
		/* TODO inspect the error to device whether  */
		g_warning("snapd notification init failed");
		return FALSE;
	}

	self->snapd_observer = g_object_ref(obs);
	return TRUE;
}

static gboolean
fu_snapd_notify_secure_boot_dbx_write(FuSecureBootDevice *dev,
				      FuFirmware *fw,
				      gpointer user_data,
				      GError **error)
{
	GBytes *payload = NULL;
	FuSnapdPlugin *self = FU_SNAPD_PLUGIN(user_data);

	g_warning("snapd write observe call");
	if (fu_secure_boot_device_get_kind(dev) == FU_SECURE_BOOT_DEVICE_KIND_UEFI_DBX) {
		g_warning("snapd write observe called for UEFI DBX device");
	}

	payload = fu_firmware_get_bytes(fw, error);
	if (payload == NULL) {
		return FALSE;
	}

	if (!fu_snapd_observer_notify_secureboot_dbx_update_prepare(self->snapd_observer,
								    payload,
								    error)) {
		g_prefix_error(error, "cannot notify snapd: ");
		return FALSE;
	}

	g_debug("successfully notified snapd of a DBX update");

	return TRUE;
}
