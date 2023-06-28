/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuEngineConfig"

#include "config.h"

#include <fwupdplugin.h>

#include "fu-engine-config.h"

struct _FuEngineConfig {
	FuConfig parent_instance;
	GHashTable *os_release;	      /* (element-type utf-8 utf-8) */
	GPtrArray *disabled_devices;  /* (element-type utf-8) */
	GPtrArray *disabled_plugins;  /* (element-type utf-8) */
	GPtrArray *approved_firmware; /* (element-type utf-8) */
	GPtrArray *blocked_firmware;  /* (element-type utf-8) */
	GPtrArray *uri_schemes;	      /* (element-type utf-8) */
	GPtrArray *trusted_reports;   /* (element-type FwupdReport) */
	GArray *trusted_uids;	      /* (element-type guint64) */
	gchar *host_bkc;
	gchar *esp_location;
	gboolean allow_emulation;
};

G_DEFINE_TYPE(FuEngineConfig, fu_engine_config, FU_TYPE_CONFIG)

/* defaults changed here will also be reflected in the fwupd.conf man page */
#define FU_DAEMON_CONFIG_DEFAULT_DISABLED_PLUGINS      "test;test_ble"
#define FU_DAEMON_CONFIG_DEFAULT_URI_SCHEMES	       "file;https;http;ipfs"
#define FU_DAEMON_CONFIG_DEFAULT_UPDATE_MOTD	       TRUE
#define FU_DAEMON_CONFIG_DEFAULT_IGNORE_POWER	       FALSE
#define FU_DAEMON_CONFIG_DEFAULT_ONLY_TRUSTED	       TRUE
#define FU_DAEMON_CONFIG_DEFAULT_SHOW_DEVICE_PRIVATE   TRUE
#define FU_DAEMON_CONFIG_DEFAULT_ALLOW_EMULATION       FALSE
#define FU_DAEMON_CONFIG_DEFAULT_ENUMERATE_ALL_DEVICES TRUE
#define FU_DAEMON_CONFIG_DEFAULT_TRUSTED_UIDS	       NULL
#define FU_DAEMON_CONFIG_DEFAULT_HOST_BKC	       NULL
#define FU_DAEMON_CONFIG_DEFAULT_TRUSTED_REPORTS       "VendorId=$OEM"
#define FU_DAEMON_CONFIG_DEFAULT_RELEASE_DEDUPE	       TRUE
#define FU_DAEMON_CONFIG_DEFAULT_RELEASE_PRIORITY      "local"

static FwupdReport *
fu_engine_config_report_from_spec(FuEngineConfig *self, const gchar *report_spec, GError **error)
{
	g_auto(GStrv) parts = g_strsplit(report_spec, "&", -1);
	g_autoptr(FwupdReport) report = fwupd_report_new();

	for (guint i = 0; parts[i] != NULL; i++) {
		const gchar *value = NULL;
		g_auto(GStrv) kv = g_strsplit(parts[i], "=", 2);
		if (g_strv_length(kv) != 2) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "failed to parse report specifier key=value %s",
				    parts[i]);
			return NULL;
		}
		if (g_str_has_prefix(kv[1], "$"))
			value = g_hash_table_lookup(self->os_release, kv[1] + 1);
		if (value == NULL)
			value = kv[1];
		if (g_strcmp0(kv[0], "VendorId") == 0) {
			guint64 tmp = 0;
			if (g_strcmp0(value, "$OEM") == 0) {
				fwupd_report_add_flag(report, FWUPD_REPORT_FLAG_FROM_OEM);
			} else {
				if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, error)) {
					g_prefix_error(error, "failed to parse '%s': ", value);
					return NULL;
				}
				fwupd_report_set_vendor_id(report, tmp);
			}
		} else if (g_strcmp0(kv[0], "DistroId") == 0) {
			fwupd_report_set_distro_id(report, value);
		} else if (g_strcmp0(kv[0], "DistroVariant") == 0) {
			fwupd_report_set_distro_variant(report, value);
		} else if (g_strcmp0(kv[0], "DistroVersion") == 0) {
			fwupd_report_set_distro_version(report, value);
		} else if (g_strcmp0(kv[0], "RemoteId") == 0) {
			fwupd_report_set_remote_id(report, value);
		} else {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "failed to parse report specifier key %s",
				    kv[0]);
			return NULL;
		}
	}

	/* success */
	return g_steal_pointer(&report);
}

static void
fu_engine_config_reload(FuEngineConfig *self)
{
	g_auto(GStrv) approved_firmware = NULL;
	g_auto(GStrv) blocked_firmware = NULL;
	g_auto(GStrv) devices = NULL;
	g_auto(GStrv) plugins = NULL;
	g_auto(GStrv) report_specs = NULL;
	g_auto(GStrv) uids = NULL;
	g_auto(GStrv) uri_schemes = NULL;
	g_autofree gchar *domains = NULL;
	g_autofree gchar *host_bkc = NULL;
	g_autofree gchar *esp_location = NULL;

	/* get disabled devices */
	g_ptr_array_set_size(self->disabled_devices, 0);
	devices = fu_config_get_value_strv(FU_CONFIG(self), "fwupd", "DisabledDevices", NULL);
	if (devices != NULL) {
		for (guint i = 0; devices[i] != NULL; i++)
			g_ptr_array_add(self->disabled_devices, g_strdup(devices[i]));
	}

	/* get disabled plugins */
	g_ptr_array_set_size(self->disabled_plugins, 0);
	plugins = fu_config_get_value_strv(FU_CONFIG(self),
					   "fwupd",
					   "DisabledPlugins",
					   FU_DAEMON_CONFIG_DEFAULT_DISABLED_PLUGINS);
	if (plugins != NULL) {
		for (guint i = 0; plugins[i] != NULL; i++)
			g_ptr_array_add(self->disabled_plugins, g_strdup(plugins[i]));
	}

	/* get approved firmware */
	g_ptr_array_set_size(self->approved_firmware, 0);
	approved_firmware =
	    fu_config_get_value_strv(FU_CONFIG(self), "fwupd", "ApprovedFirmware", NULL);
	if (approved_firmware != NULL) {
		for (guint i = 0; approved_firmware[i] != NULL; i++)
			g_ptr_array_add(self->approved_firmware, g_strdup(approved_firmware[i]));
	}

	/* get blocked firmware */
	g_ptr_array_set_size(self->blocked_firmware, 0);
	blocked_firmware =
	    fu_config_get_value_strv(FU_CONFIG(self), "fwupd", "BlockedFirmware", NULL);
	if (blocked_firmware != NULL) {
		for (guint i = 0; blocked_firmware[i] != NULL; i++)
			g_ptr_array_add(self->blocked_firmware, g_strdup(blocked_firmware[i]));
	}

	/* get download schemes */
	g_ptr_array_set_size(self->uri_schemes, 0);
	uri_schemes = fu_config_get_value_strv(FU_CONFIG(self),
					       "fwupd",
					       "UriSchemes",
					       FU_DAEMON_CONFIG_DEFAULT_URI_SCHEMES);
	if (uri_schemes != NULL) {
		for (guint i = 0; uri_schemes[i] != NULL; i++)
			g_ptr_array_add(self->uri_schemes, g_strdup(uri_schemes[i]));
	}

	/* get the domains to run in verbose */
	domains = fu_config_get_value(FU_CONFIG(self), "fwupd", "VerboseDomains", NULL);
	if (domains != NULL && domains[0] != '\0')
		(void)g_setenv("FWUPD_VERBOSE", domains, TRUE);

	/* fetch host best known configuration */
	host_bkc = fu_config_get_value(FU_CONFIG(self),
				       "fwupd",
				       "HostBkc",
				       FU_DAEMON_CONFIG_DEFAULT_HOST_BKC);
	if (host_bkc != NULL && host_bkc[0] != '\0')
		self->host_bkc = g_steal_pointer(&host_bkc);

	/* fetch hardcoded ESP mountpoint */
	esp_location = fu_config_get_value(FU_CONFIG(self), "fwupd", "EspLocation", NULL);
	if (esp_location != NULL && esp_location[0] != '\0')
		self->esp_location = g_steal_pointer(&esp_location);

	/* get trusted uids */
	g_array_set_size(self->trusted_uids, 0);
	uids = fu_config_get_value_strv(FU_CONFIG(self),
					"fwupd",
					"TrustedUids",
					FU_DAEMON_CONFIG_DEFAULT_TRUSTED_UIDS);
	if (uids != NULL) {
		for (guint i = 0; uids[i] != NULL; i++) {
			guint64 val = 0;
			g_autoptr(GError) error_local = NULL;
			if (!fu_strtoull(uids[i], &val, 0, G_MAXUINT64, &error_local)) {
				g_warning("failed to parse UID '%s': %s",
					  uids[i],
					  error_local->message);
				continue;
			}
			g_array_append_val(self->trusted_uids, val);
		}
	}

	/* get trusted reports */
	g_ptr_array_set_size(self->trusted_reports, 0);
	report_specs = fu_config_get_value_strv(FU_CONFIG(self),
						"fwupd",
						"TrustedReports",
						FU_DAEMON_CONFIG_DEFAULT_TRUSTED_REPORTS);
	if (report_specs != NULL) {
		for (guint i = 0; report_specs[i] != NULL; i++) {
			g_autoptr(GError) error_local = NULL;
			FwupdReport *report =
			    fu_engine_config_report_from_spec(self, report_specs[i], &error_local);
			if (report == NULL) {
				g_warning("failed to parse %s: %s",
					  report_specs[i],
					  error_local->message);
				continue;
			}
			g_ptr_array_add(self->trusted_reports, report);
		}
	}
}

static void
fu_engine_config_changed_cb(FuEngineConfig *config, gpointer user_data)
{
	FuEngineConfig *self = FU_ENGINE_CONFIG(config);
	fu_engine_config_reload(self);
}

guint
fu_engine_config_get_idle_timeout(FuEngineConfig *self)
{
	return fu_config_get_value_u64(FU_CONFIG(self), "fwupd", "IdleTimeout", 7200);
}

GPtrArray *
fu_engine_config_get_disabled_devices(FuEngineConfig *self)
{
	g_return_val_if_fail(FU_IS_ENGINE_CONFIG(self), NULL);
	return self->disabled_devices;
}

GArray *
fu_engine_config_get_trusted_uids(FuEngineConfig *self)
{
	g_return_val_if_fail(FU_IS_ENGINE_CONFIG(self), NULL);
	return self->trusted_uids;
}

GPtrArray *
fu_engine_config_get_trusted_reports(FuEngineConfig *self)
{
	g_return_val_if_fail(FU_IS_CONFIG(self), NULL);
	return self->trusted_reports;
}

GPtrArray *
fu_engine_config_get_blocked_firmware(FuEngineConfig *self)
{
	g_return_val_if_fail(FU_IS_ENGINE_CONFIG(self), NULL);
	return self->blocked_firmware;
}

guint
fu_engine_config_get_uri_scheme_prio(FuEngineConfig *self, const gchar *scheme)
{
	guint idx = 0;
	if (!g_ptr_array_find_with_equal_func(self->uri_schemes, scheme, g_str_equal, &idx))
		return G_MAXUINT;
	return idx;
}

guint64
fu_engine_config_get_archive_size_max(FuEngineConfig *self)
{
	guint64 memory_size = fu_common_get_memory_size();
	guint64 value_default = memory_size > 0 ? MIN(memory_size / 4, G_MAXUINT32)
						: 512 * 0x100000;
	return fu_config_get_value_u64(FU_CONFIG(self), "fwupd", "ArchiveSizeMax", value_default);
}

GPtrArray *
fu_engine_config_get_disabled_plugins(FuEngineConfig *self)
{
	g_return_val_if_fail(FU_IS_ENGINE_CONFIG(self), NULL);
	return self->disabled_plugins;
}

GPtrArray *
fu_engine_config_get_approved_firmware(FuEngineConfig *self)
{
	g_return_val_if_fail(FU_IS_ENGINE_CONFIG(self), NULL);
	return self->approved_firmware;
}

gboolean
fu_engine_config_get_update_motd(FuEngineConfig *self)
{
	return fu_config_get_value_bool(FU_CONFIG(self),
					"fwupd",
					"UpdateMotd",
					FU_DAEMON_CONFIG_DEFAULT_UPDATE_MOTD);
}

gboolean
fu_engine_config_get_ignore_power(FuEngineConfig *self)
{
	return fu_config_get_value_bool(FU_CONFIG(self),
					"fwupd",
					"IgnorePower",
					FU_DAEMON_CONFIG_DEFAULT_IGNORE_POWER);
}

gboolean
fu_engine_config_get_only_trusted(FuEngineConfig *self)
{
	return fu_config_get_value_bool(FU_CONFIG(self),
					"fwupd",
					"OnlyTrusted",
					FU_DAEMON_CONFIG_DEFAULT_ONLY_TRUSTED);
}

gboolean
fu_engine_config_get_show_device_private(FuEngineConfig *self)
{
	return fu_config_get_value_bool(FU_CONFIG(self),
					"fwupd",
					"ShowDevicePrivate",
					FU_DAEMON_CONFIG_DEFAULT_SHOW_DEVICE_PRIVATE);
}

gboolean
fu_engine_config_get_allow_emulation(FuEngineConfig *self)
{
	return fu_config_get_value_bool(FU_CONFIG(self),
					"fwupd",
					"AllowEmulation",
					FU_DAEMON_CONFIG_DEFAULT_ALLOW_EMULATION);
}

gboolean
fu_engine_config_get_release_dedupe(FuEngineConfig *self)
{
	return fu_config_get_value_bool(FU_CONFIG(self),
					"fwupd",
					"ReleaseDedupe",
					FU_DAEMON_CONFIG_DEFAULT_RELEASE_DEDUPE);
}

FuReleasePriority
fu_engine_config_get_release_priority(FuEngineConfig *self)
{
	g_autofree gchar *tmp = fu_config_get_value(FU_CONFIG(self),
						    "fwupd",
						    "ReleasePriority",
						    FU_DAEMON_CONFIG_DEFAULT_RELEASE_PRIORITY);
	return fu_release_priority_from_string(tmp);
}

gboolean
fu_engine_config_get_enumerate_all_devices(FuEngineConfig *self)
{
	return fu_config_get_value_bool(FU_CONFIG(self),
					"fwupd",
					"EnumerateAllDevices",
					FU_DAEMON_CONFIG_DEFAULT_ENUMERATE_ALL_DEVICES);
}

const gchar *
fu_engine_config_get_host_bkc(FuEngineConfig *self)
{
	g_return_val_if_fail(FU_IS_ENGINE_CONFIG(self), NULL);
	return self->host_bkc;
}

const gchar *
fu_engine_config_get_esp_location(FuEngineConfig *self)
{
	g_return_val_if_fail(FU_IS_ENGINE_CONFIG(self), NULL);
	return self->esp_location;
}

static void
fu_engine_config_init(FuEngineConfig *self)
{
	self->disabled_devices = g_ptr_array_new_with_free_func(g_free);
	self->disabled_plugins = g_ptr_array_new_with_free_func(g_free);
	self->approved_firmware = g_ptr_array_new_with_free_func(g_free);
	self->blocked_firmware = g_ptr_array_new_with_free_func(g_free);
	self->trusted_uids = g_array_new(FALSE, FALSE, sizeof(guint64));
	self->trusted_reports = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	self->uri_schemes = g_ptr_array_new_with_free_func(g_free);
	g_signal_connect(self, "loaded", G_CALLBACK(fu_engine_config_changed_cb), NULL);
	g_signal_connect(self, "changed", G_CALLBACK(fu_engine_config_changed_cb), NULL);

	/* optionally used for substitutions */
	self->os_release = fwupd_get_os_release(NULL);
}

static void
fu_engine_config_finalize(GObject *obj)
{
	FuEngineConfig *self = FU_ENGINE_CONFIG(obj);

	if (self->os_release != NULL)
		g_hash_table_unref(self->os_release);
	g_ptr_array_unref(self->disabled_devices);
	g_ptr_array_unref(self->disabled_plugins);
	g_ptr_array_unref(self->approved_firmware);
	g_ptr_array_unref(self->blocked_firmware);
	g_ptr_array_unref(self->uri_schemes);
	g_ptr_array_unref(self->trusted_reports);
	g_array_unref(self->trusted_uids);
	g_free(self->host_bkc);
	g_free(self->esp_location);

	G_OBJECT_CLASS(fu_engine_config_parent_class)->finalize(obj);
}

static void
fu_engine_config_class_init(FuEngineConfigClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_engine_config_finalize;
}

FuEngineConfig *
fu_engine_config_new(void)
{
	return FU_ENGINE_CONFIG(g_object_new(FU_TYPE_ENGINE_CONFIG, NULL));
}
