/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuEngine"

#include "config.h"

#ifdef HAVE_GIO_UNIX
#include <gio/gunixinputstream.h>
#endif
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UTSNAME_H
#include <sys/utsname.h>
#endif
#include <errno.h>

#ifdef _WIN32
#include <sysinfoapi.h>
#include <winerror.h>
#include <winreg.h>
#endif

#include <fwupdplugin.h>

#include "fwupd-common-private.h"
#include "fwupd-device-private.h"
#include "fwupd-enums-private.h"
#include "fwupd-release-private.h"
#include "fwupd-remote-private.h"
#include "fwupd-resources.h"
#include "fwupd-security-attr-private.h"

#include "fu-cabinet.h"
#include "fu-context-private.h"
#include "fu-coswid-firmware.h"
#include "fu-debug.h"
#include "fu-device-list.h"
#include "fu-device-private.h"
#include "fu-engine-helper.h"
#include "fu-engine-request.h"
#include "fu-engine.h"
#include "fu-firmware-builder.h"
#include "fu-hash.h"
#include "fu-history.h"
#include "fu-idle.h"
#include "fu-kenv.h"
#include "fu-keyring-utils.h"
#include "fu-mutex.h"
#include "fu-plugin-list.h"
#include "fu-plugin-private.h"
#include "fu-release.h"
#include "fu-remote-list.h"
#include "fu-security-attr.h"
#include "fu-security-attrs-private.h"
#include "fu-udev-device-private.h"
#include "fu-version.h"

#ifdef HAVE_GUDEV
#include "fu-udev-backend.h"
#endif
#ifdef HAVE_GUSB
#include "fu-usb-backend.h"
#endif
#ifdef HAVE_BLUEZ
#include "fu-bluez-backend.h"
#endif

/* only needed until we hard depend on jcat 0.1.3 */
#include <libjcat/jcat-version.h>

#ifdef HAVE_SYSTEMD
#include "fu-systemd.h"
#endif

#define MINIMUM_BATTERY_PERCENTAGE_FALLBACK 10

static void
fu_engine_finalize(GObject *obj);
static void
fu_engine_ensure_security_attrs(FuEngine *self);

struct _FuEngine {
	GObject parent_instance;
	GPtrArray *backends;
	FuConfig *config;
	FuRemoteList *remote_list;
	FuDeviceList *device_list;
	gboolean tainted;
	gboolean only_trusted;
	gboolean write_history;
	gboolean host_emulation;
	guint percentage;
	FuHistory *history;
	FuIdle *idle;
	XbSilo *silo;
	XbQuery *query_component_by_guid;
	guint coldplug_id;
	FuPluginList *plugin_list;
	GPtrArray *plugin_filter;
	FuContext *ctx;
	GHashTable *runtime_versions;
	GHashTable *compile_versions;
	GHashTable *approved_firmware; /* (nullable) */
	GHashTable *blocked_firmware;  /* (nullable) */
	gchar *host_machine_id;
	JcatContext *jcat_context;
	gboolean loaded;
	gchar *host_security_id;
	FuSecurityAttrs *host_security_attrs;
	GPtrArray *local_monitors; /* (element-type GFileMonitor) */
	GMainLoop *acquiesce_loop;
	guint acquiesce_id;
	guint acquiesce_delay;
};

enum {
	SIGNAL_CHANGED,
	SIGNAL_DEVICE_ADDED,
	SIGNAL_DEVICE_REMOVED,
	SIGNAL_DEVICE_CHANGED,
	SIGNAL_DEVICE_REQUEST,
	SIGNAL_STATUS_CHANGED,
	SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = {0};

G_DEFINE_TYPE(FuEngine, fu_engine, G_TYPE_OBJECT)

static void
fu_engine_emit_changed(FuEngine *self)
{
	g_autoptr(GError) error = NULL;

	g_signal_emit(self, signals[SIGNAL_CHANGED], 0);
	fu_engine_idle_reset(self);

	/* update the motd */
	if (self->loaded && fu_config_get_update_motd(self->config)) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_engine_update_motd(self, &error_local))
			g_debug("failed to update MOTD: %s", error_local->message);
	}

	/* update the list of devices */
	if (!fu_engine_update_devices_file(self, &error))
		g_debug("failed to update list of devices: %s", error->message);
}

static void
fu_engine_emit_device_changed_safe(FuEngine *self, FuDevice *device)
{
	/* invalidate host security attributes */
	g_clear_pointer(&self->host_security_id, g_free);
	g_signal_emit(self, signals[SIGNAL_DEVICE_CHANGED], 0, device);
}

/* get the latest version of the device */
static void
fu_engine_emit_device_changed(FuEngine *self, const gchar *device_id)
{
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(GError) error = NULL;

	/* get the latest version of this */
	device = fu_device_list_get_by_id(self->device_list, device_id, &error);
	if (device == NULL) {
		g_warning("cannot emit device-changed: %s", error->message);
		return;
	}
	fu_engine_emit_device_changed_safe(self, device);
}

static const GError *
fu_engine_error_array_find(GPtrArray *errors, FwupdError error_code)
{
	for (guint j = 0; j < errors->len; j++) {
		const GError *error = g_ptr_array_index(errors, j);
		if (g_error_matches(error, FWUPD_ERROR, error_code))
			return error;
	}
	return NULL;
}

static guint
fu_engine_error_array_count(GPtrArray *errors, FwupdError error_code)
{
	guint cnt = 0;
	for (guint j = 0; j < errors->len; j++) {
		const GError *error = g_ptr_array_index(errors, j);
		if (g_error_matches(error, FWUPD_ERROR, error_code))
			cnt++;
	}
	return cnt;
}

static gboolean
fu_engine_error_array_matches_any(GPtrArray *errors, FwupdError *error_codes)
{
	for (guint j = 0; j < errors->len; j++) {
		const GError *error = g_ptr_array_index(errors, j);
		gboolean matches_any = FALSE;
		for (guint i = 0; error_codes[i] != FWUPD_ERROR_LAST; i++) {
			if (g_error_matches(error, FWUPD_ERROR, error_codes[i])) {
				matches_any = TRUE;
				break;
			}
		}
		if (!matches_any)
			return FALSE;
	}
	return TRUE;
}

/**
 * fu_engine_error_array_get_best:
 * @errors: (element-type GError): array of errors
 *
 * Finds the 'best' error to show the user from a array of errors, creating a
 * completely bespoke error where required.
 *
 * Returns: (transfer full): a #GError, never %NULL
 **/
GError *
fu_engine_error_array_get_best(GPtrArray *errors)
{
	FwupdError err_prio[] = {FWUPD_ERROR_INVALID_FILE,
				 FWUPD_ERROR_VERSION_SAME,
				 FWUPD_ERROR_VERSION_NEWER,
				 FWUPD_ERROR_NOT_SUPPORTED,
				 FWUPD_ERROR_INTERNAL,
				 FWUPD_ERROR_NOT_FOUND,
				 FWUPD_ERROR_LAST};
	FwupdError err_all_uptodate[] = {FWUPD_ERROR_VERSION_SAME,
					 FWUPD_ERROR_NOT_FOUND,
					 FWUPD_ERROR_NOT_SUPPORTED,
					 FWUPD_ERROR_LAST};
	FwupdError err_all_newer[] = {FWUPD_ERROR_VERSION_NEWER,
				      FWUPD_ERROR_VERSION_SAME,
				      FWUPD_ERROR_NOT_FOUND,
				      FWUPD_ERROR_NOT_SUPPORTED,
				      FWUPD_ERROR_LAST};

	/* are all the errors either GUID-not-matched or version-same? */
	if (fu_engine_error_array_count(errors, FWUPD_ERROR_VERSION_SAME) > 1 &&
	    fu_engine_error_array_matches_any(errors, err_all_uptodate)) {
		return g_error_new(FWUPD_ERROR,
				   FWUPD_ERROR_NOTHING_TO_DO,
				   "All updatable firmware is already installed");
	}

	/* are all the errors either GUID-not-matched or version same or newer? */
	if (fu_engine_error_array_count(errors, FWUPD_ERROR_VERSION_NEWER) > 1 &&
	    fu_engine_error_array_matches_any(errors, err_all_newer)) {
		return g_error_new(FWUPD_ERROR,
				   FWUPD_ERROR_NOTHING_TO_DO,
				   "All updatable devices already have newer versions");
	}

	/* get the most important single error */
	for (guint i = 0; err_prio[i] != FWUPD_ERROR_LAST; i++) {
		const GError *error_tmp = fu_engine_error_array_find(errors, err_prio[i]);
		if (error_tmp != NULL)
			return g_error_copy(error_tmp);
	}

	/* fall back to something */
	return g_error_new(FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "No supported devices found");
}

FuContext *
fu_engine_get_context(FuEngine *self)
{
	return self->ctx;
}

static void
fu_engine_set_status(FuEngine *self, FwupdStatus status)
{
	/* emit changed */
	g_signal_emit(self, signals[SIGNAL_STATUS_CHANGED], 0, status);
}

static void
fu_engine_generic_notify_cb(FuDevice *device, GParamSpec *pspec, FuEngine *self)
{
	fu_engine_emit_device_changed(self, fu_device_get_id(device));
}

static void
fu_engine_history_notify_cb(FuDevice *device, GParamSpec *pspec, FuEngine *self)
{
	if (self->write_history) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_history_modify_device(self->history, device, &error_local)) {
			g_warning("failed to record history for %s: %s",
				  fu_device_get_id(device),
				  error_local->message);
		}
	}
	fu_engine_emit_device_changed(self, fu_device_get_id(device));
}

static void
fu_engine_device_request_cb(FuDevice *device, FwupdRequest *request, FuEngine *self)
{
	g_debug("Emitting DeviceRequest('Message'='%s')", fwupd_request_get_message(request));
	g_signal_emit(self, signals[SIGNAL_DEVICE_REQUEST], 0, request);
}

static void
fu_engine_watch_device(FuEngine *self, FuDevice *device)
{
	g_autoptr(FuDevice) device_old = fu_device_list_get_old(self->device_list, device);
	if (device_old != NULL) {
		g_signal_handlers_disconnect_by_func(device_old, fu_engine_generic_notify_cb, self);
		g_signal_handlers_disconnect_by_func(device_old, fu_engine_history_notify_cb, self);
		g_signal_handlers_disconnect_by_func(device_old, fu_engine_device_request_cb, self);
	}
	g_signal_connect(FU_DEVICE(device),
			 "notify::flags",
			 G_CALLBACK(fu_engine_generic_notify_cb),
			 self);
	g_signal_connect(FU_DEVICE(device),
			 "notify::problems",
			 G_CALLBACK(fu_engine_generic_notify_cb),
			 self);
	g_signal_connect(FU_DEVICE(device),
			 "notify::update-message",
			 G_CALLBACK(fu_engine_generic_notify_cb),
			 self);
	g_signal_connect(FU_DEVICE(device),
			 "notify::update-image",
			 G_CALLBACK(fu_engine_generic_notify_cb),
			 self);
	g_signal_connect(FU_DEVICE(device),
			 "notify::update-state",
			 G_CALLBACK(fu_engine_history_notify_cb),
			 self);
	g_signal_connect(FU_DEVICE(device),
			 "notify::update-error",
			 G_CALLBACK(fu_engine_history_notify_cb),
			 self);
	g_signal_connect(FU_DEVICE(device),
			 "request",
			 G_CALLBACK(fu_engine_device_request_cb),
			 self);
}

static void
fu_engine_ensure_device_battery_inhibit(FuEngine *self, FuDevice *device)
{
	if (fu_config_get_ignore_power(self->config))
		return;

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_REQUIRE_AC) &&
	    (fu_context_get_battery_state(self->ctx) == FU_BATTERY_STATE_DISCHARGING ||
	     fu_context_get_battery_state(self->ctx) == FU_BATTERY_STATE_EMPTY)) {
		fu_device_add_problem(device, FWUPD_DEVICE_PROBLEM_REQUIRE_AC_POWER);
	} else {
		fu_device_remove_problem(device, FWUPD_DEVICE_PROBLEM_REQUIRE_AC_POWER);
	}
	if (fu_context_get_battery_level(self->ctx) != FWUPD_BATTERY_LEVEL_INVALID &&
	    fu_context_get_battery_threshold(self->ctx) != FWUPD_BATTERY_LEVEL_INVALID &&
	    fu_context_get_battery_level(self->ctx) < fu_context_get_battery_threshold(self->ctx)) {
		fu_device_add_problem(device, FWUPD_DEVICE_PROBLEM_SYSTEM_POWER_TOO_LOW);
	} else {
		fu_device_remove_problem(device, FWUPD_DEVICE_PROBLEM_SYSTEM_POWER_TOO_LOW);
	}
}

static void
fu_engine_ensure_device_lid_inhibit(FuEngine *self, FuDevice *device)
{
	if (fu_device_has_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_NO_LID_CLOSED) &&
	    fu_context_get_lid_state(self->ctx) == FU_LID_STATE_CLOSED) {
		fu_device_add_problem(device, FWUPD_DEVICE_PROBLEM_LID_IS_CLOSED);
		return;
	}
	fu_device_remove_problem(device, FWUPD_DEVICE_PROBLEM_LID_IS_CLOSED);
}

static gboolean
fu_engine_acquiesce_timeout_cb(gpointer user_data)
{
	FuEngine *self = FU_ENGINE(user_data);
	g_debug("system acquiesced after %ums", self->acquiesce_delay);
	g_main_loop_quit(self->acquiesce_loop);
	self->acquiesce_id = 0;
	return G_SOURCE_REMOVE;
}

static void
fu_engine_acquiesce_reset(FuEngine *self)
{
	if (!g_main_loop_is_running(self->acquiesce_loop))
		return;
	g_debug("resetting system acquiesce timeout");
	if (self->acquiesce_id != 0)
		g_source_remove(self->acquiesce_id);
	self->acquiesce_id =
	    g_timeout_add(self->acquiesce_delay, fu_engine_acquiesce_timeout_cb, self);
}

static void
fu_engine_wait_for_acquiesce(FuEngine *self, guint acquiesce_delay)
{
	if (acquiesce_delay == 0)
		return;
	self->acquiesce_delay = acquiesce_delay;
	self->acquiesce_id = g_timeout_add(acquiesce_delay, fu_engine_acquiesce_timeout_cb, self);
	g_main_loop_run(self->acquiesce_loop);
}

static void
fu_engine_device_added_cb(FuDeviceList *device_list, FuDevice *device, FuEngine *self)
{
	fu_engine_watch_device(self, device);
	fu_engine_ensure_device_battery_inhibit(self, device);
	fu_engine_ensure_device_lid_inhibit(self, device);
	fu_engine_acquiesce_reset(self);
	g_signal_emit(self, signals[SIGNAL_DEVICE_ADDED], 0, device);
}

static void
fu_engine_device_runner_device_removed(FuEngine *self, FuDevice *device)
{
	GPtrArray *plugins = fu_plugin_list_get_all(self->plugin_list);
	for (guint j = 0; j < plugins->len; j++) {
		FuPlugin *plugin_tmp = g_ptr_array_index(plugins, j);
		fu_plugin_runner_device_removed(plugin_tmp, device);
	}
}

static void
fu_engine_device_removed_cb(FuDeviceList *device_list, FuDevice *device, FuEngine *self)
{
	fu_engine_device_runner_device_removed(self, device);
	fu_engine_acquiesce_reset(self);
	g_signal_handlers_disconnect_by_data(device, self);
	g_signal_emit(self, signals[SIGNAL_DEVICE_REMOVED], 0, device);
}

static void
fu_engine_device_changed_cb(FuDeviceList *device_list, FuDevice *device, FuEngine *self)
{
	fu_engine_watch_device(self, device);
	fu_engine_emit_device_changed(self, fu_device_get_id(device));
	fu_engine_acquiesce_reset(self);
}

static gchar *
fu_engine_request_get_localized_xpath(FuEngineRequest *request, const gchar *element)
{
	GString *xpath = g_string_new(element);
	const gchar *locale = NULL;

	/* optional; not set in tests */
	if (request != NULL)
		locale = fu_engine_request_get_locale(request);

	/* prefer the users locale if set */
	if (locale != NULL) {
		g_autofree gchar *xpath_locale = NULL;
		xpath_locale = g_strdup_printf("%s[@xml:lang='%s']|", element, locale);
		g_string_prepend(xpath, xpath_locale);
	}
	return g_string_free(xpath, FALSE);
}

/* add any client-side BKC tags */
static gboolean
fu_engine_add_local_release_metadata(FuEngine *self, FuRelease *release, GError **error)
{
	FuDevice *dev = fu_release_get_device(release);
	GPtrArray *guids;
	g_autoptr(XbQuery) query = NULL;
	g_autoptr(GError) error_query = NULL;

	/* no device matched */
	if (dev == NULL)
		return TRUE;

	/* prepare query with bound GUID parameter */
	query = xb_query_new_full(self->silo,
				  "local/components/component[@merge='append']/provides/"
				  "firmware[text()=?]/../../releases/release[@version=?]/../../"
				  "tags/tag",
				  XB_QUERY_FLAG_OPTIMIZE | XB_QUERY_FLAG_USE_INDEXES,
				  &error_query);
	if (query == NULL) {
		if (g_error_matches(error_query, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
			return TRUE;
		g_propagate_error(error, g_steal_pointer(&error_query));
		return FALSE;
	}

	/* use prepared query for each GUID */
	guids = fu_device_get_guids(dev);
	for (guint i = 0; i < guids->len; i++) {
		const gchar *guid = g_ptr_array_index(guids, i);
		g_autoptr(GError) error_local = NULL;
		g_autoptr(GPtrArray) tags = NULL;
#if LIBXMLB_CHECK_VERSION(0, 3, 0)
		g_auto(XbQueryContext) context = XB_QUERY_CONTEXT_INIT();
#endif

		/* bind GUID and then query */
#if LIBXMLB_CHECK_VERSION(0, 3, 0)
		xb_value_bindings_bind_str(xb_query_context_get_bindings(&context), 0, guid, NULL);
		xb_value_bindings_bind_str(xb_query_context_get_bindings(&context),
					   1,
					   fu_release_get_version(release),
					   NULL);
		tags = xb_silo_query_with_context(self->silo, query, &context, &error_local);
#else
		if (!xb_query_bind_str(query, 0, guid, error)) {
			g_prefix_error(error, "failed to bind GUID: ");
			return FALSE;
		}
		if (!xb_query_bind_str(query, 1, fu_release_get_version(release), error)) {
			g_prefix_error(error, "failed to bind version: ");
			return FALSE;
		}
		tags = xb_silo_query_full(self->silo, query, &error_local);
#endif
		if (tags == NULL) {
			if (g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
			    g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
				continue;
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
		for (guint j = 0; j < tags->len; j++) {
			XbNode *tag = g_ptr_array_index(tags, j);
			fu_release_add_tag(release, xb_node_get_text(tag));
		}
	}

	/* success */
	return TRUE;
}

static void
fu_engine_release_remote_id_changed_cb(FuRelease *release, GParamSpec *pspec, FuEngine *self)
{
	FwupdRemote *remote;
	const gchar *remote_id = fwupd_release_get_remote_id(FWUPD_RELEASE(release));

	if (remote_id == NULL)
		return;
	remote = fu_remote_list_get_by_id(self->remote_list, remote_id);
	if (remote == NULL) {
		g_warning("no remote found for %s", remote_id);
		return;
	}
	fu_release_set_remote(release, remote);
}

static gboolean
fu_engine_load_release(FuEngine *self,
		       FuRelease *release,
		       XbNode *component,
		       XbNode *rel,
		       FwupdInstallFlags install_flags,
		       GError **error)
{
	/* load release from XML */
	fu_release_set_config(release, self->config);

	/* set the FwupdRemote when the remote ID is set */
	g_signal_connect(FU_RELEASE(release),
			 "notify::remote-id",
			 G_CALLBACK(fu_engine_release_remote_id_changed_cb),
			 self);

	/* requirements we can check without the daemon */
	if (!fu_release_load(release, component, rel, install_flags, error))
		return FALSE;

	/* additional requirements */
	if (!fu_engine_check_requirements(self, release, install_flags, error))
		return FALSE;

	/* add any client-side BKC tags */
	if (!fu_engine_add_local_release_metadata(self, release, error))
		return FALSE;

	/* success */
	return TRUE;
}

/* finds the remote-id for the first firmware in the silo that matches this
 * container checksum */
static const gchar *
fu_engine_get_remote_id_for_checksum(FuEngine *self, const gchar *csum)
{
	g_autofree gchar *xpath = NULL;
	g_autoptr(XbNode) key = NULL;
	xpath = g_strdup_printf("components/component[@type='firmware']/releases/release/"
				"checksum[@target='container'][text()='%s']/../../"
				"../../custom/value[@key='fwupd::RemoteId']",
				csum);
	key = xb_silo_query_first(self->silo, xpath, NULL);
	if (key == NULL)
		return NULL;
	return xb_node_get_text(key);
}

/**
 * fu_engine_unlock:
 * @self: a #FuEngine
 * @device_id: a device ID
 * @error: (nullable): optional return location for an error
 *
 * Unlocks a device.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_unlock(FuEngine *self, const gchar *device_id, GError **error)
{
	FuPlugin *plugin;
	g_autoptr(FuDevice) device = NULL;

	g_return_val_if_fail(FU_IS_ENGINE(self), FALSE);
	g_return_val_if_fail(device_id != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* check the device exists */
	device = fu_device_list_get_by_id(self->device_list, device_id, error);
	if (device == NULL)
		return FALSE;

	/* get the plugin */
	plugin =
	    fu_plugin_list_find_by_name(self->plugin_list, fu_device_get_plugin(device), error);
	if (plugin == NULL)
		return FALSE;

	/* run the correct plugin that added this */
	if (!fu_plugin_runner_unlock(plugin, device, error))
		return FALSE;

	/* make the UI update */
	fu_engine_emit_device_changed_safe(self, device);
	fu_engine_emit_changed(self);
	return TRUE;
}

gboolean
fu_engine_modify_config(FuEngine *self, const gchar *key, const gchar *value, GError **error)
{
	const gchar *keys[] = {"ArchiveSizeMax",
			       "ApprovedFirmware",
			       "BlockedFirmware",
			       "DisabledDevices",
			       "DisabledPlugins",
			       "EnumerateAllDevices",
			       "HostBkc",
			       "IdleTimeout",
			       "IgnorePower",
			       "OnlyTrusted",
			       "UpdateMotd",
			       "UriSchemes",
			       "VerboseDomains",
			       NULL};

	g_return_val_if_fail(FU_IS_ENGINE(self), FALSE);
	g_return_val_if_fail(key != NULL, FALSE);
	g_return_val_if_fail(value != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* check keys are valid */
	if (!g_strv_contains(keys, key)) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "key %s not supported", key);
		return FALSE;
	}

	/* modify, effective next reboot */
	return fu_config_set_key_value(self->config, key, value, error);
}

/**
 * fu_engine_modify_remote:
 * @self: a #FuEngine
 * @remote_id: a remote ID
 * @key: the key, e.g. `Enabled`
 * @value: the key, e.g. `true`
 * @error: (nullable): optional return location for an error
 *
 * Updates the verification silo entry for a specific device.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_modify_remote(FuEngine *self,
			const gchar *remote_id,
			const gchar *key,
			const gchar *value,
			GError **error)
{
	const gchar *keys[] = {
	    "ApprovalRequired",
	    "AutomaticReports",
	    "AutomaticSecurityReports",
	    "Enabled",
	    "FirmwareBaseURI",
	    "MetadataURI",
	    "ReportURI",
	    "SecurityReportURI",
	    NULL,
	};

	/* check keys are valid */
	if (!g_strv_contains(keys, key)) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "key %s not supported", key);
		return FALSE;
	}
	return fu_remote_list_set_key_value(self->remote_list, remote_id, key, value, error);
}

/**
 * fu_engine_modify_device:
 * @self: a #FuEngine
 * @device_id: a device ID
 * @key: the key, e.g. `Flags`
 * @value: the key, e.g. `reported`
 * @error: (nullable): optional return location for an error
 *
 * Sets the reported flag for a specific device. This ensures that other
 * front-end clients for fwupd do not report the same event.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_modify_device(FuEngine *self,
			const gchar *device_id,
			const gchar *key,
			const gchar *value,
			GError **error)
{
	g_autoptr(FuDevice) device = NULL;

	/* find the correct device */
	device = fu_history_get_device_by_id(self->history, device_id, error);
	if (device == NULL)
		return FALSE;

	/* support adding a subset of device flags */
	if (g_strcmp0(key, "Flags") == 0) {
		FwupdDeviceFlags flag = fwupd_device_flag_from_string(value);
		if (flag == FWUPD_DEVICE_FLAG_UNKNOWN) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "key %s not a valid flag",
				    key);
			return FALSE;
		}
		if (flag != FWUPD_DEVICE_FLAG_REPORTED && flag != FWUPD_DEVICE_FLAG_NOTIFIED) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "flag %s cannot be set from client",
				    key);
			return FALSE;
		}
		fu_device_add_flag(device, flag);
		return fu_history_modify_device(self->history, device, error);
	}

	/* others invalid */
	g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "key %s not supported", key);
	return FALSE;
}

static const gchar *
fu_engine_checksum_type_to_string(GChecksumType checksum_type)
{
	if (checksum_type == G_CHECKSUM_SHA1)
		return "sha1";
	if (checksum_type == G_CHECKSUM_SHA256)
		return "sha256";
	if (checksum_type == G_CHECKSUM_SHA512)
		return "sha512";
	return "sha1";
}

/**
 * fu_engine_verify_update:
 * @self: a #FuEngine
 * @device_id: a device ID
 * @error: (nullable): optional return location for an error
 *
 * Updates the verification silo entry for a specific device.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_verify_update(FuEngine *self,
			const gchar *device_id,
			FuProgress *progress,
			GError **error)
{
	FuPlugin *plugin;
	GPtrArray *checksums;
	GPtrArray *guids;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *localstatedir = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderNode) component = NULL;
	g_autoptr(XbBuilderNode) provides = NULL;
	g_autoptr(XbBuilderNode) release = NULL;
	g_autoptr(XbBuilderNode) releases = NULL;
	g_autoptr(XbSilo) silo = NULL;

	g_return_val_if_fail(FU_IS_ENGINE(self), FALSE);
	g_return_val_if_fail(device_id != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* check the devices still exists */
	device = fu_device_list_get_by_id(self->device_list, device_id, error);
	if (device == NULL)
		return FALSE;

	/* get the plugin */
	plugin =
	    fu_plugin_list_find_by_name(self->plugin_list, fu_device_get_plugin(device), error);
	if (plugin == NULL)
		return FALSE;

	/* get the checksum */
	checksums = fu_device_get_checksums(device);
	if (checksums->len == 0) {
		if (!fu_plugin_runner_verify(plugin,
					     device,
					     progress,
					     FU_PLUGIN_VERIFY_FLAG_NONE,
					     error))
			return FALSE;
		fu_engine_emit_device_changed_safe(self, device);
	}

	/* we got nothing */
	if (checksums->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "device verification not supported");
		return FALSE;
	}

	/* build XML */
	component = xb_builder_node_insert(NULL, "component", "type", "firmware", NULL);
	provides = xb_builder_node_insert(component, "provides", NULL);
	guids = fu_device_get_guids(device);
	for (guint i = 0; i < guids->len; i++) {
		const gchar *guid = g_ptr_array_index(guids, i);
		g_autoptr(XbBuilderNode) provide = NULL;
		provide = xb_builder_node_insert(provides, "firmware", "type", "flashed", NULL);
		xb_builder_node_set_text(provide, guid, -1);
	}
	releases = xb_builder_node_insert(component, "releases", NULL);
	release = xb_builder_node_insert(releases,
					 "release",
					 "version",
					 fu_device_get_version(device),
					 NULL);
	for (guint i = 0; i < checksums->len; i++) {
		const gchar *checksum = g_ptr_array_index(checksums, i);
		GChecksumType kind = fwupd_checksum_guess_kind(checksum);
		g_autoptr(XbBuilderNode) csum = NULL;
		csum = xb_builder_node_insert(release,
					      "checksum",
					      "type",
					      fu_engine_checksum_type_to_string(kind),
					      "target",
					      "content",
					      NULL);
		xb_builder_node_set_text(csum, checksum, -1);
	}
	xb_builder_import_node(builder, component);

	/* save silo */
	localstatedir = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR_PKG);
	fn = g_strdup_printf("%s/verify/%s.xml", localstatedir, device_id);
	if (!fu_path_mkdir_parent(fn, error))
		return FALSE;
	file = g_file_new_for_path(fn);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, error);
	if (silo == NULL)
		return FALSE;
	if (!xb_silo_export_file(silo, file, XB_NODE_EXPORT_FLAG_FORMAT_MULTILINE, NULL, error))
		return FALSE;

	/* success */
	return TRUE;
}

static XbNode *
fu_engine_get_component_by_guid(FuEngine *self, const gchar *guid)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(XbNode) component = NULL;
#if LIBXMLB_CHECK_VERSION(0, 3, 0)
	g_auto(XbQueryContext) context = XB_QUERY_CONTEXT_INIT();
#endif

	/* no components in silo */
	if (self->query_component_by_guid == NULL)
		return NULL;

#if LIBXMLB_CHECK_VERSION(0, 3, 0)
	xb_query_context_set_flags(&context, XB_QUERY_FLAG_USE_INDEXES);
	xb_value_bindings_bind_str(xb_query_context_get_bindings(&context), 0, guid, NULL);
	component = xb_silo_query_first_with_context(self->silo,
						     self->query_component_by_guid,
						     &context,
						     &error_local);
#else
	if (!xb_query_bind_str(self->query_component_by_guid, 0, guid, &error_local)) {
		g_warning("failed to bind 0: %s", error_local->message);
		return NULL;
	}
	component =
	    xb_silo_query_first_full(self->silo, self->query_component_by_guid, &error_local);
#endif
	if (component == NULL) {
		if (!g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) &&
		    !g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
			g_warning("ignoring: %s", error_local->message);
		return NULL;
	}
	return g_object_ref(component);
}

XbNode *
fu_engine_get_component_by_guids(FuEngine *self, FuDevice *device)
{
	GPtrArray *guids = fu_device_get_guids(device);
	XbNode *component = NULL;
	for (guint i = 0; i < guids->len; i++) {
		const gchar *guid = g_ptr_array_index(guids, i);
		component = fu_engine_get_component_by_guid(self, guid);
		if (component != NULL)
			break;
	}
	return component;
}

static XbNode *
fu_engine_verify_from_local_metadata(FuEngine *self, FuDevice *device, GError **error)
{
	g_autofree gchar *fn = NULL;
	g_autofree gchar *localstatedir = NULL;
	g_autofree gchar *xpath = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbNode) release = NULL;
	g_autoptr(XbSilo) silo = NULL;

	localstatedir = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR_PKG);
	fn = g_strdup_printf("%s/verify/%s.xml", localstatedir, fu_device_get_id(device));
	file = g_file_new_for_path(fn);
	if (!g_file_query_exists(file, NULL)) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "failed to find %s", fn);
		return NULL;
	}

	if (!xb_builder_source_load_file(source, file, XB_BUILDER_SOURCE_FLAG_NONE, NULL, error))
		return NULL;
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, error);
	if (silo == NULL)
		return NULL;
	xpath = g_strdup_printf("component/releases/release[@version='%s']",
				fu_device_get_version(device));
	release = xb_silo_query_first(silo, xpath, error);
	if (release == NULL)
		return NULL;

	/* silo has to have same lifecycle as node */
	g_object_set_data_full(G_OBJECT(release),
			       "XbSilo",
			       g_steal_pointer(&silo),
			       (GDestroyNotify)g_object_unref);
	return g_steal_pointer(&release);
}

static XbNode *
fu_engine_verify_from_system_metadata(FuEngine *self, FuDevice *device, GError **error)
{
	FwupdVersionFormat fmt = fu_device_get_version_format(device);
	GPtrArray *guids = fu_device_get_guids(device);
	g_autoptr(XbQuery) query = NULL;

	/* prepare query with bound GUID parameter */
	query = xb_query_new_full(self->silo,
				  "components/component[@type='firmware']/"
				  "provides/firmware[@type='flashed'][text()=?]/"
				  "../../releases/release",
				  XB_QUERY_FLAG_OPTIMIZE | XB_QUERY_FLAG_USE_INDEXES,
				  error);
	if (query == NULL)
		return NULL;

	/* use prepared query for each GUID */
	for (guint i = 0; i < guids->len; i++) {
		const gchar *guid = g_ptr_array_index(guids, i);
		g_autoptr(GError) error_local = NULL;
		g_autoptr(GPtrArray) releases = NULL;
#if LIBXMLB_CHECK_VERSION(0, 3, 0)
		g_auto(XbQueryContext) context = XB_QUERY_CONTEXT_INIT();
#endif

		/* bind GUID and then query */
#if LIBXMLB_CHECK_VERSION(0, 3, 0)
		xb_value_bindings_bind_str(xb_query_context_get_bindings(&context), 0, guid, NULL);
		releases = xb_silo_query_with_context(self->silo, query, &context, &error_local);
#else
		if (!xb_query_bind_str(query, 0, guid, error)) {
			g_prefix_error(error, "failed to bind string: ");
			return NULL;
		}
		releases = xb_silo_query_full(self->silo, query, &error_local);
#endif
		if (releases == NULL) {
			if (g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
			    g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT)) {
				g_debug("could not find %s: %s", guid, error_local->message);
				continue;
			}
			g_propagate_error(error, g_steal_pointer(&error_local));
			return NULL;
		}
		for (guint j = 0; j < releases->len; j++) {
			XbNode *rel = g_ptr_array_index(releases, j);
			const gchar *rel_ver = xb_node_get_attr(rel, "version");
			g_autofree gchar *tmp_ver = fu_version_parse_from_format(rel_ver, fmt);
			if (fu_version_compare(tmp_ver, fu_device_get_version(device), fmt) == 0)
				return g_object_ref(rel);
		}
	}

	/* not found */
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "failed to find release");
	return NULL;
}

/**
 * fu_engine_verify:
 * @self: a #FuEngine
 * @device_id: a device ID
 * @error: (nullable): optional return location for an error
 *
 * Verifies a device firmware checksum using the verification silo entry.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_verify(FuEngine *self, const gchar *device_id, FuProgress *progress, GError **error)
{
	FuPlugin *plugin;
	GPtrArray *checksums;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GString) xpath_csum = g_string_new(NULL);
	g_autoptr(XbNode) csum = NULL;
	g_autoptr(XbNode) release = NULL;

	g_return_val_if_fail(FU_IS_ENGINE(self), FALSE);
	g_return_val_if_fail(device_id != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* check the id exists */
	device = fu_device_list_get_by_id(self->device_list, device_id, error);
	if (device == NULL)
		return FALSE;

	/* get the plugin */
	plugin =
	    fu_plugin_list_find_by_name(self->plugin_list, fu_device_get_plugin(device), error);
	if (plugin == NULL)
		return FALSE;

	/* update the device firmware hashes if possible */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE)) {
		if (!fu_plugin_runner_verify(plugin,
					     device,
					     progress,
					     FU_PLUGIN_VERIFY_FLAG_NONE,
					     error))
			return FALSE;
	}

	/* find component in local metadata */
	release = fu_engine_verify_from_local_metadata(self, device, &error_local);
	if (release == NULL) {
		if (!g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) &&
		    !g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT)) {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
	}

	/* try again with the system metadata */
	if (release == NULL) {
		g_autoptr(GError) error_system = NULL;
		release = fu_engine_verify_from_system_metadata(self, device, &error_system);
		if (release == NULL) {
			if (!g_error_matches(error_system, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) &&
			    !g_error_matches(error_system,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_ARGUMENT)) {
				g_propagate_error(error, g_steal_pointer(&error_system));
				return FALSE;
			}
		}
	}
	if (release == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "No release found for version %s",
			    fu_device_get_version(device));
		return FALSE;
	}

	/* get the matching checksum */
	checksums = fu_device_get_checksums(device);
	if (checksums->len == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "No device checksums for %s",
			    fu_device_get_version(device));
		return FALSE;
	}

	/* do any of the checksums in the release match any in the device */
	for (guint j = 0; j < checksums->len; j++) {
		const gchar *hash_tmp = g_ptr_array_index(checksums, j);
		xb_string_append_union(xpath_csum,
				       "checksum[@target='device'][text()='%s']",
				       hash_tmp);
		xb_string_append_union(xpath_csum,
				       "checksum[@target='content'][text()='%s']",
				       hash_tmp);
	}
	csum = xb_node_query_first(release, xpath_csum->str, NULL);
	if (csum == NULL) {
		g_autoptr(GString) checksums_device = g_string_new(NULL);
		g_autoptr(GString) checksums_metadata = g_string_new(NULL);
		g_autoptr(GPtrArray) csums = NULL;
		g_autoptr(GString) xpath = g_string_new(NULL);

		/* get all checksums to display a useful error */
		xb_string_append_union(xpath, "checksum[@target='device']");
		if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE))
			xb_string_append_union(xpath, "checksum[@target='content']");
		csums = xb_node_query(release, xpath->str, 0, NULL);
		if (csums == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "No stored checksums for %s",
				    fu_device_get_version(device));
			return FALSE;
		}
		for (guint i = 0; i < csums->len; i++) {
			XbNode *csum_tmp = g_ptr_array_index(csums, i);
			xb_string_append_union(checksums_metadata,
					       "%s",
					       xb_node_get_text(csum_tmp));
		}
		for (guint i = 0; i < checksums->len; i++) {
			const gchar *hash_tmp = g_ptr_array_index(checksums, i);
			xb_string_append_union(checksums_device, "%s", hash_tmp);
		}
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "For %s %s expected %s, got %s",
			    fu_device_get_name(device),
			    fu_device_get_version(device),
			    checksums_metadata->str,
			    checksums_device->str);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_engine_require_vercmp(XbNode *req, const gchar *version, FwupdVersionFormat fmt, GError **error)
{
	gboolean ret = FALSE;
	gint rc = 0;
	const gchar *tmp = xb_node_get_attr(req, "compare");
	const gchar *version_req = xb_node_get_attr(req, "version");

	if (g_strcmp0(tmp, "eq") == 0) {
		rc = fu_version_compare(version, version_req, fmt);
		ret = rc == 0;
	} else if (g_strcmp0(tmp, "ne") == 0) {
		rc = fu_version_compare(version, version_req, fmt);
		ret = rc != 0;
	} else if (g_strcmp0(tmp, "lt") == 0) {
		rc = fu_version_compare(version, version_req, fmt);
		ret = rc < 0;
	} else if (g_strcmp0(tmp, "gt") == 0) {
		rc = fu_version_compare(version, version_req, fmt);
		ret = rc > 0;
	} else if (g_strcmp0(tmp, "le") == 0) {
		rc = fu_version_compare(version, version_req, fmt);
		ret = rc <= 0;
	} else if (g_strcmp0(tmp, "ge") == 0) {
		rc = fu_version_compare(version, version_req, fmt);
		ret = rc >= 0;
	} else if (g_strcmp0(tmp, "glob") == 0) {
		ret = fu_path_fnmatch(version_req, version);
	} else if (g_strcmp0(tmp, "regex") == 0) {
		ret = g_regex_match_simple(version_req, version, 0, 0);
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to compare [%s] and [%s]",
			    version_req,
			    version);
		return FALSE;
	}

	/* set error */
	if (!ret) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed predicate [%s %s %s]",
			    version_req,
			    tmp,
			    version);
	}
	return ret;
}

static gboolean
fu_engine_check_requirement_not_child(FuEngine *self, XbNode *req, FuDevice *device, GError **error)
{
	GPtrArray *children = fu_device_get_children(device);

	/* only <firmware> supported */
	if (g_strcmp0(xb_node_get_element(req), "firmware") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot handle not-child %s requirement",
			    xb_node_get_element(req));
		return FALSE;
	}

	/* check each child */
	for (guint i = 0; i < children->len; i++) {
		FuDevice *child = g_ptr_array_index(children, i);
		const gchar *version = fu_device_get_version(child);
		if (version == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no version provided by %s, child of %s",
				    fu_device_get_name(child),
				    fu_device_get_name(device));
			return FALSE;
		}
		if (fu_engine_require_vercmp(req,
					     version,
					     fu_device_get_version_format(child),
					     NULL)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Not compatible with child device version %s",
				    version);
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_engine_check_requirement_vendor_id(FuEngine *self, XbNode *req, FuDevice *device, GError **error)
{
	GPtrArray *vendor_ids;
	const gchar *vendor_ids_metadata;
	g_autofree gchar *vendor_ids_device = NULL;

	/* devices without vendor IDs should not exist! */
	vendor_ids = fu_device_get_vendor_ids(device);
	if (vendor_ids->len == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "device [%s] has no vendor ID",
			    fu_device_get_id(device));
		return FALSE;
	}

	/* metadata with empty vendor IDs should not exist! */
	vendor_ids_metadata = xb_node_get_attr(req, "version");
	if (vendor_ids_metadata == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "metadata has no vendor ID");
		return FALSE;
	}

	/* it is always safe to use a regex, even for simple strings */
	vendor_ids_device = fu_strjoin("|", vendor_ids);
	if (!g_regex_match_simple(vendor_ids_metadata, vendor_ids_device, 0, 0)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "Not compatible with vendor %s: got %s",
			    vendor_ids_device,
			    vendor_ids_metadata);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_engine_check_requirement_firmware(FuEngine *self,
				     XbNode *req,
				     FuDevice *device,
				     FwupdInstallFlags flags,
				     GError **error)
{
	guint64 depth;
	g_autoptr(FuDevice) device_actual = g_object_ref(device);
	g_autoptr(GError) error_local = NULL;

	/* look at the parent device */
	depth = xb_node_get_attr_as_uint(req, "depth");
	if (depth != G_MAXUINT64) {
		for (guint64 i = 0; i < depth; i++) {
			FuDevice *device_tmp = fu_device_get_parent(device_actual);
			if (device_actual == NULL) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "No parent device for %s "
					    "(%" G_GUINT64_FORMAT "/%" G_GUINT64_FORMAT ")",
					    fu_device_get_name(device_actual),
					    i,
					    depth);
				return FALSE;
			}
			g_set_object(&device_actual, device_tmp);
		}
	}

	/* old firmware version */
	if (xb_node_get_text(req) == NULL) {
		const gchar *version = fu_device_get_version(device_actual);
		if (!fu_engine_require_vercmp(req,
					      version,
					      fu_device_get_version_format(device_actual),
					      &error_local)) {
			if (g_strcmp0(xb_node_get_attr(req, "compare"), "ge") == 0) {
				g_set_error(
				    error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "Not compatible with firmware version %s, requires >= %s",
				    version,
				    xb_node_get_attr(req, "version"));
			} else {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "Not compatible with firmware version: %s",
					    error_local->message);
			}
			return FALSE;
		}
		return TRUE;
	}

	/* bootloader version */
	if (g_strcmp0(xb_node_get_text(req), "bootloader") == 0) {
		const gchar *version = fu_device_get_version_bootloader(device_actual);
		if (!fu_engine_require_vercmp(req,
					      version,
					      fu_device_get_version_format(device_actual),
					      &error_local)) {
			if (g_strcmp0(xb_node_get_attr(req, "compare"), "ge") == 0) {
				g_set_error(
				    error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Not compatible with bootloader version %s, requires >= %s",
				    version,
				    xb_node_get_attr(req, "version"));

			} else {
				g_debug("Bootloader is not compatible: %s", error_local->message);
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_NOT_SUPPORTED,
						    "Bootloader is not compatible");
			}
			return FALSE;
		}
		return TRUE;
	}

	/* vendor ID */
	if (g_strcmp0(xb_node_get_text(req), "vendor-id") == 0) {
		if (flags & FWUPD_INSTALL_FLAG_IGNORE_VID_PID)
			return TRUE;
		return fu_engine_check_requirement_vendor_id(self, req, device_actual, error);
	}

	/* child version */
	if (g_strcmp0(xb_node_get_text(req), "not-child") == 0)
		return fu_engine_check_requirement_not_child(self, req, device_actual, error);

	/* another device */
	if (fwupd_guid_is_valid(xb_node_get_text(req))) {
		const gchar *guid = xb_node_get_text(req);
		const gchar *version;

		/* find if the other device exists */
		if (depth == G_MAXUINT64) {
			g_autoptr(FuDevice) device_tmp = NULL;
			device_tmp = fu_device_list_get_by_guid(self->device_list, guid, error);
			if (device_tmp == NULL)
				return FALSE;
			g_set_object(&device_actual, device_tmp);
			/* look for a sibling */
		} else if (depth == 0) {
			FuDevice *child = NULL;
			FuDevice *parent = fu_device_get_parent(device_actual);
			GPtrArray *children;
			if (parent == NULL) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "No parent specified for device %s",
					    fu_device_get_name(device_actual));
				return FALSE;
			}
			children = fu_device_get_children(parent);
			for (guint i = 0; i < children->len; i++) {
				child = g_ptr_array_index(children, i);
				if (fu_device_has_guid(child, guid))
					break;
				child = NULL;
			}
			if (child == NULL) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "No sibling found with GUID of %s",
					    guid);
				return FALSE;
			}
			g_set_object(&device_actual, child);
			/* verify the parent device has the GUID */
		} else {
			if (!fu_device_has_guid(device_actual, guid)) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "No GUID of %s on parent device %s",
					    guid,
					    fu_device_get_name(device_actual));
				return FALSE;
			}
		}

		/* get the version of the other device */
		version = fu_device_get_version(device_actual);
		if (version != NULL && xb_node_get_attr(req, "compare") != NULL &&
		    !fu_engine_require_vercmp(req,
					      version,
					      fu_device_get_version_format(device_actual),
					      &error_local)) {
			if (g_strcmp0(xb_node_get_attr(req, "compare"), "ge") == 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "Not compatible with %s version %s, requires >= %s",
					    fu_device_get_name(device_actual),
					    version,
					    xb_node_get_attr(req, "version"));
			} else {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "Not compatible with %s: %s",
					    fu_device_get_name(device_actual),
					    error_local->message);
			}
			return FALSE;
		}
		return TRUE;
	}

	/* not supported */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "cannot handle firmware requirement '%s'",
		    xb_node_get_text(req));
	return FALSE;
}

static gboolean
fu_engine_check_requirement_id(FuEngine *self, XbNode *req, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	const gchar *version = g_hash_table_lookup(self->runtime_versions, xb_node_get_text(req));
	if (version == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "no version available for %s",
			    xb_node_get_text(req));
		return FALSE;
	}
	if (!fu_engine_require_vercmp(req, version, FWUPD_VERSION_FORMAT_UNKNOWN, &error_local)) {
		if (g_strcmp0(xb_node_get_attr(req, "compare"), "ge") == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "Not compatible with %s version %s, requires >= %s",
				    xb_node_get_text(req),
				    version,
				    xb_node_get_attr(req, "version"));
		} else {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "Not compatible with %s version: %s",
				    xb_node_get_text(req),
				    error_local->message);
		}
		return FALSE;
	}

	g_debug("requirement %s %s %s -> %s passed",
		xb_node_get_attr(req, "version"),
		xb_node_get_attr(req, "compare"),
		version,
		xb_node_get_text(req));
	return TRUE;
}

static gboolean
fu_engine_check_requirement_hardware(FuEngine *self, XbNode *req, GError **error)
{
	g_auto(GStrv) hwid_split = NULL;

	/* split and treat as OR */
	hwid_split = g_strsplit(xb_node_get_text(req), "|", -1);
	for (guint i = 0; hwid_split[i] != NULL; i++) {
		if (fu_context_has_hwid_guid(self->ctx, hwid_split[i])) {
			g_debug("HWID provided %s", hwid_split[i]);
			return TRUE;
		}
	}

	/* nothing matched */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_FILE,
		    "no HWIDs matched %s",
		    xb_node_get_text(req));
	return FALSE;
}

static gboolean
fu_engine_check_requirement_client(FuEngine *self,
				   FuEngineRequest *request,
				   XbNode *req,
				   GError **error)
{
	FwupdFeatureFlags flags;
	g_auto(GStrv) feature_split = NULL;

	/* split and treat as AND */
	feature_split = g_strsplit(xb_node_get_text(req), "|", -1);
	flags = fu_engine_request_get_feature_flags(request);
	for (guint i = 0; feature_split[i] != NULL; i++) {
		FwupdFeatureFlags flag = fwupd_feature_flag_from_string(feature_split[i]);

		/* not recognized */
		if (flag == FWUPD_FEATURE_FLAG_LAST) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "client requirement %s unknown",
				    feature_split[i]);
			return FALSE;
		}

		/* not supported */
		if ((flags & flag) == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "client requirement %s not supported",
				    feature_split[i]);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_engine_check_requirement(FuEngine *self,
			    FuRelease *release,
			    XbNode *req,
			    FwupdInstallFlags flags,
			    GError **error)
{
	FuDevice *device = fu_release_get_device(release);
	FuEngineRequest *request = fu_release_get_request(release);

	/* ensure component requirement */
	if (g_strcmp0(xb_node_get_element(req), "id") == 0)
		return fu_engine_check_requirement_id(self, req, error);

	/* ensure firmware requirement */
	if (g_strcmp0(xb_node_get_element(req), "firmware") == 0) {
		if (device == NULL)
			return TRUE;
		return fu_engine_check_requirement_firmware(self, req, device, flags, error);
	}

	/* ensure hardware requirement */
	if (g_strcmp0(xb_node_get_element(req), "hardware") == 0)
		return fu_engine_check_requirement_hardware(self, req, error);

	/* ensure client requirement */
	if (g_strcmp0(xb_node_get_element(req), "client") == 0)
		return fu_engine_check_requirement_client(self, request, req, error);

	/* not supported */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "cannot handle requirement type %s",
		    xb_node_get_element(req));
	return FALSE;
}

gboolean
fu_engine_check_trust(FuEngine *self, FuRelease *release, GError **error)
{
	if (fu_config_get_only_trusted(self->config) &&
	    (fu_release_get_trust_flags(release) & FWUPD_TRUST_FLAG_PAYLOAD) == 0) {
		g_autofree gchar *sysconfdir = fu_path_from_kind(FU_PATH_KIND_SYSCONFDIR_PKG);
		g_autofree gchar *fn = g_build_filename(sysconfdir, "daemon.conf", NULL);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware signature missing or not trusted; "
			    "set OnlyTrusted=false in %s ONLY if you are a firmware developer",
			    fn);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_engine_check_soft_requirement(FuEngine *self,
				 FuRelease *release,
				 XbNode *req,
				 FwupdInstallFlags flags,
				 GError **error)
{
	g_autoptr(GError) error_local = NULL;
	if (!fu_engine_check_requirement(self, release, req, flags, &error_local)) {
		if (flags & FWUPD_INSTALL_FLAG_FORCE) {
			g_debug("ignoring soft-requirement due to --force: %s",
				error_local->message);
			return TRUE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_engine_check_requirements(FuEngine *self,
			     FuRelease *release,
			     FwupdInstallFlags flags,
			     GError **error)
{
	GPtrArray *reqs;

	/* hard requirements */
	reqs = fu_release_get_hard_reqs(release);
	if (reqs != NULL) {
		for (guint i = 0; i < reqs->len; i++) {
			XbNode *req = g_ptr_array_index(reqs, i);
			if (!fu_engine_check_requirement(self, release, req, flags, error))
				return FALSE;
		}
	}

	/* soft requirements */
	reqs = fu_release_get_soft_reqs(release);
	if (reqs != NULL) {
		for (guint i = 0; i < reqs->len; i++) {
			XbNode *req = g_ptr_array_index(reqs, i);
			if (!fu_engine_check_soft_requirement(self, release, req, flags, error))
				return FALSE;
		}
	}

	/* success */
	return TRUE;
}

void
fu_engine_idle_reset(FuEngine *self)
{
	fu_idle_reset(self->idle);
}

static gchar *
fu_engine_get_boot_time(void)
{
	g_autofree gchar *buf = NULL;
	g_auto(GStrv) lines = NULL;
	if (!g_file_get_contents("/proc/stat", &buf, NULL, NULL))
		return NULL;
	lines = g_strsplit(buf, "\n", -1);
	for (guint i = 0; lines[i] != NULL; i++) {
		if (g_str_has_prefix(lines[i], "btime "))
			return g_strdup(lines[i] + 6);
	}
	return NULL;
}

static gboolean
fu_engine_get_report_metadata_os_release(GHashTable *hash, GError **error)
{
	g_autoptr(GHashTable) os_release = NULL;
	struct {
		const gchar *key;
		const gchar *val;
	} distro_kv[] = {{"ID", "DistroId"},
			 {"VERSION_ID", "DistroVersion"},
			 {"VARIANT_ID", "DistroVariant"},
			 {NULL, NULL}};

	/* get all required os-release keys */
	os_release = fwupd_get_os_release(error);
	if (os_release == NULL)
		return FALSE;
	for (guint i = 0; distro_kv[i].key != NULL; i++) {
		const gchar *tmp = g_hash_table_lookup(os_release, distro_kv[i].key);
		if (tmp != NULL) {
			g_hash_table_insert(hash, g_strdup(distro_kv[i].val), g_strdup(tmp));
		}
	}
	return TRUE;
}

#ifdef __linux__
static gchar *
fu_engine_get_proc_cmdline(GError **error)
{
	gsize bufsz = 0;
	g_autofree gchar *buf = NULL;
	g_autoptr(GString) cmdline_safe = g_string_new(NULL);
	const gchar *ignore[] = {
	    "",
	    "auto",
	    "boot",
	    "BOOT_IMAGE",
	    "console",
	    "cryptdevice",
	    "cryptkey",
	    "earlycon",
	    "earlyprintk",
	    "ether",
	    "initrd",
	    "ip",
	    "LANG",
	    "loglevel",
	    "luks.key",
	    "luks.name",
	    "luks.options",
	    "luks.uuid",
	    "mount.usr",
	    "mount.usrflags",
	    "mount.usrfstype",
	    "netdev",
	    "netroot",
	    "nfsaddrs",
	    "nfs.nfs4_unique_id",
	    "nfsroot",
	    "noplymouth",
	    "ostree",
	    "quiet",
	    "rd.dm.uuid",
	    "rd.luks.allow-discards",
	    "rd.luks.key",
	    "rd.luks.name",
	    "rd.luks.options",
	    "rd.luks.uuid",
	    "rd.lvm.lv",
	    "rd.lvm.vg",
	    "rd.md.uuid",
	    "rd.systemd.mask",
	    "rd.systemd.wants",
	    "resume",
	    "resumeflags",
	    "rhgb",
	    "ro",
	    "root",
	    "rootflags",
	    "roothash",
	    "rw",
	    "showopts",
	    "splash",
	    "swap",
	    "systemd.mask",
	    "systemd.unit",
	    "systemd.verity_root_data",
	    "systemd.verity_root_hash",
	    "systemd.wants",
	    "verbose",
	    "vt.handoff",
	    "zfs",
	    NULL, /* last entry */
	};

	/* get a PII-safe kernel command line */
	if (!g_file_get_contents("/proc/cmdline", &buf, &bufsz, error))
		return NULL;
	if (bufsz > 0) {
		g_auto(GStrv) tokens = fu_strsplit(buf, bufsz - 1, " ", -1);
		for (guint i = 0; tokens[i] != NULL; i++) {
			g_auto(GStrv) kv = NULL;
			if (strlen(tokens[i]) == 0)
				continue;
			kv = g_strsplit(tokens[i], "=", 2);
			if (g_strv_contains(ignore, kv[0]))
				continue;
			if (cmdline_safe->len > 0)
				g_string_append(cmdline_safe, " ");
			g_string_append(cmdline_safe, tokens[i]);
		}
	}
	return g_string_free(g_steal_pointer(&cmdline_safe), FALSE);
}
#endif

static gboolean
fu_engine_get_report_metadata_kernel_cmdline(GHashTable *hash, GError **error)
{
	g_autofree gchar *cmdline = NULL;

#ifdef __linux__
	/* Linuxish */
	cmdline = fu_engine_get_proc_cmdline(error);
#elif defined(__FreeBSD__)
	/* BSDish */
	cmdline = fu_kenv_get_string("kernel_options", error);
#elif defined(_WIN32)
	/* Windows */
	cmdline = g_strdup("");
#else
	g_set_error_literal(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "cannot get KernelCmdline on this OS");
#endif
	if (cmdline == NULL)
		return FALSE;
	if (cmdline[0] != '\0') {
		g_hash_table_insert(hash, g_strdup("KernelCmdline"), g_steal_pointer(&cmdline));
	}
	return TRUE;
}

static void
fu_engine_add_report_metadata_bool(GHashTable *hash, const gchar *key, gboolean value)
{
	g_hash_table_insert(hash, g_strdup(key), g_strdup(value ? "True" : "False"));
}

GHashTable *
fu_engine_get_report_metadata(FuEngine *self, GError **error)
{
	const gchar *tmp;
	gchar *btime;
#ifdef HAVE_UTSNAME_H
	struct utsname name_tmp;
#endif
	g_autoptr(GHashTable) hash = NULL;
	g_autoptr(GList) compile_keys = g_hash_table_get_keys(self->compile_versions);
	g_autoptr(GList) runtime_keys = g_hash_table_get_keys(self->runtime_versions);

	/* convert all the runtime and compile-time versions */
	hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	for (GList *l = compile_keys; l != NULL; l = l->next) {
		const gchar *id = l->data;
		const gchar *version = g_hash_table_lookup(self->compile_versions, id);
		g_hash_table_insert(hash,
				    g_strdup_printf("CompileVersion(%s)", id),
				    g_strdup(version));
	}
	for (GList *l = runtime_keys; l != NULL; l = l->next) {
		const gchar *id = l->data;
		const gchar *version = g_hash_table_lookup(self->runtime_versions, id);
		g_hash_table_insert(hash,
				    g_strdup_printf("RuntimeVersion(%s)", id),
				    g_strdup(version));
	}
	if (!fu_engine_get_report_metadata_os_release(hash, error))
		return NULL;
	if (!fu_engine_get_report_metadata_kernel_cmdline(hash, error))
		return NULL;

	/* these affect the report credibility */
	fu_engine_add_report_metadata_bool(hash, "FwupdTainted", self->tainted);
#ifdef SUPPORTED_BUILD
	fu_engine_add_report_metadata_bool(hash, "FwupdSupported", TRUE);
#else
	fu_engine_add_report_metadata_bool(hash, "FwupdSupported", FALSE);
#endif

	/* find out what BKC is being targeted to understand "odd" upgrade paths */
	tmp = fu_config_get_host_bkc(self->config);
	if (tmp != NULL)
		g_hash_table_insert(hash, g_strdup("HostBkc"), g_strdup(tmp));

	/* DMI data */
	tmp = fu_context_get_hwid_value(self->ctx, FU_HWIDS_KEY_PRODUCT_NAME);
	if (tmp != NULL)
		g_hash_table_insert(hash, g_strdup("HostProduct"), g_strdup(tmp));
	tmp = fu_context_get_hwid_value(self->ctx, FU_HWIDS_KEY_FAMILY);
	if (tmp != NULL)
		g_hash_table_insert(hash, g_strdup("HostFamily"), g_strdup(tmp));
	tmp = fu_context_get_hwid_value(self->ctx, FU_HWIDS_KEY_PRODUCT_SKU);
	if (tmp != NULL)
		g_hash_table_insert(hash, g_strdup("HostSku"), g_strdup(tmp));
	tmp = fu_context_get_hwid_value(self->ctx, FU_HWIDS_KEY_MANUFACTURER);
	if (tmp != NULL)
		g_hash_table_insert(hash, g_strdup("HostVendor"), g_strdup(tmp));

		/* kernel version is often important for debugging failures */
#ifdef HAVE_UTSNAME_H
	memset(&name_tmp, 0, sizeof(struct utsname));
	if (uname(&name_tmp) >= 0) {
		g_hash_table_insert(hash, g_strdup("CpuArchitecture"), g_strdup(name_tmp.machine));
		g_hash_table_insert(hash, g_strdup("KernelName"), g_strdup(name_tmp.sysname));
		g_hash_table_insert(hash, g_strdup("KernelVersion"), g_strdup(name_tmp.release));
	}
#endif

	/* add the kernel boot time so we can detect a reboot */
	btime = fu_engine_get_boot_time();
	if (btime != NULL)
		g_hash_table_insert(hash, g_strdup("BootTime"), btime);

	return g_steal_pointer(&hash);
}

/**
 * fu_engine_composite_prepare:
 * @self: a #FuEngine
 * @devices: (element-type #FuDevice): devices that will be updated
 * @error: (nullable): optional return location for an error
 *
 * Calls into the plugin loader, informing each plugin of the pending upgrade(s).
 *
 * Any failure in any plugin will abort all of the actions before they are started.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_composite_prepare(FuEngine *self, GPtrArray *devices, GError **error)
{
	GPtrArray *plugins = fu_plugin_list_get_all(self->plugin_list);
	for (guint j = 0; j < plugins->len; j++) {
		FuPlugin *plugin_tmp = g_ptr_array_index(plugins, j);
		if (!fu_plugin_runner_composite_prepare(plugin_tmp, devices, error))
			return FALSE;
	}
	return TRUE;
}

/**
 * fu_engine_composite_cleanup:
 * @self: a #FuEngine
 * @devices: (element-type #FuDevice): devices that will be updated
 * @error: (nullable): optional return location for an error
 *
 * Calls into the plugin loader, informing each plugin of the pending upgrade(s).
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_composite_cleanup(FuEngine *self, GPtrArray *devices, GError **error)
{
	GPtrArray *plugins = fu_plugin_list_get_all(self->plugin_list);
	for (guint j = 0; j < plugins->len; j++) {
		FuPlugin *plugin_tmp = g_ptr_array_index(plugins, j);
		if (!fu_plugin_runner_composite_cleanup(plugin_tmp, devices, error))
			return FALSE;
	}
	return TRUE;
}

static gint
fu_engine_sort_release_versions_cb(gconstpointer a, gconstpointer b)
{
	FuRelease *na = *((FuRelease **)a);
	FuRelease *nb = *((FuRelease **)b);
	FuDevice *device = fu_release_get_device(na);
	const gchar *va = fu_release_get_version(na);
	const gchar *vb = fu_release_get_version(nb);
	return fu_version_compare(va, vb, fu_device_get_version_format(device));
}

/**
 * fu_engine_install_releases:
 * @self: a #FuEngine
 * @request: a #FuEngineRequest
 * @releases: (element-type FuRelease): a device
 * @blob_cab: the #GBytes of the .cab file
 * @flags: install flags, e.g. %FWUPD_DEVICE_FLAG_UPDATABLE
 * @error: (nullable): optional return location for an error
 *
 * Installs a specific firmware file on one or more install tasks.
 *
 * By this point all the requirements and tests should have been done in
 * fu_engine_check_requirements() so this should not fail before running
 * the plugin loader.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_install_releases(FuEngine *self,
			   FuEngineRequest *request,
			   GPtrArray *releases,
			   GBytes *blob_cab,
			   FuProgress *progress,
			   FwupdInstallFlags flags,
			   GError **error)
{
	g_autoptr(FuIdleLocker) locker = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) devices_new = NULL;

	/* do not allow auto-shutdown during this time */
	locker = fu_idle_locker_new(self->idle, "update");
	g_assert(locker != NULL);

	/* install these in the right order */
	g_ptr_array_sort(releases, fu_engine_sort_release_versions_cb);

	/* notify the plugins about the composite action */
	devices = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (guint i = 0; i < releases->len; i++) {
		FuRelease *release = g_ptr_array_index(releases, i);
		g_debug("composite update %u: %s",
			i + 1,
			fu_device_get_id(fu_release_get_device(release)));
		g_ptr_array_add(devices, g_object_ref(fu_release_get_device(release)));
	}
	if (!fu_engine_composite_prepare(self, devices, error)) {
		g_prefix_error(error, "failed to prepare composite action: ");
		return FALSE;
	}

	/* all authenticated, so install all the things */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, releases->len);
	for (guint i = 0; i < releases->len; i++) {
		FuRelease *release = g_ptr_array_index(releases, i);
		if (!fu_engine_install_release(self,
					       release,
					       blob_cab,
					       fu_progress_get_child(progress),
					       flags,
					       error)) {
			g_autoptr(GError) error_local = NULL;
			if (!fu_engine_composite_cleanup(self, devices, &error_local)) {
				g_warning("failed to cleanup failed composite action: %s",
					  error_local->message);
			}
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* set all the device statuses back to unknown */
	for (guint i = 0; i < releases->len; i++) {
		FuRelease *release = g_ptr_array_index(releases, i);
		FuDevice *device = fu_release_get_device(release);
		fwupd_device_set_status(FWUPD_DEVICE(device), FWUPD_STATUS_UNKNOWN);
	}

	/* get a new list of devices in case they replugged */
	devices_new = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device;
		g_autoptr(FuDevice) device_new = NULL;
		g_autoptr(GError) error_local = NULL;
		device = g_ptr_array_index(devices, i);
		device_new = fu_device_list_get_by_id(self->device_list,
						      fu_device_get_id(device),
						      &error_local);
		if (device_new == NULL) {
			g_debug("failed to find new device: %s", error_local->message);
			continue;
		}
		g_ptr_array_add(devices_new, g_steal_pointer(&device_new));
	}

	/* notify the plugins about the composite action */
	if (!fu_engine_composite_cleanup(self, devices_new, error)) {
		g_prefix_error(error, "failed to cleanup composite action: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_engine_add_release_metadata(FuEngine *self, FuRelease *release, FuPlugin *plugin, GError **error)
{
	GPtrArray *metadata_sources;
	g_autoptr(GHashTable) metadata_device = NULL;
	g_autoptr(GHashTable) metadata_hash = NULL;

	/* build the version metadata */
	metadata_hash = fu_engine_get_report_metadata(self, error);
	if (metadata_hash == NULL)
		return FALSE;
	fu_release_add_metadata(release, metadata_hash);
	if (fu_plugin_get_report_metadata(plugin) != NULL)
		fu_release_add_metadata(release, fu_plugin_get_report_metadata(plugin));
	metadata_device = fu_device_report_metadata_pre(fu_release_get_device(release));
	if (metadata_device != NULL)
		fu_release_add_metadata(release, metadata_device);

	/* allow other plugins to contribute metadata too */
	metadata_sources = fu_plugin_get_rules(plugin, FU_PLUGIN_RULE_METADATA_SOURCE);
	if (metadata_sources != NULL) {
		for (guint i = 0; i < metadata_sources->len; i++) {
			FuPlugin *plugin_tmp;
			const gchar *plugin_name = g_ptr_array_index(metadata_sources, i);
			g_autoptr(GError) error_local = NULL;

			plugin_tmp = fu_plugin_list_find_by_name(self->plugin_list,
								 plugin_name,
								 &error_local);
			if (plugin_tmp == NULL) {
				g_warning("could not add metadata for %s: %s",
					  plugin_name,
					  error_local->message);
				continue;
			}
			if (fu_plugin_get_report_metadata(plugin_tmp) != NULL) {
				fwupd_release_add_metadata(
				    FWUPD_RELEASE(release),
				    fu_plugin_get_report_metadata(plugin_tmp));
			}
		}
	}
	return TRUE;
}

static gboolean
fu_engine_is_running_offline(FuEngine *self)
{
#ifdef HAVE_SYSTEMD
	g_autofree gchar *default_target = NULL;
	g_autoptr(GError) error = NULL;
	default_target = fu_systemd_get_default_target(&error);
	if (default_target == NULL) {
		g_warning("failed to get default.target: %s", error->message);
		return FALSE;
	}
	return g_strcmp0(default_target, "system-update.target") == 0;
#else
	return FALSE;
#endif
}

#ifdef HAVE_GIO_UNIX
static gchar *
fu_realpath(const gchar *filename, GError **error)
{
	char full_tmp[PATH_MAX];

	g_return_val_if_fail(filename != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

#ifdef HAVE_REALPATH
	if (realpath(filename, full_tmp) == NULL) {
#else
	if (_fullpath(full_tmp, filename, sizeof(full_tmp)) == NULL) {
#endif
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "cannot resolve path: %s",
			    strerror(errno));
		return NULL;
	}
	if (!g_file_test(full_tmp, G_FILE_TEST_EXISTS)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "cannot find path: %s",
			    full_tmp);
		return NULL;
	}
	return g_strdup(full_tmp);
}
#endif

static gboolean
fu_engine_offline_setup(GError **error)
{
#ifdef HAVE_GIO_UNIX
	gint rc;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *symlink_target = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR_PKG);
	g_autofree gchar *trigger = fu_path_from_kind(FU_PATH_KIND_OFFLINE_TRIGGER);

	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* does already exist */
	filename = fu_realpath(trigger, NULL);
	if (g_strcmp0(filename, symlink_target) == 0) {
		g_debug("%s already points to %s, skipping creation", trigger, symlink_target);
		return TRUE;
	}

	/* create symlink for the systemd-system-update-generator */
	rc = symlink(symlink_target, trigger);
	if (rc < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to create symlink %s to %s: %s",
			    trigger,
			    symlink_target,
			    strerror(errno));
		return FALSE;
	}
	return TRUE;
#else
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "Not supported as <gio-unix.h> not available");
	return FALSE;
#endif
}

static gboolean
fu_engine_offline_invalidate(GError **error)
{
	g_autofree gchar *trigger = fu_path_from_kind(FU_PATH_KIND_OFFLINE_TRIGGER);
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GFile) file1 = NULL;

	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	file1 = g_file_new_for_path(trigger);
	if (!g_file_query_exists(file1, NULL))
		return TRUE;
	if (!g_file_delete(file1, NULL, &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Cannot delete %s: %s",
			    trigger,
			    error_local->message);
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_engine_schedule_update:
 * @self: a #FuEngine
 * @device: a device
 * @release: a release
 * @blob_cab: a data blob
 * @flags: install flags
 * @error: (nullable): optional return location for an error
 *
 * Schedule an offline update for the device
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.3.5
 **/
gboolean
fu_engine_schedule_update(FuEngine *self,
			  FuDevice *device,
			  FwupdRelease *release,
			  GBytes *blob_cab,
			  FwupdInstallFlags flags,
			  GError **error)
{
	gchar tmpname[] = {"XXXXXX.cab"};
	g_autofree gchar *dirname = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuHistory) history = NULL;
	g_autoptr(GFile) file = NULL;

#ifndef HAVE_FWUPDOFFLINE
	/* sanity check */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "Not supported as compiled without offline support");
	return FALSE;
#endif

	/* id already exists */
	history = fu_history_new();
	if ((flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
		g_autoptr(FuDevice) res_tmp = NULL;
		res_tmp = fu_history_get_device_by_id(history, fu_device_get_id(device), NULL);
		if (res_tmp != NULL &&
		    fu_device_get_update_state(res_tmp) == FWUPD_UPDATE_STATE_PENDING) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_ALREADY_PENDING,
				    "%s is already scheduled to be updated",
				    fu_device_get_id(device));
			return FALSE;
		}
	}

	/* create directory */
	dirname = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR_PKG);
	file = g_file_new_for_path(dirname);
	if (!g_file_query_exists(file, NULL)) {
		if (!g_file_make_directory_with_parents(file, NULL, error))
			return FALSE;
	}

	/* get a random filename */
	for (guint i = 0; i < 6; i++)
		tmpname[i] = (gchar)g_random_int_range('A', 'Z');
	filename = g_build_filename(dirname, tmpname, NULL);

	/* just copy to the temp file */
	if (!g_file_set_contents(filename,
				 g_bytes_get_data(blob_cab, NULL),
				 (gssize)g_bytes_get_size(blob_cab),
				 error))
		return FALSE;

	/* schedule for next boot */
	g_debug("schedule %s to be installed to %s on next boot",
		filename,
		fu_device_get_id(device));
	fwupd_release_set_filename(release, filename);

	/* add to database */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_set_update_state(device, FWUPD_UPDATE_STATE_PENDING);
	if (!fu_history_add_device(history, device, release, error))
		return FALSE;

	/* next boot we run offline */
	return fu_engine_offline_setup(error);
}

/**
 * fu_engine_install_release:
 * @self: a #FuEngine
 * @release: a #FuRelease
 * @blob_cab: the #GBytes of the .cab file
 * @progress: a #FuProgress
 * @flags: install flags, e.g. %FWUPD_INSTALL_FLAG_ALLOW_OLDER
 * @error: (nullable): optional return location for an error
 *
 * Installs a specific release on a device.
 *
 * By this point all the requirements and tests should have been done in
 * fu_engine_check_requirements() so this should not fail before running
 * the plugin loader.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_install_release(FuEngine *self,
			  FuRelease *release,
			  GBytes *blob_cab,
			  FuProgress *progress,
			  FwupdInstallFlags flags,
			  GError **error)
{
	FuDevice *device_orig = fu_release_get_device(release);
	FuEngineRequest *request = fu_release_get_request(release);
	FuPlugin *plugin;
	FwupdFeatureFlags feature_flags = FWUPD_FEATURE_FLAG_NONE;
	FwupdVersionFormat fmt;
	GBytes *blob_fw;
	const gchar *tmp;
	const gchar *version_rel;
	g_autofree gchar *version_orig = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuDevice) device_tmp = NULL;
	g_autoptr(GBytes) blob_fw2 = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_ENGINE(self), FALSE);
	g_return_val_if_fail(FU_IS_RELEASE(release), FALSE);
	g_return_val_if_fail(FU_IS_PROGRESS(progress), FALSE);
	g_return_val_if_fail(blob_cab != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* optional for tests */
	if (request != NULL)
		feature_flags = fu_engine_request_get_feature_flags(request);

	/* add the checksum of the container blob if not already set */
	if (fwupd_release_get_checksums(FWUPD_RELEASE(release))->len == 0) {
		GChecksumType checksum_types[] = {G_CHECKSUM_SHA256, G_CHECKSUM_SHA1, 0};
		for (guint i = 0; checksum_types[i] != 0; i++) {
			g_autofree gchar *checksum =
			    g_compute_checksum_for_bytes(checksum_types[i], blob_cab);
			fwupd_release_add_checksum(FWUPD_RELEASE(release), checksum);
		}
	}

	/* not in bootloader mode */
	device = g_object_ref(fu_release_get_device(release));
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		/* both optional; the plugin can specify a fallback */
		tmp = fwupd_release_get_detach_caption(FWUPD_RELEASE(release));
		if (tmp != NULL)
			fu_device_set_update_message(device, tmp);
		tmp = fwupd_release_get_detach_image(FWUPD_RELEASE(release));
		if (tmp != NULL)
			fu_device_set_update_image(device, tmp);
	}

	/* schedule this for the next reboot if not in system-update.target,
	 * but first check if allowed on battery power */
	if ((flags & FWUPD_INSTALL_FLAG_OFFLINE) > 0 && !fu_engine_is_running_offline(self)) {
		FuPlugin *plugin_tmp =
		    fu_plugin_list_find_by_name(self->plugin_list, "upower", NULL);
		if (plugin_tmp != NULL) {
			if (!fu_plugin_runner_prepare(plugin_tmp, device, progress, flags, error))
				return FALSE;
		}
		fu_progress_set_status(progress, FWUPD_STATUS_SCHEDULING);
		if (!fu_engine_add_release_metadata(self, release, plugin_tmp, error))
			return FALSE;
		return fu_engine_schedule_update(self,
						 device,
						 FWUPD_RELEASE(release),
						 blob_cab,
						 flags,
						 error);
	}

	/* set this for the callback */
	self->write_history = (flags & FWUPD_INSTALL_FLAG_NO_HISTORY) == 0;

	/* get per-release firmware blob */
	blob_fw = fu_release_get_fw_blob(release);
	if (blob_fw == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "Failed to get firmware blob from release");
		return FALSE;
	}

	/* use a bubblewrap helper script to build the firmware */
	tmp = fu_release_get_builder_script(release);
	if (tmp != NULL) {
		blob_fw2 = fu_firmware_builder_process(blob_fw,
						       tmp,
						       fu_release_get_builder_output(release),
						       error);
		if (blob_fw2 == NULL)
			return FALSE;
	} else {
		blob_fw2 = g_bytes_ref(blob_fw);
	}

	/* get the plugin */
	plugin =
	    fu_plugin_list_find_by_name(self->plugin_list, fu_device_get_plugin(device), error);
	if (plugin == NULL)
		return FALSE;

	/* add device to database */
	if ((flags & FWUPD_INSTALL_FLAG_NO_HISTORY) == 0) {
		if (!fu_engine_add_release_metadata(self, release, plugin, error))
			return FALSE;
		if (!fu_history_add_device(self->history, device, FWUPD_RELEASE(release), error))
			return FALSE;
	}

	/* install firmware blob */
	version_orig = g_strdup(fu_device_get_version(device));
	if (!fu_engine_install_blob(self,
				    device,
				    blob_fw2,
				    progress,
				    flags,
				    feature_flags,
				    &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_AC_POWER_REQUIRED) ||
		    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_BATTERY_LEVEL_TOO_LOW) ||
		    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NEEDS_USER_ACTION) ||
		    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_BROKEN_SYSTEM)) {
			fu_device_set_update_state(device_orig,
						   FWUPD_UPDATE_STATE_FAILED_TRANSIENT);
		} else {
			fu_device_set_update_state(device_orig, FWUPD_UPDATE_STATE_FAILED);
		}
		fu_device_set_update_error(device_orig, error_local->message);
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	/* the device may have changed */
	device_tmp = fu_device_list_get_by_id(self->device_list, fu_device_get_id(device), error);
	if (device_tmp == NULL) {
		g_prefix_error(error, "failed to get device after install: ");
		return FALSE;
	}
	g_set_object(&device, device_tmp);

	/* update state (which updates the database if required) */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT) ||
	    fu_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN)) {
		fu_device_set_update_state(device_orig, FWUPD_UPDATE_STATE_NEEDS_REBOOT);
		return TRUE;
	}

	/* for online updates, verify the version changed if not a re-install */
	fmt = fu_device_get_version_format(device);
	version_rel = fu_release_get_version(release);
	if (version_rel != NULL && fu_version_compare(version_orig, version_rel, fmt) != 0 &&
	    fu_version_compare(version_orig, fu_device_get_version(device), fmt) == 0 &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION)) {
		g_autofree gchar *str = NULL;
		fu_device_set_update_state(device, FWUPD_UPDATE_STATE_FAILED);
		str = g_strdup_printf("device version not updated on success, %s != %s",
				      version_rel,
				      fu_device_get_version(device));
		fu_device_set_update_error(device, str);
	}

	/* mark success unless needs a reboot */
	if (fu_device_get_update_state(device) != FWUPD_UPDATE_STATE_NEEDS_REBOOT)
		fu_device_set_update_state(device, FWUPD_UPDATE_STATE_SUCCESS);

	/* wait for the system to acquiesce if required */
	if (fu_device_get_acquiesce_delay(device_orig) > 0) {
		fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_BUSY);
		fu_engine_wait_for_acquiesce(self, fu_device_get_acquiesce_delay(device_orig));
	}

	/* make the UI update */
	fu_engine_emit_changed(self);

	return TRUE;
}

/**
 * fu_engine_get_plugins:
 * @self: a #FuPluginList
 *
 * Gets all the plugins that have been added.
 *
 * Returns: (transfer none) (element-type FuPlugin): the plugins
 *
 * Since: 1.0.8
 **/
GPtrArray *
fu_engine_get_plugins(FuEngine *self)
{
	g_return_val_if_fail(FU_IS_ENGINE(self), NULL);
	return fu_plugin_list_get_all(self->plugin_list);
}

/**
 * fu_engine_get_device:
 * @self: a #FuEngine
 * @device_id: a device ID
 * @error: (nullable): optional return location for an error
 *
 * Gets a specific device.
 *
 * Returns: (transfer full): a device, or %NULL if not found
 **/
FuDevice *
fu_engine_get_device(FuEngine *self, const gchar *device_id, GError **error)
{
	g_autoptr(FuDevice) device = NULL;

	/* wait for any device to disconnect and reconnect */
	if (!fu_device_list_wait_for_replug(self->device_list, error)) {
		g_prefix_error(error, "failed to wait for detach replug: ");
		return NULL;
	}

	/* get the new device */
	device = fu_device_list_get_by_id(self->device_list, device_id, error);
	if (device == NULL)
		return NULL;

	/* success */
	return g_steal_pointer(&device);
}

/* same as FuDevice->prepare, but with the device open */
static gboolean
fu_engine_device_prepare(FuEngine *self,
			 FuDevice *device,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error)
{
	g_autoptr(FuDeviceLocker) locker = fu_device_locker_new(device, error);
	if (locker == NULL) {
		g_prefix_error(error, "failed to open device for prepare: ");
		return FALSE;
	}

	/* check battery level is sane */
	if (fu_device_get_battery_level(device) > 0 &&
	    fu_device_get_battery_level(device) < fu_device_get_battery_threshold(device)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BATTERY_LEVEL_TOO_LOW,
			    "battery level is too low: %u%%",
			    fu_device_get_battery_level(device));
		return FALSE;
	}

	return fu_device_prepare(device, progress, flags, error);
}

/* same as FuDevice->cleanup, but with the device open */
static gboolean
fu_engine_device_cleanup(FuEngine *self,
			 FuDevice *device,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_WILL_DISAPPEAR)) {
		g_debug("skipping device cleanup due to will-disappear flag");
		return TRUE;
	}

	locker = fu_device_locker_new(device, error);
	if (locker == NULL) {
		g_prefix_error(error, "failed to open device for cleanup: ");
		return FALSE;
	}
	return fu_device_cleanup(device, progress, flags, error);
}

static gboolean
fu_engine_device_check_power(FuEngine *self,
			     FuDevice *device,
			     FwupdInstallFlags flags,
			     GError **error)
{
	if (flags & FWUPD_INSTALL_FLAG_IGNORE_POWER) {
		g_autofree gchar *configdir = fu_path_from_kind(FU_PATH_KIND_SYSCONFDIR_PKG);
		g_autofree gchar *configfile = g_build_filename(configdir, "daemon.conf", NULL);
		g_warning("Ignoring deprecated flag provided by client "
			  "'FWUPD_INSTALL_FLAG_IGNORE_POWER'. To ignore power levels, modify %s",
			  configfile);
	}

	if (fu_config_get_ignore_power(self->config))
		return TRUE;

	/* not charging */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_REQUIRE_AC) &&
	    (fu_context_get_battery_state(self->ctx) == FU_BATTERY_STATE_DISCHARGING ||
	     fu_context_get_battery_state(self->ctx) == FU_BATTERY_STATE_EMPTY)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_AC_POWER_REQUIRED,
				    "Cannot install update "
				    "when not on AC power unless forced");
		return FALSE;
	}

	/* not enough just in case */
	if (fu_context_get_battery_level(self->ctx) != FWUPD_BATTERY_LEVEL_INVALID &&
	    fu_context_get_battery_threshold(self->ctx) != FWUPD_BATTERY_LEVEL_INVALID &&
	    fu_context_get_battery_level(self->ctx) < fu_context_get_battery_threshold(self->ctx)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BATTERY_LEVEL_TOO_LOW,
			    "Cannot install update when system battery "
			    "is not at least %u%% unless forced",
			    fu_context_get_battery_threshold(self->ctx));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_engine_prepare(FuEngine *self,
		  const gchar *device_id,
		  FuProgress *progress,
		  FwupdInstallFlags flags,
		  GError **error)
{
	GPtrArray *plugins = fu_plugin_list_get_all(self->plugin_list);
	g_autofree gchar *str = NULL;
	g_autoptr(FuDevice) device = NULL;

	/* the device and plugin both may have changed */
	device = fu_engine_get_device(self, device_id, error);
	if (device == NULL) {
		g_prefix_error(error, "failed to get device before update prepare: ");
		return FALSE;
	}
	fu_device_inhibit(device, "update-in-progress", "An update is in progress");

	if (!fu_engine_device_check_power(self, device, flags, error))
		return FALSE;

	str = fu_device_to_string(device);
	g_debug("prepare -> %s", str);
	if (!fu_engine_device_prepare(self, device, progress, flags, error)) // XXXX
		return FALSE;
	for (guint j = 0; j < plugins->len; j++) {
		FuPlugin *plugin_tmp = g_ptr_array_index(plugins, j);
		if (!fu_plugin_runner_prepare(plugin_tmp, device, progress, flags, error)) // XXX
			return FALSE;
	}

	/* wait for device to disconnect and reconnect */
	if (!fu_device_list_wait_for_replug(self->device_list, error)) {
		g_prefix_error(error, "failed to wait for prepare replug: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_engine_cleanup(FuEngine *self,
		  const gchar *device_id,
		  FuProgress *progress,
		  FwupdInstallFlags flags,
		  GError **error)
{
	GPtrArray *plugins = fu_plugin_list_get_all(self->plugin_list);
	g_autofree gchar *str = NULL;
	g_autoptr(FuDevice) device = NULL;

	/* the device and plugin both may have changed */
	device = fu_engine_get_device(self, device_id, error);
	if (device == NULL) {
		g_prefix_error(error, "failed to get device before update cleanup: ");
		return FALSE;
	}
	fu_device_uninhibit(device, "update-in-progress");
	str = fu_device_to_string(device);
	g_debug("cleanup -> %s", str);
	if (!fu_engine_device_cleanup(self, device, progress, flags, error))
		return FALSE;
	for (guint j = 0; j < plugins->len; j++) {
		FuPlugin *plugin_tmp = g_ptr_array_index(plugins, j);
		if (!fu_plugin_runner_cleanup(plugin_tmp, device, progress, flags, error))
			return FALSE;
	}

	/* wait for device to disconnect and reconnect */
	if (!fu_device_list_wait_for_replug(self->device_list, error)) {
		g_prefix_error(error, "failed to wait for cleanup replug: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_engine_detach(FuEngine *self,
		 const gchar *device_id,
		 FuProgress *progress,
		 FwupdFeatureFlags feature_flags,
		 GError **error)
{
	FuPlugin *plugin;
	g_autofree gchar *str = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuDeviceLocker) poll_locker = NULL;

	/* the device and plugin both may have changed */
	device = fu_engine_get_device(self, device_id, error);
	if (device == NULL) {
		g_prefix_error(error, "failed to get device before update detach: ");
		return FALSE;
	}

	/* pause the polling */
	poll_locker = fu_device_poll_locker_new(device, error);
	if (poll_locker == NULL)
		return FALSE;

	str = fu_device_to_string(device);
	g_debug("detach -> %s", str);
	plugin =
	    fu_plugin_list_find_by_name(self->plugin_list, fu_device_get_plugin(device), error);
	if (plugin == NULL)
		return FALSE;
	if (!fu_plugin_runner_detach(plugin, device, progress, error))
		return FALSE;

	/* support older clients without the ability to do immediate requests */
	if ((feature_flags & FWUPD_FEATURE_FLAG_REQUESTS) == 0 &&
	    fu_device_get_request_cnt(device, FWUPD_REQUEST_KIND_IMMEDIATE) > 0) {
		/* fallback to something sane */
		if (fu_device_get_update_message(device) == NULL) {
			g_autofree gchar *tmp = NULL;
			tmp = g_strdup_printf("Device %s needs to manually be put in update mode",
					      fu_device_get_name(device));
			fu_device_set_update_message(device, tmp);
		}

		/* abort and require client to re-submit */
		fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NEEDS_USER_ACTION,
				    fu_device_get_update_message(device));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_engine_attach(FuEngine *self, const gchar *device_id, FuProgress *progress, GError **error)
{
	FuPlugin *plugin;
	g_autofree gchar *str = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuDeviceLocker) poll_locker = NULL;

	/* the device and plugin both may have changed */
	device = fu_engine_get_device(self, device_id, error);
	if (device == NULL) {
		g_prefix_error(error, "failed to get device before update attach: ");
		return FALSE;
	}
	str = fu_device_to_string(device);
	g_debug("attach -> %s", str);
	plugin =
	    fu_plugin_list_find_by_name(self->plugin_list, fu_device_get_plugin(device), error);
	if (plugin == NULL)
		return FALSE;

	/* pause the polling */
	poll_locker = fu_device_poll_locker_new(device, error);
	if (poll_locker == NULL)
		return FALSE;

	if (!fu_plugin_runner_attach(plugin, device, progress, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_engine_set_progress(FuEngine *self, const gchar *device_id, FuProgress *progress, GError **error)
{
	g_autoptr(FuDevice) device = NULL;

	/* the device and plugin both may have changed */
	device = fu_engine_get_device(self, device_id, error);
	if (device == NULL) {
		g_prefix_error(error, "failed to get device before setting progress: ");
		return FALSE;
	}
	fu_device_set_progress(device, progress);
	return TRUE;
}

gboolean
fu_engine_activate(FuEngine *self, const gchar *device_id, FuProgress *progress, GError **error)
{
	FuPlugin *plugin;
	g_autofree gchar *str = NULL;
	g_autoptr(FuDevice) device = NULL;

	g_return_val_if_fail(FU_IS_ENGINE(self), FALSE);
	g_return_val_if_fail(device_id != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* check the device exists */
	device = fu_device_list_get_by_id(self->device_list, device_id, error);
	if (device == NULL)
		return FALSE;
	str = fu_device_to_string(device);
	g_debug("activate -> %s", str);
	plugin =
	    fu_plugin_list_find_by_name(self->plugin_list, fu_device_get_plugin(device), error);
	if (plugin == NULL)
		return FALSE;
	g_debug("Activating %s", fu_device_get_name(device));

	if (!fu_plugin_runner_activate(plugin, device, progress, error))
		return FALSE;

	fu_engine_emit_device_changed_safe(self, device);
	fu_engine_emit_changed(self);

	return TRUE;
}

static gboolean
fu_engine_reload(FuEngine *self, const gchar *device_id, GError **error)
{
	FuPlugin *plugin;
	g_autofree gchar *str = NULL;
	g_autoptr(FuDevice) device = NULL;

	/* the device and plugin both may have changed */
	device = fu_engine_get_device(self, device_id, error);
	if (device == NULL) {
		g_prefix_error(error, "failed to get device before update reload: ");
		return FALSE;
	}
	str = fu_device_to_string(device);
	g_debug("reload -> %s", str);
	plugin =
	    fu_plugin_list_find_by_name(self->plugin_list, fu_device_get_plugin(device), error);
	if (plugin == NULL)
		return FALSE;

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_WILL_DISAPPEAR)) {
		g_debug("skipping reload due to will-disappear flag");
		return TRUE;
	}

	if (!fu_plugin_runner_reload(plugin, device, error)) {
		g_prefix_error(error, "failed to reload device: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_engine_write_firmware(FuEngine *self,
			 const gchar *device_id,
			 GBytes *blob_fw,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuPlugin *plugin;
	g_autofree gchar *str = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuDevice) device_pending = NULL;
	g_autoptr(FuDeviceLocker) poll_locker = NULL;

	/* cancel the pending action */
	if (!fu_engine_offline_invalidate(error))
		return FALSE;

	/* the device and plugin both may have changed */
	device = fu_engine_get_device(self, device_id, error);
	if (device == NULL) {
		g_prefix_error(error, "failed to get device before update: ");
		return FALSE;
	}

	/* pause the polling */
	poll_locker = fu_device_poll_locker_new(device, error);
	if (poll_locker == NULL)
		return FALSE;

	device_pending = fu_history_get_device_by_id(self->history, device_id, NULL);
	str = fu_device_to_string(device);
	g_debug("update -> %s", str);
	plugin =
	    fu_plugin_list_find_by_name(self->plugin_list, fu_device_get_plugin(device), error);
	if (plugin == NULL)
		return FALSE;
	if (!fu_plugin_runner_write_firmware(plugin, device, blob_fw, progress, flags, error)) {
		g_autoptr(GError) error_attach = NULL;
		g_autoptr(GError) error_cleanup = NULL;

		/* attack back into runtime then cleanup */
		fu_progress_reset(progress);
		if (!fu_plugin_runner_attach(plugin, device, progress, &error_attach)) {
			g_warning("failed to attach device after failed update: %s",
				  error_attach->message);
		}
		fu_progress_reset(progress);
		if (!fu_engine_cleanup(self, device_id, progress, flags, &error_cleanup)) {
			g_warning("failed to update-cleanup after failed update: %s",
				  error_cleanup->message);
		}
		return FALSE;
	}

	/* cleanup */
	if (device_pending != NULL) {
		const gchar *tmp;
		FwupdRelease *release;

		/* update history database */
		fu_device_set_update_state(device, FWUPD_UPDATE_STATE_SUCCESS);
		if (!fu_history_modify_device(self->history, device, error))
			return FALSE;

		/* delete cab file */
		release = fu_device_get_release_default(device_pending);
		tmp = fwupd_release_get_filename(release);
		if (tmp != NULL && g_str_has_prefix(tmp, FWUPD_LIBEXECDIR)) {
			g_autoptr(GError) error_delete = NULL;
			g_autoptr(GFile) file = NULL;
			file = g_file_new_for_path(tmp);
			if (!g_file_delete(file, NULL, &error_delete)) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "Failed to delete %s: %s",
					    tmp,
					    error_delete->message);
				return FALSE;
			}
		}
	}
	return TRUE;
}

GBytes *
fu_engine_firmware_dump(FuEngine *self,
			FuDevice *device,
			FuProgress *progress,
			FwupdInstallFlags flags,
			GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuDeviceLocker) poll_locker = NULL;

	/* pause the polling */
	poll_locker = fu_device_poll_locker_new(device, error);
	if (poll_locker == NULL)
		return NULL;

	/* open, read, close */
	locker = fu_device_locker_new(device, error);
	if (locker == NULL) {
		g_prefix_error(error, "failed to open device for firmware read: ");
		return NULL;
	}
	return fu_device_dump_firmware(device, progress, error);
}

FuFirmware *
fu_engine_firmware_read(FuEngine *self,
			FuDevice *device,
			FuProgress *progress,
			FwupdInstallFlags flags,
			GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuDeviceLocker) poll_locker = NULL;

	/* pause the polling */
	poll_locker = fu_device_poll_locker_new(device, error);
	if (poll_locker == NULL)
		return NULL;

	/* open, read, close */
	locker = fu_device_locker_new(device, error);
	if (locker == NULL) {
		g_prefix_error(error, "failed to open device for firmware read: ");
		return NULL;
	}
	return fu_device_read_firmware(device, progress, error);
}

gboolean
fu_engine_install_blob(FuEngine *self,
		       FuDevice *device,
		       GBytes *blob_fw,
		       FuProgress *progress,
		       FwupdInstallFlags flags,
		       FwupdFeatureFlags feature_flags,
		       GError **error)
{
	guint retries = 0;
	g_autofree gchar *device_id = NULL;
	g_autoptr(GTimer) timer = g_timer_new();

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_NO_PROFILE);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "prepare");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "cleanup");

	/* test the firmware is not an empty blob */
	if (g_bytes_get_size(blob_fw) == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "Firmware is invalid as has zero size");
		return FALSE;
	}

	/* mark this as modified even if we actually fail to do the update */
	fu_device_set_modified(device, (guint64)g_get_real_time() / G_USEC_PER_SEC);

	/* signal to all the plugins the update is about to happen */
	device_id = g_strdup(fu_device_get_id(device));
	if (!fu_engine_prepare(self, device_id, fu_progress_get_child(progress), flags, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* plugins can set FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED to run again, but they
	 * must return TRUE rather than an error */
	do {
		g_autoptr(FuDevice) device_tmp = NULL;
		FuProgress *progress_local = fu_progress_get_child(progress);

		/* check for a loop */
		if (++retries > 5) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "aborting device write loop, limit 5");
			return FALSE;
		}

		/* progress */
		if (!fu_engine_set_progress(self, device_id, progress_local, error))
			return FALSE;
		if (fu_progress_get_steps(progress_local) == 0) {
			fu_progress_set_id(progress_local, G_STRLOC);
			fu_progress_add_flag(progress_local, FU_PROGRESS_FLAG_GUESSED);
			fu_progress_add_step(progress_local, FWUPD_STATUS_DEVICE_RESTART, 2, NULL);
			fu_progress_add_step(progress_local, FWUPD_STATUS_DEVICE_WRITE, 94, NULL);
			fu_progress_add_step(progress_local, FWUPD_STATUS_DEVICE_RESTART, 2, NULL);
			fu_progress_add_step(progress_local, FWUPD_STATUS_DEVICE_BUSY, 2, NULL);
		} else if (fu_progress_get_steps(progress_local) != 4) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "FuDevice->set_progress did not set "
					    "detach,write,attach,reload steps");
			return FALSE;
		}

		/* detach to bootloader mode */
		if (!fu_engine_detach(self,
				      device_id,
				      fu_progress_get_child(progress_local),
				      feature_flags,
				      error))
			return FALSE;
		fu_progress_step_done(progress_local);

		/* install */
		if (!fu_engine_write_firmware(self,
					      device_id,
					      blob_fw,
					      fu_progress_get_child(progress_local),
					      flags,
					      error))
			return FALSE;
		fu_progress_step_done(progress_local);

		/* attach into runtime mode */
		if (!fu_engine_attach(self,
				      device_id,
				      fu_progress_get_child(progress_local),
				      error))
			return FALSE;
		fu_progress_step_done(progress_local);

		/* get the new version number */
		if (!fu_engine_reload(self, device_id, error))
			return FALSE;
		fu_progress_step_done(progress_local);

		/* the device and plugin both may have changed */
		device_tmp = fu_engine_get_device(self, device_id, error);
		if (device_tmp == NULL) {
			g_prefix_error(error, "failed to get device after install blob: ");
			return FALSE;
		}
		if (!fu_device_has_flag(device_tmp, FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED))
			break;

		/* don't rely on a plugin clearing this */
		fu_device_remove_flag(device_tmp, FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED);
		fu_progress_reset(progress_local);

	} while (TRUE);
	fu_progress_step_done(progress);

	/* signal to all the plugins the update has happened */
	if (!fu_engine_cleanup(self, device_id, fu_progress_get_child(progress), flags, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* make the UI update */
	fu_engine_emit_device_changed(self, device_id);
	g_debug("Updating %s took %f seconds",
		fu_device_get_name(device),
		g_timer_elapsed(timer, NULL));
	return TRUE;
}

static FuDevice *
fu_engine_get_item_by_id_fallback_history(FuEngine *self, const gchar *id, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;

	/* not a wildcard */
	if (g_strcmp0(id, FWUPD_DEVICE_ID_ANY) != 0) {
		g_autoptr(FuDevice) dev = NULL;
		g_autoptr(GError) error_local = NULL;

		/* get this one device */
		dev = fu_history_get_device_by_id(self->history, id, &error_local);
		if (dev == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "Failed to find %s in history database: %s",
				    id,
				    error_local->message);
			return NULL;
		}

		/* only useful */
		if (fu_device_get_update_state(dev) == FWUPD_UPDATE_STATE_SUCCESS ||
		    fu_device_get_update_state(dev) == FWUPD_UPDATE_STATE_FAILED_TRANSIENT ||
		    fu_device_get_update_state(dev) == FWUPD_UPDATE_STATE_FAILED) {
			return g_steal_pointer(&dev);
		}

		/* nothing in database */
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOTHING_TO_DO,
			    "Device %s has no results to report",
			    fu_device_get_id(dev));
		return NULL;
	}

	/* allow '*' for any */
	devices = fu_history_get_devices(self->history, error);
	if (devices == NULL)
		return NULL;
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index(devices, i);
		if (fu_device_get_update_state(dev) == FWUPD_UPDATE_STATE_SUCCESS ||
		    fu_device_get_update_state(dev) == FWUPD_UPDATE_STATE_FAILED_TRANSIENT ||
		    fu_device_get_update_state(dev) == FWUPD_UPDATE_STATE_FAILED)
			return g_object_ref(dev);
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOTHING_TO_DO,
			    "Failed to find any useful results to report");
	return NULL;
}

static gboolean
fu_engine_create_silo_index(FuEngine *self, GError **error)
{
	g_autoptr(GPtrArray) components = NULL;

	/* print what we've got */
	components = xb_silo_query(self->silo, "components/component[@type='firmware']", 0, NULL);
	if (components == NULL)
		return TRUE;
	g_debug("%u components now in silo", components->len);

	/* build the index */
	if (!xb_silo_query_build_index(self->silo, "components/component", "type", error))
		return FALSE;
	if (!xb_silo_query_build_index(self->silo,
				       "components/component[@type='firmware']/provides/firmware",
				       "type",
				       error))
		return FALSE;
	if (!xb_silo_query_build_index(self->silo,
				       "components/component[@type='firmware']/provides/firmware",
				       NULL,
				       error))
		return FALSE;
	if (!xb_silo_query_build_index(self->silo,
				       "components/component[@type='firmware']/tags/tag",
				       "namespace",
				       error))
		return FALSE;

	/* create prepared queries to save time later */
	self->query_component_by_guid =
	    xb_query_new_full(self->silo,
			      "components/component[@type='firmware']/"
			      "provides/firmware[@type=$'flashed'][text()=?]/"
			      "../..",
			      XB_QUERY_FLAG_OPTIMIZE,
			      error);
	if (self->query_component_by_guid == NULL) {
		g_prefix_error(error, "failed to prepare query: ");
		return FALSE;
	}
	return TRUE;
}

/* for the self tests */
void
fu_engine_set_silo(FuEngine *self, XbSilo *silo)
{
	g_autoptr(GError) error_local = NULL;
	g_return_if_fail(FU_IS_ENGINE(self));
	g_return_if_fail(XB_IS_SILO(silo));
	g_set_object(&self->silo, silo);
	if (!fu_engine_create_silo_index(self, &error_local))
		g_warning("failed to create indexes: %s", error_local->message);
}

static gboolean
fu_engine_appstream_upgrade_cb(XbBuilderFixup *self,
			       XbBuilderNode *bn,
			       gpointer user_data,
			       GError **error)
{
	if (g_strcmp0(xb_builder_node_get_element(bn), "metadata") == 0)
		xb_builder_node_set_element(bn, "custom");
	return TRUE;
}

static XbBuilderSource *
fu_engine_create_metadata_builder_source(FuEngine *self, const gchar *fn, GError **error)
{
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autofree gchar *xml = NULL;

	g_debug("building metadata for %s", fn);
	blob = fu_bytes_get_contents(fn, error);
	if (blob == NULL)
		return NULL;

	/* convert the silo for the CAB into a XbBuilderSource */
	silo = fu_engine_get_silo_from_blob(self, blob, error);
	if (silo == NULL)
		return NULL;
	xml = xb_silo_export(silo, XB_NODE_EXPORT_FLAG_NONE, error);
	if (xml == NULL)
		return NULL;
	if (!xb_builder_source_load_xml(source, xml, XB_BUILDER_SOURCE_FLAG_NONE, error))
		return NULL;
	return g_steal_pointer(&source);
}

static gboolean
fu_engine_create_metadata(FuEngine *self, XbBuilder *builder, FwupdRemote *remote, GError **error)
{
	g_autoptr(GPtrArray) files = NULL;
	const gchar *path;

	/* find all files in directory */
	path = fwupd_remote_get_filename_cache(remote);
	files = fu_path_get_files(path, error);
	if (files == NULL)
		return FALSE;

	/* add each source */
	for (guint i = 0; i < files->len; i++) {
		g_autoptr(XbBuilderNode) custom = NULL;
		g_autoptr(XbBuilderSource) source = NULL;
		g_autoptr(GError) error_local = NULL;
		const gchar *fn = g_ptr_array_index(files, i);
		g_autofree gchar *fn_lowercase = g_ascii_strdown(fn, -1);

		/* check is cab file */
		if (!g_str_has_suffix(fn_lowercase, ".cab")) {
			g_debug("ignoring: %s", fn);
			continue;
		}

		/* build source for file */
		source = fu_engine_create_metadata_builder_source(self, fn, &error_local);
		if (source == NULL) {
			g_warning("failed to create builder source: %s", error_local->message);
			continue;
		}

		/* add metadata */
		custom = xb_builder_node_new("custom");
		xb_builder_node_insert_text(custom,
					    "value",
					    fn,
					    "key",
					    "fwupd::FilenameCache",
					    NULL);
		xb_builder_node_insert_text(custom,
					    "value",
					    fwupd_remote_get_id(remote),
					    "key",
					    "fwupd::RemoteId",
					    NULL);
		xb_builder_source_set_info(source, custom);
		xb_builder_import_source(builder, source);
	}
	return TRUE;
}

static void
fu_engine_ensure_device_supported(FuEngine *self, FuDevice *device)
{
	gboolean is_supported = FALSE;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(FuEngineRequest) request = NULL;

	/* all flags set */
	request = fu_engine_request_new(FU_ENGINE_REQUEST_KIND_ONLY_SUPPORTED);
	fu_engine_request_set_feature_flags(request, ~0);

	/* get all releases that pass the requirements */
	releases = fu_engine_get_releases_for_device(self, request, device, &error);
	if (releases == NULL) {
		if (!g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO) &&
		    !g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
			g_warning("failed to get releases for %s: %s",
				  fu_device_get_name(device),
				  error->message);
		}
	} else {
		if (releases->len > 0)
			is_supported = TRUE;
	}

	/* was supported, now unsupported */
	if (!is_supported) {
		if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_SUPPORTED)) {
			fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_SUPPORTED);
			fu_engine_emit_device_changed_safe(self, device);
		}
		return;
	}

	/* was unsupported, now supported */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_SUPPORTED)) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_SUPPORTED);
		fu_engine_emit_device_changed_safe(self, device);
	}
}

static void
fu_engine_md_refresh_device_name(FuEngine *self, FuDevice *device, XbNode *component)
{
	const gchar *name = NULL;

	/* require data */
	if (component == NULL)
		return;

	/* copy 1:1 */
	name = xb_node_query_text(component, "name", NULL);
	if (name != NULL) {
		fu_device_set_name(device, name);
		fu_device_remove_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_MD_SET_NAME);
	}
}

static void
fu_engine_md_refresh_device_vendor(FuEngine *self, FuDevice *device, XbNode *component)
{
	const gchar *vendor = NULL;

	/* require data */
	if (component == NULL)
		return;

	/* copy 1:1 */
	vendor = xb_node_query_text(component, "developer_name", NULL);
	if (vendor != NULL) {
		fu_device_set_vendor(device, vendor);
		fu_device_remove_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_MD_SET_VENDOR);
	}
}

static void
fu_engine_md_refresh_device_signed(FuEngine *self, FuDevice *device, XbNode *component)
{
	const gchar *value = NULL;

	/* require data */
	if (component == NULL)
		return;

	/* already set, possibly by a quirk */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD) ||
	    fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD))
		return;

	/* copy 1:1 */
	value = xb_node_query_text(component, "custom/value[@key='LVFS::DeviceIntegrity']", NULL);
	if (value != NULL) {
		if (g_strcmp0(value, "signed") == 0) {
			fu_device_add_flag(device, FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
		} else if (g_strcmp0(value, "unsigned") == 0) {
			fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
		} else {
			g_warning("payload value unexpected: %s, expected signed|unsigned", value);
		}
		fu_device_remove_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_MD_SET_VENDOR);
	}
}

static void
fu_engine_md_refresh_device_icon(FuEngine *self, FuDevice *device, XbNode *component)
{
	const gchar *icon = NULL;

	/* require data */
	if (component == NULL)
		return;

	/* copy 1:1 */
	icon = xb_node_query_text(component, "icon", NULL);
	if (icon != NULL) {
		fu_device_add_icon(device, icon);
		fu_device_remove_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_MD_SET_ICON);
	}
}

static const gchar *
fu_common_device_category_to_name(const gchar *cat)
{
	if (g_strcmp0(cat, "X-EmbeddedController") == 0)
		return "Embedded Controller";
	if (g_strcmp0(cat, "X-ManagementEngine") == 0)
		return "Intel Management Engine";
	if (g_strcmp0(cat, "X-CorporateManagementEngine") == 0)
		return "Intel Management Engine";
	if (g_strcmp0(cat, "X-ConsumerManagementEngine") == 0)
		return "Intel Management Engine";
	if (g_strcmp0(cat, "X-ThunderboltController") == 0)
		return "Thunderbolt Controller";
	if (g_strcmp0(cat, "X-PlatformSecurityProcessor") == 0)
		return "Platform Security Processor";
	if (g_strcmp0(cat, "X-CpuMicrocode") == 0)
		return "CPU Microcode";
	if (g_strcmp0(cat, "X-Battery") == 0)
		return "Battery";
	if (g_strcmp0(cat, "X-Camera") == 0)
		return "Camera";
	if (g_strcmp0(cat, "X-TPM") == 0)
		return "TPM";
	if (g_strcmp0(cat, "X-Touchpad") == 0)
		return "Touchpad";
	if (g_strcmp0(cat, "X-Mouse") == 0)
		return "Mouse";
	if (g_strcmp0(cat, "X-Keyboard") == 0)
		return "Keyboard";
	if (g_strcmp0(cat, "X-VideoDisplay") == 0)
		return "Display";
	if (g_strcmp0(cat, "X-BaseboardManagementController") == 0)
		return "BMC";
	if (g_strcmp0(cat, "X-UsbReceiver") == 0)
		return "USB Receiver";
	return NULL;
}

static void
fu_engine_md_refresh_device_name_category(FuEngine *self, FuDevice *device, XbNode *component)
{
	const gchar *name = NULL;
	g_autoptr(GPtrArray) cats = NULL;

	/* require data */
	if (component == NULL)
		return;

	/* get AppStream and safe-compat categories */
	cats = xb_node_query(component, "categories/category|X-categories/category", 0, NULL);
	if (cats == NULL)
		return;
	for (guint i = 0; i < cats->len; i++) {
		XbNode *n = g_ptr_array_index(cats, i);
		name = fu_common_device_category_to_name(xb_node_get_text(n));
		if (name != NULL)
			break;
	}
	if (name != NULL) {
		fu_device_set_name(device, name);
		fu_device_remove_internal_flag(device,
					       FU_DEVICE_INTERNAL_FLAG_MD_SET_NAME_CATEGORY);
	}
}

static void
_g_ptr_array_reverse(GPtrArray *array)
{
	guint last_idx = array->len - 1;
	for (guint i = 0; i < array->len / 2; i++) {
		gpointer tmp = array->pdata[i];
		array->pdata[i] = array->pdata[last_idx - i];
		array->pdata[last_idx - i] = tmp;
	}
}

static void
fu_engine_md_refresh_device_verfmt(FuEngine *self, FuDevice *device, XbNode *component)
{
	FwupdVersionFormat verfmt = FWUPD_VERSION_FORMAT_UNKNOWN;
	g_autoptr(GPtrArray) verfmts = NULL;

	/* require data */
	if (component == NULL)
		return;

	/* get metadata */
	verfmts = xb_node_query(component, "custom/value[@key='LVFS::VersionFormat']", 0, NULL);
	if (verfmts == NULL)
		return;
	_g_ptr_array_reverse(verfmts);
	for (guint i = 0; i < verfmts->len; i++) {
		XbNode *value = g_ptr_array_index(verfmts, i);
		verfmt = fwupd_version_format_from_string(xb_node_get_text(value));
		if (verfmt != FWUPD_VERSION_FORMAT_UNKNOWN)
			break;
	}

	/* found and different to existing */
	if (verfmt != FWUPD_VERSION_FORMAT_UNKNOWN &&
	    fu_device_get_version_format(device) != verfmt) {
		fu_device_set_version_format(device, verfmt);
		if (fu_device_get_version_raw(device) != 0x0) {
			g_autofree gchar *version = NULL;
			version = fu_version_from_uint32(fu_device_get_version_raw(device), verfmt);
			fu_device_set_version(device, version);
		}
		if (fu_device_get_version_lowest_raw(device) != 0x0) {
			g_autofree gchar *version = NULL;
			version = fu_version_from_uint32(fu_device_get_version_lowest_raw(device),
							 verfmt);
			fu_device_set_version_lowest(device, version);
		}
		if (fu_device_get_version_bootloader_raw(device) != 0x0) {
			g_autofree gchar *version = NULL;
			version =
			    fu_version_from_uint32(fu_device_get_version_bootloader_raw(device),
						   verfmt);
			fu_device_set_version_bootloader(device, version);
		}
	}

	/* do not try to do this again */
	fu_device_remove_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_MD_SET_VERFMT);
}

void
fu_engine_md_refresh_device_from_component(FuEngine *self, FuDevice *device, XbNode *component)
{
	/* set the name */
	if (fu_device_has_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_MD_SET_NAME))
		fu_engine_md_refresh_device_name(self, device, component);
	if (fu_device_has_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_MD_SET_NAME_CATEGORY))
		fu_engine_md_refresh_device_name_category(self, device, component);
	if (fu_device_has_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_MD_SET_ICON))
		fu_engine_md_refresh_device_icon(self, device, component);
	if (fu_device_has_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_MD_SET_VENDOR))
		fu_engine_md_refresh_device_vendor(self, device, component);
	if (fu_device_has_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_MD_SET_SIGNED))
		fu_engine_md_refresh_device_signed(self, device, component);

	/* fix the version */
	if (fu_device_has_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_MD_SET_VERFMT))
		fu_engine_md_refresh_device_verfmt(self, device, component);
}

static void
fu_engine_md_refresh_devices(FuEngine *self)
{
	g_autoptr(GPtrArray) devices = fu_device_list_get_all(self->device_list);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		g_autoptr(XbNode) component = fu_engine_get_component_by_guids(self, device);

		/* set or clear the SUPPORTED flag */
		fu_engine_ensure_device_supported(self, device);

		/* fixup the name and format as needed */
		fu_engine_md_refresh_device_from_component(self, device, component);
	}
}

static gboolean
fu_engine_load_metadata_store_local(FuEngine *self,
				    XbBuilder *builder,
				    FuPathKind path_kind,
				    GError **error)
{
	g_autofree gchar *fn = fu_path_from_kind(path_kind);
	g_autofree gchar *metadata_path = g_build_filename(fn, "local.d", NULL);
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) metadata_fns = NULL;

	metadata_fns = fu_path_glob(metadata_path, "*.xml", &error_local);
	if (metadata_fns == NULL) {
		g_debug("ignoring: %s", error_local->message);
		return TRUE;
	}
	for (guint i = 0; i < metadata_fns->len; i++) {
		const gchar *path = g_ptr_array_index(metadata_fns, i);
		g_autoptr(XbBuilderSource) source = xb_builder_source_new();
		g_autoptr(GFile) file = g_file_new_for_path(path);
		g_debug("loading local metadata: %s", path);
		if (!xb_builder_source_load_file(source,
						 file,
						 XB_BUILDER_SOURCE_FLAG_NONE,
						 NULL,
						 error))
			return FALSE;
		xb_builder_source_set_prefix(source, "local");
		xb_builder_import_source(builder, source);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_engine_load_metadata_store(FuEngine *self, FuEngineLoadFlags flags, GError **error)
{
	GPtrArray *remotes;
	XbBuilderCompileFlags compile_flags = XB_BUILDER_COMPILE_FLAG_IGNORE_INVALID;
	g_autoptr(GFile) xmlb = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();

	/* clear existing silo */
	g_clear_object(&self->silo);

	/* verbose profiling */
	if (g_getenv("FWUPD_XMLB_VERBOSE") != NULL) {
		xb_builder_set_profile_flags(builder,
					     XB_SILO_PROFILE_FLAG_XPATH |
						 XB_SILO_PROFILE_FLAG_DEBUG);
	}

	/* load each enabled metadata file */
	remotes = fu_remote_list_get_all(self->remote_list);
	for (guint i = 0; i < remotes->len; i++) {
		const gchar *path = NULL;
		g_autoptr(GError) error_local = NULL;
		g_autoptr(GFile) file = NULL;
		g_autoptr(XbBuilderFixup) fixup = NULL;
		g_autoptr(XbBuilderNode) custom = NULL;
		g_autoptr(XbBuilderSource) source = xb_builder_source_new();

		FwupdRemote *remote = g_ptr_array_index(remotes, i);
		if (!fwupd_remote_get_enabled(remote))
			continue;
		path = fwupd_remote_get_filename_cache(remote);
		if (!g_file_test(path, G_FILE_TEST_EXISTS))
			continue;

		/* generate all metadata on demand */
		if (fwupd_remote_get_kind(remote) == FWUPD_REMOTE_KIND_DIRECTORY) {
			g_debug("building metadata for remote '%s'", fwupd_remote_get_id(remote));
			if (!fu_engine_create_metadata(self, builder, remote, &error_local)) {
				g_warning("failed to generate remote %s: %s",
					  fwupd_remote_get_id(remote),
					  error_local->message);
			}
			continue;
		}

		/* save the remote-id in the custom metadata space */
		file = g_file_new_for_path(path);
		if (!xb_builder_source_load_file(source,
						 file,
						 XB_BUILDER_SOURCE_FLAG_NONE,
						 NULL,
						 &error_local)) {
			g_warning("failed to load remote %s: %s",
				  fwupd_remote_get_id(remote),
				  error_local->message);
			continue;
		}

		/* fix up any legacy installed files */
		fixup = xb_builder_fixup_new("AppStreamUpgrade",
					     fu_engine_appstream_upgrade_cb,
					     self,
					     NULL);
		xb_builder_fixup_set_max_depth(fixup, 3);
		xb_builder_source_add_fixup(source, fixup);

		/* add metadata */
		custom = xb_builder_node_new("custom");
		xb_builder_node_insert_text(custom,
					    "value",
					    path,
					    "key",
					    "fwupd::FilenameCache",
					    NULL);
		xb_builder_node_insert_text(custom,
					    "value",
					    fwupd_remote_get_id(remote),
					    "key",
					    "fwupd::RemoteId",
					    NULL);
		xb_builder_source_set_info(source, custom);

		/* we need to watch for changes? */
		xb_builder_import_source(builder, source);
	}

	/* add any client-side data, e.g. BKC tags */
	if (!fu_engine_load_metadata_store_local(self,
						 builder,
						 FU_PATH_KIND_LOCALSTATEDIR_PKG,
						 error))
		return FALSE;
	if (!fu_engine_load_metadata_store_local(self, builder, FU_PATH_KIND_DATADIR_PKG, error))
		return FALSE;

	/* on a read-only filesystem don't care about the cache GUID */
	if (flags & FU_ENGINE_LOAD_FLAG_READONLY)
		compile_flags |= XB_BUILDER_COMPILE_FLAG_IGNORE_GUID;

	/* ensure silo is up to date */
	if (flags & FU_ENGINE_LOAD_FLAG_NO_CACHE) {
		g_autoptr(GFileIOStream) iostr = NULL;
		xmlb = g_file_new_tmp(NULL, &iostr, error);
		if (xmlb == NULL)
			return FALSE;
	} else {
		g_autofree gchar *cachedirpkg = fu_path_from_kind(FU_PATH_KIND_CACHEDIR_PKG);
		g_autofree gchar *xmlbfn = g_build_filename(cachedirpkg, "metadata.xmlb", NULL);
		xmlb = g_file_new_for_path(xmlbfn);
	}
	self->silo = xb_builder_ensure(builder, xmlb, compile_flags, NULL, error);
	if (self->silo == NULL) {
		g_prefix_error(error, "cannot create metadata.xmlb: ");
		return FALSE;
	}

	/* success */
	return fu_engine_create_silo_index(self, error);
}

static void
fu_engine_config_changed_cb(FuConfig *config, FuEngine *self)
{
	fu_idle_set_timeout(self->idle, fu_config_get_idle_timeout(config));
}

static void
fu_engine_metadata_changed(FuEngine *self)
{
	g_autoptr(GError) error_local = NULL;
	if (!fu_engine_load_metadata_store(self, FU_ENGINE_LOAD_FLAG_NONE, &error_local))
		g_warning("Failed to reload metadata store: %s", error_local->message);

	/* set device properties from the metadata */
	fu_engine_md_refresh_devices(self);

	/* invalidate host security attributes */
	g_clear_pointer(&self->host_security_id, g_free);

	/* make the UI update */
	fu_engine_emit_changed(self);
}

static void
fu_engine_remote_list_changed_cb(FuRemoteList *remote_list, FuEngine *self)
{
	fu_engine_metadata_changed(self);
}

static gint
fu_engine_sort_jcat_results_timestamp_cb(gconstpointer a, gconstpointer b)
{
	JcatResult *ra = *((JcatResult **)a);
	JcatResult *rb = *((JcatResult **)b);
	if (jcat_result_get_timestamp(ra) < jcat_result_get_timestamp(rb))
		return 1;
	if (jcat_result_get_timestamp(ra) > jcat_result_get_timestamp(rb))
		return -1;
	return 0;
}

static JcatResult *
fu_engine_get_newest_signature_jcat_result(GPtrArray *results, GError **error)
{
	/* sort by timestamp, newest first */
	g_ptr_array_sort(results, fu_engine_sort_jcat_results_timestamp_cb);

	/* get the first signature, ignoring the checksums */
	for (guint i = 0; i < results->len; i++) {
		JcatResult *result = g_ptr_array_index(results, i);
		if (jcat_result_get_method(result) == JCAT_BLOB_METHOD_SIGNATURE)
			return g_object_ref(result);
	}

	/* should never happen due to %JCAT_VERIFY_FLAG_REQUIRE_SIGNATURE */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "no signature method in results");
	return NULL;
}

static JcatResult *
fu_engine_get_system_jcat_result(FuEngine *self, FwupdRemote *remote, GError **error)
{
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) blob_sig = NULL;
	g_autoptr(GInputStream) istream = NULL;
	g_autoptr(GPtrArray) results = NULL;
	g_autoptr(JcatItem) jcat_item = NULL;
	g_autoptr(JcatFile) jcat_file = jcat_file_new();

	blob = fu_bytes_get_contents(fwupd_remote_get_filename_cache(remote), error);
	if (blob == NULL)
		return NULL;
	blob_sig = fu_bytes_get_contents(fwupd_remote_get_filename_cache_sig(remote), error);
	if (blob_sig == NULL)
		return NULL;
	istream = g_memory_input_stream_new_from_bytes(blob_sig);
	if (!jcat_file_import_stream(jcat_file, istream, JCAT_IMPORT_FLAG_NONE, NULL, error))
		return NULL;
	jcat_item = jcat_file_get_item_default(jcat_file, error);
	if (jcat_item == NULL)
		return NULL;
	results = jcat_context_verify_item(self->jcat_context,
					   blob,
					   jcat_item,
					   JCAT_VERIFY_FLAG_REQUIRE_CHECKSUM |
					       JCAT_VERIFY_FLAG_REQUIRE_SIGNATURE,
					   error);
	if (results == NULL)
		return NULL;

	/* return the newest signature */
	return fu_engine_get_newest_signature_jcat_result(results, error);
}

static gboolean
fu_engine_validate_result_timestamp(JcatResult *jcat_result,
				    JcatResult *jcat_result_old,
				    GError **error)
{
	gint64 delta = 0;

	g_return_val_if_fail(JCAT_IS_RESULT(jcat_result), FALSE);
	g_return_val_if_fail(JCAT_IS_RESULT(jcat_result_old), FALSE);

	if (jcat_result_get_timestamp(jcat_result) == 0) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "no signing timestamp");
		return FALSE;
	}
	if (jcat_result_get_timestamp(jcat_result_old) > 0) {
		delta = jcat_result_get_timestamp(jcat_result) -
			jcat_result_get_timestamp(jcat_result_old);
	}
	if (delta < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "new signing timestamp was %" G_GINT64_FORMAT " seconds older",
			    -delta);
		return FALSE;
	}
	if (delta > 0)
		g_debug("timestamp increased, so no rollback");
	return TRUE;
}

/**
 * fu_engine_update_metadata_bytes:
 * @self: a #FuEngine
 * @remote_id: a remote ID, e.g. `lvfs`
 * @bytes_raw: Blob of metadata
 * @bytes_sig: Blob of metadata signature, typically Jcat binary format
 * @error: (nullable): optional return location for an error
 *
 * Updates the metadata for a specific remote.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_update_metadata_bytes(FuEngine *self,
				const gchar *remote_id,
				GBytes *bytes_raw,
				GBytes *bytes_sig,
				GError **error)
{
	FwupdKeyringKind keyring_kind;
	FwupdRemote *remote;
	JcatVerifyFlags jcat_flags = JCAT_VERIFY_FLAG_REQUIRE_SIGNATURE;
	g_autoptr(JcatFile) jcat_file = jcat_file_new();

	g_return_val_if_fail(FU_IS_ENGINE(self), FALSE);
	g_return_val_if_fail(remote_id != NULL, FALSE);
	g_return_val_if_fail(bytes_raw != NULL, FALSE);
	g_return_val_if_fail(bytes_sig != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* check remote is valid */
	remote = fu_remote_list_get_by_id(self->remote_list, remote_id);
	if (remote == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "remote %s not found",
			    remote_id);
		return FALSE;
	}
	if (!fwupd_remote_get_enabled(remote)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "remote %s not enabled",
			    remote_id);
		return FALSE;
	}

	/* verify JCatFile, or create a dummy one from legacy data */
	keyring_kind = fwupd_remote_get_keyring_kind(remote);
	if (keyring_kind == FWUPD_KEYRING_KIND_JCAT) {
		g_autoptr(GInputStream) istream = NULL;
		istream = g_memory_input_stream_new_from_bytes(bytes_sig);
		if (!jcat_file_import_stream(jcat_file,
					     istream,
					     JCAT_IMPORT_FLAG_NONE,
					     NULL,
					     error))
			return FALSE;
		jcat_flags |= JCAT_VERIFY_FLAG_REQUIRE_CHECKSUM;
	} else if (keyring_kind == FWUPD_KEYRING_KIND_GPG) {
		g_autoptr(JcatBlob) jcab_blob = NULL;
		g_autoptr(JcatItem) jcat_item = jcat_item_new("");
		jcab_blob = jcat_blob_new(JCAT_BLOB_KIND_GPG, bytes_sig);
		jcat_item_add_blob(jcat_item, jcab_blob);
		jcat_file_add_item(jcat_file, jcat_item);
	} else if (keyring_kind == FWUPD_KEYRING_KIND_PKCS7) {
		g_autoptr(JcatBlob) jcab_blob = NULL;
		g_autoptr(JcatItem) jcat_item = jcat_item_new("");
		jcab_blob = jcat_blob_new(JCAT_BLOB_KIND_PKCS7, bytes_sig);
		jcat_item_add_blob(jcat_item, jcab_blob);
		jcat_file_add_item(jcat_file, jcat_item);
	}

	/* verify file */
	if (keyring_kind != FWUPD_KEYRING_KIND_NONE) {
		g_autoptr(GError) error_local = NULL;
		g_autoptr(GPtrArray) results = NULL;
		g_autoptr(JcatItem) jcat_item = NULL;
		g_autoptr(JcatResult) jcat_result = NULL;
		g_autoptr(JcatResult) jcat_result_old = NULL;

		/* this should only be signing one thing */
		jcat_item = jcat_file_get_item_default(jcat_file, error);
		if (jcat_item == NULL)
			return FALSE;
		results = jcat_context_verify_item(self->jcat_context,
						   bytes_raw,
						   jcat_item,
						   jcat_flags,
						   error);
		if (results == NULL)
			return FALSE;

		/* return the newest signature */
		jcat_result = fu_engine_get_newest_signature_jcat_result(results, error);
		if (jcat_result == NULL)
			return FALSE;

		/* verify the metadata was signed later than the existing
		 * metadata for this remote to mitigate a rollback attack */
		jcat_result_old = fu_engine_get_system_jcat_result(self, remote, &error_local);
		if (jcat_result_old == NULL) {
			if (g_error_matches(error_local, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
				g_debug("no existing valid keyrings: %s", error_local->message);
			} else {
				g_warning("could not get existing keyring result: %s",
					  error_local->message);
			}
		} else {
			if (!fu_engine_validate_result_timestamp(jcat_result,
								 jcat_result_old,
								 error))
				return FALSE;
		}
	}

	/* save XML and signature to remotes.d */
	if (!fu_bytes_set_contents(fwupd_remote_get_filename_cache(remote), bytes_raw, error))
		return FALSE;
	if (keyring_kind != FWUPD_KEYRING_KIND_NONE) {
		if (!fu_bytes_set_contents(fwupd_remote_get_filename_cache_sig(remote),
					   bytes_sig,
					   error))
			return FALSE;
	}
	if (!fu_engine_load_metadata_store(self, FU_ENGINE_LOAD_FLAG_NONE, error))
		return FALSE;

	/* refresh SUPPORTED flag on devices */
	fu_engine_md_refresh_devices(self);

	/* invalidate host security attributes */
	g_clear_pointer(&self->host_security_id, g_free);

	/* make the UI update */
	fu_engine_emit_changed(self);
	return TRUE;
}

/**
 * fu_engine_update_metadata:
 * @self: a #FuEngine
 * @remote_id: a remote ID, e.g. `lvfs`
 * @fd: file descriptor of the metadata
 * @fd_sig: file descriptor of the metadata signature
 * @error: (nullable): optional return location for an error
 *
 * Updates the metadata for a specific remote.
 *
 * Note: this will close the fds when done
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_update_metadata(FuEngine *self,
			  const gchar *remote_id,
			  gint fd,
			  gint fd_sig,
			  GError **error)
{
#ifdef HAVE_GIO_UNIX
	g_autoptr(GBytes) bytes_raw = NULL;
	g_autoptr(GBytes) bytes_sig = NULL;
	g_autoptr(GInputStream) stream_fd = NULL;
	g_autoptr(GInputStream) stream_sig = NULL;

	g_return_val_if_fail(FU_IS_ENGINE(self), FALSE);
	g_return_val_if_fail(remote_id != NULL, FALSE);
	g_return_val_if_fail(fd > 0, FALSE);
	g_return_val_if_fail(fd_sig > 0, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* ensures the fd's are closed on error */
	stream_fd = g_unix_input_stream_new(fd, TRUE);
	stream_sig = g_unix_input_stream_new(fd_sig, TRUE);

	/* read the entire file into memory */
	bytes_raw = g_input_stream_read_bytes(stream_fd, 0x100000, NULL, error);
	if (bytes_raw == NULL)
		return FALSE;

	/* read signature */
	bytes_sig = g_input_stream_read_bytes(stream_sig, 0x100000, NULL, error);
	if (bytes_sig == NULL)
		return FALSE;

	/* update with blobs */
	return fu_engine_update_metadata_bytes(self, remote_id, bytes_raw, bytes_sig, error);
#else
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "Not supported as <glib-unix.h> is unavailable");
	return FALSE;
#endif
}

/**
 * fu_engine_get_silo_from_blob:
 * @self: a #FuEngine
 * @blob_cab: a data blob
 * @error: (nullable): optional return location for an error
 *
 * Creates a silo from a .cab file blob.
 *
 * Returns: (transfer container): a #XbSilo, or %NULL
 **/
XbSilo *
fu_engine_get_silo_from_blob(FuEngine *self, GBytes *blob_cab, GError **error)
{
	g_autoptr(FuCabinet) cabinet = fu_cabinet_new();

	g_return_val_if_fail(FU_IS_ENGINE(self), NULL);
	g_return_val_if_fail(blob_cab != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* load file */
	fu_engine_set_status(self, FWUPD_STATUS_DECOMPRESSING);
	fu_cabinet_set_size_max(cabinet, fu_config_get_archive_size_max(self->config));
	fu_cabinet_set_jcat_context(cabinet, self->jcat_context);
	if (!fu_cabinet_parse(cabinet, blob_cab, FU_CABINET_PARSE_FLAG_NONE, error))
		return NULL;
	return fu_cabinet_get_silo(cabinet);
}

static FuDevice *
fu_engine_get_result_from_component(FuEngine *self,
				    FuEngineRequest *request,
				    XbNode *component,
				    GError **error)
{
	FwupdReleaseFlags release_flags = FWUPD_RELEASE_FLAG_NONE;
	g_autofree gchar *description_xpath = NULL;
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuRelease) release = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GError) error_reqs = NULL;
	g_autoptr(GPtrArray) provides = NULL;
	g_autoptr(GPtrArray) tags = NULL;
	g_autoptr(XbNode) description = NULL;
	g_autoptr(XbNode) rel = NULL;
#if LIBXMLB_CHECK_VERSION(0, 2, 0)
	g_autoptr(XbQuery) query = NULL;
#endif

	dev = fu_device_new(self->ctx);
	provides = xb_node_query(component, "provides/firmware[@type=$'flashed']", 0, &error_local);
	if (provides == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to get release: %s",
			    error_local->message);
		return NULL;
	}
	for (guint i = 0; i < provides->len; i++) {
		XbNode *prov = XB_NODE(g_ptr_array_index(provides, i));
		const gchar *guid;
		g_autoptr(FuDevice) device = NULL;

		/* is a online or offline update appropriate */
		guid = xb_node_get_text(prov);
		if (guid == NULL)
			continue;
		device = fu_device_list_get_by_guid(self->device_list, guid, NULL);
		if (device != NULL) {
			fu_device_set_name(dev, fu_device_get_name(device));
			fu_device_set_flags(dev, fu_device_get_flags(device));
			fu_device_set_internal_flags(dev, fu_device_get_internal_flags(device));
			fu_device_set_id(dev, fu_device_get_id(device));
			fu_device_set_version_raw(dev, fu_device_get_version_raw(device));
			fu_device_set_version_format(dev, fu_device_get_version_format(device));
			fu_device_set_version(dev, fu_device_get_version(device));
		} else {
			fu_device_inhibit(dev, "not-found", "Device was not found");
		}

		/* add GUID */
		fu_device_add_guid(dev, guid);
	}
	if (fu_device_get_guids(dev)->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "component has no GUIDs");
		return NULL;
	}

	/* add tags */
	tags = xb_node_query(component, "tags/tag[@namespace=$'lvfs']", 0, NULL);
	if (tags != NULL) {
		for (guint i = 0; i < tags->len; i++) {
			XbNode *tag = g_ptr_array_index(tags, i);
			fu_release_add_tag(release, xb_node_get_text(tag));
		}
	}

	/* add EOL flag */
	if (xb_node_get_attr(component, "date_eol") != NULL)
		fu_device_add_flag(dev, FWUPD_DEVICE_FLAG_END_OF_LIFE);

	/* check we can install it */
	release = fu_release_new();
	fu_release_set_device(release, dev);
	fu_release_set_request(release, request);
	if (!fu_engine_load_release(self,
				    release,
				    component,
				    NULL,
				    FWUPD_INSTALL_FLAG_IGNORE_VID_PID,
				    &error_reqs)) {
		if (!fu_device_has_inhibit(dev, "not-found"))
			fu_device_inhibit(dev, "failed-reqs", error_reqs->message);
		/* continue */
	}

	/* verify trust */
#if LIBXMLB_CHECK_VERSION(0, 2, 0)
	query = xb_query_new_full(xb_node_get_silo(component),
				  "releases/release",
				  XB_QUERY_FLAG_FORCE_NODE_CACHE,
				  error);
	if (query == NULL)
		return NULL;
	rel = xb_node_query_first_full(component, query, &error_local);
#else
	rel = xb_node_query_first(component, "releases/release", &error_local);
#endif
	if (rel == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to get release: %s",
			    error_local->message);
		return NULL;
	}
	if (!fu_keyring_get_release_flags(rel, &release_flags, &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
			g_warning("Ignoring verification: %s", error_local->message);
		} else {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return NULL;
		}
	}
	fwupd_release_set_flags(FWUPD_RELEASE(release), release_flags);

	/* create a result with all the metadata in */
	description_xpath = fu_engine_request_get_localized_xpath(request, "description");
	description = xb_node_query_first(component, description_xpath, NULL);
	if (description != NULL) {
		g_autofree gchar *xml = NULL;
		xml = xb_node_export(description, XB_NODE_EXPORT_FLAG_ONLY_CHILDREN, NULL);
		if (xml != NULL)
			fu_device_set_description(dev, xml);
	}

	/* refresh the device to the new version format too */
	fu_engine_md_refresh_device_from_component(self, dev, component);

	/* success */
	fu_device_add_release(dev, FWUPD_RELEASE(release));
	return g_steal_pointer(&dev);
}

static gint
fu_engine_get_details_sort_cb(gconstpointer a, gconstpointer b)
{
	FuDevice *device1 = *((FuDevice **)a);
	FuDevice *device2 = *((FuDevice **)b);
	if (!fu_device_has_flag(device1, FWUPD_DEVICE_FLAG_UPDATABLE) &&
	    fu_device_has_flag(device2, FWUPD_DEVICE_FLAG_UPDATABLE))
		return 1;
	if (fu_device_has_flag(device1, FWUPD_DEVICE_FLAG_UPDATABLE) &&
	    !fu_device_has_flag(device2, FWUPD_DEVICE_FLAG_UPDATABLE))
		return -1;
	return 0;
}

/* for self tests */
GPtrArray *
fu_engine_get_details_for_bytes(FuEngine *self,
				FuEngineRequest *request,
				GBytes *blob,
				GError **error)
{
	const gchar *remote_id;
	GChecksumType checksum_types[] = {G_CHECKSUM_SHA256, G_CHECKSUM_SHA1, 0};
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) components = NULL;
	g_autoptr(GPtrArray) details = NULL;
	g_autoptr(GPtrArray) checksums = g_ptr_array_new_with_free_func(g_free);
	g_autoptr(XbSilo) silo = NULL;

	silo = fu_engine_get_silo_from_blob(self, blob, error);
	if (silo == NULL)
		return NULL;
	components = xb_silo_query(silo, "components/component[@type='firmware']", 0, &error_local);
	if (components == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "no components: %s",
			    error_local->message);
		return NULL;
	}

	/* build the index */
	if (!xb_silo_query_build_index(silo,
				       "components/component[@type='firmware']/provides/firmware",
				       "type",
				       error))
		return NULL;
	if (!xb_silo_query_build_index(silo,
				       "components/component[@type='firmware']/provides/firmware",
				       NULL,
				       error))
		return NULL;

	/* calculate the checksums of the blob */
	for (guint i = 0; checksum_types[i] != 0; i++)
		g_ptr_array_add(checksums, g_compute_checksum_for_bytes(checksum_types[i], blob));

	/* does this exist in any enabled remote */
	for (guint i = 0; i < checksums->len; i++) {
		const gchar *csum = g_ptr_array_index(checksums, i);
		remote_id = fu_engine_get_remote_id_for_checksum(self, csum);
		if (remote_id != NULL)
			break;
	}

	/* create results with all the metadata in */
	details = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (guint i = 0; i < components->len; i++) {
		XbNode *component = g_ptr_array_index(components, i);
		FuDevice *dev;
		FwupdRelease *rel;
		dev = fu_engine_get_result_from_component(self, request, component, error);
		if (dev == NULL)
			return NULL;
		rel = fu_device_get_release_default(dev);
		if (remote_id != NULL) {
			fwupd_release_set_remote_id(rel, remote_id);
			fu_device_add_flag(dev, FWUPD_DEVICE_FLAG_SUPPORTED);
		}
		if (fu_device_has_internal_flag(dev, FU_DEVICE_INTERNAL_FLAG_MD_SET_VERFMT))
			fu_engine_md_refresh_device_verfmt(self, dev, component);

		/* add the checksum of the container blob */
		for (guint j = 0; j < checksums->len; j++) {
			const gchar *csum = g_ptr_array_index(checksums, j);
			fwupd_release_add_checksum(rel, csum);
		}

		/* if this matched a device on the system, ensure all the
		 * requirements passed before setting UPDATABLE */
		if (fu_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE)) {
			g_autoptr(FuRelease) release = fu_release_new();
			g_autoptr(GError) error_req = NULL;
			FwupdInstallFlags install_flags =
			    FWUPD_INSTALL_FLAG_OFFLINE | FWUPD_INSTALL_FLAG_IGNORE_VID_PID |
			    FWUPD_INSTALL_FLAG_ALLOW_REINSTALL |
			    FWUPD_INSTALL_FLAG_ALLOW_BRANCH_SWITCH | FWUPD_INSTALL_FLAG_ALLOW_OLDER;

			fu_release_set_device(release, dev);
			fu_release_set_request(release, request);
			if (!fu_engine_load_release(self,
						    release,
						    component,
						    NULL,
						    install_flags,
						    &error_req)) {
				g_debug("%s failed requirement checks: %s",
					fu_device_get_id(dev),
					error_req->message);
				fu_device_inhibit(dev, "failed-reqs", error_req->message);
			} else {
				g_debug("%s passed requirement checks", fu_device_get_id(dev));
				fu_device_uninhibit(dev, "failed-reqs");
			}
		}

		g_ptr_array_add(details, dev);
	}

	/* order multiple devices so that the one that passes the requirement
	 * is listed first */
	g_ptr_array_sort(details, fu_engine_get_details_sort_cb);

	return g_steal_pointer(&details);
}

/**
 * fu_engine_get_details:
 * @self: a #FuEngine
 * @request: a #FuEngineRequest
 * @fd: a file descriptor
 * @error: (nullable): optional return location for an error
 *
 * Gets the details about a local file.
 *
 * Note: this will close the fd when done
 *
 * Returns: (transfer container) (element-type FuDevice): results
 **/
GPtrArray *
fu_engine_get_details(FuEngine *self, FuEngineRequest *request, gint fd, GError **error)
{
	g_autoptr(GBytes) blob = NULL;

	g_return_val_if_fail(FU_IS_ENGINE(self), NULL);
	g_return_val_if_fail(fd > 0, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* get all components */
	blob = fu_bytes_get_contents_fd(fd, fu_config_get_archive_size_max(self->config), error);
	if (blob == NULL)
		return NULL;
	return fu_engine_get_details_for_bytes(self, request, blob, error);
}

static gint
fu_engine_sort_devices_by_priority_name(gconstpointer a, gconstpointer b)
{
	FuDevice *dev_a = *((FuDevice **)a);
	FuDevice *dev_b = *((FuDevice **)b);
	gint prio_a = fu_device_get_priority(dev_a);
	gint prio_b = fu_device_get_priority(dev_b);
	const gchar *name_a = fu_device_get_name(dev_a);
	const gchar *name_b = fu_device_get_name(dev_b);

	if (prio_a > prio_b)
		return -1;
	if (prio_a < prio_b)
		return 1;
	if (g_strcmp0(name_a, name_b) > 0)
		return 1;
	if (g_strcmp0(name_a, name_b) < 0)
		return -1;
	return 0;
}

/**
 * fu_engine_get_devices:
 * @self: a #FuEngine
 * @error: (nullable): optional return location for an error
 *
 * Gets the list of devices.
 *
 * Returns: (transfer container) (element-type FwupdDevice): results
 **/
GPtrArray *
fu_engine_get_devices(FuEngine *self, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;

	g_return_val_if_fail(FU_IS_ENGINE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	devices = fu_device_list_get_active(self->device_list);
	if (devices->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "No detected devices");
		return NULL;
	}
	g_ptr_array_sort(devices, fu_engine_sort_devices_by_priority_name);
	return g_steal_pointer(&devices);
}

/**
 * fu_engine_get_devices_by_guid:
 * @self: a #FuEngine
 * @guid: a GUID
 * @error: (nullable): optional return location for an error
 *
 * Gets a specific device.
 *
 * Returns: (transfer full): a device, or %NULL if not found
 **/
GPtrArray *
fu_engine_get_devices_by_guid(FuEngine *self, const gchar *guid, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) devices_tmp = NULL;

	/* find the devices by GUID */
	devices_tmp = fu_device_list_get_all(self->device_list);
	devices = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (guint i = 0; i < devices_tmp->len; i++) {
		FuDevice *dev_tmp = g_ptr_array_index(devices_tmp, i);
		if (fu_device_has_guid(dev_tmp, guid))
			g_ptr_array_add(devices, g_object_ref(dev_tmp));
	}

	/* nothing */
	if (devices->len == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "failed to find any device providing %s",
			    guid);
		return NULL;
	}

	/* success */
	return g_steal_pointer(&devices);
}

/**
 * fu_engine_get_devices_by_composite_id:
 * @self: a #FuEngine
 * @composite_id: a device ID
 * @error: (nullable): optional return location for an error
 *
 * Gets all devices that match a specific composite ID.
 *
 * Returns: (transfer full) (element-type FuDevice): devices
 **/
GPtrArray *
fu_engine_get_devices_by_composite_id(FuEngine *self, const gchar *composite_id, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) devices_tmp = NULL;

	/* find the devices by composite ID */
	devices_tmp = fu_device_list_get_all(self->device_list);
	devices = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (guint i = 0; i < devices_tmp->len; i++) {
		FuDevice *dev_tmp = g_ptr_array_index(devices_tmp, i);
		if (g_strcmp0(fu_device_get_composite_id(dev_tmp), composite_id) == 0)
			g_ptr_array_add(devices, g_object_ref(dev_tmp));
	}

	/* nothing */
	if (devices->len == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "failed to find any device with composite ID %s",
			    composite_id);
		return NULL;
	}

	/* success */
	return g_steal_pointer(&devices);
}

static void
fu_engine_get_history_set_hsi_attrs(FuEngine *self, FuDevice *device)
{
	g_autoptr(GPtrArray) vals = NULL;

	/* ensure up to date */
	fu_engine_ensure_security_attrs(self);

	/* add attributes */
	vals = fu_security_attrs_get_all(self->host_security_attrs);
	for (guint i = 0; i < vals->len; i++) {
		FwupdSecurityAttr *attr = g_ptr_array_index(vals, i);
		const gchar *tmp;
		tmp = fwupd_security_attr_result_to_string(fwupd_security_attr_get_result(attr));
		fu_device_set_metadata(device, fwupd_security_attr_get_appstream_id(attr), tmp);
	}

	/* computed value */
	fu_device_set_metadata(device, "HSI", self->host_security_id);
}

/**
 * fu_engine_get_history:
 * @self: a #FuEngine
 * @error: (nullable): optional return location for an error
 *
 * Gets the list of history.
 *
 * Returns: (transfer container) (element-type FwupdDevice): results
 **/
GPtrArray *
fu_engine_get_history(FuEngine *self, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;

	g_return_val_if_fail(FU_IS_ENGINE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	devices = fu_history_get_devices(self->history, error);
	if (devices == NULL)
		return NULL;
	if (devices->len == 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO, "No history");
		return NULL;
	}

	/* if this is the system firmware device, add the HSI attrs */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index(devices, i);
		if (fu_device_has_instance_id(dev, "main-system-firmware"))
			fu_engine_get_history_set_hsi_attrs(self, dev);
	}

	/* try to set the remote ID for each device */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index(devices, i);
		FwupdRelease *rel;
		GPtrArray *csums;

		/* get the checksums */
		rel = fu_device_get_release_default(dev);
		if (rel == NULL)
			continue;

		/* find the checksum that matches */
		csums = fwupd_release_get_checksums(rel);
		for (guint j = 0; j < csums->len; j++) {
			const gchar *csum = g_ptr_array_index(csums, j);
			const gchar *remote_id = fu_engine_get_remote_id_for_checksum(self, csum);
			if (remote_id != NULL) {
				fu_device_add_flag(dev, FWUPD_DEVICE_FLAG_SUPPORTED);
				fwupd_release_set_remote_id(rel, remote_id);
				break;
			}
		}
	}

	return g_steal_pointer(&devices);
}

#if !GLIB_CHECK_VERSION(2, 62, 0)
static GPtrArray *
g_ptr_array_copy(GPtrArray *array, GCopyFunc func, gpointer user_data)
{
	GPtrArray *new = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (guint i = 0; i < array->len; i++) {
		GObject *obj = g_ptr_array_index(array, i);
		g_ptr_array_add(new, g_object_ref(obj));
	}
	return new;
}
#endif

/**
 * fu_engine_get_remotes:
 * @self: a #FuEngine
 * @error: (nullable): optional return location for an error
 *
 * Gets the list of remotes in use by the engine.
 *
 * Returns: (transfer container) (element-type FwupdRemote): results
 **/
GPtrArray *
fu_engine_get_remotes(FuEngine *self, GError **error)
{
	GPtrArray *remotes;

	g_return_val_if_fail(FU_IS_ENGINE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	remotes = fu_remote_list_get_all(self->remote_list);
	if (remotes->len == 0) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "No remotes configured");
		return NULL;
	}

	/* deep copy so the remote list can be kept up to date */
	return g_ptr_array_copy(remotes, (GCopyFunc)g_object_ref, NULL);
}

/**
 * fu_engine_get_remote_by_id:
 * @self: a #FuEngine
 * @remote_id: a string representation of a remote
 * @error: (nullable): optional return location for an error
 *
 * Gets the FwupdRemote object.
 *
 * Returns: FwupdRemote
 **/
FwupdRemote *
fu_engine_get_remote_by_id(FuEngine *self, const gchar *remote_id, GError **error)
{
	g_autoptr(GPtrArray) remotes = NULL;

	remotes = fu_engine_get_remotes(self, error);
	if (remotes == NULL)
		return NULL;

	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index(remotes, i);
		if (g_strcmp0(remote_id, fwupd_remote_get_id(remote)) == 0)
			return remote;
	}

	g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "Couldn't find remote %s", remote_id);

	return NULL;
}

static gint
fu_engine_sort_releases_cb(gconstpointer a, gconstpointer b, gpointer user_data)
{
	FuDevice *device = FU_DEVICE(user_data);
	FwupdRelease *rel_a = FWUPD_RELEASE(*((FwupdRelease **)a));
	FwupdRelease *rel_b = FWUPD_RELEASE(*((FwupdRelease **)b));
	gint rc;

	/* first by branch */
	rc = g_strcmp0(fwupd_release_get_branch(rel_b), fwupd_release_get_branch(rel_a));
	if (rc != 0)
		return rc;

	/* then by version */
	return fu_version_compare(fwupd_release_get_version(rel_b),
				  fwupd_release_get_version(rel_a),
				  fu_device_get_version_format(device));
}

static gboolean
fu_engine_check_release_is_approved(FuEngine *self, FwupdRelease *rel)
{
	GPtrArray *csums = fwupd_release_get_checksums(rel);
	if (self->approved_firmware == NULL)
		return FALSE;
	for (guint i = 0; i < csums->len; i++) {
		const gchar *csum = g_ptr_array_index(csums, i);
		g_debug("checking %s against approved list", csum);
		if (g_hash_table_lookup(self->approved_firmware, csum) != NULL)
			return TRUE;
	}
	return FALSE;
}

static gboolean
fu_engine_check_release_is_blocked(FuEngine *self, FuRelease *release)
{
	GPtrArray *csums = fu_release_get_checksums(release);
	if (self->blocked_firmware == NULL)
		return FALSE;
	for (guint i = 0; i < csums->len; i++) {
		const gchar *csum = g_ptr_array_index(csums, i);
		if (g_hash_table_lookup(self->blocked_firmware, csum) != NULL)
			return TRUE;
	}
	return FALSE;
}

static gboolean
fu_engine_add_releases_for_device_component(FuEngine *self,
					    FuEngineRequest *request,
					    FuDevice *device,
					    XbNode *component,
					    GPtrArray *releases,
					    GError **error)
{
	FwupdFeatureFlags feature_flags;
	FwupdVersionFormat fmt = fu_device_get_version_format(device);
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) releases_tmp = NULL;
	FwupdInstallFlags install_flags =
	    FWUPD_INSTALL_FLAG_OFFLINE | FWUPD_INSTALL_FLAG_IGNORE_VID_PID |
	    FWUPD_INSTALL_FLAG_ALLOW_BRANCH_SWITCH | FWUPD_INSTALL_FLAG_ALLOW_REINSTALL |
	    FWUPD_INSTALL_FLAG_ALLOW_OLDER;

	/* get all releases */
	releases_tmp = xb_node_query(component, "releases/release", 0, &error_local);
	if (releases_tmp == NULL) {
		if (g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return TRUE;
		if (g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
			return TRUE;
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}
	feature_flags = fu_engine_request_get_feature_flags(request);
	for (guint i = 0; i < releases_tmp->len; i++) {
		XbNode *rel = g_ptr_array_index(releases_tmp, i);
		const gchar *remote_id;
		const gchar *update_message;
		const gchar *update_image;
		gint vercmp;
		GPtrArray *checksums;
		GPtrArray *locations;
		g_autoptr(FuRelease) release = fu_release_new();
		g_autoptr(GError) error_loop = NULL;

		/* create new FwupdRelease for the XbNode */
		fu_release_set_request(release, request);
		fu_release_set_device(release, device);
		if (!fu_engine_load_release(self,
					    release,
					    component,
					    rel,
					    install_flags,
					    &error_loop)) {
			g_debug("failed to set release for component: %s", error_loop->message);
			continue;
		}

		/* fall back to quirk-provided value */
		if (fwupd_release_get_install_duration(FWUPD_RELEASE(release)) == 0) {
			fwupd_release_set_install_duration(FWUPD_RELEASE(release),
							   fu_device_get_install_duration(device));
		}

		/* invalid */
		locations = fwupd_release_get_locations(FWUPD_RELEASE(release));
		if (locations->len == 0)
			continue;
		checksums = fu_release_get_checksums(release);
		if (checksums->len == 0)
			continue;

		/* different branch */
		if (g_strcmp0(fu_release_get_branch(release), fu_device_get_branch(device)) != 0) {
			if ((feature_flags & FWUPD_FEATURE_FLAG_SWITCH_BRANCH) == 0) {
				g_debug("client does not understand branches, skipping %s:%s",
					fu_release_get_branch(release),
					fu_release_get_version(release));
				continue;
			}
			fu_release_add_flag(release, FWUPD_RELEASE_FLAG_IS_ALTERNATE_BRANCH);
		}

		/* test for upgrade or downgrade */
		vercmp = fu_version_compare(fu_release_get_version(release),
					    fu_device_get_version(device),
					    fmt);
		if (vercmp > 0)
			fu_release_add_flag(release, FWUPD_RELEASE_FLAG_IS_UPGRADE);
		else if (vercmp < 0)
			fu_release_add_flag(release, FWUPD_RELEASE_FLAG_IS_DOWNGRADE);

		/* lower than allowed to downgrade to */
		if (fu_device_get_version_lowest(device) != NULL &&
		    fu_version_compare(fu_release_get_version(release),
				       fu_device_get_version_lowest(device),
				       fmt) < 0) {
			fu_release_add_flag(release, FWUPD_RELEASE_FLAG_BLOCKED_VERSION);
		}

		/* manually blocked */
		if (fu_engine_check_release_is_blocked(self, release))
			fu_release_add_flag(release, FWUPD_RELEASE_FLAG_BLOCKED_APPROVAL);

		/* check if remote is filtering firmware */
		remote_id = fwupd_release_get_remote_id(FWUPD_RELEASE(release));
		if (remote_id != NULL) {
			FwupdRemote *remote = fu_engine_get_remote_by_id(self, remote_id, NULL);
			if (remote != NULL && fwupd_remote_get_approval_required(remote) &&
			    !fu_engine_check_release_is_approved(self, FWUPD_RELEASE(release))) {
				fu_release_add_flag(release, FWUPD_RELEASE_FLAG_BLOCKED_APPROVAL);
			}
		}

		/* add update message if exists but device doesn't already have one */
		update_message = fwupd_release_get_update_message(FWUPD_RELEASE(release));
		if (fwupd_device_get_update_message(FWUPD_DEVICE(device)) == NULL &&
		    update_message != NULL) {
			fu_device_set_update_message(device, update_message);
		}
		update_image = fwupd_release_get_update_image(FWUPD_RELEASE(release));
		if (fwupd_device_get_update_image(FWUPD_DEVICE(device)) == NULL &&
		    update_image != NULL) {
			fwupd_device_set_update_image(FWUPD_DEVICE(device), update_image);
		}
		/* success */
		g_ptr_array_add(releases, g_steal_pointer(&release));
	}

	/* success */
	return TRUE;
}

static const gchar *
fu_engine_get_branch_fallback(const gchar *nullable_branch)
{
	if (nullable_branch == NULL)
		return "default";
	return nullable_branch;
}

GPtrArray *
fu_engine_get_releases_for_device(FuEngine *self,
				  FuEngineRequest *request,
				  FuDevice *device,
				  GError **error)
{
	GPtrArray *device_guids;
	const gchar *version;
	g_autoptr(GError) error_all = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) branches = NULL;
	g_autoptr(GPtrArray) components = NULL;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(GString) xpath = g_string_new(NULL);

	/* get device version */
	version = fu_device_get_version(device);
	if (version == NULL) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no version set");
		return NULL;
	}

	/* only show devices that can be updated */
	if (!fu_engine_request_has_feature_flag(request, FWUPD_FEATURE_FLAG_SHOW_PROBLEMS) &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE) &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "is not updatable");
		return NULL;
	}

	/* get all the components that provide any of these GUIDs */
	device_guids = fu_device_get_guids(device);
	for (guint i = 0; i < device_guids->len; i++) {
		const gchar *guid = g_ptr_array_index(device_guids, i);
		xb_string_append_union(xpath,
				       "components/component[@type='firmware']/"
				       "provides/firmware[@type=$'flashed'][text()=$'%s']/"
				       "../..",
				       guid);
	}
	components = xb_silo_query(self->silo, xpath->str, 0, &error_local);
	if (components == NULL) {
		if (g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
		    g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT)) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOTHING_TO_DO,
					    "No releases found");
			return NULL;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return NULL;
	}

	/* find all the releases that pass all the requirements */
	releases = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (guint i = 0; i < components->len; i++) {
		XbNode *component = XB_NODE(g_ptr_array_index(components, i));
		g_autoptr(GError) error_tmp = NULL;
		if (!fu_engine_add_releases_for_device_component(self,
								 request,
								 device,
								 component,
								 releases,
								 &error_tmp)) {
			if (error_all == NULL) {
				error_all = g_steal_pointer(&error_tmp);
				continue;
			}

			/* assume the domain and code is the same */
			g_prefix_error(&error_all, "%s, ", error_tmp->message);
		}
	}

	/* are there multiple branches available */
	branches = g_ptr_array_new_with_free_func(g_free);
	g_ptr_array_add(branches,
			g_strdup(fu_engine_get_branch_fallback(fu_device_get_branch(device))));
	for (guint i = 0; i < releases->len; i++) {
		FwupdRelease *rel_tmp = FWUPD_RELEASE(g_ptr_array_index(releases, i));
		const gchar *branch_tmp =
		    fu_engine_get_branch_fallback(fwupd_release_get_branch(rel_tmp));
#if GLIB_CHECK_VERSION(2, 54, 3)
		if (g_ptr_array_find_with_equal_func(branches, branch_tmp, g_str_equal, NULL))
			continue;
#endif
		g_ptr_array_add(branches, g_strdup(branch_tmp));
	}
	if (branches->len > 1)
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_HAS_MULTIPLE_BRANCHES);

	/* return the compound error */
	if (releases->len == 0) {
		if (error_all != NULL) {
			g_propagate_prefixed_error(error,
						   g_steal_pointer(&error_all),
						   "No releases found: ");
			return NULL;
		}
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO, "No releases found");
		return NULL;
	}
	return g_steal_pointer(&releases);
}

/**
 * fu_engine_get_releases:
 * @self: a #FuEngine
 * @request: a #FuEngineRequest
 * @device_id: a device ID
 * @error: (nullable): optional return location for an error
 *
 * Gets the releases available for a specific device.
 *
 * Returns: (transfer container) (element-type FwupdDevice): results
 **/
GPtrArray *
fu_engine_get_releases(FuEngine *self,
		       FuEngineRequest *request,
		       const gchar *device_id,
		       GError **error)
{
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(GPtrArray) releases = NULL;

	g_return_val_if_fail(FU_IS_ENGINE(self), NULL);
	g_return_val_if_fail(device_id != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* find the device */
	device = fu_device_list_get_by_id(self->device_list, device_id, error);
	if (device == NULL)
		return NULL;

	/* get all the releases for the device */
	releases = fu_engine_get_releases_for_device(self, request, device, error);
	if (releases == NULL)
		return NULL;
	if (releases->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "No releases for device");
		return NULL;
	}
	g_ptr_array_sort_with_data(releases, fu_engine_sort_releases_cb, device);
	return g_steal_pointer(&releases);
}

/**
 * fu_engine_get_downgrades:
 * @self: a #FuEngine
 * @request: a #FuEngineRequest
 * @device_id: a device ID
 * @error: (nullable): optional return location for an error
 *
 * Gets the downgrades available for a specific device.
 *
 * Returns: (transfer container) (element-type FwupdDevice): results
 **/
GPtrArray *
fu_engine_get_downgrades(FuEngine *self,
			 FuEngineRequest *request,
			 const gchar *device_id,
			 GError **error)
{
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(GPtrArray) releases_tmp = NULL;
	g_autoptr(GString) error_str = g_string_new(NULL);

	g_return_val_if_fail(FU_IS_ENGINE(self), NULL);
	g_return_val_if_fail(device_id != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* find the device */
	device = fu_device_list_get_by_id(self->device_list, device_id, error);
	if (device == NULL)
		return NULL;

	/* get all the releases for the device */
	releases_tmp = fu_engine_get_releases_for_device(self, request, device, error);
	if (releases_tmp == NULL)
		return NULL;
	releases = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (guint i = 0; i < releases_tmp->len; i++) {
		FwupdRelease *rel_tmp = g_ptr_array_index(releases_tmp, i);

		/* same as installed */
		if (!fwupd_release_has_flag(rel_tmp, FWUPD_RELEASE_FLAG_IS_UPGRADE) &&
		    !fwupd_release_has_flag(rel_tmp, FWUPD_RELEASE_FLAG_IS_DOWNGRADE)) {
			g_string_append_printf(error_str,
					       "%s=same, ",
					       fwupd_release_get_version(rel_tmp));
			g_debug("ignoring %s as the same as %s",
				fwupd_release_get_version(rel_tmp),
				fu_device_get_version(device));
			continue;
		}

		/* newer than current */
		if (fwupd_release_has_flag(rel_tmp, FWUPD_RELEASE_FLAG_IS_UPGRADE)) {
			g_string_append_printf(error_str,
					       "%s=newer, ",
					       fwupd_release_get_version(rel_tmp));
			g_debug("ignoring %s as newer than %s",
				fwupd_release_get_version(rel_tmp),
				fu_device_get_version(device));
			continue;
		}

		/* don't show releases we are not allowed to downgrade to */
		if (fwupd_release_has_flag(rel_tmp, FWUPD_RELEASE_FLAG_BLOCKED_VERSION)) {
			g_string_append_printf(error_str,
					       "%s=lowest, ",
					       fwupd_release_get_version(rel_tmp));
			g_debug("ignoring %s as older than lowest %s",
				fwupd_release_get_version(rel_tmp),
				fu_device_get_version_lowest(device));
			continue;
		}

		/* different branch */
		if (fwupd_release_has_flag(rel_tmp, FWUPD_RELEASE_FLAG_IS_ALTERNATE_BRANCH)) {
			g_debug("ignoring release %s as branch %s, and device is %s",
				fwupd_release_get_version(rel_tmp),
				fwupd_release_get_branch(rel_tmp),
				fu_device_get_branch(device));
			continue;
		}

		g_ptr_array_add(releases, g_object_ref(rel_tmp));
	}
	if (error_str->len > 2)
		g_string_truncate(error_str, error_str->len - 2);
	if (releases->len == 0) {
		if (error_str->len > 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "current version is %s: %s",
				    fu_device_get_version(device),
				    error_str->str);
		} else {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "current version is %s",
				    fu_device_get_version(device));
		}
		return NULL;
	}
	g_ptr_array_sort_with_data(releases, fu_engine_sort_releases_cb, device);
	return g_steal_pointer(&releases);
}

GPtrArray *
fu_engine_get_approved_firmware(FuEngine *self)
{
	GPtrArray *checksums = g_ptr_array_new_with_free_func(g_free);
	if (self->approved_firmware != NULL) {
		g_autoptr(GList) keys = g_hash_table_get_keys(self->approved_firmware);
		for (GList *l = keys; l != NULL; l = l->next) {
			const gchar *csum = l->data;
			g_ptr_array_add(checksums, g_strdup(csum));
		}
	}
	return checksums;
}

void
fu_engine_add_approved_firmware(FuEngine *self, const gchar *checksum)
{
	if (self->approved_firmware == NULL) {
		self->approved_firmware =
		    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	}
	g_hash_table_add(self->approved_firmware, g_strdup(checksum));
}

GPtrArray *
fu_engine_get_blocked_firmware(FuEngine *self)
{
	GPtrArray *checksums = g_ptr_array_new_with_free_func(g_free);
	if (self->blocked_firmware != NULL) {
		g_autoptr(GList) keys = g_hash_table_get_keys(self->blocked_firmware);
		for (GList *l = keys; l != NULL; l = l->next) {
			const gchar *csum = l->data;
			g_ptr_array_add(checksums, g_strdup(csum));
		}
	}
	return checksums;
}

void
fu_engine_add_blocked_firmware(FuEngine *self, const gchar *checksum)
{
	if (self->blocked_firmware == NULL) {
		self->blocked_firmware =
		    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	}
	g_hash_table_add(self->blocked_firmware, g_strdup(checksum));
}

gboolean
fu_engine_set_blocked_firmware(FuEngine *self, GPtrArray *checksums, GError **error)
{
	/* update in-memory hash */
	if (self->blocked_firmware != NULL) {
		g_hash_table_unref(self->blocked_firmware);
		self->blocked_firmware = NULL;
	}
	for (guint i = 0; i < checksums->len; i++) {
		const gchar *csum = g_ptr_array_index(checksums, i);
		fu_engine_add_blocked_firmware(self, csum);
	}

	/* save database */
	if (!fu_history_clear_blocked_firmware(self->history, error))
		return FALSE;
	for (guint i = 0; i < checksums->len; i++) {
		const gchar *csum = g_ptr_array_index(checksums, i);
		if (!fu_history_add_blocked_firmware(self->history, csum, error))
			return FALSE;
	}
	return TRUE;
}

gchar *
fu_engine_self_sign(FuEngine *self, const gchar *value, JcatSignFlags flags, GError **error)
{
	g_autoptr(JcatBlob) jcat_signature = NULL;
	g_autoptr(JcatEngine) jcat_engine = NULL;
	g_autoptr(JcatResult) jcat_result = NULL;
	g_autoptr(GBytes) payload = NULL;

	/* create detached signature and verify */
	jcat_engine = jcat_context_get_engine(self->jcat_context, JCAT_BLOB_KIND_PKCS7, error);
	if (jcat_engine == NULL)
		return NULL;
	payload = g_bytes_new(value, strlen(value));
	jcat_signature = jcat_engine_self_sign(jcat_engine, payload, flags, error);
	if (jcat_signature == NULL)
		return NULL;
	jcat_result = jcat_engine_self_verify(jcat_engine,
					      payload,
					      jcat_blob_get_data(jcat_signature),
					      JCAT_VERIFY_FLAG_NONE,
					      error);
	if (jcat_result == NULL)
		return NULL;
	return jcat_blob_get_data_as_string(jcat_signature);
}

/**
 * fu_engine_get_upgrades:
 * @self: a #FuEngine
 * @request: a #FuEngineRequest
 * @device_id: a device ID
 * @error: (nullable): optional return location for an error
 *
 * Gets the upgrades available for a specific device.
 *
 * Returns: (transfer container) (element-type FwupdDevice): results
 **/
GPtrArray *
fu_engine_get_upgrades(FuEngine *self,
		       FuEngineRequest *request,
		       const gchar *device_id,
		       GError **error)
{
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(GPtrArray) releases_tmp = NULL;
	g_autoptr(GString) error_str = g_string_new(NULL);

	g_return_val_if_fail(FU_IS_ENGINE(self), NULL);
	g_return_val_if_fail(device_id != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* find the device */
	device = fu_device_list_get_by_id(self->device_list, device_id, error);
	if (device == NULL)
		return NULL;

	/* don't show upgrades again until we reboot */
	if (fu_device_get_update_state(device) == FWUPD_UPDATE_STATE_NEEDS_REBOOT) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "A reboot is pending");
		return NULL;
	}

	/* get all the releases for the device */
	releases_tmp = fu_engine_get_releases_for_device(self, request, device, error);
	if (releases_tmp == NULL)
		return NULL;
	releases = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (guint i = 0; i < releases_tmp->len; i++) {
		FwupdRelease *rel_tmp = g_ptr_array_index(releases_tmp, i);

		/* same as installed */
		if (!fwupd_release_has_flag(rel_tmp, FWUPD_RELEASE_FLAG_IS_UPGRADE) &&
		    !fwupd_release_has_flag(rel_tmp, FWUPD_RELEASE_FLAG_IS_DOWNGRADE)) {
			g_string_append_printf(error_str,
					       "%s=same, ",
					       fwupd_release_get_version(rel_tmp));
			g_debug("ignoring %s == %s",
				fwupd_release_get_version(rel_tmp),
				fu_device_get_version(device));
			continue;
		}

		/* older than current */
		if (fwupd_release_has_flag(rel_tmp, FWUPD_RELEASE_FLAG_IS_DOWNGRADE)) {
			g_string_append_printf(error_str,
					       "%s=older, ",
					       fwupd_release_get_version(rel_tmp));
			g_debug("ignoring %s < %s",
				fwupd_release_get_version(rel_tmp),
				fu_device_get_version(device));
			continue;
		}

		/* not approved */
		if (fwupd_release_has_flag(rel_tmp, FWUPD_RELEASE_FLAG_BLOCKED_APPROVAL)) {
			g_string_append_printf(error_str,
					       "%s=not-approved, ",
					       fwupd_release_get_version(rel_tmp));
			g_debug("ignoring %s as not approved as required by %s",
				fwupd_release_get_version(rel_tmp),
				fwupd_release_get_remote_id(rel_tmp));
			continue;
		}

		/* different branch */
		if (fwupd_release_has_flag(rel_tmp, FWUPD_RELEASE_FLAG_IS_ALTERNATE_BRANCH)) {
			g_debug("ignoring release %s as branch %s, and device is %s",
				fwupd_release_get_version(rel_tmp),
				fwupd_release_get_branch(rel_tmp),
				fu_device_get_branch(device));
			continue;
		}

		g_ptr_array_add(releases, g_object_ref(rel_tmp));
	}
	if (error_str->len > 2)
		g_string_truncate(error_str, error_str->len - 2);
	if (releases->len == 0) {
		if (error_str->len > 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "current version is %s: %s",
				    fu_device_get_version(device),
				    error_str->str);
		} else {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "current version is %s",
				    fu_device_get_version(device));
		}
		return NULL;
	}
	g_ptr_array_sort_with_data(releases, fu_engine_sort_releases_cb, device);
	return g_steal_pointer(&releases);
}

/**
 * fu_engine_clear_results:
 * @self: a #FuEngine
 * @device_id: a device ID
 * @error: (nullable): optional return location for an error
 *
 * Clear the historical state of a specific device operation.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_clear_results(FuEngine *self, const gchar *device_id, GError **error)
{
	g_autoptr(FuDevice) device = NULL;
	FuPlugin *plugin;

	g_return_val_if_fail(FU_IS_ENGINE(self), FALSE);
	g_return_val_if_fail(device_id != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* find the device */
	device = fu_engine_get_item_by_id_fallback_history(self, device_id, error);
	if (device == NULL)
		return FALSE;

	/* already set on the database */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_NOTIFIED)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "device already has notified flag");
		return FALSE;
	}

	/* call into the plugin if it still exists */
	plugin =
	    fu_plugin_list_find_by_name(self->plugin_list, fu_device_get_plugin(device), error);
	if (plugin != NULL) {
		if (!fu_plugin_runner_clear_results(plugin, device, error))
			return FALSE;
	}

	/* if the offline update never got run, unstage it */
	if (fu_device_get_update_state(device) == FWUPD_UPDATE_STATE_PENDING)
		fu_device_set_update_state(device, FWUPD_UPDATE_STATE_UNKNOWN);

	/* override */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NOTIFIED);
	return fu_history_modify_device(self->history, device, error);
}

/**
 * fu_engine_get_results:
 * @self: a #FuEngine
 * @device_id: a device ID
 * @error: (nullable): optional return location for an error
 *
 * Gets the historical state of a specific device operation.
 *
 * Returns: (transfer container): a device, or %NULL
 **/
FwupdDevice *
fu_engine_get_results(FuEngine *self, const gchar *device_id, GError **error)
{
	g_autoptr(FuDevice) device = NULL;

	g_return_val_if_fail(FU_IS_ENGINE(self), NULL);
	g_return_val_if_fail(device_id != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* find the device */
	device = fu_engine_get_item_by_id_fallback_history(self, device_id, error);
	if (device == NULL)
		return NULL;

	/* the notification has already been shown to the user */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_NOTIFIED)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOTHING_TO_DO,
			    "User has already been notified about %s [%s]",
			    fu_device_get_name(device),
			    fu_device_get_id(device));
		return NULL;
	}

	/* success */
	return g_object_ref(FWUPD_DEVICE(device));
}

static void
fu_engine_plugins_setup(FuEngine *self, FuProgress *progress)
{
	GPtrArray *plugins = fu_plugin_list_get_all(self->plugin_list);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, plugins->len);
	for (guint i = 0; i < plugins->len; i++) {
		g_autoptr(GError) error = NULL;
		FuPlugin *plugin = g_ptr_array_index(plugins, i);
		if (!fu_plugin_runner_startup(plugin, fu_progress_get_child(progress), &error)) {
			fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_DISABLED);
			if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
				fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_NO_HARDWARE);
			}
			g_message("disabling plugin because: %s", error->message);
			fu_progress_add_flag(progress, FU_PROGRESS_FLAG_CHILD_FINISHED);
		}
		fu_progress_step_done(progress);
	}
}

static void
fu_engine_plugins_coldplug(FuEngine *self, FuProgress *progress)
{
	GPtrArray *plugins;
	g_autoptr(GString) str = g_string_new(NULL);

	/* exec */
	plugins = fu_plugin_list_get_all(self->plugin_list);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, plugins->len);
	for (guint i = 0; i < plugins->len; i++) {
		g_autoptr(GError) error = NULL;
		FuPlugin *plugin = g_ptr_array_index(plugins, i);
		if (!fu_plugin_runner_coldplug(plugin, fu_progress_get_child(progress), &error)) {
			fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_DISABLED);
			g_message("disabling plugin because: %s", error->message);
			fu_progress_add_flag(progress, FU_PROGRESS_FLAG_CHILD_FINISHED);
		}
		fu_progress_step_done(progress);
	}

	/* print what we do have */
	for (guint i = 0; i < plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index(plugins, i);
		if (fu_plugin_has_flag(plugin, FWUPD_PLUGIN_FLAG_DISABLED))
			continue;
		g_string_append_printf(str, "%s, ", fu_plugin_get_name(plugin));
	}
	if (str->len > 2) {
		g_string_truncate(str, str->len - 2);
		g_debug("using plugins: %s", str->str);
	}
}

static void
fu_engine_plugin_device_register(FuEngine *self, FuDevice *device)
{
	GPtrArray *plugins;
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_REGISTERED)) {
		g_warning("already registered %s, ignoring", fu_device_get_id(device));
		return;
	}
	plugins = fu_plugin_list_get_all(self->plugin_list);
	for (guint i = 0; i < plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index(plugins, i);
		fu_plugin_runner_device_register(plugin, device);
	}
	for (guint i = 0; i < self->backends->len; i++) {
		FuBackend *backend = g_ptr_array_index(self->backends, i);
		fu_backend_registered(backend, device);
	}
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_REGISTERED);
}

static void
fu_engine_plugin_device_register_cb(FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	FuEngine *self = FU_ENGINE(user_data);
	fu_engine_plugin_device_register(self, device);
}

static void
fu_engine_plugin_device_added_cb(FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	FuEngine *self = FU_ENGINE(user_data);

	/* plugin has prio and device not already set from quirk */
	if (fu_plugin_get_priority(plugin) > 0 && fu_device_get_priority(device) == 0) {
		g_debug("auto-setting %s priority to %u",
			fu_device_get_id(device),
			fu_plugin_get_priority(plugin));
		fu_device_set_priority(device, fu_plugin_get_priority(plugin));
	}

	fu_engine_add_device(self, device);
}

static void
fu_engine_adopt_children(FuEngine *self, FuDevice *device)
{
	GPtrArray *guids;
	g_autoptr(GPtrArray) devices = fu_device_list_get_active(self->device_list);

	/* find the parent in any existing device */
	if (fu_device_get_parent(device) == NULL) {
		for (guint i = 0; i < devices->len; i++) {
			FuDevice *device_tmp = g_ptr_array_index(devices, i);
			if (!fu_device_has_internal_flag(
				device_tmp,
				FU_DEVICE_INTERNAL_FLAG_AUTO_PARENT_CHILDREN))
				continue;
			if (fu_device_get_physical_id(device_tmp) == NULL)
				continue;
			if (fu_device_has_parent_physical_id(
				device,
				fu_device_get_physical_id(device_tmp))) {
				fu_device_set_parent(device, device_tmp);
				break;
			}
		}
	}
	if (fu_device_get_parent(device) == NULL) {
		guids = fu_device_get_parent_guids(device);
		for (guint j = 0; j < guids->len; j++) {
			const gchar *guid = g_ptr_array_index(guids, j);
			for (guint i = 0; i < devices->len; i++) {
				FuDevice *device_tmp = g_ptr_array_index(devices, i);
				if (fu_device_has_guid(device_tmp, guid)) {
					fu_device_set_parent(device, device_tmp);
					break;
				}
			}
		}
	}

	/* the new device is the parent to an existing child */
	for (guint j = 0; j < devices->len; j++) {
		GPtrArray *parent_physical_ids = NULL;
		FuDevice *device_tmp = g_ptr_array_index(devices, j);
		if (fu_device_get_parent(device_tmp) != NULL)
			continue;
		parent_physical_ids = fu_device_get_parent_physical_ids(device_tmp);
		if (parent_physical_ids == NULL)
			continue;
		for (guint i = 0; i < parent_physical_ids->len; i++) {
			const gchar *parent_physical_id = g_ptr_array_index(parent_physical_ids, i);
			if (g_strcmp0(parent_physical_id, fu_device_get_physical_id(device)) == 0)
				fu_device_set_parent(device_tmp, device);
		}
	}
	guids = fu_device_get_guids(device);
	for (guint j = 0; j < guids->len; j++) {
		const gchar *guid = g_ptr_array_index(guids, j);
		for (guint i = 0; i < devices->len; i++) {
			FuDevice *device_tmp = g_ptr_array_index(devices, i);
			if (fu_device_get_parent(device_tmp) != NULL)
				continue;
			if (fu_device_has_parent_guid(device_tmp, guid))
				fu_device_set_parent(device_tmp, device);
		}
	}
}

static void
fu_engine_set_proxy_device(FuEngine *self, FuDevice *device)
{
	GPtrArray *guids;
	g_autoptr(FuDevice) proxy = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	if (fu_device_get_proxy(device) != NULL)
		return;
	if (fu_device_get_proxy_guid(device) == NULL)
		return;

	/* find the proxy GUID in any existing device */
	proxy =
	    fu_device_list_get_by_guid(self->device_list, fu_device_get_proxy_guid(device), NULL);
	if (proxy != NULL) {
		g_debug("setting proxy of %s to %s for %s",
			fu_device_get_id(proxy),
			fu_device_get_id(device),
			fu_device_get_proxy_guid(device));
		fu_device_set_proxy(device, proxy);
		return;
	}

	/* are we the parent of an existing device */
	guids = fu_device_get_guids(device);
	for (guint j = 0; j < guids->len; j++) {
		const gchar *guid = g_ptr_array_index(guids, j);
		devices = fu_device_list_get_active(self->device_list);
		for (guint i = 0; i < devices->len; i++) {
			FuDevice *device_tmp = g_ptr_array_index(devices, i);
			if (g_strcmp0(fu_device_get_proxy_guid(device_tmp), guid) == 0) {
				g_debug("adding proxy of %s to %s for %s",
					fu_device_get_id(device),
					fu_device_get_id(device_tmp),
					guid);
				fu_device_set_proxy(device_tmp, device);
				return;
			}
		}
	}

	/* nothing found */
	g_warning("did not find proxy device %s", fu_device_get_proxy_guid(device));
}

static void
fu_engine_device_inherit_history(FuEngine *self, FuDevice *device)
{
	g_autoptr(FuDevice) device_history = NULL;

	/* any success or failed update? */
	device_history = fu_history_get_device_by_id(self->history, fu_device_get_id(device), NULL);
	if (device_history == NULL)
		return;

	/* the device is still running the old firmware version and so if it
	 * required activation before, it still requires it now -- note:
	 * we can't just check for version_new=version to allow for re-installs */
	if (fu_device_has_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_INHERIT_ACTIVATION) &&
	    fu_device_has_flag(device_history, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION)) {
		FwupdRelease *release = fu_device_get_release_default(device_history);
		if (fu_version_compare(fu_device_get_version(device),
				       fwupd_release_get_version(release),
				       fu_device_get_version_format(device)) != 0) {
			g_debug("inheriting needs-activation for %s as version %s != %s",
				fu_device_get_name(device),
				fu_device_get_version(device),
				fwupd_release_get_version(release));
			fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
		}
	}
}

void
fu_engine_add_device(FuEngine *self, FuDevice *device)
{
	GPtrArray *disabled_devices;
	GPtrArray *device_guids;
	g_autoptr(XbNode) component = NULL;

	/* device has no GUIDs set! */
	device_guids = fu_device_get_guids(device);
	if (device_guids->len == 0) {
		g_warning("no GUIDs for device %s [%s]",
			  fu_device_get_name(device),
			  fu_device_get_id(device));
		return;
	}

	/* is this GUID disabled */
	disabled_devices = fu_config_get_disabled_devices(self->config);
	for (guint i = 0; i < disabled_devices->len; i++) {
		const gchar *disabled_guid = g_ptr_array_index(disabled_devices, i);
		for (guint j = 0; j < device_guids->len; j++) {
			const gchar *device_guid = g_ptr_array_index(device_guids, j);
			if (g_strcmp0(disabled_guid, device_guid) == 0) {
				g_debug("%s [%s] is disabled [%s], ignoring from %s",
					fu_device_get_name(device),
					fu_device_get_id(device),
					device_guid,
					fu_device_get_plugin(device));
				return;
			}
		}
	}

	/* does the device not have an assigned protocol */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE) &&
	    fu_device_get_protocols(device)->len == 0) {
		g_warning("device %s [%s] does not define an update protocol",
			  fu_device_get_id(device),
			  fu_device_get_name(device));
	}

#ifndef SUPPORTED_BUILD
	/* we don't know if this device has a signed or unsigned payload */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE) &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD) &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD) &&
	    !fu_device_has_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_MD_SET_SIGNED)) {
		g_warning("%s device does not define payload %s [%s]",
			  fu_device_get_plugin(device),
			  fu_device_get_name(device),
			  fu_device_get_id(device));
	}
#endif

	/* if this device is locked get some metadata from AppStream */
	component = fu_engine_get_component_by_guids(self, device);
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_LOCKED)) {
		if (component != NULL) {
			g_autoptr(XbNode) rel = NULL;
			rel = xb_node_query_first(component, "releases/release", NULL);
			if (rel != NULL) {
				g_autoptr(FuRelease) release = fu_release_new();
				g_autoptr(GError) error_local = NULL;
				fu_release_set_device(release, device);
				if (!fu_engine_load_release(self,
							    release,
							    component,
							    rel,
							    FWUPD_INSTALL_FLAG_NONE,
							    &error_local)) {
					g_warning("failed to set AppStream release: %s",
						  error_local->message);
				} else {
					fu_device_add_release(device, FWUPD_RELEASE(release));
				}
			}
		}
	}

	/* set or clear the SUPPORTED flag */
	fu_engine_ensure_device_supported(self, device);

	/* fixup the name and format as needed */
	fu_engine_md_refresh_device_from_component(self, device, component);

	/* adopt any required children, which may or may not already exist */
	fu_engine_adopt_children(self, device);

	/* set the proxy device if specified by GUID */
	fu_engine_set_proxy_device(self, device);

	/* set any alternate objects on the device from the ID */
	if (fu_device_get_alternate_id(device) != NULL) {
		g_autoptr(FuDevice) device_alt = NULL;
		device_alt = fu_device_list_get_by_id(self->device_list,
						      fu_device_get_alternate_id(device),
						      NULL);
		if (device_alt != NULL)
			fu_device_set_alternate(device, device_alt);
	}

	/* sometimes inherit flags from recent history */
	fu_engine_device_inherit_history(self, device);

	/* notify all plugins about this new device */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_REGISTERED))
		fu_engine_plugin_device_register(self, device);

	if (fu_device_get_version_format(device) == FWUPD_VERSION_FORMAT_UNKNOWN &&
	    fu_version_guess_format(fu_device_get_version(device)) == FWUPD_VERSION_FORMAT_NUMBER) {
		fu_device_inhibit(device, "version-format", "VersionFormat is ambiguous");
	}

	/* no vendor-id, and so no way to lock it down! */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE) &&
	    fu_device_get_vendor_ids(device)->len == 0) {
		fu_device_inhibit(device, "vendor-id", "No vendor ID set");
	}

	/* create new device */
	fu_device_list_add(self->device_list, device);

	/* fix order */
	fu_device_list_depsolve_order(self->device_list, device);

	/* fixup the name and format as needed from cached metadata */
	if (component != NULL)
		fu_engine_md_refresh_device_from_component(self, device, component);

	/* match the metadata so clients can tell if the device is worthy */
	fu_engine_ensure_device_supported(self, device);

	fu_engine_emit_changed(self);
}

static void
fu_engine_plugin_rules_changed_cb(FuPlugin *plugin, gpointer user_data)
{
	FuEngine *self = FU_ENGINE(user_data);
	GPtrArray *rules = fu_plugin_get_rules(plugin, FU_PLUGIN_RULE_INHIBITS_IDLE);
	if (rules == NULL)
		return;
	for (guint j = 0; j < rules->len; j++) {
		const gchar *tmp = g_ptr_array_index(rules, j);
		fu_idle_inhibit(self->idle, tmp);
	}
}

static void
fu_engine_plugin_config_changed_cb(FuPlugin *plugin, gpointer user_data)
{
	FuEngine *self = FU_ENGINE(user_data);
	g_info("config file for %s changed, sending SHUTDOWN", fu_plugin_get_name(plugin));
	fu_engine_set_status(self, FWUPD_STATUS_SHUTDOWN);
}

static void
fu_engine_context_security_changed_cb(FuContext *ctx, gpointer user_data)
{
	FuEngine *self = FU_ENGINE(user_data);

	/* invalidate host security attributes */
	g_clear_pointer(&self->host_security_id, g_free);

	/* make UI refresh */
	fu_engine_emit_changed(self);
}

static void
fu_engine_plugin_device_removed_cb(FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	FuEngine *self = (FuEngine *)user_data;
	FuPlugin *plugin_old;
	g_autoptr(FuDevice) device_tmp = NULL;
	g_autoptr(GError) error = NULL;

	device_tmp = fu_device_list_get_by_id(self->device_list, fu_device_get_id(device), &error);
	if (device_tmp == NULL) {
		g_debug("failed to find device %s: %s", fu_device_get_id(device), error->message);
		return;
	}

	/* get the plugin */
	plugin_old =
	    fu_plugin_list_find_by_name(self->plugin_list, fu_device_get_plugin(device), &error);
	if (plugin_old == NULL) {
		g_debug("failed to find plugin %s: %s",
			fu_device_get_plugin(device),
			error->message);
		return;
	}

	/* check this came from the same plugin */
	if (g_strcmp0(fu_plugin_get_name(plugin), fu_plugin_get_name(plugin_old)) != 0) {
		g_debug("ignoring duplicate removal from %s", fu_plugin_get_name(plugin));
		return;
	}

	/* make the UI update */
	fu_device_list_remove(self->device_list, device);
	fu_engine_emit_changed(self);
}

/* this is called by the self tests as well */
void
fu_engine_add_plugin(FuEngine *self, FuPlugin *plugin)
{
	if (fu_plugin_is_open(plugin)) {
		/* plugin does not match built version */
		if (fu_plugin_get_build_hash(plugin) == NULL) {
			const gchar *name = fu_plugin_get_name(plugin);
			g_warning("%s should call fu_plugin_set_build_hash()", name);
			self->tainted = TRUE;
		} else if (g_strcmp0(fu_plugin_get_build_hash(plugin), FU_BUILD_HASH) != 0) {
			const gchar *name = fu_plugin_get_name(plugin);
			g_warning("%s has incorrect built version %s",
				  name,
				  fu_plugin_get_build_hash(plugin));
			self->tainted = TRUE;
		}
	}

	fu_plugin_list_add(self->plugin_list, plugin);
}

gboolean
fu_engine_is_uid_trusted(FuEngine *self, guint64 calling_uid)
{
	GArray *trusted;

	/* root is always trusted */
	if (calling_uid == 0)
		return TRUE;

	trusted = fu_config_get_trusted_uids(self->config);
	for (guint i = 0; i < trusted->len; i++) {
		if (calling_uid == g_array_index(trusted, guint64, i))
			return TRUE;
	}
	return FALSE;
}

static gboolean
fu_engine_is_plugin_name_disabled(FuEngine *self, const gchar *name)
{
	GPtrArray *disabled = fu_config_get_disabled_plugins(self->config);
	for (guint i = 0; i < disabled->len; i++) {
		const gchar *name_tmp = g_ptr_array_index(disabled, i);
		if (g_strcmp0(name_tmp, name) == 0)
			return TRUE;
	}
	return FALSE;
}

static gboolean
fu_engine_is_plugin_name_enabled(FuEngine *self, const gchar *name)
{
	if (self->plugin_filter->len == 0)
		return TRUE;
	for (guint i = 0; i < self->plugin_filter->len; i++) {
		const gchar *name_tmp = g_ptr_array_index(self->plugin_filter, i);
		if (fu_path_fnmatch(name_tmp, name))
			return TRUE;
	}
	return FALSE;
}

void
fu_engine_add_plugin_filter(FuEngine *self, const gchar *plugin_glob)
{
	GString *str;
	g_return_if_fail(FU_IS_ENGINE(self));
	g_return_if_fail(plugin_glob != NULL);
	str = g_string_new(plugin_glob);
	fu_string_replace(str, "-", "_");
	g_ptr_array_add(self->plugin_filter, g_string_free(str, FALSE));
}

static gboolean
fu_engine_plugin_check_supported_cb(FuPlugin *plugin, const gchar *guid, FuEngine *self)
{
	g_autoptr(XbNode) n = NULL;
	g_autofree gchar *xpath = NULL;

	if (fu_config_get_enumerate_all_devices(self->config))
		return TRUE;

	xpath = g_strdup_printf("components/component[@type='firmware']/"
				"provides/firmware[@type='flashed'][text()='%s']",
				guid);
	n = xb_silo_query_first(self->silo, xpath, NULL);
	return n != NULL;
}

gboolean
fu_engine_get_tainted(FuEngine *self)
{
	return self->tainted;
}

FuConfig *
fu_engine_get_config(FuEngine *self)
{
	g_return_val_if_fail(FU_IS_ENGINE(self), NULL);
	return self->config;
}

const gchar *
fu_engine_get_host_vendor(FuEngine *self)
{
	const gchar *result = NULL;
	g_return_val_if_fail(FU_IS_ENGINE(self), NULL);
	result = fu_context_get_hwid_value(self->ctx, FU_HWIDS_KEY_MANUFACTURER);
	return result != NULL ? result : "Unknown Vendor";
}

const gchar *
fu_engine_get_host_product(FuEngine *self)
{
	const gchar *result = NULL;
	g_return_val_if_fail(FU_IS_ENGINE(self), NULL);
	result = fu_context_get_hwid_value(self->ctx, FU_HWIDS_KEY_PRODUCT_NAME);
	return result != NULL ? result : "Unknown Product";
}

const gchar *
fu_engine_get_host_machine_id(FuEngine *self)
{
	g_return_val_if_fail(FU_IS_ENGINE(self), NULL);
	return self->host_machine_id;
}

const gchar *
fu_engine_get_host_bkc(FuEngine *self)
{
	g_return_val_if_fail(FU_IS_ENGINE(self), NULL);
	if (fu_config_get_host_bkc(self->config) == NULL)
		return "";
	return fu_config_get_host_bkc(self->config);
}

#ifdef HAVE_HSI
static void
fu_engine_ensure_security_attrs_tainted(FuEngine *self)
{
	gboolean disabled_plugins = FALSE;
	GPtrArray *disabled = fu_config_get_disabled_plugins(self->config);
	g_autoptr(FwupdSecurityAttr) attr =
	    fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_FWUPD_PLUGINS);
	fwupd_security_attr_set_plugin(attr, "core");
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);

	fu_security_attrs_append(self->host_security_attrs, attr);
	for (guint i = 0; i < disabled->len; i++) {
		const gchar *name_tmp = g_ptr_array_index(disabled, i);
		if (!g_str_has_prefix(name_tmp, "test") && g_strcmp0(name_tmp, "invalid") != 0) {
			disabled_plugins = TRUE;
			break;
		}
	}
	if (self->tainted) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_TAINTED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_OS);
		return;
	}
	if (self->plugin_filter->len > 0 || disabled_plugins) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_OS);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_TAINTED);
}

/*
 * Get chassis type from SMBIOS data and verify HSI makes sense for it
 */
static gchar *
fu_engine_attrs_calculate_hsi_for_chassis(FuEngine *self)
{
	guint val =
	    fu_context_get_smbios_integer(self->ctx, FU_SMBIOS_STRUCTURE_TYPE_CHASSIS, 0x05);

	switch (val) {
	case FU_SMBIOS_CHASSIS_KIND_DESKTOP:
	case FU_SMBIOS_CHASSIS_KIND_LOW_PROFILE_DESKTOP:
	case FU_SMBIOS_CHASSIS_KIND_MINI_TOWER:
	case FU_SMBIOS_CHASSIS_KIND_TOWER:
	case FU_SMBIOS_CHASSIS_KIND_PORTABLE:
	case FU_SMBIOS_CHASSIS_KIND_LAPTOP:
	case FU_SMBIOS_CHASSIS_KIND_NOTEBOOK:
	case FU_SMBIOS_CHASSIS_KIND_ALL_IN_ONE:
	case FU_SMBIOS_CHASSIS_KIND_SUB_NOTEBOOK:
	case FU_SMBIOS_CHASSIS_KIND_LUNCH_BOX:
	case FU_SMBIOS_CHASSIS_KIND_MAIN_SERVER:
	case FU_SMBIOS_CHASSIS_KIND_TABLET:
	case FU_SMBIOS_CHASSIS_KIND_CONVERTIBLE:
	case FU_SMBIOS_CHASSIS_KIND_DETACHABLE:
	case FU_SMBIOS_CHASSIS_KIND_IOT_GATEWAY:
	case FU_SMBIOS_CHASSIS_KIND_EMBEDDED_PC:
	case FU_SMBIOS_CHASSIS_KIND_MINI_PC:
	case FU_SMBIOS_CHASSIS_KIND_STICK_PC:
		return fu_security_attrs_calculate_hsi(self->host_security_attrs,
						       FU_SECURITY_ATTRS_FLAG_ADD_VERSION);
	default:
		break;
	}

	return g_strdup_printf("HSI-INVALID:chassis[0x%02x]", val);
}

static gboolean
fu_engine_record_security_attrs(FuEngine *self, GError **error)
{
#if JSON_CHECK_VERSION(1, 6, 0)
	g_autoptr(GPtrArray) attrs_array = NULL;
	g_autofree gchar *json = NULL;

	/* convert attrs to json string */
	json = fu_security_attrs_to_json_string(self->host_security_attrs, error);
	if (json == NULL) {
		g_prefix_error(error, "cannot convert current attrs to string: ");
		return FALSE;
	}

	/* check that we did not store this already last boot */
	attrs_array = fu_history_get_security_attrs(self->history, 1, error);
	if (attrs_array == NULL) {
		g_prefix_error(error, "failed to get historical attr: ");
		return FALSE;
	}
	if (attrs_array->len > 0) {
		FuSecurityAttrs *attrs_tmp = g_ptr_array_index(attrs_array, 0);
		if (fu_security_attrs_equal(attrs_tmp, self->host_security_attrs)) {
			g_debug("skipping writing HSI attrs to database as unchanged");
			return TRUE;
		}
	}

	/* write new values */
	if (!fu_history_add_security_attribute(self->history,
					       json,
					       self->host_security_id,
					       error)) {
		g_prefix_error(error, "failed to write to DB: ");
		return FALSE;
	}
#endif

	/* success */
	return TRUE;
}

static void
fu_engine_security_attrs_depsolve(FuEngine *self)
{
	g_autoptr(GPtrArray) items = NULL;

	/* set the fallback names for clients without native translations */
	items = fu_security_attrs_get_all(self->host_security_attrs);
	for (guint i = 0; i < items->len; i++) {
		FwupdSecurityAttr *attr = g_ptr_array_index(items, i);
		if (fwupd_security_attr_get_name(attr) == NULL) {
			g_autofree gchar *name_tmp = fu_security_attr_get_name(attr);
			if (name_tmp == NULL) {
				g_warning("failed to get fallback for %s",
					  fwupd_security_attr_get_appstream_id(attr));
				continue;
			}
			fwupd_security_attr_set_name(attr, name_tmp);
		}
	}

	/* set the obsoletes flag for each attr */
	fu_security_attrs_depsolve(self->host_security_attrs);

	/* distil into one simple string */
	g_free(self->host_security_id);
	self->host_security_id = fu_engine_attrs_calculate_hsi_for_chassis(self);
}
#endif

static gboolean
fu_engine_security_attrs_from_json(FuEngine *self, JsonNode *json_node, GError **error)
{
	JsonObject *obj;

	/* sanity check */
	if (!JSON_NODE_HOLDS_OBJECT(json_node)) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "not JSON object");
		return FALSE;
	}

	/* not supplied */
	obj = json_node_get_object(json_node);
	if (!json_object_has_member(obj, "SecurityAttributes"))
		return TRUE;
	if (!fu_security_attrs_from_json(self->host_security_attrs, json_node, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_engine_devices_from_json(FuEngine *self, JsonNode *json_node, GError **error)
{
	JsonArray *array;
	JsonObject *obj;

	/* sanity check */
	if (!JSON_NODE_HOLDS_OBJECT(json_node)) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "not JSON object");
		return FALSE;
	}

	/* not supplied */
	obj = json_node_get_object(json_node);
	if (!json_object_has_member(obj, "Devices"))
		return TRUE;

	/* this has to exist */
	array = json_object_get_array_member(obj, "Devices");
	for (guint i = 0; i < json_array_get_length(array); i++) {
		JsonNode *node_tmp = json_array_get_element(array, i);
		g_autoptr(FuDevice) device = fu_device_new(self->ctx);
		if (!fwupd_device_from_json(FWUPD_DEVICE(device), node_tmp, error))
			return FALSE;
		fu_device_set_plugin(device, "dummy");
		fu_device_add_problem(device, FWUPD_DEVICE_PROBLEM_IS_EMULATED);
		if (!fu_device_setup(device, error))
			return FALSE;
		fu_engine_add_device(self, device);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_engine_load_host_emulation(FuEngine *self, const gchar *fn, GError **error)
{
	g_autoptr(JsonParser) parser = json_parser_new();
	g_autoptr(GFile) file = g_file_new_for_path(fn);
	g_autoptr(GInputStream) istream_json = NULL;
	g_autoptr(GInputStream) istream_raw = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* add an attr so we know this is emulated and do not offer to upload results */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_HOST_EMULATION);
	fwupd_security_attr_set_plugin(attr, "core");
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fu_security_attrs_append(self->host_security_attrs, attr);

	/* add from file */
	istream_raw = G_INPUT_STREAM(g_file_read(file, NULL, error));
	if (istream_raw == NULL)
		return FALSE;
	if (g_str_has_suffix(fn, ".gz")) {
		g_autoptr(GConverter) conv =
		    G_CONVERTER(g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_GZIP));
		istream_json = g_converter_input_stream_new(istream_raw, conv);
	} else {
		istream_json = g_object_ref(istream_raw);
	}
	if (!json_parser_load_from_stream(parser, istream_json, NULL, error))
		return FALSE;
	if (!fu_engine_devices_from_json(self, json_parser_get_root(parser), error))
		return FALSE;
	if (!fu_engine_security_attrs_from_json(self, json_parser_get_root(parser), error))
		return FALSE;

#ifdef HAVE_HSI
	/* depsolve */
	fu_engine_security_attrs_depsolve(self);
#endif

	/* success */
	return TRUE;
}

static void
fu_engine_ensure_security_attrs(FuEngine *self)
{
#ifdef HAVE_HSI
	GPtrArray *plugins = fu_plugin_list_get_all(self->plugin_list);
	g_autoptr(GPtrArray) devices = fu_device_list_get_all(self->device_list);
	g_autoptr(GError) error = NULL;

	/* already valid */
	if (self->host_security_id != NULL || self->host_emulation)
		return;

	/* clear old values */
	fu_security_attrs_remove_all(self->host_security_attrs);

	/* built in */
	fu_engine_ensure_security_attrs_tainted(self);

	/* call into devices */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		fu_device_add_security_attrs(device, self->host_security_attrs);
	}

	/* call into plugins */
	for (guint j = 0; j < plugins->len; j++) {
		FuPlugin *plugin_tmp = g_ptr_array_index(plugins, j);
		fu_plugin_runner_add_security_attrs(plugin_tmp, self->host_security_attrs);
	}

	/* depsolve */
	fu_engine_security_attrs_depsolve(self);

	/* record into the database (best effort) */
	if (!fu_engine_record_security_attrs(self, &error))
		g_warning("failed to record HSI attributes: %s", error->message);
#endif
}

const gchar *
fu_engine_get_host_security_id(FuEngine *self)
{
	g_return_val_if_fail(FU_IS_ENGINE(self), NULL);
	fu_engine_ensure_security_attrs(self);
	return self->host_security_id;
}

FuSecurityAttrs *
fu_engine_get_host_security_attrs(FuEngine *self)
{
	g_return_val_if_fail(FU_IS_ENGINE(self), NULL);
	fu_engine_ensure_security_attrs(self);
	return g_object_ref(self->host_security_attrs);
}

FuSecurityAttrs *
fu_engine_get_host_security_events(FuEngine *self, guint limit, GError **error)
{
	g_autoptr(FuSecurityAttrs) events = fu_security_attrs_new();
#if JSON_CHECK_VERSION(1, 6, 0)
	g_autoptr(GPtrArray) attrs_array = NULL;

	g_return_val_if_fail(FU_IS_ENGINE(self), NULL);

	attrs_array = fu_history_get_security_attrs(self->history, limit, error);
	if (attrs_array == NULL)
		return NULL;
	for (guint i = 1; i < attrs_array->len; i++) {
		FuSecurityAttrs *attrs_new = g_ptr_array_index(attrs_array, i - 1);
		FuSecurityAttrs *attrs_old = g_ptr_array_index(attrs_array, i - 0);
		g_autoptr(GPtrArray) diffs = fu_security_attrs_compare(attrs_old, attrs_new);
		for (guint j = 0; j < diffs->len; j++) {
			FwupdSecurityAttr *attr = g_ptr_array_index(diffs, j);
			fu_security_attrs_append_internal(events, attr);
		}
	}
#endif

	/* success */
	return g_steal_pointer(&events);
}

static void
fu_engine_load_plugins_filename(FuEngine *self, const gchar *filename, FuProgress *progress)
{
	g_autofree gchar *name = NULL;
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(GError) error_local = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_name(progress, filename);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_NO_PROFILE);
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 97, "add");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 3, "open");

	/* sanity check */
	name = fu_plugin_guess_name_from_fn(filename);
	if (name == NULL) {
		fu_progress_finished(progress);
		return;
	}

	/* open module */
	plugin = fu_plugin_new(self->ctx);
	fu_plugin_set_name(plugin, name);
	fu_engine_add_plugin(self, plugin);
	fu_progress_step_done(progress);

	/* open the plugin and call ->load() */
	if (!fu_plugin_open(plugin, filename, &error_local))
		g_warning("cannot load: %s", error_local->message);
	fu_progress_step_done(progress);
}

static void
fu_engine_load_plugins_filenames(FuEngine *self, GPtrArray *filenames, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, filenames->len);
	for (guint i = 0; i < filenames->len; i++) {
		const gchar *filename = g_ptr_array_index(filenames, i);
		fu_engine_load_plugins_filename(self, filename, fu_progress_get_child(progress));
		fu_progress_step_done(progress);
	}
}

static gboolean
fu_engine_load_plugins(FuEngine *self, FuProgress *progress, GError **error)
{
	const gchar *fn;
	g_autoptr(GDir) dir = NULL;
	g_autoptr(GPtrArray) filenames = g_ptr_array_new_with_free_func(g_free);
	g_autofree gchar *plugin_path = NULL;
	g_autofree gchar *suffix = g_strdup_printf(".%s", G_MODULE_SUFFIX);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_NO_PROFILE);
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 13, "search");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 87, "load");

	/* search */
	plugin_path = fu_path_from_kind(FU_PATH_KIND_PLUGINDIR_PKG);
	dir = g_dir_open(plugin_path, 0, error);
	if (dir == NULL)
		return FALSE;
	while ((fn = g_dir_read_name(dir)) != NULL) {
		/* ignore non-plugins */
		if (!g_str_has_suffix(fn, suffix))
			continue;
		g_ptr_array_add(filenames, g_build_filename(plugin_path, fn, NULL));
	}
	fu_progress_step_done(progress);

	/* load */
	fu_engine_load_plugins_filenames(self, filenames, fu_progress_get_child(progress));
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_engine_plugins_init(FuEngine *self, FuProgress *progress, GError **error)
{
	GPtrArray *plugins = fu_plugin_list_get_all(self->plugin_list);
	g_autoptr(GPtrArray) plugins_disabled = g_ptr_array_new_with_free_func(g_free);
	g_autoptr(GPtrArray) plugins_disabled_rt = g_ptr_array_new_with_free_func(g_free);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, plugins->len);
	for (guint i = 0; i < plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index(plugins, i);
		const gchar *name = fu_plugin_get_name(plugin);

		/* progress */
		fu_progress_set_name(fu_progress_get_child(progress), name);

		/* is disabled */
		if (fu_engine_is_plugin_name_disabled(self, name) ||
		    !fu_engine_is_plugin_name_enabled(self, name)) {
			g_ptr_array_add(plugins_disabled, g_strdup(name));
			fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_DISABLED);
			fu_progress_step_done(progress);
			continue;
		}

		/* init plugin, adding device and firmware GTypes */
		fu_plugin_runner_init(plugin);

		/* runtime disabled */
		if (fu_plugin_has_flag(plugin, FWUPD_PLUGIN_FLAG_DISABLED)) {
			g_ptr_array_add(plugins_disabled_rt, g_strdup(name));
			fu_progress_step_done(progress);
			continue;
		}

		/* watch for changes */
		g_signal_connect(FU_PLUGIN(plugin),
				 "device-added",
				 G_CALLBACK(fu_engine_plugin_device_added_cb),
				 self);
		g_signal_connect(FU_PLUGIN(plugin),
				 "device-removed",
				 G_CALLBACK(fu_engine_plugin_device_removed_cb),
				 self);
		g_signal_connect(FU_PLUGIN(plugin),
				 "device-register",
				 G_CALLBACK(fu_engine_plugin_device_register_cb),
				 self);
		g_signal_connect(FU_PLUGIN(plugin),
				 "check-supported",
				 G_CALLBACK(fu_engine_plugin_check_supported_cb),
				 self);
		g_signal_connect(FU_PLUGIN(plugin),
				 "rules-changed",
				 G_CALLBACK(fu_engine_plugin_rules_changed_cb),
				 self);
		g_signal_connect(FU_PLUGIN(plugin),
				 "config-changed",
				 G_CALLBACK(fu_engine_plugin_config_changed_cb),
				 self);
		fu_progress_step_done(progress);
	}

	/* show list */
	if (plugins_disabled->len > 0) {
		g_autofree gchar *str = NULL;
		g_ptr_array_add(plugins_disabled, NULL);
		str = g_strjoinv(", ", (gchar **)plugins_disabled->pdata);
		g_debug("plugins disabled: %s", str);
	}
	if (plugins_disabled_rt->len > 0) {
		g_autofree gchar *str = NULL;
		g_ptr_array_add(plugins_disabled_rt, NULL);
		str = g_strjoinv(", ", (gchar **)plugins_disabled_rt->pdata);
		g_debug("plugins runtime-disabled: %s", str);
	}

	/* depsolve into the correct order */
	if (!fu_plugin_list_depsolve(self->plugin_list, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_engine_cleanup_state(GError **error)
{
	const gchar *filenames[] = {"/var/cache/app-info/xmls/fwupd-verify.xml",
				    "/var/cache/app-info/xmls/fwupd.xml",
				    NULL};
	for (guint i = 0; filenames[i] != NULL; i++) {
		g_autoptr(GFile) file = g_file_new_for_path(filenames[i]);
		if (g_file_query_exists(file, NULL)) {
			if (!g_file_delete(file, NULL, error))
				return FALSE;
		}
	}
	return TRUE;
}

static void
fu_engine_backend_device_removed_cb(FuBackend *backend, FuDevice *device, FuEngine *self)
{
	g_autoptr(GPtrArray) devices = NULL;

	/* debug */
	if (g_getenv("FWUPD_PROBE_VERBOSE") != NULL) {
		g_debug("%s removed %s",
			fu_backend_get_name(backend),
			fu_device_get_backend_id(device));
	}

	/* go through each device and remove any that match */
	devices = fu_device_list_get_all(self->device_list);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device_tmp = g_ptr_array_index(devices, i);
		if (g_strcmp0(fu_device_get_backend_id(device_tmp),
			      fu_device_get_backend_id(device)) == 0) {
			if (fu_device_has_internal_flag(device_tmp,
							FU_DEVICE_INTERNAL_FLAG_NO_AUTO_REMOVE)) {
				g_debug("not auto-removing backend device %s [%s] due to flags",
					fu_device_get_name(device_tmp),
					fu_device_get_id(device_tmp));
				continue;
			}
			g_debug("auto-removing backend device %s [%s]",
				fu_device_get_name(device_tmp),
				fu_device_get_id(device_tmp));
			fu_device_list_remove(self->device_list, device_tmp);
			fu_engine_emit_changed(self);
		}
	}
}

static gboolean
fu_engine_backend_device_added_run_plugin(FuEngine *self,
					  FuDevice *device,
					  const gchar *plugin_name,
					  FuProgress *progress,
					  GError **error)
{
	FuPlugin *plugin;

	/* find plugin */
	fu_progress_set_name(progress, plugin_name);
	plugin = fu_plugin_list_find_by_name(self->plugin_list, plugin_name, error);
	if (plugin == NULL)
		return FALSE;

	/* run the ->probe() then ->setup() vfuncs */
	if (!fu_plugin_runner_backend_device_added(plugin, device, error)) {
#ifdef SUPPORTED_BUILD
		/* sanity check */
		if (*error == NULL) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_ARGUMENT,
				    "%s failed but no error set",
				    fu_device_get_backend_id(device));
			return FALSE;
		}
#endif
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_engine_backend_device_added_run_plugins(FuEngine *self, FuDevice *device, FuProgress *progress)
{
	g_autoptr(GPtrArray) possible_plugins = fu_device_get_possible_plugins(device);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, possible_plugins->len);
	for (guint i = 0; i < possible_plugins->len; i++) {
		const gchar *plugin_name = g_ptr_array_index(possible_plugins, i);
		g_autoptr(GError) error_local = NULL;
		if (!fu_engine_backend_device_added_run_plugin(self,
							       device,
							       plugin_name,
							       fu_progress_get_child(progress),
							       &error_local)) {
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
				if (g_getenv("FWUPD_PROBE_VERBOSE") != NULL) {
					g_debug("%s ignoring: %s",
						plugin_name,
						error_local->message);
				}
			} else {
				g_warning("failed to add device %s: %s",
					  fu_device_get_backend_id(device),
					  error_local->message);
			}
			fu_progress_add_flag(progress, FU_PROGRESS_FLAG_CHILD_FINISHED);
			fu_progress_step_done(progress);
			continue;
		}
		fu_progress_step_done(progress);
	}
}

static void
fu_engine_backend_device_added(FuEngine *self, FuDevice *device, FuProgress *progress)
{
	g_autoptr(GError) error_local = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_NO_PROFILE);
	fu_progress_set_name(progress, fu_device_get_backend_id(device));
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 50, "probe-baseclass");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 50, "query-possible-plugins");

	/* super useful for plugin development */
	if (g_getenv("FWUPD_PROBE_VERBOSE") != NULL) {
		g_autofree gchar *str = fu_device_to_string(FU_DEVICE(device));
		g_debug("%s added %s", fu_device_get_backend_id(device), str);
	}

	/* add any extra quirks */
	fu_device_set_context(device, self->ctx);
	if (!fu_device_probe(device, &error_local)) {
		if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
			g_warning("failed to probe device %s: %s",
				  fu_device_get_backend_id(device),
				  error_local->message);
		} else if (g_getenv("FWUPD_PROBE_VERBOSE") != NULL) {
			g_debug("failed to probe device %s : %s",
				fu_device_get_backend_id(device),
				error_local->message);
		}
		fu_progress_finished(progress);
		return;
	}
	fu_progress_step_done(progress);

	/* super useful for plugin development */
	if (g_getenv("FWUPD_PROBE_VERBOSE") != NULL) {
		g_autofree gchar *str = fu_device_to_string(FU_DEVICE(device));
		g_debug("%s added %s", fu_device_get_backend_id(device), str);
	}

	/* can be specified using a quirk */
	fu_engine_backend_device_added_run_plugins(self, device, fu_progress_get_child(progress));
	fu_progress_step_done(progress);
}

static void
fu_engine_backend_device_added_cb(FuBackend *backend, FuDevice *device, FuEngine *self)
{
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	fu_engine_backend_device_added(self, device, progress);
}

static void
fu_engine_backend_device_changed_cb(FuBackend *backend, FuDevice *device, FuEngine *self)
{
	GPtrArray *plugins = fu_plugin_list_get_all(self->plugin_list);
	g_autoptr(GPtrArray) devices = NULL;

	/* debug */
	if (g_getenv("FWUPD_PROBE_VERBOSE") != NULL) {
		g_debug("%s changed %s",
			fu_backend_get_name(backend),
			fu_device_get_physical_id(device));
	}

	/* emit changed on any that match */
	devices = fu_device_list_get_all(self->device_list);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device_tmp = g_ptr_array_index(devices, i);
		if (!FU_IS_UDEV_DEVICE(device_tmp))
			continue;
		if (g_strcmp0(fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device_tmp)),
			      fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device))) == 0) {
			fu_udev_device_emit_changed(FU_UDEV_DEVICE(device));
		}
	}

	/* run all plugins */
	for (guint j = 0; j < plugins->len; j++) {
		FuPlugin *plugin_tmp = g_ptr_array_index(plugins, j);
		g_autoptr(GError) error = NULL;
		if (!fu_plugin_runner_backend_device_changed(plugin_tmp, device, &error)) {
#ifdef SUPPORTED_BUILD
			/* sanity check */
			if (error == NULL) {
				g_critical(
				    "failed to change device %s: exec failed but no error set!",
				    fu_device_get_backend_id(device));
				continue;
			}
#endif
			if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
				g_debug("%s ignoring: %s",
					fu_plugin_get_name(plugin_tmp),
					error->message);
				continue;
			}
			g_warning("%s failed to change device %s: %s",
				  fu_plugin_get_name(plugin_tmp),
				  fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device)),
				  error->message);
		}
	}
}

static void
fu_engine_load_quirks_for_hwid(FuEngine *self, const gchar *hwid)
{
	FuPlugin *plugin;
	const gchar *value;
	g_auto(GStrv) plugins = NULL;

	/* does prefixed quirk exist */
	value = fu_context_lookup_quirk_by_id(self->ctx, hwid, FU_QUIRKS_PLUGIN);
	if (value == NULL)
		return;
	plugins = g_strsplit(value, ",", -1);
	for (guint i = 0; plugins[i] != NULL; i++) {
		g_autoptr(GError) error_local = NULL;
		plugin = fu_plugin_list_find_by_name(self->plugin_list, plugins[i], &error_local);
		if (plugin == NULL) {
			g_debug("no %s plugin for HwId %s: %s",
				plugins[i],
				hwid,
				error_local->message);
			continue;
		}
		g_debug("enabling %s due to HwId %s", plugins[i], hwid);
		fu_plugin_remove_flag(plugin, FWUPD_PLUGIN_FLAG_REQUIRE_HWID);
	}
}

static gboolean
fu_engine_update_history_device(FuEngine *self, FuDevice *dev_history, GError **error)
{
	FuPlugin *plugin;
	FwupdRelease *rel_history;
	g_autofree gchar *btime = NULL;
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(GHashTable) metadata_device = NULL;

	/* is in the device list */
	dev = fu_device_list_get_by_id(self->device_list, fu_device_get_id(dev_history), error);
	if (dev == NULL)
		return FALSE;

	/* does the installed version match what we tried to install
	 * before fwupd was restarted */
	rel_history = fu_device_get_release_default(dev_history);
	if (rel_history == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "no release for history FuDevice");
		return FALSE;
	}

	/* is this the same boot time as when we scheduled the update,
	 * i.e. has fwupd been restarted before we rebooted */
	btime = fu_engine_get_boot_time();
	if (g_strcmp0(fwupd_release_get_metadata_item(rel_history, "BootTime"), btime) == 0) {
		g_debug("service restarted, but no reboot has taken place");

		/* if it needed reboot then, it also needs it now... */
		if (fu_device_get_update_state(dev_history) == FWUPD_UPDATE_STATE_NEEDS_REBOOT) {
			g_debug("inheriting needs-reboot for %s", fu_device_get_name(dev));
			fu_device_set_update_state(dev, FWUPD_UPDATE_STATE_NEEDS_REBOOT);
		}
		return TRUE;
	}

	/* save any additional report metadata */
	metadata_device = fu_device_report_metadata_post(dev);
	if (metadata_device != NULL && g_hash_table_size(metadata_device) > 0) {
		fwupd_release_add_metadata(rel_history, metadata_device);
		if (!fu_history_set_device_metadata(self->history,
						    fu_device_get_id(dev_history),
						    fwupd_release_get_metadata(rel_history),
						    error)) {
			g_prefix_error(error, "failed to set metadata: ");
			return FALSE;
		}
	}

	/* the system is running with the new firmware version */
	if (fu_version_compare(fu_device_get_version(dev),
			       fwupd_release_get_version(rel_history),
			       fu_device_get_version_format(dev)) == 0) {
		GPtrArray *checksums;
		g_debug("installed version %s matching history %s",
			fu_device_get_version(dev),
			fwupd_release_get_version(rel_history));

		/* copy over runtime checksums if set from probe() */
		checksums = fu_device_get_checksums(dev);
		for (guint i = 0; i < checksums->len; i++) {
			const gchar *csum = g_ptr_array_index(checksums, i);
			fu_device_add_checksum(dev_history, csum);
		}
		fu_device_set_version_format(dev_history, fu_device_get_version_format(dev));
		fu_device_set_version(dev_history, fu_device_get_version(dev));
		fu_device_remove_flag(dev_history, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
		fu_device_set_update_state(dev_history, FWUPD_UPDATE_STATE_SUCCESS);
		return fu_history_modify_device(self->history, dev_history, error);
	}

	/* does the plugin know the update failure */
	plugin = fu_plugin_list_find_by_name(self->plugin_list, fu_device_get_plugin(dev), error);
	if (plugin == NULL)
		return FALSE;
	if (!fu_plugin_runner_get_results(plugin, dev, error))
		return FALSE;

	/* the plugin either can't tell us the error, or doesn't know itself */
	if (fu_device_get_update_state(dev) != FWUPD_UPDATE_STATE_FAILED &&
	    fu_device_get_update_state(dev) != FWUPD_UPDATE_STATE_FAILED_TRANSIENT) {
		g_debug("falling back to generic failure");
		fu_device_set_update_state(dev_history, FWUPD_UPDATE_STATE_FAILED);
		fu_device_set_update_error(dev_history, "failed to run update on reboot");
	} else {
		fu_device_set_update_state(dev_history, fu_device_get_update_state(dev));
		fu_device_set_update_error(dev_history, fu_device_get_update_error(dev));
	}

	/* update the state in the database */
	return fu_history_modify_device(self->history, dev_history, error);
}

static gboolean
fu_engine_update_history_database(FuEngine *self, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;

	/* get any devices */
	devices = fu_history_get_devices(self->history, error);
	if (devices == NULL)
		return FALSE;
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index(devices, i);
		g_autoptr(GError) error_local = NULL;

		/* not in the required state */
		if (fu_device_get_update_state(dev) != FWUPD_UPDATE_STATE_NEEDS_REBOOT &&
		    fu_device_get_update_state(dev) != FWUPD_UPDATE_STATE_PENDING)
			continue;

		/* try to save the new update-state, but ignoring any error */
		if (!fu_engine_update_history_device(self, dev, &error_local)) {
			g_warning("failed to update history database: %s", error_local->message);
		}
	}
	return TRUE;
}

static void
fu_engine_ensure_client_certificate(FuEngine *self)
{
	g_autoptr(GBytes) blob = g_bytes_new_static(NULL, 0);
	g_autoptr(GError) error = NULL;
	g_autoptr(JcatBlob) jcat_sig = NULL;
	g_autoptr(JcatEngine) jcat_engine = NULL;

	/* create keyring and sign dummy data to ensure certificate exists */
	jcat_engine = jcat_context_get_engine(self->jcat_context, JCAT_BLOB_KIND_PKCS7, &error);
	if (jcat_engine == NULL) {
		g_message("failed to create keyring: %s", error->message);
		return;
	}
	jcat_sig = jcat_engine_self_sign(jcat_engine, blob, JCAT_SIGN_FLAG_NONE, &error);
	if (jcat_sig == NULL) {
		if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT)) {
			g_debug("client certificate now exists: %s", error->message);
			return;
		}
		g_message("failed to sign using keyring: %s", error->message);
		return;
	}
	g_debug("client certificate exists and working");
}

static void
fu_engine_context_set_battery_threshold(FuContext *ctx)
{
	guint64 minimum_battery;
	g_autofree gchar *battery_str = NULL;
	g_autofree gchar *vendor = NULL;

	vendor = fu_context_get_hwid_replace_value(ctx, FU_HWIDS_KEY_MANUFACTURER, NULL);
	if (vendor != NULL) {
		g_autofree gchar *vendor_guid = fwupd_guid_hash_string(vendor);
		battery_str = g_strdup(
		    fu_context_lookup_quirk_by_id(ctx, vendor_guid, FU_QUIRKS_BATTERY_THRESHOLD));
	}
	if (battery_str == NULL) {
		minimum_battery = MINIMUM_BATTERY_PERCENTAGE_FALLBACK;
	} else {
		g_autoptr(GError) error_local = NULL;
		if (!fu_strtoull(battery_str, &minimum_battery, 0, 100, &error_local)) {
			g_warning("invalid minimum battery level specified: %s",
				  error_local->message);
			minimum_battery = MINIMUM_BATTERY_PERCENTAGE_FALLBACK;
		}
	}
	fu_context_set_battery_threshold(ctx, minimum_battery);
}

static gboolean
fu_engine_ensure_paths_exist(GError **error)
{
	FuPathKind path_kinds[] = {FU_PATH_KIND_LOCALSTATEDIR_QUIRKS,
				   FU_PATH_KIND_LOCALSTATEDIR_METADATA,
				   FU_PATH_KIND_LOCALSTATEDIR_REMOTES,
				   FU_PATH_KIND_CACHEDIR_PKG,
				   FU_PATH_KIND_LAST};
	for (guint i = 0; path_kinds[i] != FU_PATH_KIND_LAST; i++) {
		g_autofree gchar *fn = fu_path_from_kind(path_kinds[i]);
		if (!fu_path_mkdir(fn, error))
			return FALSE;
	}
	return TRUE;
}

static void
fu_engine_local_metadata_changed_cb(GFileMonitor *monitor,
				    GFile *file,
				    GFile *other_file,
				    GFileMonitorEvent event_type,
				    gpointer user_data)
{
	FuEngine *self = FU_ENGINE(user_data);
	fu_engine_metadata_changed(self);
}

static gboolean
fu_engine_load_local_metadata_watches(FuEngine *self, GError **error)
{
	const FuPathKind path_kinds[] = {FU_PATH_KIND_DATADIR_PKG, FU_PATH_KIND_LOCALSTATEDIR_PKG};

	/* add the watches even if the directory does not exist */
	for (guint i = 0; i < G_N_ELEMENTS(path_kinds); i++) {
		GFileMonitor *monitor;
		g_autoptr(GFile) file = NULL;
		g_autoptr(GError) error_local = NULL;
		g_autofree gchar *base = fu_path_from_kind(path_kinds[i]);
		g_autofree gchar *fn = g_build_filename(base, "local.d", NULL);

		file = g_file_new_for_path(fn);
		monitor = g_file_monitor_directory(file, G_FILE_MONITOR_NONE, NULL, &error_local);
		if (monitor == NULL) {
			g_warning("failed to watch %s: %s", fn, error_local->message);
			continue;
		}
		g_signal_connect(monitor,
				 "changed",
				 G_CALLBACK(fu_engine_local_metadata_changed_cb),
				 self);
		g_ptr_array_add(self->local_monitors, g_steal_pointer(&monitor));
	}

	/* success */
	return TRUE;
}

#ifdef _WIN32
static gchar *
fu_common_win32_registry_get_string(HKEY hkey,
				    const gchar *subkey,
				    const gchar *value,
				    GError **error)
{
	gchar buf[255] = {'\0'};
	DWORD bufsz = sizeof(buf);
	LSTATUS rc;

	rc = RegGetValue(hkey, subkey, value, RRF_RT_REG_SZ, NULL, (PVOID)&buf, &bufsz);
	if (rc != ERROR_SUCCESS) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVAL,
			    "Failed to get registry string %s [0x%lX]",
			    subkey,
			    (unsigned long)rc);
		return NULL;
	}
	return g_strndup(buf, bufsz);
}
#endif

static gboolean
fu_engine_backends_coldplug_backend_add_devices(FuEngine *self,
						FuBackend *backend,
						FuProgress *progress,
						GError **error)
{
	g_autoptr(GPtrArray) devices = fu_backend_get_devices(backend);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, devices->len);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		fu_engine_backend_device_added(self, device, fu_progress_get_child(progress));
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_engine_backends_coldplug_backend(FuEngine *self,
				    FuBackend *backend,
				    FuProgress *progress,
				    GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_NO_PROFILE);
	fu_progress_set_name(progress, fu_backend_get_name(backend));
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "coldplug");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 99, "add-devices");

	/* coldplug */
	if (!fu_backend_coldplug(backend, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* add */
	fu_engine_backends_coldplug_backend_add_devices(self,
							backend,
							fu_progress_get_child(progress),
							error);
	fu_progress_step_done(progress);

	/* success */
	g_signal_connect(FU_BACKEND(backend),
			 "device-added",
			 G_CALLBACK(fu_engine_backend_device_added_cb),
			 self);
	g_signal_connect(FU_BACKEND(backend),
			 "device-removed",
			 G_CALLBACK(fu_engine_backend_device_removed_cb),
			 self);
	g_signal_connect(FU_BACKEND(backend),
			 "device-changed",
			 G_CALLBACK(fu_engine_backend_device_changed_cb),
			 self);
	return TRUE;
}

static void
fu_engine_backends_coldplug(FuEngine *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, self->backends->len);
	for (guint i = 0; i < self->backends->len; i++) {
		FuBackend *backend = g_ptr_array_index(self->backends, i);
		g_autoptr(GError) error_backend = NULL;

		if (!fu_backend_get_enabled(backend)) {
			fu_progress_step_done(progress);
			continue;
		}
		if (!fu_engine_backends_coldplug_backend(self,
							 backend,
							 fu_progress_get_child(progress),
							 &error_backend)) {
			g_warning("failed to coldplug backend %s: %s",
				  fu_backend_get_name(backend),
				  error_backend->message);
			fu_progress_step_done(progress);
			continue;
		}
		fu_progress_step_done(progress);
	}
}

/**
 * fu_engine_load:
 * @self: a #FuEngine
 * @flags: engine load flags, e.g. %FU_ENGINE_LOAD_FLAG_READONLY
 * @progress: a #FuProgress
 * @error: (nullable): optional return location for an error
 *
 * Load the firmware update engine so it is ready for use.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_load(FuEngine *self, FuEngineLoadFlags flags, FuProgress *progress, GError **error)
{
	FuQuirksLoadFlags quirks_flags = FU_QUIRKS_LOAD_FLAG_NONE;
	GPtrArray *guids;
	const gchar *host_emulate = g_getenv("FWUPD_HOST_EMULATE");
	guint backend_cnt = 0;
	g_autoptr(GPtrArray) checksums_approved = NULL;
	g_autoptr(GPtrArray) checksums_blocked = NULL;
	g_autoptr(GError) error_quirks = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_ENGINE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* avoid re-loading a second time if fu-tool or fu-util request to */
	if (self->loaded)
		return TRUE;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_NO_PROFILE);
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "read-config");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "read-remotes");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "ensure-client-cert");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "write-db");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "load-plugins");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "load-quirks");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "load-hwinfo");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "load-appstream");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "backend-setup");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "plugins-init");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "hwid-quirks");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "plugins-setup");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 3, "plugins-coldplug");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 90, "backend-coldplug");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "update-history-db");

	/* sanity check libraries are in sync with daemon */
	if (g_strcmp0(fwupd_version_string(), VERSION) != 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVAL,
			    "libfwupd version %s does not match daemon %s",
			    fwupd_version_string(),
			    VERSION);
		return FALSE;
	}
	if (g_strcmp0(fu_version_string(), VERSION) != 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVAL,
			    "libfwupdplugin version %s does not match daemon %s",
			    fu_version_string(),
			    VERSION);
		return FALSE;
	}

	/* cache machine ID so we can use it from a sandboxed app */
#ifdef _WIN32
	self->host_machine_id =
	    fu_common_win32_registry_get_string(HKEY_LOCAL_MACHINE,
						"SOFTWARE\\Microsoft\\Cryptography",
						"MachineGuid",
						&error_local);
#else
	self->host_machine_id = fwupd_build_machine_id("fwupd", &error_local);
#endif
	if (self->host_machine_id == NULL)
		g_debug("failed to build machine-id: %s", error_local->message);

	/* ensure these exist before starting */
	if (!fu_engine_ensure_paths_exist(error))
		return FALSE;

	/* read config file */
	if (!fu_config_load(self->config, error)) {
		g_prefix_error(error, "Failed to load config: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* read remotes */
	if (flags & FU_ENGINE_LOAD_FLAG_REMOTES) {
		FuRemoteListLoadFlags remote_list_flags = FU_REMOTE_LIST_LOAD_FLAG_NONE;
		if (flags & FU_ENGINE_LOAD_FLAG_READONLY)
			remote_list_flags |= FU_REMOTE_LIST_LOAD_FLAG_READONLY_FS;
		if (flags & FU_ENGINE_LOAD_FLAG_NO_CACHE)
			remote_list_flags |= FU_REMOTE_LIST_LOAD_FLAG_NO_CACHE;
		if (!fu_remote_list_load(self->remote_list, remote_list_flags, error)) {
			g_prefix_error(error, "Failed to load remotes: ");
			return FALSE;
		}
	}
	fu_progress_step_done(progress);

	/* create client certificate */
	fu_engine_ensure_client_certificate(self);
	fu_progress_step_done(progress);

	/* get hardcoded approved and blocked firmware */
	checksums_approved = fu_config_get_approved_firmware(self->config);
	for (guint i = 0; i < checksums_approved->len; i++) {
		const gchar *csum = g_ptr_array_index(checksums_approved, i);
		fu_engine_add_approved_firmware(self, csum);
	}
	checksums_blocked = fu_config_get_blocked_firmware(self->config);
	for (guint i = 0; i < checksums_blocked->len; i++) {
		const gchar *csum = g_ptr_array_index(checksums_blocked, i);
		fu_engine_add_blocked_firmware(self, csum);
	}

	/* get extra firmware saved to the database */
	checksums_approved = fu_history_get_approved_firmware(self->history, error);
	if (checksums_approved == NULL)
		return FALSE;
	for (guint i = 0; i < checksums_approved->len; i++) {
		const gchar *csum = g_ptr_array_index(checksums_approved, i);
		fu_engine_add_approved_firmware(self, csum);
	}
	checksums_blocked = fu_history_get_blocked_firmware(self->history, error);
	if (checksums_blocked == NULL)
		return FALSE;
	for (guint i = 0; i < checksums_blocked->len; i++) {
		const gchar *csum = g_ptr_array_index(checksums_blocked, i);
		fu_engine_add_blocked_firmware(self, csum);
	}
	fu_progress_step_done(progress);

	/* load plugins early, as we have to call ->load() *before* building quirk silo */
	if (!fu_engine_load_plugins(self, fu_progress_get_child(progress), error)) {
		g_prefix_error(error, "Failed to load plugins: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* set up idle exit */
	if ((flags & FU_ENGINE_LOAD_FLAG_NO_IDLE_SOURCES) == 0)
		fu_idle_set_timeout(self->idle, fu_config_get_idle_timeout(self->config));

	/* on a read-only filesystem don't care about the cache GUID */
	if (flags & FU_ENGINE_LOAD_FLAG_READONLY)
		quirks_flags |= FU_QUIRKS_LOAD_FLAG_READONLY_FS;
	if (flags & FU_ENGINE_LOAD_FLAG_NO_CACHE)
		quirks_flags |= FU_QUIRKS_LOAD_FLAG_NO_CACHE;
	if (!fu_context_load_quirks(self->ctx, quirks_flags, &error_quirks))
		g_warning("Failed to load quirks: %s", error_quirks->message);
	fu_progress_step_done(progress);

	/* load SMBIOS and the hwids */
	if (flags & FU_ENGINE_LOAD_FLAG_HWINFO)
		fu_context_load_hwinfo(self->ctx, NULL);
	fu_progress_step_done(progress);

	/* load AppStream metadata */
	if (!fu_engine_load_metadata_store(self, flags, error)) {
		g_prefix_error(error, "Failed to load AppStream data: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* watch the local.d directories for changes */
	if (!fu_engine_load_local_metadata_watches(self, error))
		return FALSE;

	/* add the "built-in" firmware types */
	fu_context_add_firmware_gtype(self->ctx, "raw", FU_TYPE_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "dfu", FU_TYPE_DFU_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "fdt", FU_TYPE_FDT_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "fit", FU_TYPE_FIT_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "dfuse", FU_TYPE_DFUSE_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "ifwi-cpd", FU_TYPE_IFWI_CPD_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "ifwi-fpt", FU_TYPE_IFWI_FPT_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "oprom", FU_TYPE_OPROM_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "fmap", FU_TYPE_FMAP_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "ihex", FU_TYPE_IHEX_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "srec", FU_TYPE_SREC_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "archive", FU_TYPE_ARCHIVE_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "smbios", FU_TYPE_SMBIOS);
	fu_context_add_firmware_gtype(self->ctx, "efi-firmware-file", FU_TYPE_EFI_FIRMWARE_FILE);
	fu_context_add_firmware_gtype(self->ctx,
				      "efi-firmware-filesystem",
				      FU_TYPE_EFI_FIRMWARE_FILESYSTEM);
	fu_context_add_firmware_gtype(self->ctx,
				      "efi-firmware-section",
				      FU_TYPE_EFI_FIRMWARE_SECTION);
	fu_context_add_firmware_gtype(self->ctx,
				      "efi-firmware-volume",
				      FU_TYPE_EFI_FIRMWARE_VOLUME);
	fu_context_add_firmware_gtype(self->ctx, "ifd-bios", FU_TYPE_IFD_BIOS);
	fu_context_add_firmware_gtype(self->ctx, "ifd-firmware", FU_TYPE_IFD_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "cfu-offer", FU_TYPE_CFU_OFFER);
	fu_context_add_firmware_gtype(self->ctx, "cfu-payload", FU_TYPE_CFU_PAYLOAD);
	fu_context_add_firmware_gtype(self->ctx, "uswid", FU_TYPE_USWID_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "coswid", FU_TYPE_COSWID_FIRMWARE);

	/* we are emulating a different host */
	if (host_emulate != NULL) {
		g_autofree gchar *fn = NULL;

		/* did the user specify an absolue path */
		if (g_file_test(host_emulate, G_FILE_TEST_EXISTS)) {
			fn = g_strdup(host_emulate);
		} else {
			g_autofree gchar *datadir = fu_path_from_kind(FU_PATH_KIND_DATADIR_PKG);
			fn = g_build_filename(datadir, "host-emulate.d", host_emulate, NULL);
		}
		if (!fu_engine_load_host_emulation(self, fn, error)) {
			g_prefix_error(error, "failed to load emulated host: ");
			return FALSE;
		}

		/* do not load actual hardware */
		flags &= ~FU_ENGINE_LOAD_FLAG_COLDPLUG;
		self->host_emulation = TRUE;
	}

	/* set up backends */
	if (flags & FU_ENGINE_LOAD_FLAG_COLDPLUG) {
		for (guint i = 0; i < self->backends->len; i++) {
			FuBackend *backend = g_ptr_array_index(self->backends, i);
			g_autoptr(GError) error_backend = NULL;
			if (!fu_backend_setup(backend,
					      fu_progress_get_child(progress),
					      &error_backend)) {
				g_debug("failed to setup backend %s: %s",
					fu_backend_get_name(backend),
					error_backend->message);
				continue;
			}
			backend_cnt++;
		}
		if (backend_cnt == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "all backends failed setup");
			return FALSE;
		}
	}
	fu_progress_step_done(progress);

	/* delete old data files */
	if (!fu_engine_cleanup_state(error)) {
		g_prefix_error(error, "Failed to clean up: ");
		return FALSE;
	}

	/* init plugins, adding device and firmware GTypes */
	if (!fu_engine_plugins_init(self, fu_progress_get_child(progress), error)) {
		g_prefix_error(error, "failed to init plugins: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* set quirks for each hwid */
	guids = fu_context_get_hwid_guids(self->ctx);
	for (guint i = 0; i < guids->len; i++) {
		const gchar *hwid = g_ptr_array_index(guids, i);
		fu_engine_load_quirks_for_hwid(self, hwid);
	}
	fu_progress_step_done(progress);

	/* set up battery threshold */
	fu_engine_context_set_battery_threshold(self->ctx);

	/* watch the device list for updates and proxy */
	g_signal_connect(FU_DEVICE_LIST(self->device_list),
			 "added",
			 G_CALLBACK(fu_engine_device_added_cb),
			 self);
	g_signal_connect(FU_DEVICE_LIST(self->device_list),
			 "removed",
			 G_CALLBACK(fu_engine_device_removed_cb),
			 self);
	g_signal_connect(FU_DEVICE_LIST(self->device_list),
			 "changed",
			 G_CALLBACK(fu_engine_device_changed_cb),
			 self);
	fu_engine_set_status(self, FWUPD_STATUS_LOADING);

	/* add devices */
	if (flags & FU_ENGINE_LOAD_FLAG_COLDPLUG) {
		fu_engine_plugins_setup(self, fu_progress_get_child(progress));
		fu_progress_step_done(progress);
		fu_engine_plugins_coldplug(self, fu_progress_get_child(progress));
		fu_progress_step_done(progress);
	} else {
		fu_progress_step_done(progress);
		fu_progress_step_done(progress);
	}

	/* coldplug backends */
	if (flags & FU_ENGINE_LOAD_FLAG_COLDPLUG)
		fu_engine_backends_coldplug(self, fu_progress_get_child(progress));
	fu_progress_step_done(progress);

	/* update the db for devices that were updated during the reboot */
	if (!fu_engine_update_history_database(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	fu_engine_set_status(self, FWUPD_STATUS_IDLE);
	self->loaded = TRUE;

	/* let clients know engine finished starting up */
	fu_engine_emit_changed(self);

	/* success */
	return TRUE;
}

static void
fu_engine_class_init(FuEngineClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_engine_finalize;

	/**
	 * FuEngine::changed:
	 * @self: the #FuEngine instance that emitted the signal
	 *
	 * The ::changed signal is emitted when the engine has changed, for instance when a device
	 * state has been modified.
	 **/
	signals[SIGNAL_CHANGED] = g_signal_new("changed",
					       G_TYPE_FROM_CLASS(object_class),
					       G_SIGNAL_RUN_LAST,
					       0,
					       NULL,
					       NULL,
					       g_cclosure_marshal_VOID__VOID,
					       G_TYPE_NONE,
					       0);
	/**
	 * FuEngine::device-added:
	 * @self: the #FuEngine instance that emitted the signal
	 * @device: the #FuDevice
	 *
	 * The ::device-added signal is emitted when a device has been added.
	 **/
	signals[SIGNAL_DEVICE_ADDED] = g_signal_new("device-added",
						    G_TYPE_FROM_CLASS(object_class),
						    G_SIGNAL_RUN_LAST,
						    0,
						    NULL,
						    NULL,
						    g_cclosure_marshal_VOID__OBJECT,
						    G_TYPE_NONE,
						    1,
						    FU_TYPE_DEVICE);
	/**
	 * FuEngine::device-removed:
	 * @self: the #FuEngine instance that emitted the signal
	 * @device: the #FuDevice
	 *
	 * The ::device-removed signal is emitted when a device has been removed.
	 **/
	signals[SIGNAL_DEVICE_REMOVED] = g_signal_new("device-removed",
						      G_TYPE_FROM_CLASS(object_class),
						      G_SIGNAL_RUN_LAST,
						      0,
						      NULL,
						      NULL,
						      g_cclosure_marshal_VOID__OBJECT,
						      G_TYPE_NONE,
						      1,
						      FU_TYPE_DEVICE);
	/**
	 * FuEngine::device-changed:
	 * @self: the #FuEngine instance that emitted the signal
	 * @device: the #FuDevice
	 *
	 * The ::device-changed signal is emitted when a device has been changed.
	 **/
	signals[SIGNAL_DEVICE_CHANGED] = g_signal_new("device-changed",
						      G_TYPE_FROM_CLASS(object_class),
						      G_SIGNAL_RUN_LAST,
						      0,
						      NULL,
						      NULL,
						      g_cclosure_marshal_VOID__OBJECT,
						      G_TYPE_NONE,
						      1,
						      FU_TYPE_DEVICE);
	/**
	 * FuEngine::device-request:
	 * @self: the #FuEngine instance that emitted the signal
	 * @request: the #FwupdRequest
	 *
	 * The ::device-request signal is emitted when the engine has asked the front end for an
	 * interactive request.
	 **/
	signals[SIGNAL_DEVICE_REQUEST] = g_signal_new("device-request",
						      G_TYPE_FROM_CLASS(object_class),
						      G_SIGNAL_RUN_LAST,
						      0,
						      NULL,
						      NULL,
						      g_cclosure_marshal_VOID__OBJECT,
						      G_TYPE_NONE,
						      1,
						      FWUPD_TYPE_REQUEST);
	/**
	 * FuEngine::status-changed:
	 * @self: the #FuEngine instance that emitted the signal
	 * @status: the #FwupdStatus
	 *
	 * The ::status-changed signal is emitted when the daemon global status has changed.
	 **/
	signals[SIGNAL_STATUS_CHANGED] = g_signal_new("status-changed",
						      G_TYPE_FROM_CLASS(object_class),
						      G_SIGNAL_RUN_LAST,
						      0,
						      NULL,
						      NULL,
						      g_cclosure_marshal_VOID__UINT,
						      G_TYPE_NONE,
						      1,
						      G_TYPE_UINT);
}

void
fu_engine_add_runtime_version(FuEngine *self, const gchar *component_id, const gchar *version)
{
	fu_context_add_runtime_version(self->ctx, component_id, version);
}

static void
fu_engine_context_battery_changed_cb(FuContext *ctx, GParamSpec *pspec, FuEngine *self)
{
	g_autoptr(GPtrArray) devices = fu_device_list_get_all(self->device_list);

	/* apply policy on any existing devices */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		fu_engine_ensure_device_battery_inhibit(self, device);
		fu_engine_ensure_device_lid_inhibit(self, device);
	}
}

static void
fu_engine_idle_status_notify_cb(FuIdle *idle, GParamSpec *pspec, FuEngine *self)
{
	FwupdStatus status = fu_idle_get_status(idle);
	if (status == FWUPD_STATUS_SHUTDOWN)
		fu_engine_set_status(self, status);
}

static void
fu_engine_init(FuEngine *self)
{
#ifdef HAVE_UTSNAME_H
	struct utsname uname_tmp;
#endif
	g_autofree gchar *keyring_path = NULL;
	g_autofree gchar *pkidir_fw = NULL;
	g_autofree gchar *pkidir_md = NULL;
	g_autofree gchar *sysconfdir = NULL;
	self->percentage = 0;
	self->config = fu_config_new();
	self->remote_list = fu_remote_list_new();
	self->device_list = fu_device_list_new();
	self->ctx = fu_context_new();
	self->idle = fu_idle_new();
	self->history = fu_history_new();
	self->plugin_list = fu_plugin_list_new();
	self->plugin_filter = g_ptr_array_new_with_free_func(g_free);
	self->host_security_attrs = fu_security_attrs_new();
	self->backends = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	self->local_monitors = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	self->runtime_versions = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	self->compile_versions = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	self->acquiesce_loop = g_main_loop_new(NULL, FALSE);

	fu_context_set_runtime_versions(self->ctx, self->runtime_versions);
	fu_context_set_compile_versions(self->ctx, self->compile_versions);

	g_signal_connect(FU_CONTEXT(self->ctx),
			 "security-changed",
			 G_CALLBACK(fu_engine_context_security_changed_cb),
			 self);
	g_signal_connect(FU_CONTEXT(self->ctx),
			 "notify::battery-state",
			 G_CALLBACK(fu_engine_context_battery_changed_cb),
			 self);
	g_signal_connect(FU_CONTEXT(self->ctx),
			 "notify::lid-state",
			 G_CALLBACK(fu_engine_context_battery_changed_cb),
			 self);
	g_signal_connect(FU_CONTEXT(self->ctx),
			 "notify::battery-level",
			 G_CALLBACK(fu_engine_context_battery_changed_cb),
			 self);
	g_signal_connect(FU_CONTEXT(self->ctx),
			 "notify::battery-threshold",
			 G_CALLBACK(fu_engine_context_battery_changed_cb),
			 self);

	g_signal_connect(FU_CONFIG(self->config),
			 "changed",
			 G_CALLBACK(fu_engine_config_changed_cb),
			 self);
	g_signal_connect(FU_REMOTE_LIST(self->remote_list),
			 "changed",
			 G_CALLBACK(fu_engine_remote_list_changed_cb),
			 self);

	g_signal_connect(FU_IDLE(self->idle),
			 "notify::status",
			 G_CALLBACK(fu_engine_idle_status_notify_cb),
			 self);

	/* backends */
#ifdef HAVE_GUSB
	g_ptr_array_add(self->backends, fu_usb_backend_new());
#endif
#ifdef HAVE_GUDEV
	g_ptr_array_add(self->backends,
			fu_udev_backend_new(fu_context_get_udev_subsystems(self->ctx)));
#endif
#ifdef HAVE_BLUEZ
	g_ptr_array_add(self->backends, fu_bluez_backend_new());
#endif

	/* setup Jcat context */
	self->jcat_context = jcat_context_new();
	keyring_path = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR_PKG);
	jcat_context_set_keyring_path(self->jcat_context, keyring_path);
	sysconfdir = fu_path_from_kind(FU_PATH_KIND_SYSCONFDIR);
	pkidir_fw = g_build_filename(sysconfdir, "pki", "fwupd", NULL);
	jcat_context_add_public_keys(self->jcat_context, pkidir_fw);
	pkidir_md = g_build_filename(sysconfdir, "pki", "fwupd-metadata", NULL);
	jcat_context_add_public_keys(self->jcat_context, pkidir_md);

	/* add some runtime versions of things the daemon depends on */
	fu_engine_add_runtime_version(self, "org.freedesktop.fwupd", VERSION);
#if G_USB_CHECK_VERSION(0, 3, 1)
	fu_engine_add_runtime_version(self, "org.freedesktop.gusb", g_usb_version_string());
#endif
#if LIBJCAT_CHECK_VERSION(0, 1, 11)
	fu_engine_add_runtime_version(self, "com.hughsie.libjcat", jcat_version_string());
#endif

	/* optional kernel version */
#ifdef HAVE_UTSNAME_H
	memset(&uname_tmp, 0, sizeof(uname_tmp));
	if (uname(&uname_tmp) >= 0)
		fu_engine_add_runtime_version(self, "org.kernel", uname_tmp.release);
#endif

	g_hash_table_insert(self->compile_versions,
			    g_strdup("org.freedesktop.fwupd"),
			    g_strdup(VERSION));
#ifdef HAVE_GUSB
	g_hash_table_insert(self->compile_versions,
			    g_strdup("org.freedesktop.gusb"),
			    g_strdup_printf("%i.%i.%i",
					    G_USB_MAJOR_VERSION,
					    G_USB_MINOR_VERSION,
					    G_USB_MICRO_VERSION));
#endif
	g_hash_table_insert(self->compile_versions,
			    g_strdup("com.hughsie.libjcat"),
			    g_strdup_printf("%i.%i.%i",
					    JCAT_MAJOR_VERSION,
					    JCAT_MINOR_VERSION,
					    JCAT_MICRO_VERSION));

	/* register /org/freedesktop/fwupd globally */
	g_resources_register(fu_get_resource());
}

static void
fu_engine_finalize(GObject *obj)
{
	FuEngine *self = FU_ENGINE(obj);
	GPtrArray *plugins = fu_plugin_list_get_all(self->plugin_list);

	for (guint i = 0; i < plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index(plugins, i);
		g_signal_handlers_disconnect_by_data(plugin, self);
	}
	for (guint i = 0; i < self->local_monitors->len; i++) {
		GFileMonitor *monitor = g_ptr_array_index(self->local_monitors, i);
		g_file_monitor_cancel(monitor);
	}

	if (self->silo != NULL)
		g_object_unref(self->silo);
	if (self->query_component_by_guid != NULL)
		g_object_unref(self->query_component_by_guid);
	if (self->coldplug_id != 0)
		g_source_remove(self->coldplug_id);
	if (self->approved_firmware != NULL)
		g_hash_table_unref(self->approved_firmware);
	if (self->blocked_firmware != NULL)
		g_hash_table_unref(self->blocked_firmware);
	if (self->acquiesce_id != 0)
		g_source_remove(self->acquiesce_id);
	g_main_loop_unref(self->acquiesce_loop);

	g_free(self->host_machine_id);
	g_free(self->host_security_id);
	g_object_unref(self->host_security_attrs);
	g_object_unref(self->idle);
	g_object_unref(self->config);
	g_object_unref(self->remote_list);
	g_object_unref(self->ctx);
	g_object_unref(self->history);
	g_object_unref(self->device_list);
	g_object_unref(self->jcat_context);
	g_ptr_array_unref(self->plugin_filter);
	g_ptr_array_unref(self->backends);
	g_ptr_array_unref(self->local_monitors);
	g_hash_table_unref(self->runtime_versions);
	g_hash_table_unref(self->compile_versions);
	g_object_unref(self->plugin_list);

	G_OBJECT_CLASS(fu_engine_parent_class)->finalize(obj);
}

FuEngine *
fu_engine_new(void)
{
	FuEngine *self;
	self = g_object_new(FU_TYPE_ENGINE, NULL);
	return FU_ENGINE(self);
}
