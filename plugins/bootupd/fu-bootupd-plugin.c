/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <systemd/sd-json.h>
#include <systemd/sd-varlink.h>

#include "fu-bootupd-plugin.h"

struct _FuBootupdPlugin {
	FuPlugin parent_instance;
	gchar *varlink_address;
};

G_DEFINE_TYPE(FuBootupdPlugin, fu_bootupd_plugin, FU_TYPE_PLUGIN)

G_DEFINE_AUTOPTR_CLEANUP_FUNC(sd_varlink, sd_varlink_unref)

/* The Varlink service provided by bootupd is socket-activated and only present
 * on systems that are managed by bootupd, e.g. Fedora CoreOS */
#define FU_BOOTUPD_VARLINK_ADDRESS "/run/bootupd/org.coreos.bootupd1"

static gboolean
fu_bootupd_plugin_sync_updates(FuBootupdPlugin *self,
			       const gchar *partuuid,
			       const gchar *capsule_dir,
			       GError **error)
{
	const char *error_id = NULL;
	int r;
	g_autoptr(sd_varlink) vl = NULL;

	/* connect to the socket-activated service */
	r = sd_varlink_connect_address(&vl, self->varlink_address);
	if (r < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to connect to %s: %s",
			    self->varlink_address,
			    fwupd_strerror(-r));
		return FALSE;
	}

	/* do not block indefinitely waiting for bootupd to reply */
	r = sd_varlink_set_relative_timeout(vl, 5 * G_USEC_PER_SEC);
	if (r < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to set varlink timeout: %s",
			    fwupd_strerror(-r));
		return FALSE;
	}

	/* ask bootupd to sync the files we just wrote to the ESP */
	r = sd_varlink_callbo(vl,
			      "org.coreos.bootupd1.SyncFwupdUpdates",
			      /* ret_parameters= */ NULL,
			      &error_id,
			      SD_JSON_BUILD_PAIR_STRING("partuuid", partuuid),
			      SD_JSON_BUILD_PAIR_STRING("capsule_dir", capsule_dir));
	if (r < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to call org.coreos.bootupd1.SyncFwupdUpdates: %s",
			    fwupd_strerror(-r));
		return FALSE;
	}
	if (error_id != NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "org.coreos.bootupd1.SyncFwupdUpdates returned %s",
			    error_id);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_bootupd_plugin_sync_esp(FuBootupdPlugin *self,
			   FuVolume *volume,
			   const gchar *filename,
			   GError **error)
{
	g_autofree gchar *capsule_dir = NULL;
	g_autofree gchar *dirname = NULL;
	g_autofree gchar *metadata_msg = NULL;
	g_autofree gchar *mount_point = fu_volume_get_mount_point(volume);
	g_autofree gchar *partuuid = fu_volume_get_partition_uuid(volume);
	g_autoptr(GFile) dir_file = NULL;
	g_autoptr(GFile) mount_file = NULL;

	/* we can only tell bootupd about volumes we can identify */
	if (partuuid == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "no partition UUID for volume, ignoring");
		return FALSE;
	}
	if (mount_point == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no mount point for volume, ignoring");
		return FALSE;
	}

	/* the capsule directory is relative to the root of the ESP, e.g. EFI/fedora/fw */
	dirname = g_path_get_dirname(filename);
	mount_file = g_file_new_for_path(mount_point);
	dir_file = g_file_new_for_path(dirname);
	if (g_file_equal(mount_file, dir_file)) {
		capsule_dir = g_strdup("");
	} else {
		capsule_dir = g_file_get_relative_path(mount_file, dir_file);
		if (capsule_dir == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "%s is not within %s, ignoring",
				    filename,
				    mount_point);
			return FALSE;
		}
	}

	/* notify bootupd so it can sync the other ESPs */
	if (!fu_bootupd_plugin_sync_updates(self, partuuid, capsule_dir, error)) {
		g_prefix_error_literal(error, "failed to sync: ");
		return FALSE;
	}
	g_debug("synced bootupd updates for %s in %s", partuuid, capsule_dir);

	/* success */
	metadata_msg = g_strdup_printf("synced %s", capsule_dir);
	fu_plugin_add_report_metadata(FU_PLUGIN(self), "bootupd", metadata_msg);
	return TRUE;
}

static void
fu_bootupd_plugin_sync_esp_cb(FuContext *ctx,
			      FuVolume *volume,
			      const gchar *filename,
			      gpointer user_data)
{
	FuBootupdPlugin *self = FU_BOOTUPD_PLUGIN(user_data);
	g_autoptr(GError) error_local = NULL;
	if (!fu_bootupd_plugin_sync_esp(self, volume, filename, &error_local)) {
		fu_plugin_add_report_metadata(FU_PLUGIN(self), "bootupd", error_local->message);
		g_warning("failed to notify: %s", error_local->message);
	}
}

static gboolean
fu_bootupd_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuBootupdPlugin *self = FU_BOOTUPD_PLUGIN(plugin);
	FuContext *ctx = fu_plugin_get_context(plugin);

	if (self->varlink_address == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no bootupd varlink service set");
		return FALSE;
	}
	if (!g_file_test(self->varlink_address, G_FILE_TEST_EXISTS)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no bootupd varlink service at %s",
			    self->varlink_address);
		return FALSE;
	}

	g_signal_connect_object(FU_CONTEXT(ctx),
				"esp-write",
				G_CALLBACK(fu_bootupd_plugin_sync_esp_cb),
				self,
				0);

	/* success */
	return TRUE;
}

void
fu_bootupd_plugin_set_varlink_address(FuBootupdPlugin *self, const gchar *varlink_address)
{
	g_return_if_fail(FU_IS_BOOTUPD_PLUGIN(self));

	if (g_strcmp0(self->varlink_address, varlink_address) == 0)
		return;
	g_set_str(&self->varlink_address, varlink_address);
}

static void
fu_bootupd_plugin_init(FuBootupdPlugin *self)
{
	fu_bootupd_plugin_set_varlink_address(self, "/run/bootupd/org.coreos.bootupd1");
}

static void
fu_bootupd_plugin_finalize(GObject *object)
{
	FuBootupdPlugin *self = FU_BOOTUPD_PLUGIN(object);

	g_free(self->varlink_address);

	G_OBJECT_CLASS(fu_bootupd_plugin_parent_class)->finalize(object);
}

static void
fu_bootupd_plugin_class_init(FuBootupdPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_bootupd_plugin_finalize;
	plugin_class->startup = fu_bootupd_plugin_startup;
}
