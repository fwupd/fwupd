/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuEngine"

#include "config.h"

#include <fcntl.h>

#ifdef HAVE_GIO_UNIX
#include <gio/gunixinputstream.h>
#endif
#ifdef HAVE_PASSIM
#include <passim.h>
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
#include "fwupd-remote-private.h"
#include "fwupd-resources.h"
#include "fwupd-security-attr-private.h"

#include "fu-backend-private.h"
#include "fu-bios-settings-private.h"
#include "fu-config-private.h"
#include "fu-context-private.h"
#include "fu-coswid-firmware.h"
#include "fu-debug.h"
#include "fu-device-list.h"
#include "fu-device-private.h"
#include "fu-device-progress.h"
#include "fu-engine-helper.h"
#include "fu-engine-request.h"
#include "fu-engine-requirements.h"
#include "fu-engine.h"
#include "fu-history.h"
#include "fu-idle.h"
#include "fu-plugin-builtin.h"
#include "fu-plugin-list.h"
#include "fu-plugin-private.h"
#include "fu-release.h"
#include "fu-remote-list.h"
#include "fu-remote.h"
#include "fu-security-attr-common.h"
#include "fu-security-attrs-private.h"
#include "fu-udev-device-private.h"
#include "fu-usb-backend.h"
#include "fu-usb-device-fw-ds20.h"
#include "fu-usb-device-ms-ds20.h"
#include "fu-usb-device-private.h"

#ifdef HAVE_GIO_UNIX
#include "fu-unix-seekable-input-stream.h"
#endif
#ifdef HAVE_GUDEV
#include "fu-udev-backend.h"
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

#define FU_ENGINE_UPDATE_MOTD_DELAY 5 /* s */

#define FU_ENGINE_MAX_METADATA_SIZE  0x2000000 /* 32MB */
#define FU_ENGINE_MAX_SIGNATURE_SIZE 0x100000  /* 1MB */

static void
fu_engine_constructed(GObject *obj);
static void
fu_engine_finalize(GObject *obj);
static void
fu_engine_ensure_security_attrs(FuEngine *self);
static gboolean
fu_engine_backends_save_phase(FuEngine *self, GError **error);
static gboolean
fu_engine_emulation_load_phase(FuEngine *self, GError **error);

struct _FuEngine {
	GObject parent_instance;
	FuEngineConfig *config;
	FuRemoteList *remote_list;
	FuDeviceList *device_list;
	gboolean only_trusted;
	gboolean write_history;
	gboolean host_emulation;
	guint percentage;
	FuHistory *history;
	FuIdle *idle;
	XbSilo *silo;
	XbQuery *query_component_by_guid;
	XbQuery *query_container_checksum1; /* container checksum -> release */
	XbQuery *query_container_checksum2; /* artifact checksum -> release */
	XbQuery *query_tag_by_guid_version;
	guint coldplug_id;
	FuPluginList *plugin_list;
	GPtrArray *plugin_filter;
	FuContext *ctx;
	GHashTable *approved_firmware;	      /* (nullable) */
	GHashTable *blocked_firmware;	      /* (nullable) */
	GHashTable *emulation_phases;	      /* (element-type int GBytes) */
	GHashTable *emulation_ids;	      /* (element-type str int) */
	GHashTable *device_changed_allowlist; /* (element-type str int) */
	gchar *host_machine_id;
	JcatContext *jcat_context;
	gboolean loaded;
	gchar *host_security_id;
	FuSecurityAttrs *host_security_attrs;
	GPtrArray *local_monitors; /* (element-type GFileMonitor) */
	GMainLoop *acquiesce_loop;
	guint acquiesce_id;
	guint acquiesce_delay;
	guint update_motd_id;
	FuEngineInstallPhase install_phase;
#ifdef HAVE_PASSIM
	PassimClient *passim_client;
#endif
};

enum { PROP_0, PROP_CONTEXT, PROP_LAST };

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

gboolean
fu_engine_get_loaded(FuEngine *self)
{
	return self->loaded;
}

static gboolean
fu_engine_update_motd_timeout_cb(gpointer user_data)
{
	FuEngine *self = FU_ENGINE(user_data);
	g_autoptr(GError) error_local = NULL;

	/* busy */
	if (fu_idle_has_inhibit(self->idle, FU_IDLE_INHIBIT_SIGNALS))
		return G_SOURCE_CONTINUE;

	/* update now */
	if (!fu_engine_update_motd(self, &error_local))
		g_info("failed to update MOTD: %s", error_local->message);
	self->update_motd_id = 0;
	return G_SOURCE_REMOVE;
}

static void
fu_engine_update_motd_reset(FuEngine *self)
{
	g_info("resetting update motd timeout");
	if (self->update_motd_id != 0)
		g_source_remove(self->update_motd_id);
	self->update_motd_id = g_timeout_add_seconds(FU_ENGINE_UPDATE_MOTD_DELAY,
						     fu_engine_update_motd_timeout_cb,
						     self);
}

static void
fu_engine_emit_changed(FuEngine *self)
{
	g_autoptr(GError) error = NULL;

	/* do nothing */
	if (!self->loaded)
		return;

	g_signal_emit(self, signals[SIGNAL_CHANGED], 0);
	fu_engine_idle_reset(self);

	/* update the motd */
	if (fu_engine_config_get_update_motd(self->config))
		fu_engine_update_motd_reset(self);

	/* update the list of devices */
	if (!fu_engine_update_devices_file(self, &error))
		g_info("failed to update list of devices: %s", error->message);
}

static void
fu_engine_emit_device_changed_safe(FuEngine *self, FuDevice *device)
{
	/* do nothing */
	if (!self->loaded)
		return;

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
	if (fu_idle_has_inhibit(self->idle, FU_IDLE_INHIBIT_SIGNALS) &&
	    !g_hash_table_contains(self->device_changed_allowlist, fu_device_get_id(device))) {
		g_debug("suppressing notification from %s as transaction is in progress",
			fu_device_get_id(device));
		return;
	}
	fu_engine_emit_device_changed(self, fu_device_get_id(device));
}

static void
fu_engine_ensure_device_problem_priority_full(FuEngine *self,
					      FuDevice *device,
					      FuDevice *device_tmp)
{
	/* not a match */
	if (g_strcmp0(fu_device_get_id(device_tmp), fu_device_get_equivalent_id(device)) != 0 &&
	    g_strcmp0(fu_device_get_equivalent_id(device_tmp), fu_device_get_id(device)) != 0)
		return;

	/* new device is better */
	if (fu_device_get_priority(device_tmp) < fu_device_get_priority(device)) {
		fu_device_add_problem(device_tmp, FWUPD_DEVICE_PROBLEM_LOWER_PRIORITY);
		fu_device_remove_problem(device, FWUPD_DEVICE_PROBLEM_LOWER_PRIORITY);
		return;
	}

	/* old device is better */
	if (fu_device_get_priority(device_tmp) > fu_device_get_priority(device)) {
		fu_device_remove_problem(device_tmp, FWUPD_DEVICE_PROBLEM_LOWER_PRIORITY);
		fu_device_add_problem(device, FWUPD_DEVICE_PROBLEM_LOWER_PRIORITY);
		return;
	}

	/* the plugin needs to tell us which one is better! */
	g_warning("no priority difference, unsetting both");
	fu_device_remove_problem(device, FWUPD_DEVICE_PROBLEM_LOWER_PRIORITY);
	fu_device_remove_problem(device_tmp, FWUPD_DEVICE_PROBLEM_LOWER_PRIORITY);
}

static void
fu_engine_ensure_device_problem_priority(FuEngine *self, FuDevice *device)
{
	g_autoptr(GPtrArray) devices = fu_device_list_get_active(self->device_list);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device_tmp = g_ptr_array_index(devices, i);
		fu_engine_ensure_device_problem_priority_full(self, device, device_tmp);
	}
}

static void
fu_engine_device_equivalent_id_notify_cb(FuDevice *device, GParamSpec *pspec, FuEngine *self)
{
	/* make sure the lower priority equivalent device has the problem */
	fu_engine_ensure_device_problem_priority(self, device);
}

static void
fu_engine_history_notify_cb(FuDevice *device, GParamSpec *pspec, FuEngine *self)
{
	if (self->write_history) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_history_modify_device(self->history, device, &error_local)) {
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
				g_debug("ignoring: %s", error_local->message);
			} else {
				g_warning("failed to record history for %s: %s",
					  fu_device_get_id(device),
					  error_local->message);
			}
		}
	}
	fu_engine_emit_device_changed(self, fu_device_get_id(device));
}

static void
fu_engine_device_request_cb(FuDevice *device, FwupdRequest *request, FuEngine *self)
{
	g_info("Emitting DeviceRequest('Message'='%s')", fwupd_request_get_message(request));
	g_signal_emit(self, signals[SIGNAL_DEVICE_REQUEST], 0, request);
}

static void
fu_engine_set_install_phase(FuEngine *self, FuEngineInstallPhase install_phase)
{
	g_info("install phase now %s", fu_engine_install_phase_to_string(install_phase));
	self->install_phase = install_phase;
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
			 "notify::equivalent-id",
			 G_CALLBACK(fu_engine_device_equivalent_id_notify_cb),
			 self);
	g_signal_connect(FU_DEVICE(device),
			 "request",
			 G_CALLBACK(fu_engine_device_request_cb),
			 self);
}

static void
fu_engine_ensure_device_power_inhibit(FuEngine *self, FuDevice *device)
{
	if (fu_engine_config_get_ignore_power(self->config))
		return;

	if (fu_device_is_updatable(device) &&
	    fu_device_has_flag(device, FWUPD_DEVICE_FLAG_REQUIRE_AC) &&
	    !fu_power_state_is_ac(fu_context_get_power_state(self->ctx))) {
		fu_device_add_problem(device, FWUPD_DEVICE_PROBLEM_REQUIRE_AC_POWER);
	} else {
		fu_device_remove_problem(device, FWUPD_DEVICE_PROBLEM_REQUIRE_AC_POWER);
	}
	if (fu_device_is_updatable(device) &&
	    !fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_IGNORE_SYSTEM_POWER) &&
	    fu_context_get_battery_level(self->ctx) != FWUPD_BATTERY_LEVEL_INVALID &&
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
	if (fu_device_is_updatable(device) &&
	    fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_NO_LID_CLOSED) &&
	    fu_context_get_lid_state(self->ctx) == FU_LID_STATE_CLOSED) {
		fu_device_add_problem(device, FWUPD_DEVICE_PROBLEM_LID_IS_CLOSED);
		return;
	}
	fu_device_remove_problem(device, FWUPD_DEVICE_PROBLEM_LID_IS_CLOSED);
}

static void
fu_engine_ensure_device_display_required_inhibit(FuEngine *self, FuDevice *device)
{
	if (fu_device_is_updatable(device) &&
	    fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_DISPLAY_REQUIRED) &&
	    fu_context_get_display_state(self->ctx) == FU_DISPLAY_STATE_DISCONNECTED) {
		fu_device_add_problem(device, FWUPD_DEVICE_PROBLEM_DISPLAY_REQUIRED);
		return;
	}
	fu_device_remove_problem(device, FWUPD_DEVICE_PROBLEM_DISPLAY_REQUIRED);
}

static void
fu_engine_ensure_device_system_inhibit(FuEngine *self, FuDevice *device)
{
	if (fu_context_has_flag(self->ctx, FU_CONTEXT_FLAG_SYSTEM_INHIBIT)) {
		fu_device_add_problem(device, FWUPD_DEVICE_PROBLEM_SYSTEM_INHIBIT);
		return;
	}
	fu_device_remove_problem(device, FWUPD_DEVICE_PROBLEM_SYSTEM_INHIBIT);
}

static gboolean
fu_engine_acquiesce_timeout_cb(gpointer user_data)
{
	FuEngine *self = FU_ENGINE(user_data);
	g_info("system acquiesced after %ums", self->acquiesce_delay);
	g_main_loop_quit(self->acquiesce_loop);
	self->acquiesce_id = 0;
	return G_SOURCE_REMOVE;
}

static void
fu_engine_acquiesce_reset(FuEngine *self)
{
	if (!g_main_loop_is_running(self->acquiesce_loop))
		return;
	g_info("resetting system acquiesce timeout");
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
	fu_engine_ensure_device_problem_priority(self, device);
	fu_engine_ensure_device_power_inhibit(self, device);
	fu_engine_ensure_device_lid_inhibit(self, device);
	fu_engine_ensure_device_display_required_inhibit(self, device);
	fu_engine_ensure_device_system_inhibit(self, device);
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

/* add any client-side BKC tags */
static gboolean
fu_engine_add_local_release_metadata(FuEngine *self, FuRelease *release, GError **error)
{
	FuDevice *dev = fu_release_get_device(release);
	GPtrArray *guids;

	/* no device matched */
	if (dev == NULL)
		return TRUE;

	/* not set up */
	if (self->query_tag_by_guid_version == NULL)
		return TRUE;

	/* use prepared query for each GUID */
	guids = fu_device_get_guids(dev);
	for (guint i = 0; i < guids->len; i++) {
		const gchar *guid = g_ptr_array_index(guids, i);
		g_autoptr(GError) error_local = NULL;
		g_autoptr(GPtrArray) tags = NULL;
		g_auto(XbQueryContext) context = XB_QUERY_CONTEXT_INIT();

		/* bind GUID and then query */
		xb_value_bindings_bind_str(xb_query_context_get_bindings(&context), 0, guid, NULL);
		xb_value_bindings_bind_str(xb_query_context_get_bindings(&context),
					   1,
					   fu_release_get_version(release),
					   NULL);
		tags = xb_silo_query_with_context(self->silo,
						  self->query_tag_by_guid_version,
						  &context,
						  &error_local);
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

/* private, for self tests */
void
fu_engine_add_remote(FuEngine *self, FwupdRemote *remote)
{
	g_return_if_fail(FU_IS_ENGINE(self));
	g_return_if_fail(FWUPD_IS_REMOTE(remote));
	fu_remote_list_add_remote(self->remote_list, remote);
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
fu_engine_compare_report_trusted(FwupdReport *report_trusted, FwupdReport *report)
{
	if (fwupd_report_has_flag(report_trusted, FWUPD_REPORT_FLAG_FROM_OEM) &&
	    !fwupd_report_has_flag(report, FWUPD_REPORT_FLAG_FROM_OEM))
		return FALSE;
	if (fwupd_report_has_flag(report_trusted, FWUPD_REPORT_FLAG_IS_UPGRADE) &&
	    !fwupd_report_has_flag(report, FWUPD_REPORT_FLAG_IS_UPGRADE))
		return FALSE;
	if (fwupd_report_get_vendor_id(report_trusted) != 0) {
		if (fwupd_report_get_vendor_id(report_trusted) !=
		    fwupd_report_get_vendor_id(report))
			return FALSE;
	}
	if (fwupd_report_get_distro_id(report_trusted) != NULL) {
		if (g_strcmp0(fwupd_report_get_distro_id(report_trusted),
			      fwupd_report_get_distro_id(report)) != 0)
			return FALSE;
	}
	if (fwupd_report_get_distro_version(report_trusted) != NULL) {
		if (g_strcmp0(fwupd_report_get_distro_version(report_trusted),
			      fwupd_report_get_distro_version(report)) != 0)
			return FALSE;
	}
	if (fwupd_report_get_distro_variant(report_trusted) != NULL) {
		if (g_strcmp0(fwupd_report_get_distro_variant(report_trusted),
			      fwupd_report_get_distro_variant(report)) != 0)
			return FALSE;
	}
	if (fwupd_report_get_remote_id(report_trusted) != NULL) {
		if (g_strcmp0(fwupd_report_get_remote_id(report_trusted),
			      fwupd_report_get_remote_id(report)) != 0)
			return FALSE;
	}
	return TRUE;
}

static void
fu_engine_add_trusted_report(FuEngine *self, FuRelease *release)
{
	GPtrArray *reports = fu_release_get_reports(release);
	GPtrArray *trusted_reports = fu_engine_config_get_trusted_reports(self->config);

	for (guint i = 0; i < reports->len; i++) {
		FwupdReport *report = g_ptr_array_index(reports, i);
		for (guint j = 0; j < trusted_reports->len; j++) {
			FwupdReport *trusted_report = g_ptr_array_index(trusted_reports, j);
			if (fu_engine_compare_report_trusted(trusted_report, report)) {
				g_autofree gchar *str =
				    fwupd_codec_to_string(FWUPD_CODEC(trusted_report));
				g_debug("add trusted-report to %s:%s as trusted: %s",
					fu_release_get_appstream_id(release),
					fu_release_get_version(release),
					str);
				fu_release_add_flag(release, FWUPD_RELEASE_FLAG_TRUSTED_REPORT);
				return;
			}
		}
	}
}

static gboolean
fu_engine_load_release(FuEngine *self,
		       FuRelease *release,
		       FuCabinet *cabinet,
		       XbNode *component,
		       XbNode *rel,
		       FwupdInstallFlags install_flags,
		       GError **error)
{
	g_return_val_if_fail(cabinet == NULL || FU_IS_CABINET(cabinet), FALSE);

	/* load release from XML */
	fu_release_set_config(release, self->config);

	/* set the FwupdRemote when the remote ID is set */
	g_signal_connect(FU_RELEASE(release),
			 "notify::remote-id",
			 G_CALLBACK(fu_engine_release_remote_id_changed_cb),
			 self);

	/* requirements we can check without the daemon */
	if (!fu_release_load(release, cabinet, component, rel, install_flags, error))
		return FALSE;

	/* relax these */
	if (fu_engine_config_get_ignore_requirements(self->config))
		install_flags |= FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS;

	/* additional requirements */
	if (!fu_engine_requirements_check(self, release, install_flags, error))
		return FALSE;

	/* add any client-side BKC tags */
	if (!fu_engine_add_local_release_metadata(self, release, error))
		return FALSE;

	/* add the trusted report metadata if appropriate */
	fu_engine_add_trusted_report(self, release);

	/* success */
	return TRUE;
}

/* finds the release for the first firmware in the silo that matches this
 * container or artifact checksum */
static XbNode *
fu_engine_get_release_for_checksum(FuEngine *self, const gchar *csum)
{
	g_auto(XbQueryContext) context = XB_QUERY_CONTEXT_INIT();
	xb_value_bindings_bind_str(xb_query_context_get_bindings(&context), 0, csum, NULL);
	if (self->query_container_checksum1 != NULL) {
		g_autoptr(XbNode) rel =
		    xb_silo_query_first_with_context(self->silo,
						     self->query_container_checksum1,
						     &context,
						     NULL);
		if (rel != NULL)
			return g_steal_pointer(&rel);
	}
	if (self->query_container_checksum2 != NULL) {
		g_autoptr(XbNode) rel =
		    xb_silo_query_first_with_context(self->silo,
						     self->query_container_checksum2,
						     &context,
						     NULL);
		if (rel != NULL)
			return g_steal_pointer(&rel);
	}

	/* failed */
	return NULL;
}

/* does this exist in any enabled remote */
gchar *
fu_engine_get_remote_id_for_stream(FuEngine *self, GInputStream *stream)
{
	GChecksumType checksum_types[] = {G_CHECKSUM_SHA256, G_CHECKSUM_SHA1, 0};

	g_return_val_if_fail(FU_IS_ENGINE(self), NULL);
	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), NULL);

	for (guint i = 0; checksum_types[i] != 0; i++) {
		g_autofree gchar *csum = NULL;
		g_autoptr(XbNode) rel = NULL;
		csum = fu_input_stream_compute_checksum(stream, checksum_types[i], NULL);
		if (csum != NULL)
			rel = fu_engine_get_release_for_checksum(self, csum);
		if (rel != NULL) {
			const gchar *remote_id =
			    xb_node_query_text(rel,
					       "../../../custom/value[@key='fwupd::RemoteId']",
					       NULL);
			if (remote_id != NULL)
				return g_strdup(remote_id);
		}
	}
	return NULL;
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
fu_engine_reset_config(FuEngine *self, const gchar *section, GError **error)
{
	/* reset, effective next reboot */
	return fu_config_reset_defaults(FU_CONFIG(self->config), section, error);
}

gboolean
fu_engine_modify_config(FuEngine *self,
			const gchar *section,
			const gchar *key,
			const gchar *value,
			GError **error)
{
	FuPlugin *plugin;

	g_return_val_if_fail(FU_IS_ENGINE(self), FALSE);
	g_return_val_if_fail(section != NULL, FALSE);
	g_return_val_if_fail(key != NULL, FALSE);
	g_return_val_if_fail(value != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* check keys are valid */
	if (g_strcmp0(section, "fwupd") == 0) {
		const gchar *keys[] = {"ArchiveSizeMax",
				       "AllowEmulation",
				       "ApprovedFirmware",
				       "BlockedFirmware",
				       "DisabledDevices",
				       "DisabledPlugins",
				       "EnumerateAllDevices",
				       "EspLocation",
				       "HostBkc",
				       "IdleTimeout",
				       "IgnorePower",
				       "OnlyTrusted",
				       "P2pPolicy",
				       "ReleaseDedupe",
				       "ReleasePriority",
				       "ShowDevicePrivate",
				       "TestDevices",
				       "TrustedReports",
				       "TrustedUids",
				       "UpdateMotd",
				       "UriSchemes",
				       "VerboseDomains",
				       NULL};
		if (!g_strv_contains(keys, key)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "key %s not supported for [%s]",
				    key,
				    section);
			return FALSE;
		}

		/* many options need a reboot after this */
		if (!fu_config_set_value(FU_CONFIG(self->config), section, key, value, error))
			return FALSE;

		/* reload remotes */
		if (g_strcmp0(key, "TestDevices") == 0 &&
		    !fu_remote_list_set_testing_remote_enabled(
			self->remote_list,
			fu_engine_config_get_test_devices(self->config),
			error))
			return FALSE;

		return TRUE;
	}

	/* handled per-plugin */
	plugin = fu_plugin_list_find_by_name(self->plugin_list, section, error);
	if (plugin == NULL)
		return FALSE;
	return fu_plugin_runner_modify_config(plugin, key, value, error);
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
	    "MetadataURI",
	    "ReportURI",
	    "Username",
	    "Password",
	    NULL,
	};

	/* check keys are valid */
	if (!g_strv_contains(keys, key)) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "key %s not supported", key);
		return FALSE;
	}
	return fu_remote_list_set_key_value(self->remote_list, remote_id, key, value, error);
}

static gboolean
fu_engine_modify_single_bios_setting(FuEngine *self,
				     const gchar *key,
				     const gchar *value,
				     gboolean force_ro,
				     GError **error)
{
	FwupdBiosSetting *attr = fu_context_get_bios_setting(self->ctx, key);
	if (attr == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "attribute not found");
		return FALSE;
	}
	if (!fwupd_bios_setting_write_value(attr, value, error))
		return FALSE;
	if (force_ro)
		fwupd_bios_setting_set_read_only(attr, TRUE);
	return TRUE;
}

/**
 * fu_engine_modify_bios_settings:
 * @self: a #FuEngine
 * @settings: Hashtable of settings/values to configure
 * @force_ro: a #gboolean indicating if BIOS settings should also be made read-only
 * @error: (nullable): optional return location for an error
 *
 * Use the kernel API to set one or more BIOS settings.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_modify_bios_settings(FuEngine *self,
			       GHashTable *settings,
			       gboolean force_ro,
			       GError **error)
{
	g_autoptr(FuBiosSettings) bios_settings = fu_context_get_bios_settings(self->ctx);
	gboolean changed = FALSE;
	GHashTableIter iter;
	gpointer key, value;

	g_return_val_if_fail(FU_IS_ENGINE(self), FALSE);
	g_return_val_if_fail(settings != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	g_hash_table_iter_init(&iter, settings);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		g_autoptr(GError) error_local = NULL;
		if (value == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "attribute %s missing value",
				    (const gchar *)key);
			return FALSE;
		}
		if (!fu_engine_modify_single_bios_setting(self,
							  key,
							  value,
							  force_ro,
							  &error_local)) {
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO)) {
				g_debug("%s", error_local->message);
				continue;
			}
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
		changed = TRUE;
	}

	if (!changed) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "no BIOS settings needed to be changed");
		return FALSE;
	}

	if (!fu_bios_settings_get_pending_reboot(bios_settings, &changed, error))
		return FALSE;
	g_info("pending_reboot is now %d", changed);
	return TRUE;
}

static void
fu_engine_ensure_context_flag_save_events(FuEngine *self)
{
	if (g_hash_table_size(self->emulation_ids) > 0 &&
	    fu_engine_config_get_allow_emulation(self->config)) {
		fu_context_add_flag(self->ctx, FU_CONTEXT_FLAG_SAVE_EVENTS);
	} else {
		fu_context_remove_flag(self->ctx, FU_CONTEXT_FLAG_SAVE_EVENTS);
	}
}

static gboolean
fu_engine_remove_device_flag(FuEngine *self,
			     const gchar *device_id,
			     FwupdDeviceFlags flag,
			     GError **error)
{
	FuDevice *proxy;
	g_autoptr(FuDevice) device = NULL;

	if (flag == FWUPD_DEVICE_FLAG_NOTIFIED) {
		device = fu_history_get_device_by_id(self->history, device_id, error);
		if (device == NULL)
			return FALSE;
		fu_device_remove_flag(device, flag);
		return fu_history_modify_device(self->history, device, error);
	}
	if (flag == FWUPD_DEVICE_FLAG_EMULATED) {
		device = fu_device_list_get_by_id(self->device_list, device_id, error);
		if (device == NULL)
			return FALSE;
		if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "device %s is not emulated",
				    fu_device_get_id(device));
			return FALSE;
		}
		if (fu_device_get_backend(device) == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "device %s requires backend",
				    fu_device_get_id(device));
			return FALSE;
		}
		fu_backend_device_removed(fu_device_get_backend(device), device);
		return TRUE;
	}
	if (flag == FWUPD_DEVICE_FLAG_EMULATION_TAG) {
		device = fu_device_list_get_by_id(self->device_list, device_id, error);
		if (device == NULL)
			return FALSE;
		proxy = fu_device_get_proxy(device);
		if (proxy != NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "device %s uses a proxy, remove the flag on %s instead",
				    fu_device_get_id(device),
				    fu_device_get_id(proxy));
			return FALSE;
		}
		g_hash_table_remove(self->emulation_ids, fu_device_get_id(device));
		fu_engine_ensure_context_flag_save_events(self);
		return TRUE;
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "flag cannot be removed from client");
	return FALSE;
}

static void
fu_engine_emit_device_request_replug_and_install(FuEngine *self, FuDevice *device)
{
	g_autoptr(FwupdRequest) request = fwupd_request_new();
	fwupd_request_set_id(request, FWUPD_REQUEST_ID_REPLUG_INSTALL);
	fwupd_request_set_device_id(request, fu_device_get_id(device));
	fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_IMMEDIATE);
	fwupd_request_add_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	fwupd_request_set_message(request,
				  "Unplug and replug the device, then install the firmware.");
	g_signal_emit(self, signals[SIGNAL_DEVICE_REQUEST], 0, request);
}

static gboolean
fu_engine_add_device_flag(FuEngine *self,
			  const gchar *device_id,
			  FwupdDeviceFlags flag,
			  GError **error)
{
	FuDevice *proxy;
	g_autoptr(FuDevice) device = NULL;

	if (flag == FWUPD_DEVICE_FLAG_REPORTED || flag == FWUPD_DEVICE_FLAG_NOTIFIED) {
		device = fu_history_get_device_by_id(self->history, device_id, error);
		if (device == NULL)
			return FALSE;
		fu_device_add_flag(device, flag);
		return fu_history_modify_device(self->history, device, error);
	}
	if (flag == FWUPD_DEVICE_FLAG_EMULATION_TAG) {
		device = fu_device_list_get_by_id(self->device_list, device_id, error);
		if (device == NULL)
			return FALSE;
		proxy = fu_device_get_proxy(device);
		if (proxy != NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "device %s uses a proxy, set the flag on %s instead",
				    fu_device_get_id(device),
				    fu_device_get_id(proxy));
			return FALSE;
		}
		g_hash_table_insert(self->emulation_ids,
				    g_strdup(fu_device_get_id(device)),
				    GUINT_TO_POINTER(1));
		fu_engine_ensure_context_flag_save_events(self);
		fu_engine_emit_device_request_replug_and_install(self, device);
		return TRUE;
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "flag cannot be added from client");
	return FALSE;
}

static gboolean
fu_engine_modify_device_flags(FuEngine *self,
			      const gchar *device_id,
			      const gchar *value,
			      GError **error)
{
	/* add or remove a subset of device flags */
	if (g_str_has_prefix(value, "~")) {
		return fu_engine_remove_device_flag(self,
						    device_id,
						    fwupd_device_flag_from_string(value + 1),
						    error);
	}
	return fu_engine_add_device_flag(self,
					 device_id,
					 fwupd_device_flag_from_string(value),
					 error);
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
	g_return_val_if_fail(FU_IS_ENGINE(self), FALSE);
	g_return_val_if_fail(device_id != NULL, FALSE);
	g_return_val_if_fail(key != NULL, FALSE);
	g_return_val_if_fail(value != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	if (g_strcmp0(key, "Flags") == 0)
		return fu_engine_modify_device_flags(self, device_id, value, error);
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
	g_autoptr(FuDeviceProgress) device_progress = NULL;

	g_return_val_if_fail(FU_IS_ENGINE(self), FALSE);
	g_return_val_if_fail(device_id != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* check the devices still exists */
	device = fu_device_list_get_by_id(self->device_list, device_id, error);
	if (device == NULL)
		return FALSE;
	device_progress = fu_device_progress_new(device, progress);
	g_return_val_if_fail(device_progress != NULL, FALSE);

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
	g_auto(XbQueryContext) context = XB_QUERY_CONTEXT_INIT();

	/* no components in silo */
	if (self->query_component_by_guid == NULL)
		return NULL;

	xb_query_context_set_flags(&context, XB_QUERY_FLAG_USE_INDEXES);
	xb_value_bindings_bind_str(xb_query_context_get_bindings(&context), 0, guid, NULL);
	component = xb_silo_query_first_with_context(self->silo,
						     self->query_component_by_guid,
						     &context,
						     &error_local);
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
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "failed to find %s", fn);
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
	if (query == NULL) {
		fu_error_convert(error);
		return NULL;
	}

	/* use prepared query for each GUID */
	for (guint i = 0; i < guids->len; i++) {
		const gchar *guid = g_ptr_array_index(guids, i);
		g_autoptr(GError) error_local = NULL;
		g_autoptr(GPtrArray) releases = NULL;
		g_auto(XbQueryContext) context = XB_QUERY_CONTEXT_INIT();

		/* bind GUID and then query */
		xb_value_bindings_bind_str(xb_query_context_get_bindings(&context), 0, guid, NULL);
		releases = xb_silo_query_with_context(self->silo, query, &context, &error_local);
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
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "failed to find release");
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
			if (!g_error_matches(error_system, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND) &&
			    !g_error_matches(error_system, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA)) {
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
		g_autofree gchar *checksums_device = fu_strjoin("|", checksums);
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
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "For %s %s expected %s, got %s",
			    fu_device_get_name(device),
			    fu_device_get_version(device),
			    checksums_metadata->str,
			    checksums_device);
		return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
fu_engine_check_trust(FuEngine *self, FuRelease *release, GError **error)
{
	g_autofree gchar *str = fu_release_to_string(release);

	g_debug("checking trust of %s", str);
	if (fu_engine_config_get_only_trusted(self->config) &&
	    !fu_release_has_flag(release, FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD)) {
		g_autofree gchar *sysconfdir = fu_path_from_kind(FU_PATH_KIND_SYSCONFDIR_PKG);
		g_autofree gchar *fn = g_build_filename(sysconfdir, "fwupd.conf", NULL);
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

void
fu_engine_idle_reset(FuEngine *self)
{
	fu_idle_reset(self->idle);
}

guint32
fu_engine_idle_inhibit(FuEngine *self, FuIdleInhibit inhibit, const gchar *reason)
{
	return fu_idle_inhibit(self->idle, inhibit, reason);
}

void
fu_engine_idle_uninhibit(FuEngine *self, guint32 token)
{
	fu_idle_uninhibit(self->idle, token);
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

static FuDevice *
fu_engine_get_cpu_device(FuEngine *self)
{
	g_autoptr(GPtrArray) devices = fu_device_list_get_active(self->device_list);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		if (fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_HOST_CPU))
			return g_object_ref(device);
	}
	return NULL;
}

static void
fu_engine_get_report_metadata_cpu_device(FuEngine *self, GHashTable *hash)
{
	g_autoptr(FuDevice) device = NULL;

	device = fu_engine_get_cpu_device(self);
	if (device == NULL) {
		g_info("failed to find CPU device");
		return;
	}
	if (fu_device_get_vendor(device) == NULL || fu_device_get_name(device) == NULL) {
		g_info("not enough data to include CpuModel");
		return;
	}
	g_hash_table_insert(
	    hash,
	    g_strdup("CpuModel"),
	    g_strdup_printf("%s %s", fu_device_get_vendor(device), fu_device_get_name(device)));
}

static gboolean
fu_engine_get_report_metadata_os_release(GHashTable *hash, GError **error)
{
#ifdef HOST_MACHINE_SYSTEM_DARWIN
	g_autofree gchar *stdout = NULL;
	g_autofree gchar *sw_vers = g_find_program_in_path("sw_vers");
	g_auto(GStrv) split = NULL;
	struct {
		const gchar *key;
		const gchar *val;
	} kvs[] = {{"ProductName:", "DistroName"},
		   {"ProductVersion:", FWUPD_RESULT_KEY_DISTRO_VERSION},
		   {"BuildVersion:", FWUPD_RESULT_KEY_DISTRO_VARIANT},
		   {NULL, NULL}};

	/* macOS */
	if (sw_vers == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_READ, "No os-release found");
		return FALSE;
	}

	/* parse from format:
	 *    ProductName:    Mac OS X
	 *    ProductVersion: 10.14.6
	 *    BuildVersion:   18G103
	 */
	if (!g_spawn_command_line_sync(sw_vers, &stdout, NULL, NULL, error))
		return FALSE;
	split = g_strsplit(stdout, "\n", -1);
	for (guint j = 0; split[j] != NULL; j++) {
		for (guint i = 0; kvs[i].key != NULL; i++) {
			if (g_str_has_prefix(split[j], kvs[i].key)) {
				g_autofree gchar *tmp = g_strdup(split[j] + strlen(kvs[i].key));
				g_hash_table_insert(hash,
						    g_strdup(kvs[i].val),
						    g_strdup(g_strstrip(tmp)));
			}
		}
	}
	g_hash_table_insert(hash, g_strdup(FWUPD_RESULT_KEY_DISTRO_ID), g_strdup("macos"));
#else
	struct {
		const gchar *key;
		const gchar *val;
	} distro_kv[] = {{G_OS_INFO_KEY_ID, FWUPD_RESULT_KEY_DISTRO_ID},
			 {G_OS_INFO_KEY_NAME, "DistroName"},
			 {G_OS_INFO_KEY_PRETTY_NAME, "DistroPrettyName"},
			 {G_OS_INFO_KEY_VERSION_ID, FWUPD_RESULT_KEY_DISTRO_VERSION},
			 {"VARIANT_ID", FWUPD_RESULT_KEY_DISTRO_VARIANT},
			 {NULL, NULL}};

	/* get all required os-release keys */
	for (guint i = 0; distro_kv[i].key != NULL; i++) {
		g_autofree gchar *tmp = g_get_os_info(distro_kv[i].key);
		if (tmp != NULL) {
			g_hash_table_insert(hash,
					    g_strdup(distro_kv[i].val),
					    g_steal_pointer(&tmp));
		}
	}
#endif
	return TRUE;
}

static GHashTable *
fu_engine_load_os_release(const gchar *filename, GError **error)
{
	g_autofree gchar *buf = NULL;
	g_autofree gchar *filename2 = g_strdup(filename);
	g_auto(GStrv) lines = NULL;
	g_autoptr(GHashTable) hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

	/* load each line */
	if (!g_file_get_contents(filename2, &buf, NULL, error))
		return NULL;
	lines = g_strsplit(buf, "\n", -1);
	for (guint i = 0; lines[i] != NULL; i++) {
		gsize len, off = 0;
		g_auto(GStrv) split = NULL;

		/* split up into sections */
		split = g_strsplit(lines[i], "=", 2);
		if (g_strv_length(split) < 2)
			continue;

		/* remove double quotes if set both ends */
		len = strlen(split[1]);
		if (len == 0)
			continue;
		if (split[1][0] == '\"' && split[1][len - 1] == '\"') {
			off++;
			len -= 2;
		}
		g_hash_table_insert(hash, g_strdup(split[0]), g_strndup(split[1] + off, len));
	}
	return g_steal_pointer(&hash);
}

static gboolean
fu_engine_get_report_metadata_lsb_release(GHashTable *hash, GError **error)
{
	const gchar *fn = "/etc/lsb-release";
	g_autoptr(GHashTable) os_release = NULL;
	struct {
		const gchar *key;
		const gchar *val;
	} distro_kv[] = {{"CHROMEOS_RELEASE_TRACK", "DistroReleaseTrack"},
			 {"CHROMEOS_RELEASE_BOARD", "DistroReleaseBoard"},
			 {NULL, NULL}};
	if (!g_file_test(fn, G_FILE_TEST_EXISTS))
		return TRUE;
	os_release = fu_engine_load_os_release(fn, error);
	if (os_release == NULL)
		return FALSE;
	for (guint i = 0; distro_kv[i].key != NULL; i++) {
		const gchar *tmp = g_hash_table_lookup(os_release, distro_kv[i].key);
		if (tmp != NULL)
			g_hash_table_insert(hash, g_strdup(distro_kv[i].val), g_strdup(tmp));
	}
	return TRUE;
}

static gboolean
fu_engine_get_report_metadata_kernel_cmdline(GHashTable *hash, GError **error)
{
	g_autofree gchar *cmdline = NULL;
	cmdline = fu_common_get_kernel_cmdline(error);
	if (cmdline == NULL)
		return FALSE;
	if (cmdline[0] != '\0')
		g_hash_table_insert(hash, g_strdup("KernelCmdline"), g_steal_pointer(&cmdline));
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
	GHashTable *compile_versions = fu_context_get_compile_versions(self->ctx);
	GHashTable *runtime_versions = fu_context_get_runtime_versions(self->ctx);
	const gchar *tmp;
	gchar *btime;
#ifdef HAVE_UTSNAME_H
	struct utsname name_tmp;
#endif
	g_autoptr(GHashTable) hash = NULL;
	g_autoptr(GList) compile_keys = g_hash_table_get_keys(compile_versions);
	g_autoptr(GList) runtime_keys = g_hash_table_get_keys(runtime_versions);

	/* convert all the runtime and compile-time versions */
	hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	for (GList *l = compile_keys; l != NULL; l = l->next) {
		const gchar *id = l->data;
		const gchar *version = g_hash_table_lookup(compile_versions, id);
		g_hash_table_insert(hash,
				    g_strdup_printf("CompileVersion(%s)", id),
				    g_strdup(version));
	}
	for (GList *l = runtime_keys; l != NULL; l = l->next) {
		const gchar *id = l->data;
		const gchar *version = g_hash_table_lookup(runtime_versions, id);
		g_hash_table_insert(hash,
				    g_strdup_printf("RuntimeVersion(%s)", id),
				    g_strdup(version));
	}
	fu_engine_get_report_metadata_cpu_device(self, hash);
	if (!fu_engine_get_report_metadata_os_release(hash, error))
		return NULL;
	if (!fu_engine_get_report_metadata_lsb_release(hash, error))
		return NULL;
	if (!fu_engine_get_report_metadata_kernel_cmdline(hash, error))
		return NULL;

		/* these affect the report credibility */
#ifdef SUPPORTED_BUILD
	fu_engine_add_report_metadata_bool(hash, "FwupdSupported", TRUE);
#else
	fu_engine_add_report_metadata_bool(hash, "FwupdSupported", FALSE);
#endif

	/* find out what BKC is being targeted to understand "odd" upgrade paths */
	tmp = fu_engine_config_get_host_bkc(self->config);
	if (tmp != NULL)
		g_hash_table_insert(hash, g_strdup("HostBkc"), g_strdup(tmp));

#ifdef HAVE_PASSIM
	/* this is useful to know if passim support is actually helping bandwidth use */
	g_hash_table_insert(
	    hash,
	    g_strdup("PassimDownloadSaving"),
	    g_strdup_printf("%" G_GUINT64_FORMAT,
			    passim_client_get_download_saving(self->passim_client)));
#endif

	/* DMI data */
	if (fu_context_has_flag(self->ctx, FU_CONTEXT_FLAG_LOADED_HWINFO)) {
		struct {
			const gchar *hwid;
			const gchar *name;
		} keys[] = {{FU_HWIDS_KEY_BASEBOARD_MANUFACTURER, "HostBaseboardManufacturer"},
			    {FU_HWIDS_KEY_BASEBOARD_PRODUCT, "HostBaseboardProduct"},
			    {FU_HWIDS_KEY_BIOS_MAJOR_RELEASE, "HostBiosMajorRelease"},
			    {FU_HWIDS_KEY_BIOS_MINOR_RELEASE, "HostBiosMinorRelease"},
			    {FU_HWIDS_KEY_BIOS_VENDOR, "HostBiosVendor"},
			    {FU_HWIDS_KEY_BIOS_VERSION, "HostBiosVersion"},
			    {FU_HWIDS_KEY_FIRMWARE_MAJOR_RELEASE, "HostFirmwareMajorRelease"},
			    {FU_HWIDS_KEY_FIRMWARE_MINOR_RELEASE, "HostFirmwareMinorRelease"},
			    {FU_HWIDS_KEY_ENCLOSURE_KIND, "HostEnclosureKind"},
			    {FU_HWIDS_KEY_FAMILY, "HostFamily"},
			    {FU_HWIDS_KEY_MANUFACTURER, "HostVendor"},
			    {FU_HWIDS_KEY_PRODUCT_NAME, "HostProduct"},
			    {FU_HWIDS_KEY_PRODUCT_SKU, "HostSku"},
			    {NULL, NULL}};
		for (guint i = 0; keys[i].hwid != NULL; i++) {
			tmp = fu_context_get_hwid_value(self->ctx, keys[i].hwid);
			if (tmp != NULL)
				g_hash_table_insert(hash, g_strdup(keys[i].name), g_strdup(tmp));
		}
	}

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

	/* add context information */
	g_hash_table_insert(
	    hash,
	    g_strdup("PowerState"),
	    g_strdup(fu_power_state_to_string(fu_context_get_power_state(self->ctx))));
	g_hash_table_insert(
	    hash,
	    g_strdup("DisplayState"),
	    g_strdup(fu_display_state_to_string(fu_context_get_display_state(self->ctx))));
	g_hash_table_insert(hash,
			    g_strdup("LidState"),
			    g_strdup(fu_lid_state_to_string(fu_context_get_lid_state(self->ctx))));
	g_hash_table_insert(hash,
			    g_strdup("BatteryLevel"),
			    g_strdup_printf("%u", fu_context_get_battery_level(self->ctx)));
	g_hash_table_insert(hash,
			    g_strdup("BatteryThreshold"),
			    g_strdup_printf("%u", fu_context_get_battery_threshold(self->ctx)));

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
	gboolean any_emulated = FALSE;

	/* we are emulating a device */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED))
			any_emulated = TRUE;
	}
	if (any_emulated) {
		if (!fu_engine_emulation_load_phase(self, error))
			return FALSE;
	}

	for (guint j = 0; j < plugins->len; j++) {
		FuPlugin *plugin_tmp = g_ptr_array_index(plugins, j);
		if (!fu_plugin_runner_composite_prepare(plugin_tmp, devices, error))
			return FALSE;
	}

	/* save to emulated phase */
	if (fu_context_has_flag(self->ctx, FU_CONTEXT_FLAG_SAVE_EVENTS) && !any_emulated) {
		if (!fu_engine_backends_save_phase(self, error))
			return FALSE;
	}

	/* wait for any device to disconnect and reconnect */
	if (!fu_device_list_wait_for_replug(self->device_list, error)) {
		g_prefix_error(error, "failed to wait for composite prepare: ");
		return FALSE;
	}

	/* success */
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
	gboolean any_emulated = FALSE;

	/* we are emulating a device */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED))
			any_emulated = TRUE;
	}
	if (any_emulated) {
		if (!fu_engine_emulation_load_phase(self, error))
			return FALSE;
	}

	for (guint j = 0; j < plugins->len; j++) {
		FuPlugin *plugin_tmp = g_ptr_array_index(plugins, j);
		if (!fu_plugin_runner_composite_cleanup(plugin_tmp, devices, error))
			return FALSE;
	}

	/* save to emulated phase */
	if (fu_context_has_flag(self->ctx, FU_CONTEXT_FLAG_SAVE_EVENTS) && !any_emulated) {
		if (!fu_engine_backends_save_phase(self, error))
			return FALSE;
	}

	/* wait for any device to disconnect and reconnect */
	if (!fu_device_list_wait_for_replug(self->device_list, error)) {
		g_prefix_error(error, "failed to wait for composite cleanup: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gint
fu_engine_sort_release_device_order_release_version_cb(gconstpointer a, gconstpointer b)
{
	FuRelease *na = *((FuRelease **)a);
	FuRelease *nb = *((FuRelease **)b);
	return fu_release_compare(na, nb);
}

static gboolean
fu_engine_publish_release(FuEngine *self, FuRelease *release, GError **error)
{
#ifdef HAVE_PASSIM
	FuDevice *device = fu_release_get_device(release);
	GInputStream *stream = fu_release_get_stream(release);

	/* send to passimd, if enabled and running */
	if (passim_client_get_version(self->passim_client) != NULL &&
	    fu_engine_config_get_p2p_policy(self->config) & FU_P2P_POLICY_FIRMWARE) {
		gsize streamsz = 0;
		g_autofree gchar *basename = g_path_get_basename(fu_release_get_filename(release));
		g_autofree gchar *checksum = NULL;
		g_autoptr(GError) error_passim = NULL;
		g_autoptr(PassimItem) passim_item = passim_item_new();
		if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT) ||
		    fu_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN))
			passim_item_add_flag(passim_item, PASSIM_ITEM_FLAG_NEXT_REBOOT);
		passim_item_set_max_age(passim_item, 30 * 24 * 60 * 60);
		passim_item_set_share_limit(passim_item, 50);
		passim_item_set_basename(passim_item, basename);
		checksum = fu_input_stream_compute_checksum(stream, G_CHECKSUM_SHA256, error);
		if (checksum == NULL)
			return FALSE;
		if (!fu_input_stream_size(stream, &streamsz, error))
			return FALSE;
		passim_item_set_size(passim_item, streamsz);
		passim_item_set_stream(passim_item, stream);
		passim_item_set_hash(passim_item, checksum);
		if (!passim_client_publish(self->passim_client, passim_item, &error_passim)) {
			if (!g_error_matches(error_passim, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
				g_warning("failed to publish firmware to Passim: %s",
					  error_passim->message);
			}
		} else {
			g_debug("published %s to Passim", passim_item_get_hash(passim_item));
		}
	}
#endif
	/* success */
	return TRUE;
}

static gboolean
fu_engine_install_release_version_check(FuEngine *self,
					FuRelease *release,
					FuDevice *device,
					GError **error)
{
	FwupdVersionFormat fmt = fu_device_get_version_format(device);
	const gchar *version_rel = fu_release_get_version(release);
	const gchar *version_old = fu_release_get_device_version_old(release);
	if (version_rel != NULL && fu_version_compare(version_old, version_rel, fmt) != 0 &&
	    fu_version_compare(version_old, fu_device_get_version(device), fmt) == 0 &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT) &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN) &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION)) {
		fu_device_set_update_state(device, FWUPD_UPDATE_STATE_FAILED);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "device version not updated on success, %s != %s",
			    version_rel,
			    fu_device_get_version(device));
		return FALSE;
	}

	/* success */
	return TRUE;
}

/**
 * fu_engine_install_releases:
 * @self: a #FuEngine
 * @request: a #FuEngineRequest
 * @releases: (element-type FuRelease): a device
 * @cabinet: a #FuCabinet
 * @flags: install flags, e.g. %FWUPD_DEVICE_FLAG_UPDATABLE
 * @error: (nullable): optional return location for an error
 *
 * Installs a specific firmware file on one or more install tasks.
 *
 * By this point all the requirements and tests should have been done in
 * fu_engine_requirements_check() so this should not fail before running
 * the plugin loader.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_install_releases(FuEngine *self,
			   FuEngineRequest *request,
			   GPtrArray *releases,
			   FuCabinet *cabinet,
			   FuProgress *progress,
			   FwupdInstallFlags flags,
			   GError **error)
{
	g_autoptr(FuIdleLocker) locker = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) devices_new = NULL;

	/* do not allow auto-shutdown during this time */
	locker = fu_idle_locker_new(self->idle,
				    FU_IDLE_INHIBIT_TIMEOUT | FU_IDLE_INHIBIT_SIGNALS,
				    "update");
	g_return_val_if_fail(locker != NULL, FALSE);

	/* use an allow-list for device-changed signals -- only allow any of the composite update
	 * devices to emit signals for the duration of the install */
	for (guint i = 0; i < releases->len; i++) {
		FuRelease *release = g_ptr_array_index(releases, i);
		FuDevice *device = fu_release_get_device(release);
		g_hash_table_insert(self->device_changed_allowlist,
				    g_strdup(fu_device_get_id(device)),
				    GUINT_TO_POINTER(1));
	}

	/* install these in the right order */
	g_ptr_array_sort(releases, fu_engine_sort_release_device_order_release_version_cb);

	/* notify the plugins about the composite action */
	devices = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (guint i = 0; i < releases->len; i++) {
		FuRelease *release = g_ptr_array_index(releases, i);
		FuDevice *device = fu_release_get_device(release);
		const gchar *logical_id = fu_device_get_logical_id(device);
		g_info("composite update %u: %s %s->%s (%s, order:%i: priority:%u)",
		       i + 1,
		       fu_device_get_id(device),
		       fu_device_get_version(device),
		       fu_release_get_version(release),
		       logical_id != NULL ? logical_id : "n/a",
		       fu_device_get_order(device),
		       (guint)fu_release_get_priority(release));
		g_ptr_array_add(devices, g_object_ref(device));
	}
	fu_engine_set_install_phase(self, FU_ENGINE_INSTALL_PHASE_COMPOSITE_PREPARE);
	if (!fu_engine_composite_prepare(self, devices, error)) {
		g_prefix_error(error, "failed to prepare composite action: ");
		return FALSE;
	}

	/* all authenticated, so install all the things */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, releases->len);
	for (guint i = 0; i < releases->len; i++) {
		FuRelease *release = g_ptr_array_index(releases, i);
		GInputStream *stream = fu_release_get_stream(release);
		if (stream == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "no stream for release");
			return FALSE;
		}
		if (!fu_engine_install_release(self,
					       release,
					       stream,
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
			g_info("failed to find new device: %s", error_local->message);
			continue;
		}
		g_ptr_array_add(devices_new, g_steal_pointer(&device_new));
	}

	/* notify the plugins about the composite action */
	fu_engine_set_install_phase(self, FU_ENGINE_INSTALL_PHASE_COMPOSITE_CLEANUP);
	if (!fu_engine_composite_cleanup(self, devices_new, error)) {
		g_prefix_error(error, "failed to cleanup composite action: ");
		return FALSE;
	}

	/* for online updates, verify the version changed if not a re-install */
	for (guint i = 0; i < releases->len; i++) {
		FuRelease *release = g_ptr_array_index(releases, i);
		FuDevice *device = fu_release_get_device(release);
		g_autoptr(FuDevice) device_new = NULL;
		g_autoptr(GError) error_local = NULL;

		device_new = fu_device_list_get_by_id(self->device_list,
						      fu_device_get_id(device),
						      &error_local);
		if (device_new == NULL) {
			g_info("failed to find new device: %s", error_local->message);
			continue;
		}
		if (!fu_engine_install_release_version_check(self, release, device_new, error))
			return FALSE;
	}

	/* upload to Passim */
	for (guint i = 0; i < releases->len; i++) {
		FuRelease *release = g_ptr_array_index(releases, i);
		if (!fu_engine_publish_release(self, release, error))
			return FALSE;
	}

	/* allow capturing setup again */
	fu_engine_set_install_phase(self, FU_ENGINE_INSTALL_PHASE_SETUP);

	/* make the UI update */
	fu_engine_emit_changed(self);
	return TRUE;
}

static void
fu_engine_update_release_integrity(FuEngine *self, FuRelease *release, const gchar *key)
{
	g_autoptr(GHashTable) integrity = fu_engine_integrity_new(self->ctx, NULL);
	if (integrity != NULL) {
		g_autofree gchar *str = fu_engine_integrity_to_string(integrity);
		fu_release_add_metadata_item(release, key, str);
	}
}

static gboolean
fu_engine_add_release_metadata(FuEngine *self, FuRelease *release, GError **error)
{
	g_autoptr(GHashTable) metadata_device = NULL;
	g_autoptr(GHashTable) metadata_hash = NULL;

	/* build the version metadata */
	metadata_hash = fu_engine_get_report_metadata(self, error);
	if (metadata_hash == NULL)
		return FALSE;
	fu_release_add_metadata(release, metadata_hash);
	metadata_device = fu_device_report_metadata_pre(fu_release_get_device(release));
	if (metadata_device != NULL)
		fu_release_add_metadata(release, metadata_device);
	return TRUE;
}

static gboolean
fu_engine_add_release_plugin_metadata(FuEngine *self,
				      FuRelease *release,
				      FuPlugin *plugin,
				      GError **error)
{
	GPtrArray *metadata_sources;

	/* build the version metadata */
	if (fu_plugin_get_report_metadata(plugin) != NULL)
		fu_release_add_metadata(release, fu_plugin_get_report_metadata(plugin));

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
				g_debug("could not add metadata for %s: %s",
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

	/* measure the "old" system state */
	if (fu_plugin_has_flag(plugin, FWUPD_PLUGIN_FLAG_MEASURE_SYSTEM_INTEGRITY)) {
		fu_engine_update_release_integrity(self, release, "SystemIntegrityOld");
	}

	return TRUE;
}

static gboolean
fu_engine_save_into_backup_remote(FuEngine *self, GBytes *fw, GError **error)
{
	FwupdRemote *remote_tmp = fu_remote_list_get_by_id(self->remote_list, "backup");
	g_autofree gchar *localstatepkg = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR_PKG);
	g_autofree gchar *backupdir = g_build_filename(localstatepkg, "backup", NULL);
	g_autofree gchar *backupdir_uri = g_strdup_printf("file://%s", backupdir);
	g_autofree gchar *remotes_path = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR_REMOTES);
	g_autofree gchar *remotes_fn = g_build_filename(remotes_path, "backup.conf", NULL);
	g_autofree gchar *archive_checksum = g_compute_checksum_for_bytes(G_CHECKSUM_SHA256, fw);
	g_autofree gchar *archive_basename = g_strdup_printf("%s.cab", archive_checksum);
	g_autofree gchar *archive_fn = g_build_filename(backupdir, archive_basename, NULL);
	g_autoptr(FwupdRemote) remote = fwupd_remote_new();

	/* save archive if required */
	if (!g_file_test(archive_fn, G_FILE_TEST_EXISTS)) {
		g_info("saving archive to %s", archive_fn);
		if (!fu_bytes_set_contents(archive_fn, fw, error))
			return FALSE;
	}

	/* already exists as an enabled remote */
	if (remote_tmp != NULL && fwupd_remote_has_flag(remote_tmp, FWUPD_REMOTE_FLAG_ENABLED))
		return TRUE;

	/* just enable */
	if (remote_tmp != NULL) {
		g_info("enabling remote %s", fwupd_remote_get_id(remote_tmp));
		fwupd_remote_add_flag(remote_tmp, FWUPD_REMOTE_FLAG_ENABLED);
		return fu_remote_save_to_filename(remote_tmp, remotes_fn, NULL, error);
	}

	/* create a new remote we can use for re-installing */
	g_info("creating new backup remote");
	fwupd_remote_add_flag(remote, FWUPD_REMOTE_FLAG_ENABLED);
	fwupd_remote_set_title(remote, "Backup");
	fwupd_remote_set_metadata_uri(remote, backupdir_uri);
	return fu_remote_save_to_filename(remote, remotes_fn, NULL, error);
}

/**
 * fu_engine_install_release:
 * @self: a #FuEngine
 * @release: a #FuRelease
 * @stream: the #GInputStream of the .cab file
 * @progress: a #FuProgress
 * @flags: install flags, e.g. %FWUPD_INSTALL_FLAG_ALLOW_OLDER
 * @error: (nullable): optional return location for an error
 *
 * Installs a specific release on a device.
 *
 * By this point all the requirements and tests should have been done in
 * fu_engine_requirements_check() so this should not fail before running
 * the plugin loader.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_install_release(FuEngine *self,
			  FuRelease *release,
			  GInputStream *stream,
			  FuProgress *progress,
			  FwupdInstallFlags flags,
			  GError **error)
{
	FuDevice *device_orig = fu_release_get_device(release);
	FuEngineRequest *request = fu_release_get_request(release);
	FuPlugin *plugin;
	FwupdFeatureFlags feature_flags = FWUPD_FEATURE_FLAG_NONE;
	GInputStream *stream_fw;
	const gchar *tmp;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuDevice) device_tmp = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_ENGINE(self), FALSE);
	g_return_val_if_fail(FU_IS_RELEASE(release), FALSE);
	g_return_val_if_fail(FU_IS_PROGRESS(progress), FALSE);
	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* optional for tests */
	if (request != NULL)
		feature_flags = fu_engine_request_get_feature_flags(request);

	/* add the checksum of the container blob if not already set */
	if (fwupd_release_get_checksums(FWUPD_RELEASE(release))->len == 0) {
		GChecksumType checksum_types[] = {G_CHECKSUM_SHA256, G_CHECKSUM_SHA1, 0};
		for (guint i = 0; checksum_types[i] != 0; i++) {
			g_autofree gchar *checksum =
			    fu_input_stream_compute_checksum(stream, checksum_types[i], error);
			if (checksum == NULL)
				return FALSE;
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

	/* save to persistent storage so that the device can recover without a network */
	if (fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_SAVE_INTO_BACKUP_REMOTE)) {
		g_autoptr(GBytes) blob_cab =
		    fu_input_stream_read_bytes(stream, 0, G_MAXSIZE, error);
		if (blob_cab == NULL)
			return FALSE;
		if (!fu_engine_save_into_backup_remote(self, blob_cab, error))
			return FALSE;
	}

	/* set this for the callback */
	self->write_history = (flags & FWUPD_INSTALL_FLAG_NO_HISTORY) == 0;

	/* get per-release firmware blob */
	stream_fw = fu_release_get_stream(release);
	if (stream_fw == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "Failed to get firmware stream from release");
		return FALSE;
	}

	/* get the plugin */
	plugin =
	    fu_plugin_list_find_by_name(self->plugin_list, fu_device_get_plugin(device), error);
	if (plugin == NULL)
		return FALSE;

	/* add device to database */
	if ((flags & FWUPD_INSTALL_FLAG_NO_HISTORY) == 0) {
		if (!fu_engine_add_release_metadata(self, release, error))
			return FALSE;
		if (!fu_engine_add_release_plugin_metadata(self, release, plugin, error))
			return FALSE;
		if (!fu_history_add_device(self->history, device, release, error))
			return FALSE;
	}

	/* install firmware blob */
	if (!fu_engine_install_blob(self,
				    device,
				    stream_fw,
				    progress,
				    flags,
				    feature_flags,
				    &error_local)) {
		FwupdUpdateState state = fu_device_get_update_state(device);
		if (state != FWUPD_UPDATE_STATE_FAILED &&
		    state != FWUPD_UPDATE_STATE_FAILED_TRANSIENT)
			fu_device_set_update_state(device_orig, FWUPD_UPDATE_STATE_FAILED);
		else
			fu_device_set_update_state(device_orig, state);
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

	/* mark success unless needs a reboot */
	if (fu_device_get_update_state(device) != FWUPD_UPDATE_STATE_NEEDS_REBOOT)
		fu_device_set_update_state(device, FWUPD_UPDATE_STATE_SUCCESS);

	/* wait for the system to acquiesce if required */
	if (fu_device_get_acquiesce_delay(device_orig) > 0 &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED)) {
		fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_BUSY);
		fu_engine_wait_for_acquiesce(self, fu_device_get_acquiesce_delay(device_orig));
	}

	/* success */
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
 * fu_engine_get_plugin_by_name:
 * @self: a #FuPluginList
 * @name: a plugin name, e.g. `dfu`
 * @error: (nullable): optional return location for an error
 *
 * Gets a specific plugin.
 *
 * Returns: (transfer none): a plugin, or %NULL
 *
 * Since: 1.9.6
 **/
FuPlugin *
fu_engine_get_plugin_by_name(FuEngine *self, const gchar *name, GError **error)
{
	g_return_val_if_fail(FU_IS_ENGINE(self), NULL);
	return fu_plugin_list_find_by_name(self->plugin_list, name, error);
}

static gboolean
fu_engine_emulation_load_json_blob(FuEngine *self, GBytes *json_blob, GError **error)
{
	GPtrArray *backends = fu_context_get_backends(self->ctx);
	JsonNode *root;
	g_autoptr(JsonParser) parser = json_parser_new();

	/* parse */
	if (!json_parser_load_from_data(parser,
					g_bytes_get_data(json_blob, NULL),
					g_bytes_get_size(json_blob),
					error))
		return FALSE;

	/* load into all backends */
	root = json_parser_get_root(parser);
	for (guint i = 0; i < backends->len; i++) {
		FuBackend *backend = g_ptr_array_index(backends, i);
		if (!fwupd_codec_from_json(FWUPD_CODEC(backend), root, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_engine_emulation_load_phase(FuEngine *self, GError **error)
{
	GBytes *json_blob;
	const guint8 *buf;
	gsize bufsz = 0;

	json_blob =
	    g_hash_table_lookup(self->emulation_phases, GINT_TO_POINTER(self->install_phase));
	if (json_blob == NULL)
		return TRUE;

	/* show a truncated version to the console */
	buf = g_bytes_get_data(json_blob, &bufsz);
	if (bufsz > 0) {
		g_autofree gchar *json_truncated =
		    g_strndup((const gchar *)buf, MIN(bufsz, 0x2000));
		g_info("loading phase %s: %s",
		       fu_engine_install_phase_to_string(self->install_phase),
		       json_truncated);
	}

	return fu_engine_emulation_load_json_blob(self, json_blob, error);
}

gboolean
fu_engine_emulation_load(FuEngine *self, GInputStream *stream, GError **error)
{
	gboolean got_json = FALSE;
	const gchar *json_empty = "{\"UsbDevices\":[]}";
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GBytes) json_blob = g_bytes_new_static(json_empty, strlen(json_empty));

	g_return_val_if_fail(FU_IS_ENGINE(self), FALSE);
	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* not supported */
	if (!fu_engine_config_get_allow_emulation(self->config)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "emulation is not allowed from config");
		return FALSE;
	}

	/* unload any existing devices */
	if (!fu_engine_emulation_load_json_blob(self, json_blob, error))
		return FALSE;

	/* load archive */
	archive = fu_archive_new_stream(stream, FU_ARCHIVE_FLAG_NONE, error);
	if (archive == NULL)
		return FALSE;

	/* load JSON files from archive */
	g_hash_table_remove_all(self->emulation_phases);
	for (guint phase = FU_ENGINE_INSTALL_PHASE_SETUP; phase < FU_ENGINE_INSTALL_PHASE_LAST;
	     phase++) {
		g_autofree gchar *fn =
		    g_strdup_printf("%s.json", fu_engine_install_phase_to_string(phase));
		g_autoptr(GBytes) blob = NULL;

		/* not found */
		blob = fu_archive_lookup_by_fn(archive, fn, NULL);
		if (blob == NULL)
			continue;
		got_json = TRUE;
		g_info("got emulation for phase %s", fu_engine_install_phase_to_string(phase));
		if (phase == FU_ENGINE_INSTALL_PHASE_SETUP) {
			if (!fu_engine_emulation_load_json_blob(self, blob, error))
				return FALSE;
		} else {
			g_hash_table_insert(self->emulation_phases,
					    GINT_TO_POINTER(phase),
					    g_steal_pointer(&blob));
		}
	}
	if (!got_json) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no emulation data found in archive");
		return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
fu_engine_emulation_save(FuEngine *self, GOutputStream *stream, GError **error)
{
	gboolean got_json = FALSE;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuArchive) archive = fu_archive_new(NULL, FU_ARCHIVE_FLAG_NONE, NULL);

	g_return_val_if_fail(FU_IS_ENGINE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* not supported */
	if (!fu_engine_config_get_allow_emulation(self->config)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "emulation is not allowed from config");
		return FALSE;
	}

	/* sanity check */
	for (guint phase = FU_ENGINE_INSTALL_PHASE_SETUP; phase < FU_ENGINE_INSTALL_PHASE_LAST;
	     phase++) {
		const gchar *json =
		    g_hash_table_lookup(self->emulation_phases, GINT_TO_POINTER(phase));
		g_autofree gchar *fn =
		    g_strdup_printf("%s.json", fu_engine_install_phase_to_string(phase));
		g_autoptr(GBytes) blob = NULL;

		/* nothing set */
		if (json == NULL)
			continue;
		got_json = TRUE;
		blob = g_bytes_new_static(json, strlen(json));
		fu_archive_add_entry(archive, fn, blob);
	}
	if (!got_json) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no emulation data, perhaps no devices have been added?");
		return FALSE;
	}

	/* write  */
	buf = fu_archive_write(archive, FU_ARCHIVE_FORMAT_ZIP, FU_ARCHIVE_COMPRESSION_GZIP, error);
	if (buf == NULL)
		return FALSE;
	if (!g_output_stream_write_all(stream, buf->data, buf->len, NULL, NULL, error)) {
		fu_error_convert(error);
		return FALSE;
	}
	if (!g_output_stream_flush(stream, NULL, error)) {
		fu_error_convert(error);
		return FALSE;
	}

	/* success */
	g_hash_table_remove_all(self->emulation_phases);
	return TRUE;
}

static void
fu_engine_backends_to_json(FuEngine *self, JsonBuilder *json_builder)
{
	g_autoptr(GPtrArray) devices = fu_device_list_get_active(self->device_list);

	/* not always correct, but we want to remain compatible with all the old emulation files */
	json_builder_begin_object(json_builder);
	json_builder_set_member_name(json_builder, "UsbDevices");
	json_builder_begin_array(json_builder);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);

		/* interesting? */
		if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATION_TAG))
			continue;
		json_builder_begin_object(json_builder);
		fwupd_codec_to_json(FWUPD_CODEC(device), json_builder, FWUPD_CODEC_FLAG_NONE);
		json_builder_end_object(json_builder);
	}
	json_builder_end_array(json_builder);
	json_builder_end_object(json_builder);

	/* we've recorded these, now drop them */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATION_TAG))
			continue;
		fu_device_clear_events(device);
	}
}

static gboolean
fu_engine_backends_save_phase(FuEngine *self, GError **error)
{
	const gchar *data_old;
	g_autofree gchar *data_new = NULL;
	g_autofree gchar *data_new_safe = NULL;
	g_autoptr(JsonBuilder) json_builder = json_builder_new();
	g_autoptr(JsonGenerator) json_generator = NULL;
	g_autoptr(JsonNode) json_root = NULL;

	/* all devices in all backends */
	fu_engine_backends_to_json(self, json_builder);

	json_root = json_builder_get_root(json_builder);
	json_generator = json_generator_new();
	json_generator_set_pretty(json_generator, TRUE);
	json_generator_set_root(json_generator, json_root);

	data_old =
	    g_hash_table_lookup(self->emulation_phases, GINT_TO_POINTER(self->install_phase));
	data_new = json_generator_to_data(json_generator, NULL);
	if (g_strcmp0(data_new, "") == 0) {
		g_info("no data for phase %s",
		       fu_engine_install_phase_to_string(self->install_phase));
		return TRUE;
	}
	if (g_strcmp0(data_old, data_new) == 0) {
		g_info("JSON unchanged for phase %s",
		       fu_engine_install_phase_to_string(self->install_phase));
		return TRUE;
	}
	data_new_safe = g_strndup(data_new, 8000);
	g_info("JSON %s for phase %s: %s...",
	       data_old == NULL ? "added" : "changed",
	       fu_engine_install_phase_to_string(self->install_phase),
	       data_new_safe);
	g_hash_table_insert(self->emulation_phases,
			    GINT_TO_POINTER(self->install_phase),
			    g_steal_pointer(&data_new));

	/* success */
	return TRUE;
}

/**
 * fu_engine_get_device:
 * @self: a #FuEngine
 * @device_id: a device ID
 * @error: (nullable): optional return location for an error
 *
 * Gets a specific device, optionally loading an emulated phase.
 *
 * Returns: (transfer full): a device, or %NULL if not found
 **/
FuDevice *
fu_engine_get_device(FuEngine *self, const gchar *device_id, GError **error)
{
	g_autoptr(FuDevice) device = NULL;

	/* we are emulating a device */
	if (self->install_phase != FU_ENGINE_INSTALL_PHASE_SETUP) {
		g_autoptr(FuDevice) device_old = NULL;
		device_old = fu_device_list_get_by_id(self->device_list, device_id, NULL);
		if (device_old != NULL &&
		    fu_device_has_flag(device_old, FWUPD_DEVICE_FLAG_EMULATED)) {
			if (!fu_engine_emulation_load_phase(self, error))
				return NULL;
		}
	}

	/* wait for any device to disconnect and reconnect */
	if (!fu_device_list_wait_for_replug(self->device_list, error)) {
		g_prefix_error(error, "failed to wait for device: ");
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
		g_info("skipping device cleanup due to will-disappear flag");
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
	if (fu_engine_config_get_ignore_power(self->config))
		return TRUE;

	/* not charging */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_REQUIRE_AC) &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED) &&
	    !fu_power_state_is_ac(fu_context_get_power_state(self->ctx))) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_AC_POWER_REQUIRED,
				    "Cannot install update "
				    "when not on AC power unless forced");
		return FALSE;
	}

	/* not enough just in case */
	if (!fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_IGNORE_SYSTEM_POWER) &&
	    fu_context_get_battery_level(self->ctx) != FWUPD_BATTERY_LEVEL_INVALID &&
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
	fu_device_add_problem(device, FWUPD_DEVICE_PROBLEM_UPDATE_IN_PROGRESS);

	if (!fu_engine_device_check_power(self, device, flags, error))
		return FALSE;

	str = fu_device_to_string(device);
	g_info("prepare -> %s", str);
	if (!fu_engine_device_prepare(self, device, progress, flags, error))
		return FALSE;
	for (guint j = 0; j < plugins->len; j++) {
		FuPlugin *plugin_tmp = g_ptr_array_index(plugins, j);
		if (!fu_plugin_runner_prepare(plugin_tmp, device, progress, flags, error))
			return FALSE;
	}

	/* save to emulated phase */
	if (fu_context_has_flag(self->ctx, FU_CONTEXT_FLAG_SAVE_EVENTS) &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED)) {
		if (!fu_engine_backends_save_phase(self, error))
			return FALSE;
	}

	/* wait for any device to disconnect and reconnect */
	if (!fu_device_list_wait_for_replug(self->device_list, error)) {
		g_prefix_error(error, "failed to wait for prepare replug: ");
		return FALSE;
	}

	/* success */
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
	fu_device_remove_problem(device, FWUPD_DEVICE_PROBLEM_UPDATE_IN_PROGRESS);
	str = fu_device_to_string(device);
	g_info("cleanup -> %s", str);
	if (!fu_engine_device_cleanup(self, device, progress, flags, error))
		return FALSE;
	for (guint j = 0; j < plugins->len; j++) {
		FuPlugin *plugin_tmp = g_ptr_array_index(plugins, j);
		if (!fu_plugin_runner_cleanup(plugin_tmp, device, progress, flags, error))
			return FALSE;
	}

	/* save to emulated phase */
	if (fu_context_has_flag(self->ctx, FU_CONTEXT_FLAG_SAVE_EVENTS) &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED)) {
		if (!fu_engine_backends_save_phase(self, error))
			return FALSE;
	}

	/* wait for any device to disconnect and reconnect */
	if (!fu_device_list_wait_for_replug(self->device_list, error)) {
		g_prefix_error(error, "failed to wait for cleanup replug: ");
		return FALSE;
	}

	/* success */
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
	g_autoptr(FuDeviceProgress) device_progress = NULL;

	/* the device and plugin both may have changed */
	device = fu_engine_get_device(self, device_id, error);
	if (device == NULL) {
		g_prefix_error(error, "failed to get device before update detach: ");
		return FALSE;
	}
	device_progress = fu_device_progress_new(device, progress);
	g_return_val_if_fail(device_progress != NULL, FALSE);

	/* pause the polling */
	poll_locker = fu_device_poll_locker_new(device, error);
	if (poll_locker == NULL)
		return FALSE;

	str = fu_device_to_string(device);
	g_info("detach -> %s", str);
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

	/* save to emulated phase */
	if (fu_context_has_flag(self->ctx, FU_CONTEXT_FLAG_SAVE_EVENTS) &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED)) {
		if (!fu_engine_backends_save_phase(self, error))
			return FALSE;
	}

	/* wait for any device to disconnect and reconnect */
	if (!fu_device_list_wait_for_replug(self->device_list, error)) {
		g_prefix_error(error, "failed to wait for detach replug: ");
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
	g_autoptr(FuDeviceProgress) device_progress = NULL;

	/* the device and plugin both may have changed */
	device = fu_engine_get_device(self, device_id, error);
	if (device == NULL) {
		g_prefix_error(error, "failed to get device before update attach: ");
		return FALSE;
	}
	device_progress = fu_device_progress_new(device, progress);
	g_return_val_if_fail(device_progress != NULL, FALSE);

	str = fu_device_to_string(device);
	g_info("attach -> %s", str);
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

	/* save to emulated phase */
	if (fu_context_has_flag(self->ctx, FU_CONTEXT_FLAG_SAVE_EVENTS) &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED)) {
		if (!fu_engine_backends_save_phase(self, error))
			return FALSE;
	}

	/* wait for any device to disconnect and reconnect */
	if (!fu_device_list_wait_for_replug(self->device_list, error)) {
		g_prefix_error(error, "failed to wait for attach replug: ");
		return FALSE;
	}

	/* success */
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
	g_info("activate -> %s", str);
	plugin =
	    fu_plugin_list_find_by_name(self->plugin_list, fu_device_get_plugin(device), error);
	if (plugin == NULL)
		return FALSE;
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
	g_info("reload -> %s", str);
	plugin =
	    fu_plugin_list_find_by_name(self->plugin_list, fu_device_get_plugin(device), error);
	if (plugin == NULL)
		return FALSE;

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_WILL_DISAPPEAR)) {
		g_info("skipping reload due to will-disappear flag");
		return TRUE;
	}

	if (!fu_plugin_runner_reload(plugin, device, error)) {
		g_prefix_error(error, "failed to reload device: ");
		return FALSE;
	}

	/* save to emulated phase */
	if (fu_context_has_flag(self->ctx, FU_CONTEXT_FLAG_SAVE_EVENTS) &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED)) {
		if (!fu_engine_backends_save_phase(self, error))
			return FALSE;
	}

	/* wait for any device to disconnect and reconnect */
	if (!fu_device_list_wait_for_replug(self->device_list, error)) {
		g_prefix_error(error, "failed to wait for reload replug: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_engine_write_firmware(FuEngine *self,
			 const gchar *device_id,
			 GInputStream *stream_fw,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuPlugin *plugin;
	g_autofree gchar *str = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuDeviceLocker) poll_locker = NULL;
	g_autoptr(FuDeviceProgress) device_progress = NULL;
	g_autoptr(GError) error_write = NULL;

	/* the device and plugin both may have changed */
	device = fu_engine_get_device(self, device_id, error);
	if (device == NULL) {
		g_prefix_error(error, "failed to get device before update: ");
		return FALSE;
	}
	device_progress = fu_device_progress_new(device, progress);
	g_return_val_if_fail(device_progress != NULL, FALSE);

	/* pause the polling */
	poll_locker = fu_device_poll_locker_new(device, error);
	if (poll_locker == NULL)
		return FALSE;

	str = fu_device_to_string(device);
	g_info("update -> %s", str);
	plugin =
	    fu_plugin_list_find_by_name(self->plugin_list, fu_device_get_plugin(device), error);
	if (plugin == NULL)
		return FALSE;
	if (!fu_plugin_runner_write_firmware(plugin,
					     device,
					     stream_fw,
					     progress,
					     flags,
					     &error_write)) {
		g_autofree gchar *str_write = NULL;
		g_autoptr(GError) error_attach = NULL;
		g_autoptr(GError) error_cleanup = NULL;

		if (g_error_matches(error_write, FWUPD_ERROR, FWUPD_ERROR_AC_POWER_REQUIRED) ||
		    g_error_matches(error_write, FWUPD_ERROR, FWUPD_ERROR_BATTERY_LEVEL_TOO_LOW) ||
		    g_error_matches(error_write, FWUPD_ERROR, FWUPD_ERROR_NEEDS_USER_ACTION) ||
		    g_error_matches(error_write, FWUPD_ERROR, FWUPD_ERROR_BROKEN_SYSTEM)) {
			fu_device_set_update_state(device, FWUPD_UPDATE_STATE_FAILED_TRANSIENT);
		} else {
			fu_device_set_update_state(device, FWUPD_UPDATE_STATE_FAILED);
		}

		/* this is really helpful for debugging, as we want to dump the device *before*
		 * we run cleanup */
		str_write = fu_device_to_string(device);
		g_debug("failed write-firmware '%s': %s", error_write->message, str_write);

		/* attach back into runtime then cleanup */
		if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED)) {
			fu_engine_set_install_phase(self, FU_ENGINE_INSTALL_PHASE_ATTACH);
			fu_progress_reset(progress);
			if (!fu_plugin_runner_attach(plugin, device, progress, &error_attach)) {
				g_warning("failed to attach device after failed update: %s",
					  error_attach->message);
			}
			fu_engine_set_install_phase(self, FU_ENGINE_INSTALL_PHASE_CLEANUP);
			fu_progress_reset(progress);
			if (!fu_engine_cleanup(self, device_id, progress, flags, &error_cleanup)) {
				g_warning("failed to update-cleanup after failed update: %s",
					  error_cleanup->message);
			}
		}

		/* return error to client */
		g_propagate_error(error, g_steal_pointer(&error_write));
		return FALSE;
	}

	/* save to emulated phase */
	if (fu_context_has_flag(self->ctx, FU_CONTEXT_FLAG_SAVE_EVENTS) &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED)) {
		if (!fu_engine_backends_save_phase(self, error))
			return FALSE;
	}

	/* wait for any device to disconnect and reconnect */
	if (!fu_device_list_wait_for_replug(self->device_list, error)) {
		g_prefix_error(error, "failed to wait for write-firmware replug: ");
		return FALSE;
	}

	/* success */
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
		       GInputStream *stream_fw,
		       FuProgress *progress,
		       FwupdInstallFlags flags,
		       FwupdFeatureFlags feature_flags,
		       GError **error)
{
	guint retries = 0;
	gsize streamsz = 0;
	g_autofree gchar *device_id = NULL;
	g_autoptr(GTimer) timer = g_timer_new();
	g_autoptr(FuDeviceProgress) device_progress = fu_device_progress_new(device, progress);

	g_return_val_if_fail(device_progress != NULL, FALSE);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_NO_PROFILE);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "prepare");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "cleanup");

	/* test the firmware is not an empty blob */
	if (!fu_input_stream_size(stream_fw, &streamsz, error))
		return FALSE;
	if (streamsz == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "Firmware is invalid as has zero size");
		return FALSE;
	}

	/* mark this as modified even if we actually fail to do the update */
	fu_device_set_modified_usec(device, g_get_real_time());

	/* signal to all the plugins the update is about to happen */
	device_id = g_strdup(fu_device_get_id(device));
	fu_engine_set_install_phase(self, FU_ENGINE_INSTALL_PHASE_PREPARE);
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
		fu_engine_set_install_phase(self, FU_ENGINE_INSTALL_PHASE_DETACH);
		if (!fu_engine_detach(self,
				      device_id,
				      fu_progress_get_child(progress_local),
				      feature_flags,
				      error)) {
			g_prefix_error(error, "failed to detach: ");
			return FALSE;
		}
		fu_progress_step_done(progress_local);

		/* install */
		fu_engine_set_install_phase(self, FU_ENGINE_INSTALL_PHASE_INSTALL);
		if (!fu_engine_write_firmware(self,
					      device_id,
					      stream_fw,
					      fu_progress_get_child(progress_local),
					      flags,
					      error)) {
			g_prefix_error(error, "failed to write-firmware: ");
			return FALSE;
		}
		fu_progress_step_done(progress_local);

		/* attach into runtime mode */
		fu_engine_set_install_phase(self, FU_ENGINE_INSTALL_PHASE_ATTACH);
		if (!fu_engine_attach(self,
				      device_id,
				      fu_progress_get_child(progress_local),
				      error)) {
			g_prefix_error(error, "failed to attach: ");
			return FALSE;
		}
		fu_progress_step_done(progress_local);

		/* get the new version number */
		fu_engine_set_install_phase(self, FU_ENGINE_INSTALL_PHASE_RELOAD);
		if (!fu_engine_reload(self, device_id, error)) {
			g_prefix_error(error, "failed to reload: ");
			return FALSE;
		}
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

	/* update history database */
	fu_device_set_update_state(device, FWUPD_UPDATE_STATE_SUCCESS);
	fu_device_set_install_duration(device, g_timer_elapsed(timer, NULL));
	if ((flags & FWUPD_INSTALL_FLAG_NO_HISTORY) == 0) {
		if (!fu_history_modify_device(self->history, device, error)) {
			g_prefix_error(error, "failed to set success: ");
			return FALSE;
		}
	}

	/* signal to all the plugins the update has happened */
	fu_engine_set_install_phase(self, FU_ENGINE_INSTALL_PHASE_CLEANUP);
	if (!fu_engine_cleanup(self, device_id, fu_progress_get_child(progress), flags, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* make the UI update */
	fu_engine_emit_device_changed(self, device_id);
	g_info("Updating %s took %f seconds",
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
	g_autoptr(GError) error_container_checksum1 = NULL;
	g_autoptr(GError) error_container_checksum2 = NULL;
	g_autoptr(GError) error_tag_by_guid_version = NULL;

	/* print what we've got */
	components = xb_silo_query(self->silo, "components/component[@type='firmware']", 0, NULL);
	if (components == NULL)
		return TRUE;
	g_info("%u components now in silo", components->len);

	/* clear old prepared queries */
	g_clear_object(&self->query_component_by_guid);
	g_clear_object(&self->query_container_checksum1);
	g_clear_object(&self->query_container_checksum2);
	g_clear_object(&self->query_tag_by_guid_version);

	/* build the index */
	if (!xb_silo_query_build_index(self->silo, "components/component", "type", error))
		return FALSE;
	if (!xb_silo_query_build_index(self->silo,
				       "components/component[@type='firmware']/provides/firmware",
				       "type",
				       error))
		return FALSE;
	if (!xb_silo_query_build_index(self->silo,
				       "components/component/provides/firmware",
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
			      "components/component/provides/firmware[@type=$'flashed'][text()=?]/"
			      "../..",
			      XB_QUERY_FLAG_OPTIMIZE,
			      error);
	if (self->query_component_by_guid == NULL) {
		g_prefix_error(error, "failed to prepare query: ");
		return FALSE;
	}

	/* old-style <checksum target="container"> and new-style <artifact> */
	self->query_container_checksum1 =
	    xb_query_new_full(self->silo,
			      "components/component[@type='firmware']/releases/release/"
			      "checksum[@target='container'][text()=?]/..",
			      XB_QUERY_FLAG_OPTIMIZE,
			      &error_container_checksum1);
	if (self->query_container_checksum1 == NULL)
		g_debug("ignoring prepared query: %s", error_container_checksum1->message);
	self->query_container_checksum2 =
	    xb_query_new_full(self->silo,
			      "components/component[@type='firmware']/releases/release/"
			      "artifacts/artifact[@type='binary']/checksum[text()=?]/"
			      "../../..",
			      XB_QUERY_FLAG_OPTIMIZE,
			      &error_container_checksum2);
	if (self->query_container_checksum2 == NULL)
		g_debug("ignoring prepared query: %s", error_container_checksum2->message);

	/* prepare tag query with bound GUID parameter */
	self->query_tag_by_guid_version =
	    xb_query_new_full(self->silo,
			      "local/components/component[@merge='append']/provides/"
			      "firmware[text()=?]/../../releases/release[@version=?]/../../"
			      "tags/tag",
			      XB_QUERY_FLAG_OPTIMIZE,
			      &error_tag_by_guid_version);
	if (self->query_tag_by_guid_version == NULL)
		g_debug("ignoring prepared query: %s", error_tag_by_guid_version->message);

	/* success */
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

static GInputStream *
fu_engine_builder_cabinet_adapter_cb(XbBuilderSource *source,
				     XbBuilderSourceCtx *ctx,
				     gpointer user_data,
				     GCancellable *cancellable,
				     GError **error)
{
	FuEngine *self = FU_ENGINE(user_data);
	GInputStream *stream = xb_builder_source_ctx_get_stream(ctx);
	g_autoptr(FuCabinet) cabinet = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autofree gchar *xml = NULL;

	/* convert the CAB into metadata XML */
	cabinet = fu_engine_build_cabinet_from_stream(self, stream, error);
	if (cabinet == NULL)
		return NULL;
	silo = fu_cabinet_get_silo(cabinet, error);
	if (silo == NULL)
		return NULL;
	xml = xb_silo_export(silo, XB_NODE_EXPORT_FLAG_NONE, error);
	if (xml == NULL)
		return NULL;
	return g_memory_input_stream_new_from_data(g_steal_pointer(&xml), -1, g_free);
}

static XbBuilderSource *
fu_engine_create_metadata_builder_source(FuEngine *self, const gchar *fn, GError **error)
{
	g_autoptr(GFile) file = g_file_new_for_path(fn);
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();

	g_info("using %s as metadata source", fn);
	xb_builder_source_add_simple_adapter(source,
					     "application/vnd.ms-cab-compressed,"
					     "com.microsoft.cab,"
					     ".cab,"
					     "application/octet-stream",
					     fu_engine_builder_cabinet_adapter_cb,
					     self,
					     NULL);
	if (!xb_builder_source_load_file(source,
					 file,
					 XB_BUILDER_SOURCE_FLAG_WATCH_FILE |
					     XB_BUILDER_SOURCE_FLAG_WATCH_DIRECTORY,
					 NULL,
					 error))
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
	if (path == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no filename cache for %s",
			    fwupd_remote_get_id(remote));
		return FALSE;
	}
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
			g_info("ignoring: %s", fn);
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
	gboolean update_pending = FALSE;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(FuEngineRequest) request = NULL;

	/* all flags set */
	request = fu_engine_request_new(NULL);
	fu_engine_request_add_flag(request, FU_ENGINE_REQUEST_FLAG_NO_REQUIREMENTS);
	fu_engine_request_add_flag(request, FU_ENGINE_REQUEST_FLAG_ANY_RELEASE);
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
		for (guint i = 0; i < releases->len; i++) {
			FuRelease *release = FU_RELEASE(g_ptr_array_index(releases, i));
			if (fu_release_has_flag(release, FWUPD_RELEASE_FLAG_IS_UPGRADE)) {
				update_pending = TRUE;
				break;
			}
		}
		if (update_pending) {
			fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_UPDATE_PENDING);
		} else {
			fu_device_remove_private_flag(device,
						      FU_DEVICE_PRIVATE_FLAG_UPDATE_PENDING);
		}
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
fu_engine_md_refresh_devices(FuEngine *self)
{
	g_autoptr(GPtrArray) devices = fu_device_list_get_active(self->device_list);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		g_autoptr(XbNode) component = fu_engine_get_component_by_guids(self, device);

		/* set or clear the SUPPORTED flag */
		fu_engine_ensure_device_supported(self, device);

		/* fixup the name and format as needed */
		if (component != NULL &&
		    !fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_ONLY_CHECKSUM))
			fu_device_ensure_from_component(device, component);
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
		g_info("ignoring: %s", error_local->message);
		return TRUE;
	}
	for (guint i = 0; i < metadata_fns->len; i++) {
		const gchar *path = g_ptr_array_index(metadata_fns, i);
		g_autoptr(XbBuilderSource) source = xb_builder_source_new();
		g_autoptr(GFile) file = g_file_new_for_path(path);
		g_info("loading local metadata: %s", path);
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

	/* invalidate the cache if the fwupd version changes */
	xb_builder_append_guid(builder, SOURCE_VERSION);

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
		if (!fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_ENABLED))
			continue;
		path = fwupd_remote_get_filename_cache(remote);
		if (!g_file_test(path, G_FILE_TEST_EXISTS))
			continue;

		/* generate all metadata on demand */
		if (fwupd_remote_get_kind(remote) == FWUPD_REMOTE_KIND_DIRECTORY) {
			g_info("loading metadata for remote '%s'", fwupd_remote_get_id(remote));
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
fu_engine_remote_list_ensure_p2p_policy_remote(FuEngine *self, FwupdRemote *remote)
{
	if (fwupd_remote_get_kind(remote) == FWUPD_REMOTE_KIND_DOWNLOAD) {
		FuP2pPolicy p2p_policy = fu_engine_config_get_p2p_policy(self->config);
		if (p2p_policy & FU_P2P_POLICY_METADATA)
			fwupd_remote_add_flag(remote, FWUPD_REMOTE_FLAG_ALLOW_P2P_METADATA);
		else
			fwupd_remote_remove_flag(remote, FWUPD_REMOTE_FLAG_ALLOW_P2P_METADATA);
		if (p2p_policy & FU_P2P_POLICY_FIRMWARE)
			fwupd_remote_add_flag(remote, FWUPD_REMOTE_FLAG_ALLOW_P2P_FIRMWARE);
		else
			fwupd_remote_remove_flag(remote, FWUPD_REMOTE_FLAG_ALLOW_P2P_FIRMWARE);
	}
}

static void
fu_engine_config_changed_cb(FuEngineConfig *config, FuEngine *self)
{
	GPtrArray *remotes = fu_remote_list_get_all(self->remote_list);

	fu_idle_set_timeout(self->idle, fu_engine_config_get_idle_timeout(config));

	/* allow changing the hardcoded ESP location */
	if (fu_engine_config_get_esp_location(config) != NULL)
		fu_context_set_esp_location(self->ctx, fu_engine_config_get_esp_location(config));

	/* amend P2P policy */
	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index(remotes, i);
		fu_engine_remote_list_ensure_p2p_policy_remote(self, remote);
	}
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

static void
fu_engine_remote_list_added_cb(FuRemoteList *remote_list, FwupdRemote *remote, FuEngine *self)
{
	FuReleasePriority priority = fu_engine_config_get_release_priority(self->config);
	if (priority == FU_RELEASE_PRIORITY_LOCAL &&
	    fwupd_remote_get_kind(remote) != FWUPD_REMOTE_KIND_DOWNLOAD) {
		g_debug("priority local and %s is not download remote, so bumping",
			fwupd_remote_get_id(remote));
		fwupd_remote_set_priority(remote, fwupd_remote_get_priority(remote) + 1000);
	} else if (priority == FU_RELEASE_PRIORITY_REMOTE &&
		   fwupd_remote_get_kind(remote) == FWUPD_REMOTE_KIND_DOWNLOAD) {
		g_debug("priority remote and %s is download remote, so bumping",
			fwupd_remote_get_id(remote));
		fwupd_remote_set_priority(remote, fwupd_remote_get_priority(remote) + 1000);
	}

	/* set the p2p policy */
	fu_engine_remote_list_ensure_p2p_policy_remote(self, remote);
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
	g_autoptr(GInputStream) istream = NULL;
	g_autoptr(GPtrArray) results = NULL;
	g_autoptr(JcatItem) jcat_item = NULL;
	g_autoptr(JcatFile) jcat_file = jcat_file_new();

	blob = fu_bytes_get_contents(fwupd_remote_get_filename_cache(remote), error);
	if (blob == NULL)
		return NULL;
	istream = fu_input_stream_from_path(fwupd_remote_get_filename_cache_sig(remote), error);
	if (istream == NULL)
		return NULL;
	if (!jcat_file_import_stream(jcat_file, istream, JCAT_IMPORT_FLAG_NONE, NULL, error)) {
		fu_error_convert(error);
		return NULL;
	}
	jcat_item = jcat_file_get_item_default(jcat_file, error);
	if (jcat_item == NULL) {
		fu_error_convert(error);
		return NULL;
	}
	results = jcat_context_verify_item(self->jcat_context,
					   blob,
					   jcat_item,
					   JCAT_VERIFY_FLAG_REQUIRE_CHECKSUM |
					       JCAT_VERIFY_FLAG_REQUIRE_SIGNATURE,
					   error);
	if (results == NULL) {
		fu_error_convert(error);
		return NULL;
	}

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
		g_info("timestamp increased, so no rollback");
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
	FwupdRemote *remote;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GInputStream) istream = NULL;
	g_autoptr(GPtrArray) results = NULL;
	g_autoptr(JcatFile) jcat_file = jcat_file_new();
	g_autoptr(JcatItem) jcat_item = NULL;
	g_autoptr(JcatResult) jcat_result = NULL;
	g_autoptr(JcatResult) jcat_result_old = NULL;

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
	if (!fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_ENABLED)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "remote %s not enabled",
			    remote_id);
		return FALSE;
	}

	/* verify JCatFile, or create a dummy one from legacy data */
	istream = g_memory_input_stream_new_from_bytes(bytes_sig);
	if (!jcat_file_import_stream(jcat_file, istream, JCAT_IMPORT_FLAG_NONE, NULL, error))
		return FALSE;

	/* this should only be signing one thing */
	jcat_item = jcat_file_get_item_default(jcat_file, error);
	if (jcat_item == NULL)
		return FALSE;
	results = jcat_context_verify_item(self->jcat_context,
					   bytes_raw,
					   jcat_item,
					   JCAT_VERIFY_FLAG_REQUIRE_SIGNATURE |
					       JCAT_VERIFY_FLAG_REQUIRE_CHECKSUM,
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
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE)) {
			g_info("no existing valid keyrings: %s", error_local->message);
		} else {
			g_warning("could not get existing keyring result: %s",
				  error_local->message);
		}
	} else {
		if (!fu_engine_validate_result_timestamp(jcat_result, jcat_result_old, error))
			return FALSE;
	}

	/* save XML and signature to remotes.d */
	if (!fu_bytes_set_contents(fwupd_remote_get_filename_cache(remote), bytes_raw, error))
		return FALSE;

#ifdef HAVE_PASSIM
	/* send to passimd, if enabled and running */
	if (passim_client_get_version(self->passim_client) != NULL &&
	    fwupd_remote_get_username(remote) == NULL &&
	    fwupd_remote_get_password(remote) == NULL &&
	    fu_engine_config_get_p2p_policy(self->config) & FU_P2P_POLICY_METADATA) {
		g_autofree gchar *basename =
		    g_path_get_basename(fwupd_remote_get_filename_cache(remote));
		g_autoptr(GError) error_passim = NULL;
		g_autoptr(PassimItem) passim_item = passim_item_new();
		passim_item_set_basename(passim_item, basename);
		passim_item_set_bytes(passim_item, bytes_raw);
		passim_item_set_max_age(passim_item, fwupd_remote_get_refresh_interval(remote));
		passim_item_set_share_limit(passim_item, 50);
		if (!passim_client_publish(self->passim_client, passim_item, &error_passim)) {
			if (!g_error_matches(error_passim, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
				g_warning("failed to publish metadata to Passim: %s",
					  error_passim->message);
			}
		} else {
			g_debug("published %s to Passim", passim_item_get_hash(passim_item));
		}
	}
#endif

	/* save signature to remotes.d */
	if (!fu_bytes_set_contents(fwupd_remote_get_filename_cache_sig(remote), bytes_sig, error))
		return FALSE;
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
	stream_fd = fu_unix_seekable_input_stream_new(fd, TRUE);
	stream_sig = fu_unix_seekable_input_stream_new(fd_sig, TRUE);

	/* read the entire file into memory */
	bytes_raw = fu_input_stream_read_bytes(stream_fd, 0, FU_ENGINE_MAX_METADATA_SIZE, error);
	if (bytes_raw == NULL)
		return FALSE;

	/* read signature */
	bytes_sig = fu_input_stream_read_bytes(stream_sig, 0, FU_ENGINE_MAX_SIGNATURE_SIZE, error);
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
 * fu_engine_build_cabinet_from_stream:
 * @self: a #FuEngine
 * @stream: a #GInputStream
 * @error: (nullable): optional return location for an error
 *
 * Creates a silo from a .cab file blob.
 *
 * Returns: (transfer container): a #XbSilo, or %NULL
 **/
FuCabinet *
fu_engine_build_cabinet_from_stream(FuEngine *self, GInputStream *stream, GError **error)
{
	g_autoptr(FuCabinet) cabinet = fu_cabinet_new();

	g_return_val_if_fail(FU_IS_ENGINE(self), NULL);
	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* load file */
	fu_engine_set_status(self, FWUPD_STATUS_DECOMPRESSING);
	fu_firmware_set_size_max(FU_FIRMWARE(cabinet),
				 fu_engine_config_get_archive_size_max(self->config));
	fu_cabinet_set_jcat_context(cabinet, self->jcat_context);
	if (!fu_firmware_parse_stream(FU_FIRMWARE(cabinet),
				      stream,
				      0x0,
				      FWUPD_INSTALL_FLAG_NONE,
				      error))
		return NULL;
	return g_steal_pointer(&cabinet);
}

static FuDevice *
fu_engine_get_result_from_component(FuEngine *self,
				    FuEngineRequest *request,
				    FuCabinet *cabinet,
				    XbNode *component,
				    GError **error)
{
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GError) error_reqs = NULL;
	g_autoptr(GPtrArray) provides = NULL;
	g_autoptr(GPtrArray) tags = NULL;
	g_autoptr(XbNode) rel = NULL;
	g_autoptr(XbQuery) query = NULL;

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
			fu_device_incorporate(dev, device, FU_DEVICE_INCORPORATE_FLAG_ALL);
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
	fu_release_set_device(release, dev);
	fu_release_set_request(release, request);
	query = xb_query_new_full(xb_node_get_silo(component),
				  "releases/release",
				  XB_QUERY_FLAG_FORCE_NODE_CACHE,
				  error);
	if (query == NULL)
		return NULL;
	rel = xb_node_query_first_full(component, query, &error_local);
	if (rel == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to get release: %s",
			    error_local->message);
		return NULL;
	}
	if (!fu_engine_load_release(self,
				    release,
				    cabinet,
				    component,
				    rel,
				    FWUPD_INSTALL_FLAG_IGNORE_VID_PID,
				    &error_reqs)) {
		if (!fu_device_has_inhibit(dev, "not-found"))
			fu_device_inhibit(dev, "failed-reqs", error_reqs->message);
		/* continue */
	}

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

/**
 * fu_engine_get_details:
 * @self: a #FuEngine
 * @request: a #FuEngineRequest
 * @stream: a seekable #GInputStream
 * @error: (nullable): optional return location for an error
 *
 * Gets the details about a local file.
 *
 * Note: this will close the fd when done
 *
 * Returns: (transfer container) (element-type FuDevice): results
 **/
GPtrArray *
fu_engine_get_details(FuEngine *self,
		      FuEngineRequest *request,
		      GInputStream *stream,
		      GError **error)
{
	GChecksumType checksum_types[] = {G_CHECKSUM_SHA256, G_CHECKSUM_SHA1, 0};
	g_autoptr(GPtrArray) components = NULL;
	g_autoptr(GPtrArray) details = NULL;
	g_autoptr(GPtrArray) checksums = g_ptr_array_new_with_free_func(g_free);
	g_autoptr(FuCabinet) cabinet = NULL;
	g_autoptr(XbNode) rel_by_csum = NULL;

	g_return_val_if_fail(FU_IS_ENGINE(self), NULL);
	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	cabinet = fu_engine_build_cabinet_from_stream(self, stream, error);
	if (cabinet == NULL) {
		g_prefix_error(error, "failed to load file: ");
		return NULL;
	}
	components = fu_cabinet_get_components(cabinet, error);
	if (components == NULL)
		return NULL;

	/* calculate the checksums of the blob */
	for (guint i = 0; checksum_types[i] != 0; i++) {
		g_autofree gchar *checksum =
		    fu_input_stream_compute_checksum(stream, checksum_types[i], error);
		if (checksum == NULL)
			return NULL;
		g_ptr_array_add(checksums, g_steal_pointer(&checksum));
	}

	/* does this exist in any enabled remote */
	for (guint i = 0; i < checksums->len; i++) {
		const gchar *csum = g_ptr_array_index(checksums, i);
		rel_by_csum = fu_engine_get_release_for_checksum(self, csum);
		if (rel_by_csum != NULL)
			break;
	}

	/* create results with all the metadata in */
	details = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (guint i = 0; i < components->len; i++) {
		XbNode *component = g_ptr_array_index(components, i);
		FuDevice *dev;
		g_autoptr(FuRelease) rel = fu_release_new();

		dev = fu_engine_get_result_from_component(self, request, cabinet, component, error);
		if (dev == NULL)
			return NULL;
		fu_device_add_release(dev, FWUPD_RELEASE(rel));

		if (rel_by_csum != NULL) {
			const gchar *remote_id =
			    xb_node_query_text(rel_by_csum,
					       "../../../custom/value[@key='fwupd::RemoteId']",
					       NULL);
			if (remote_id != NULL)
				fu_release_set_remote_id(rel, remote_id);
			fu_device_add_flag(dev, FWUPD_DEVICE_FLAG_SUPPORTED);
		}

		/* add the checksum of the container blob */
		for (guint j = 0; j < checksums->len; j++) {
			const gchar *csum = g_ptr_array_index(checksums, j);
			fu_release_add_checksum(rel, csum);
		}

		/* if this matched a device on the system, ensure all the
		 * requirements passed before setting UPDATABLE */
		if (fu_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE)) {
			g_autoptr(FuRelease) release = fu_release_new();
			g_autoptr(GError) error_req = NULL;
			FwupdInstallFlags install_flags =
			    FWUPD_INSTALL_FLAG_IGNORE_VID_PID | FWUPD_INSTALL_FLAG_ALLOW_REINSTALL |
			    FWUPD_INSTALL_FLAG_ALLOW_BRANCH_SWITCH | FWUPD_INSTALL_FLAG_ALLOW_OLDER;

			fu_release_set_device(release, dev);
			fu_release_set_request(release, request);
			if (!fu_engine_load_release(self,
						    release,
						    cabinet,
						    component,
						    NULL,
						    install_flags,
						    &error_req)) {
				g_info("%s failed requirement checks: %s",
				       fu_device_get_id(dev),
				       error_req->message);
				fu_device_inhibit(dev, "failed-reqs", error_req->message);
			} else {
				g_info("%s passed requirement checks", fu_device_get_id(dev));
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
	devices_tmp = fu_device_list_get_active(self->device_list);
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
 * Gets all active devices that match a specific composite ID.
 *
 * Returns: (transfer full) (element-type FuDevice): devices
 **/
GPtrArray *
fu_engine_get_devices_by_composite_id(FuEngine *self, const gchar *composite_id, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) devices_tmp = NULL;

	/* find the devices by composite ID */
	devices_tmp = fu_device_list_get_active(self->device_list);
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

static void
fu_engine_fixup_history_device(FuEngine *self, FuDevice *device)
{
	FwupdRelease *release;
	GPtrArray *csums;

	/* get the checksums */
	release = fu_device_get_release_default(device);
	if (release == NULL) {
		g_warning("no checksums from release history");
		return;
	}

	/* find the checksum that matches */
	csums = fwupd_release_get_checksums(release);
	for (guint j = 0; j < csums->len; j++) {
		const gchar *csum = g_ptr_array_index(csums, j);
		g_autoptr(XbNode) rel = fu_engine_get_release_for_checksum(self, csum);
		if (rel != NULL) {
			g_autoptr(GError) error_local = NULL;
			g_autoptr(XbNode) component = NULL;

			component = xb_node_query_first(rel, "../..", &error_local);
			if (component == NULL) {
				g_warning("failed to load component: %s", error_local->message);
				continue;
			}
			if (!fu_release_load(FU_RELEASE(release),
					     NULL,
					     component,
					     rel,
					     FWUPD_INSTALL_FLAG_NONE,
					     &error_local)) {
				g_warning("failed to load release: %s", error_local->message);
				continue;
			}
			fu_device_add_flag(device, FWUPD_DEVICE_FLAG_SUPPORTED);
			break;
		}
	}
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
	g_autoptr(GPtrArray) devices_all = NULL;
	g_autoptr(GPtrArray) devices =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

	g_return_val_if_fail(FU_IS_ENGINE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	devices_all = fu_history_get_devices(self->history, error);
	if (devices_all == NULL)
		return NULL;
	for (guint i = 0; i < devices_all->len; i++) {
		FuDevice *dev = g_ptr_array_index(devices_all, i);
		if (fu_device_has_flag(dev, FWUPD_DEVICE_FLAG_EMULATED))
			continue;
		g_ptr_array_add(devices, g_object_ref(dev));
	}
	if (devices->len == 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO, "No history");
		return NULL;
	}

	/* if this is the system firmware device, add the HSI attrs */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index(devices, i);
		if (fu_device_has_private_flag(dev, FU_DEVICE_PRIVATE_FLAG_HOST_FIRMWARE))
			fu_engine_get_history_set_hsi_attrs(self, dev);
	}

	/* try to set the remote ID for each device */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index(devices, i);
		fu_engine_fixup_history_device(self, dev);
	}

	return g_steal_pointer(&devices);
}

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
	FuRelease *rel_a = FU_RELEASE(*((FuRelease **)a));
	FuRelease *rel_b = FU_RELEASE(*((FuRelease **)b));
	gint rc;

	/* first by branch */
	rc = g_strcmp0(fu_release_get_branch(rel_b), fu_release_get_branch(rel_a));
	if (rc != 0)
		return rc;

	/* then by version */
	rc = fu_version_compare(fu_release_get_version(rel_b),
				fu_release_get_version(rel_a),
				fu_device_get_version_format(device));
	if (rc != 0)
		return rc;

	/* then by priority */
	return fu_release_compare(rel_a, rel_b);
}

static gboolean
fu_engine_check_release_is_approved(FuEngine *self, FwupdRelease *rel)
{
	GPtrArray *csums = fwupd_release_get_checksums(rel);
	if (self->approved_firmware == NULL)
		return FALSE;
	for (guint i = 0; i < csums->len; i++) {
		const gchar *csum = g_ptr_array_index(csums, i);
		g_info("checking %s against approved list", csum);
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
	    FWUPD_INSTALL_FLAG_IGNORE_VID_PID | FWUPD_INSTALL_FLAG_ALLOW_BRANCH_SWITCH |
	    FWUPD_INSTALL_FLAG_ALLOW_REINSTALL | FWUPD_INSTALL_FLAG_ALLOW_OLDER;

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
		const gchar *update_request_id;
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
					    NULL, /* cabinet */
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
		if (locations->len == 0) {
			g_autofree gchar *str = fwupd_codec_to_string(FWUPD_CODEC(release));
			g_debug("no locations for %s", str);
			continue;
		}
		checksums = fu_release_get_checksums(release);
		if (checksums->len == 0) {
			g_autofree gchar *str = fwupd_codec_to_string(FWUPD_CODEC(release));
			g_debug("no locations for %s", str);
			continue;
		}

		/* different branch */
		if (g_strcmp0(fu_release_get_branch(release), fu_device_get_branch(device)) != 0) {
			if ((feature_flags & FWUPD_FEATURE_FLAG_SWITCH_BRANCH) == 0) {
				g_info("client does not understand branches, skipping %s:%s",
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
			if (remote != NULL &&
			    fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_APPROVAL_REQUIRED) &&
			    !fu_engine_check_release_is_approved(self, FWUPD_RELEASE(release))) {
				fu_release_add_flag(release, FWUPD_RELEASE_FLAG_BLOCKED_APPROVAL);
			}
		}

		/* add update message if exists but device doesn't already have one */
		update_message = fwupd_release_get_update_message(FWUPD_RELEASE(release));
		if (fu_device_get_update_message(device) == NULL && update_message != NULL) {
			fu_device_set_update_message(device, update_message);
		}
		update_image = fwupd_release_get_update_image(FWUPD_RELEASE(release));
		if (fu_device_get_update_image(device) == NULL && update_image != NULL) {
			fu_device_set_update_image(device, update_image);
		}
		update_request_id = fu_release_get_update_request_id(release);
		if (fu_device_get_update_request_id(device) == NULL && update_request_id != NULL) {
			fu_device_add_request_flag(device,
						   FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
			fu_device_set_update_request_id(device, update_request_id);
		}

		/* success */
		g_ptr_array_add(releases, g_steal_pointer(&release));

		/* if we're only checking for SUPPORTED then *any* release is good enough */
		if (fu_engine_request_has_flag(request, FU_ENGINE_REQUEST_FLAG_ANY_RELEASE) &&
		    releases->len > 0)
			break;
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
	g_autoptr(GPtrArray) branches = NULL;
	g_autoptr(GPtrArray) releases = NULL;

	/* no components in silo */
	if (self->query_component_by_guid == NULL) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no components in silo");
		return NULL;
	}

	/* get device version */
	if (!fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_VERSION) &&
	    !fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_FLAGS)) {
		const gchar *version = fu_device_get_version(device);
		if (version == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no version set");
			return NULL;
		}
	}

	/* only show devices that can be updated */
	if (!fu_engine_request_has_feature_flag(request, FWUPD_FEATURE_FLAG_SHOW_PROBLEMS) &&
	    !fu_device_is_updatable(device)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "is not updatable");
		return NULL;
	}

	/* only show devices that can be updated */
	if (!fu_engine_request_has_feature_flag(request, FWUPD_FEATURE_FLAG_REQUESTS_NON_GENERIC) &&
	    fu_device_has_request_flag(device, FWUPD_REQUEST_FLAG_NON_GENERIC_MESSAGE)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "is not updatable as requires a non-generic request");
		return NULL;
	}

	/* get all the components that provide any of these GUIDs */
	device_guids = fu_device_get_guids(device);
	releases = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (guint j = 0; j < device_guids->len; j++) {
		const gchar *guid = g_ptr_array_index(device_guids, j);
		g_autoptr(GError) error_local = NULL;
		g_autoptr(GPtrArray) components = NULL;
		g_auto(XbQueryContext) context = XB_QUERY_CONTEXT_INIT();

		xb_query_context_set_flags(&context, XB_QUERY_FLAG_USE_INDEXES);
		xb_value_bindings_bind_str(xb_query_context_get_bindings(&context), 0, guid, NULL);
		components = xb_silo_query_with_context(self->silo,
							self->query_component_by_guid,
							&context,
							&error_local);
		if (components == NULL) {
			g_debug("%s was not found: %s", guid, error_local->message);
			continue;
		}

		/* find all the releases that pass all the requirements */
		g_debug("%s matched %u components", guid, components->len);
		for (guint i = 0; i < components->len; i++) {
			XbNode *component = XB_NODE(g_ptr_array_index(components, i));
			g_autoptr(GError) error_tmp = NULL;
			if (!fu_engine_add_releases_for_device_component(self,
									 request,
									 device,
									 component,
									 releases,
									 &error_tmp)) {
				g_debug("%s", error_tmp->message);
				continue;
			}
		}
		g_debug("%s matched %u releases", guid, releases->len);

		/* if we're only checking for SUPPORTED then *any* release is good enough */
		if (fu_engine_request_has_flag(request, FU_ENGINE_REQUEST_FLAG_ANY_RELEASE) &&
		    releases->len > 0)
			break;
	}

	/* are there multiple branches available */
	branches = g_ptr_array_new_with_free_func(g_free);
	g_ptr_array_add(branches,
			g_strdup(fu_engine_get_branch_fallback(fu_device_get_branch(device))));
	for (guint i = 0; i < releases->len; i++) {
		FwupdRelease *rel_tmp = FWUPD_RELEASE(g_ptr_array_index(releases, i));
		const gchar *branch_tmp =
		    fu_engine_get_branch_fallback(fwupd_release_get_branch(rel_tmp));
		if (g_ptr_array_find_with_equal_func(branches, branch_tmp, g_str_equal, NULL))
			continue;
		g_ptr_array_add(branches, g_strdup(branch_tmp));
	}
	if (branches->len > 1)
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_HAS_MULTIPLE_BRANCHES);

	/* return the compound error */
	if (releases->len == 0) {
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
	g_autoptr(GPtrArray) releases_deduped = NULL;

	g_return_val_if_fail(FU_IS_ENGINE(self), NULL);
	g_return_val_if_fail(FU_IS_ENGINE_REQUEST(request), NULL);
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

	/* dedupe by container checksum */
	if (fu_engine_config_get_release_dedupe(self->config)) {
		g_autoptr(GHashTable) checksums = g_hash_table_new(g_str_hash, g_str_equal);
		releases_deduped = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
		for (guint i = 0; i < releases->len; i++) {
			FuRelease *rel = g_ptr_array_index(releases, i);
			GPtrArray *csums = fu_release_get_checksums(rel);
			gboolean found = FALSE;

			/* find existing */
			for (guint j = 0; j < csums->len; j++) {
				const gchar *csum = g_ptr_array_index(csums, j);
				if (g_hash_table_contains(checksums, csum)) {
					found = TRUE;
					break;
				}
				g_hash_table_add(checksums, (gpointer)csum);
			}
			if (found) {
				g_debug("found higher priority release for %s, skipping",
					fu_release_get_version(rel));
				continue;
			}
			g_ptr_array_add(releases_deduped, g_object_ref(rel));
		}
	} else {
		releases_deduped = g_ptr_array_ref(releases);
	}

	/* success */
	return g_steal_pointer(&releases_deduped);
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
			g_info("ignoring release %s as branch %s, and device is %s",
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

static void
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

	/* there is no point checking each release */
	if (!fu_device_is_updatable(device)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "Device is not updatable");
		return NULL;
	}

	/* stay on one firmware version unless the new version is explicitly specified */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_ONLY_EXPLICIT_UPDATES)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "Installing a specific release is explicitly required");
		return NULL;
	}

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
			g_info("ignoring release %s as branch %s, and device is %s",
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

	/* if the update never got run, unstage it */
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
	FwupdRelease *rel;
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

	/* try to set some release properties for the UI */
	fu_engine_fixup_history_device(self, device);

	/* we did not either record or find the AppStream ID */
	rel = fu_device_get_release_default(device);
	if (rel == NULL || fwupd_release_get_appstream_id(rel) == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "device %s appstream id was not found",
			    fu_device_get_id(device));
		return NULL;
	}

	/* success */
	return g_object_ref(FWUPD_DEVICE(device));
}

static void
fu_engine_plugins_startup(FuEngine *self, FuProgress *progress)
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
			g_info("disabling plugin because: %s", error->message);
			fu_progress_add_flag(progress, FU_PROGRESS_FLAG_CHILD_FINISHED);
		}
		fu_progress_step_done(progress);
	}
}

static void
fu_engine_plugins_ready(FuEngine *self, FuProgress *progress)
{
	GPtrArray *plugins = fu_plugin_list_get_all(self->plugin_list);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, plugins->len);
	for (guint i = 0; i < plugins->len; i++) {
		g_autoptr(GError) error = NULL;
		FuPlugin *plugin = g_ptr_array_index(plugins, i);
		if (!fu_plugin_runner_ready(plugin, fu_progress_get_child(progress), &error)) {
			if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
				fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_NO_HARDWARE);
			}
			g_info("disabling plugin because: %s", error->message);
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
			g_info("disabling plugin because: %s", error->message);
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
		g_info("using plugins: %s", str->str);
	}
}

static void
fu_engine_plugin_device_register(FuEngine *self, FuDevice *device)
{
	GPtrArray *backends = fu_context_get_backends(self->ctx);
	GPtrArray *plugins;
	if (fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_REGISTERED)) {
		g_warning("already registered %s, ignoring", fu_device_get_id(device));
		return;
	}
	plugins = fu_plugin_list_get_all(self->plugin_list);
	for (guint i = 0; i < plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index(plugins, i);
		fu_plugin_runner_device_register(plugin, device);
	}
	for (guint i = 0; i < backends->len; i++) {
		FuBackend *backend = g_ptr_array_index(backends, i);
		fu_backend_registered(backend, device);
	}
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_REGISTERED);
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
		g_info("auto-setting %s priority to %u",
		       fu_device_get_id(device),
		       fu_plugin_get_priority(plugin));
		fu_device_set_priority(device, fu_plugin_get_priority(plugin));
	}

	fu_engine_add_device(self, device);
}

static void
fu_engine_adopt_children_device(FuEngine *self, FuDevice *device, FuDevice *device_tmp)
{
	if (fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_HOST_FIRMWARE_CHILD) &&
	    fu_device_has_private_flag(device_tmp, FU_DEVICE_PRIVATE_FLAG_HOST_FIRMWARE)) {
		fu_device_set_parent(device, device_tmp);
		fu_engine_ensure_device_supported(self, device_tmp);
		return;
	}
	if (fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_HOST_FIRMWARE) &&
	    fu_device_has_private_flag(device_tmp, FU_DEVICE_PRIVATE_FLAG_HOST_FIRMWARE_CHILD)) {
		fu_device_set_parent(device_tmp, device);
		fu_engine_ensure_device_supported(self, device_tmp);
		return;
	}
	if (fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_HOST_CPU_CHILD) &&
	    fu_device_has_private_flag(device_tmp, FU_DEVICE_PRIVATE_FLAG_HOST_CPU)) {
		fu_device_set_parent(device, device_tmp);
		fu_engine_ensure_device_supported(self, device_tmp);
		return;
	}
	if (fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_HOST_CPU) &&
	    fu_device_has_private_flag(device_tmp, FU_DEVICE_PRIVATE_FLAG_HOST_CPU_CHILD)) {
		fu_device_set_parent(device_tmp, device);
		fu_engine_ensure_device_supported(self, device_tmp);
		return;
	}
}

static void
fu_engine_set_device_parent(FuEngine *self, FuDevice *device, FuDevice *parent)
{
	fu_device_set_parent(device, parent);
	fu_engine_ensure_device_supported(self, device);
	fu_engine_ensure_device_supported(self, parent);
}

static void
fu_engine_adopt_children(FuEngine *self, FuDevice *device)
{
	GPtrArray *guids;
	g_autoptr(GPtrArray) devices = fu_device_list_get_active(self->device_list);

	/* find the parent in any existing device */
	for (guint i = 0; fu_device_get_parent(device) == NULL && i < devices->len; i++) {
		FuDevice *device_tmp = g_ptr_array_index(devices, i);
		fu_engine_adopt_children_device(self, device, device_tmp);
	}
	if (fu_device_get_parent(device) == NULL) {
		for (guint i = 0; i < devices->len; i++) {
			FuDevice *device_tmp = g_ptr_array_index(devices, i);
			if (!fu_device_has_private_flag(
				device_tmp,
				FU_DEVICE_PRIVATE_FLAG_AUTO_PARENT_CHILDREN))
				continue;
			if (fu_device_get_physical_id(device_tmp) == NULL)
				continue;
			if (fu_device_has_parent_physical_id(
				device,
				fu_device_get_physical_id(device_tmp))) {
				fu_engine_set_device_parent(self, device, device_tmp);
				break;
			}
		}
	}
	if (fu_device_get_parent(device) == NULL) {
		for (guint i = 0; i < devices->len; i++) {
			FuDevice *device_tmp = g_ptr_array_index(devices, i);
			if (!fu_device_has_private_flag(
				device_tmp,
				FU_DEVICE_PRIVATE_FLAG_AUTO_PARENT_CHILDREN))
				continue;
			if (fu_device_get_backend_id(device_tmp) == NULL)
				continue;
			if (fu_device_has_parent_backend_id(device,
							    fu_device_get_backend_id(device_tmp))) {
				fu_engine_set_device_parent(self, device, device_tmp);
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
					fu_engine_set_device_parent(self, device, device_tmp);
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
				fu_engine_set_device_parent(self, device_tmp, device);
		}
	}
	for (guint j = 0; j < devices->len; j++) {
		GPtrArray *parent_backend_ids = NULL;
		FuDevice *device_tmp = g_ptr_array_index(devices, j);
		if (fu_device_get_parent(device_tmp) != NULL)
			continue;
		parent_backend_ids = fu_device_get_parent_backend_ids(device_tmp);
		if (parent_backend_ids == NULL)
			continue;
		for (guint i = 0; i < parent_backend_ids->len; i++) {
			const gchar *parent_backend_id = g_ptr_array_index(parent_backend_ids, i);
			if (g_strcmp0(parent_backend_id, fu_device_get_backend_id(device)) == 0)
				fu_engine_set_device_parent(self, device_tmp, device);
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
				fu_engine_set_device_parent(self, device_tmp, device);
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
		g_info("setting proxy of %s to %s for %s",
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
				g_info("adding proxy of %s to %s for %s",
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

	/* ignore */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED))
		return;

	/* any success or failed update? */
	device_history = fu_history_get_device_by_id(self->history, fu_device_get_id(device), NULL);
	if (device_history == NULL)
		return;

	/* in an offline environment we may have used the .cab file to find the version-format
	 * to use for the device -- so when we reboot use the database as the archive data is no
	 * longer available */
	if (fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_VERFMT) &&
	    fu_device_get_version_format(device_history) != FWUPD_VERSION_FORMAT_UNKNOWN) {
		g_debug(
		    "absorbing version format %s into %s from history database",
		    fwupd_version_format_to_string(fu_device_get_version_format(device_history)),
		    fu_device_get_id(device));
		fu_device_set_version_format(device, fu_device_get_version_format(device_history));
	}

	/* the device is still running the old firmware version and so if it
	 * required activation before, it still requires it now -- note:
	 * we can't just check for version_new=version to allow for re-installs */
	if (fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_INHERIT_ACTIVATION) &&
	    fu_device_has_flag(device_history, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION)) {
		FwupdRelease *release = fu_device_get_release_default(device_history);
		if (fu_version_compare(fu_device_get_version(device),
				       fwupd_release_get_version(release),
				       fu_device_get_version_format(device)) != 0) {
			g_info("inheriting needs-activation for %s as version %s != %s",
			       fu_device_get_name(device),
			       fu_device_get_version(device),
			       fwupd_release_get_version(release));
			fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
		}
	}
}

static void
fu_engine_ensure_device_emulation_tag(FuEngine *self, FuDevice *device)
{
	/* already done */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATION_TAG))
		return;

	/* we matched this physical ID */
	if (fu_device_get_id(device) == NULL)
		return;
	if (!g_hash_table_contains(self->emulation_ids, fu_device_get_id(device)))
		return;

	/* success */
	g_info("adding emulation-tag to %s", fu_device_get_backend_id(device));
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_EMULATION_TAG);
	fu_engine_ensure_context_flag_save_events(self);
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
	disabled_devices = fu_engine_config_get_disabled_devices(self->config);
	for (guint i = 0; i < disabled_devices->len; i++) {
		const gchar *disabled_guid = g_ptr_array_index(disabled_devices, i);
		for (guint j = 0; j < device_guids->len; j++) {
			const gchar *device_guid = g_ptr_array_index(device_guids, j);
			if (g_strcmp0(disabled_guid, device_guid) == 0) {
				g_info("%s [%s] is disabled [%s], ignoring from %s",
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
	if (fu_device_is_updatable(device) &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD) &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD) &&
	    !fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_SIGNED)) {
		g_warning("%s [%s] does not declare signed/unsigned payload -- perhaps add "
			  "fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);",
			  fu_device_get_plugin(device),
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
							    NULL, /* cabinet */
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

	/* check if the device needs emulation-tag */
	fu_engine_ensure_device_emulation_tag(self, device);

	/* set or clear the SUPPORTED flag */
	fu_engine_ensure_device_supported(self, device);

	/* adopt any required children, which may or may not already exist */
	fu_engine_adopt_children(self, device);

	/* set the proxy device if specified by GUID */
	fu_engine_set_proxy_device(self, device);

	/* sometimes inherit flags from recent history */
	fu_engine_device_inherit_history(self, device);

	/* notify all plugins about this new device */
	if (!fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_REGISTERED))
		fu_engine_plugin_device_register(self, device);

	if (fu_device_get_version_format(device) == FWUPD_VERSION_FORMAT_UNKNOWN &&
	    fu_version_guess_format(fu_device_get_version(device)) == FWUPD_VERSION_FORMAT_NUMBER) {
		fu_device_inhibit(device, "version-format", "VersionFormat is ambiguous");
	}

	/* no vendor-id, and so no way to lock it down! */
	if (fu_device_is_updatable(device) && fu_device_get_vendor_ids(device)->len == 0) {
		fu_device_inhibit(device, "vendor-id", "No vendor ID set");
	}

	/* create new device */
	fu_device_list_add(self->device_list, device);

	/* clean up any state only valid for ->probe */
	fu_device_probe_complete(device);

	/* fix order */
	fu_device_list_depsolve_order(self->device_list, device);

	/* save to emulated phase, but avoid overwriting reload */
	if (fu_context_has_flag(self->ctx, FU_CONTEXT_FLAG_SAVE_EVENTS) &&
	    self->install_phase == FU_ENGINE_INSTALL_PHASE_SETUP &&
	    fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATION_TAG) &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED)) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_engine_backends_save_phase(self, &error_local))
			g_warning("failed to save phase: %s", error_local->message);
	}

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
		fu_idle_inhibit(self->idle, FU_IDLE_INHIBIT_TIMEOUT, tmp);
	}
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
	g_autoptr(GError) error = NULL;

	/* get the plugin */
	plugin_old =
	    fu_plugin_list_find_by_name(self->plugin_list, fu_device_get_plugin(device), &error);
	if (plugin_old == NULL) {
		g_info("failed to find plugin %s: %s",
		       fu_device_get_plugin(device),
		       error->message);
		return;
	}

	/* check this came from the same plugin */
	if (g_strcmp0(fu_plugin_get_name(plugin), fu_plugin_get_name(plugin_old)) != 0) {
		g_info("ignoring duplicate removal from %s", fu_plugin_get_name(plugin));
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
	fu_plugin_list_add(self->plugin_list, plugin);
}

gboolean
fu_engine_is_uid_trusted(FuEngine *self, guint64 calling_uid)
{
	GArray *trusted;

	/* root is always trusted */
	if (calling_uid == 0)
		return TRUE;

	trusted = fu_engine_config_get_trusted_uids(self->config);
	for (guint i = 0; i < trusted->len; i++) {
		if (calling_uid == g_array_index(trusted, guint64, i))
			return TRUE;
	}
	return FALSE;
}

static gboolean
fu_engine_is_test_plugin_disabled(FuEngine *self, FuPlugin *plugin)
{
	if (!fu_plugin_has_flag(plugin, FWUPD_PLUGIN_FLAG_TEST_ONLY))
		return FALSE;
	if (fu_engine_config_get_test_devices(self->config))
		return FALSE;
	return TRUE;
}

static gboolean
fu_engine_is_plugin_name_disabled(FuEngine *self, const gchar *name)
{
	GPtrArray *disabled = fu_engine_config_get_disabled_plugins(self->config);
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
		if (g_pattern_match_simple(name_tmp, name))
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
	g_string_replace(str, "-", "_", 0);
	g_ptr_array_add(self->plugin_filter, g_string_free(str, FALSE));
}

static gboolean
fu_engine_plugin_check_supported_cb(FuPlugin *plugin, const gchar *guid, FuEngine *self)
{
	g_autoptr(XbNode) n = NULL;
	g_autofree gchar *xpath = NULL;

	if (fu_engine_config_get_enumerate_all_devices(self->config))
		return TRUE;

	xpath = g_strdup_printf("components/component[@type='firmware']/"
				"provides/firmware[@type='flashed'][text()='%s']",
				guid);
	n = xb_silo_query_first(self->silo, xpath, NULL);
	return n != NULL;
}

FuEngineConfig *
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
	if (fu_engine_config_get_host_bkc(self->config) == NULL)
		return "";
	return fu_engine_config_get_host_bkc(self->config);
}

#ifdef HAVE_HSI
static void
fu_engine_ensure_security_attrs_supported_cpu(FuEngine *self)
{
	g_autoptr(FwupdSecurityAttr) attr =
	    fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_SUPPORTED_CPU);
	fwupd_security_attr_set_plugin(attr, "core");

	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
	fu_security_attrs_append(self->host_security_attrs, attr);
}

static void
fu_engine_ensure_security_attrs_tainted(FuEngine *self)
{
	gboolean disabled_plugins = FALSE;
	GPtrArray *disabled = fu_engine_config_get_disabled_plugins(self->config);
	g_autoptr(FwupdSecurityAttr) attr =
	    fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_FWUPD_PLUGINS);
	fwupd_security_attr_set_plugin(attr, "core");
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_TAINTED);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);

	fu_security_attrs_append(self->host_security_attrs, attr);
	for (guint i = 0; i < disabled->len; i++) {
		const gchar *name_tmp = g_ptr_array_index(disabled, i);
		if (!g_str_has_prefix(name_tmp, "test")) {
			disabled_plugins = TRUE;
			break;
		}
	}
	if (self->plugin_filter->len > 0 || disabled_plugins) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_TAINTED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_OS);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

/*
 * Get chassis type from SMBIOS data and verify HSI makes sense for it
 */
static gchar *
fu_engine_attrs_calculate_hsi_for_chassis(FuEngine *self)
{
	FuSmbiosChassisKind chassis_kind = fu_context_get_chassis_kind(self->ctx);

	/* if emulating, force the chassis type to be valid */
	if (self->host_emulation && (chassis_kind == FU_SMBIOS_CHASSIS_KIND_OTHER ||
				     chassis_kind == FU_SMBIOS_CHASSIS_KIND_UNKNOWN)) {
		g_info("forcing chassis kind %s to be valid",
		       fu_smbios_chassis_kind_to_string(chassis_kind));
		chassis_kind = FU_SMBIOS_CHASSIS_KIND_DESKTOP;
	}

	switch (chassis_kind) {
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

	return g_strdup_printf("HSI:INVALID:chassis[%s] (v%d.%d.%d)",
			       fu_smbios_chassis_kind_to_string(chassis_kind),
			       FWUPD_MAJOR_VERSION,
			       FWUPD_MINOR_VERSION,
			       FWUPD_MICRO_VERSION);
}

static gboolean
fu_engine_record_security_attrs(FuEngine *self, GError **error)
{
	g_autoptr(GPtrArray) attrs_array = NULL;
	g_autofree gchar *json = NULL;

	/* convert attrs to json string */
	json = fwupd_codec_to_json_string(FWUPD_CODEC(self->host_security_attrs),
					  FWUPD_CODEC_FLAG_NONE,
					  error);
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
			g_info("skipping writing HSI attrs to database as unchanged");
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

	/* success */
	return TRUE;
}

static void
fu_engine_security_attrs_depsolve(FuEngine *self)
{
	g_autoptr(GPtrArray) items = NULL;

	/* set the obsoletes flag for each attr */
	fu_security_attrs_depsolve(self->host_security_attrs);

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
		if (fwupd_security_attr_get_title(attr) == NULL)
			fwupd_security_attr_set_title(attr, fu_security_attr_get_title(attr));
		if (fwupd_security_attr_get_description(attr) == NULL) {
			fwupd_security_attr_set_description(attr,
							    fu_security_attr_get_description(attr));
		}
	}

	/* distil into one simple string */
	g_free(self->host_security_id);
	self->host_security_id = fu_engine_attrs_calculate_hsi_for_chassis(self);
}
#endif

/**
 * fu_history_get_previous_security_attr:
 * @self: a #FuHistory
 * @appstream_id: maximum number of attributes to return, or 0 for no limit
 * @current_setting: (nullable): current value
 * @error: return location for a #GError, or %NULL
 *
 * Gets the security attributes of the previous BIOS setting for the given
 * appstream ID and current BIOS config.
 *
 * Returns: (element-type #FuSecurityAttr) (transfer full): attr, or %NULL
 **/
static FwupdSecurityAttr *
fu_engine_get_previous_bios_security_attr(FuEngine *self,
					  const gchar *appstream_id,
					  const gchar *current_setting,
					  GError **error)
{
	g_autoptr(GPtrArray) attrs_array = NULL;

	attrs_array = fu_history_get_security_attrs(self->history, 20, error);
	if (attrs_array == NULL)
		return NULL;
	for (guint i = 0; i < attrs_array->len; i++) {
		FuSecurityAttrs *attrs = g_ptr_array_index(attrs_array, i);
		g_autoptr(GPtrArray) attr_items = fu_security_attrs_get_all(attrs);
		for (guint j = 0; j < attr_items->len; j++) {
			FwupdSecurityAttr *attr = g_ptr_array_index(attr_items, j);
			if (g_strcmp0(appstream_id, fwupd_security_attr_get_appstream_id(attr)) ==
				0 &&
			    g_strcmp0(current_setting,
				      fwupd_security_attr_get_bios_setting_current_value(attr)) !=
				0) {
				g_debug("found previous BIOS setting for %s: %s",
					appstream_id,
					fwupd_security_attr_get_bios_setting_id(attr));
				return g_object_ref(attr);
			}
		}
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot find previous BIOS value");
	return NULL;
}

/**
 * fu_engine_fix_host_security_attr:
 * @self: a #FuEngine
 * @appstream_id: the Appstream ID
 * @error: (nullable): optional return location for an error
 *
 * Fix one specific security attribute.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_fix_host_security_attr(FuEngine *self, const gchar *appstream_id, GError **error)
{
	FuPlugin *plugin;
	FwupdBiosSetting *bios_attr;
	g_autoptr(FwupdSecurityAttr) hsi_attr = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_ENGINE(self), FALSE);

	fu_engine_ensure_security_attrs(self);
	hsi_attr =
	    fu_security_attrs_get_by_appstream_id(self->host_security_attrs, appstream_id, error);
	if (hsi_attr == NULL)
		return FALSE;
	if (!fwupd_security_attr_has_flag(hsi_attr, FWUPD_SECURITY_ATTR_FLAG_CAN_FIX)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "cannot auto-fix attribute");
		return FALSE;
	}
	plugin = fu_plugin_list_find_by_name(self->plugin_list,
					     fwupd_security_attr_get_plugin(hsi_attr),
					     error);
	if (plugin == NULL)
		return FALSE;

	/* first try the per-plugin vfunc */
	if (!fu_plugin_runner_fix_host_security_attr(plugin, hsi_attr, &error_local)) {
		if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
		g_debug("ignoring %s", error_local->message);
	} else {
		g_info("fixed %s", fwupd_security_attr_get_appstream_id(hsi_attr));
		return TRUE;
	}

	/* fall back to setting the BIOS attribute */
	if (fwupd_security_attr_get_bios_setting_id(hsi_attr) == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no BIOS setting ID set");
		return FALSE;
	}
	bios_attr = fu_context_get_bios_setting(self->ctx,
						fwupd_security_attr_get_bios_setting_id(hsi_attr));
	if (bios_attr == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot get BIOS setting %s",
			    fwupd_security_attr_get_bios_setting_id(hsi_attr));
		return FALSE;
	}
	return fwupd_bios_setting_write_value(
	    bios_attr,
	    fwupd_security_attr_get_bios_setting_target_value(hsi_attr),
	    error);
}

/**
 * fu_engine_fix_host_security_attr:
 * @self: a #FuEngine
 * @appstream_id: the Appstream ID
 * @error: (nullable): optional return location for an error
 *
 * Revert the fix for one specific security attribute.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_undo_host_security_attr(FuEngine *self, const gchar *appstream_id, GError **error)
{
	FuPlugin *plugin;
	FwupdBiosSetting *bios_attr;
	g_autoptr(FwupdSecurityAttr) hsi_attr = NULL;
	g_autoptr(FwupdSecurityAttr) hsi_attr_old = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_ENGINE(self), FALSE);

	fu_engine_ensure_security_attrs(self);
	hsi_attr =
	    fu_security_attrs_get_by_appstream_id(self->host_security_attrs, appstream_id, error);
	if (hsi_attr == NULL)
		return FALSE;
	if (!fwupd_security_attr_has_flag(hsi_attr, FWUPD_SECURITY_ATTR_FLAG_CAN_UNDO)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "cannot auto-undo attribute");
		return FALSE;
	}
	plugin = fu_plugin_list_find_by_name(self->plugin_list,
					     fwupd_security_attr_get_plugin(hsi_attr),
					     error);
	if (plugin == NULL)
		return FALSE;

	/* first try the per-plugin vfunc */
	if (!fu_plugin_runner_undo_host_security_attr(plugin, hsi_attr, &error_local)) {
		if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
	}

	/* fall back to setting the BIOS attribute */
	if (fwupd_security_attr_get_bios_setting_id(hsi_attr) == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no BIOS setting ID");
		return FALSE;
	}
	bios_attr = fu_context_get_bios_setting(self->ctx,
						fwupd_security_attr_get_bios_setting_id(hsi_attr));
	if (bios_attr == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot get BIOS setting %s",
			    fwupd_security_attr_get_bios_setting_id(hsi_attr));
		return FALSE;
	}
	if (fwupd_security_attr_get_bios_setting_current_value(hsi_attr) == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no BIOS setting current value");
		return FALSE;
	}
	hsi_attr_old = fu_engine_get_previous_bios_security_attr(
	    self,
	    appstream_id,
	    fwupd_security_attr_get_bios_setting_current_value(hsi_attr),
	    error);
	if (hsi_attr_old == NULL)
		return FALSE;
	return fwupd_bios_setting_write_value(
	    bios_attr,
	    fwupd_security_attr_get_bios_setting_current_value(hsi_attr_old),
	    error);
}

static gboolean
fu_engine_security_attrs_from_json(FuEngine *self, JsonNode *json_node, GError **error)
{
	JsonObject *obj;

	/* sanity check */
	if (!JSON_NODE_HOLDS_OBJECT(json_node)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "not JSON object");
		return FALSE;
	}

	/* not supplied */
	obj = json_node_get_object(json_node);
	if (!json_object_has_member(obj, "SecurityAttributes"))
		return TRUE;
	if (!fwupd_codec_from_json(FWUPD_CODEC(self->host_security_attrs), json_node, error))
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
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "not JSON object");
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
		if (!fwupd_codec_from_json(FWUPD_CODEC(device), node_tmp, error))
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
	g_autoptr(FuBiosSettings) bios_settings = fu_context_get_bios_settings(self->ctx);

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
	if (!fwupd_codec_from_json(FWUPD_CODEC(bios_settings), json_parser_get_root(parser), error))
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
	g_autoptr(GPtrArray) devices = fu_device_list_get_active(self->device_list);
	g_autoptr(GPtrArray) vals = NULL;
	g_autoptr(GError) error = NULL;

	/* already valid */
	if (self->host_security_id != NULL || self->host_emulation)
		return;

	/* clear old values */
	fu_security_attrs_remove_all(self->host_security_attrs);

	/* built in */
	fu_engine_ensure_security_attrs_supported_cpu(self);
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

	/* sanity check */
	vals = fu_security_attrs_get_all(self->host_security_attrs);
	for (guint i = 0; i < vals->len; i++) {
		FwupdSecurityAttr *attr = g_ptr_array_index(vals, i);
		if (fwupd_security_attr_get_result(attr) == FWUPD_SECURITY_ATTR_RESULT_UNKNOWN) {
#ifdef SUPPORTED_BUILD
			g_debug("HSI attribute %s (from %s) had unknown result",
				fwupd_security_attr_get_appstream_id(attr),
				fwupd_security_attr_get_plugin(attr));
#else
			g_warning("HSI attribute %s (from %s) had unknown result",
				  fwupd_security_attr_get_appstream_id(attr),
				  fwupd_security_attr_get_plugin(attr));
#endif
		}
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
			if (fwupd_security_attr_get_title(attr) == NULL) {
				fwupd_security_attr_set_title(attr,
							      fu_security_attr_get_title(attr));
			}
			if (fwupd_security_attr_get_description(attr) == NULL) {
				fwupd_security_attr_set_description(
				    attr,
				    fu_security_attr_get_description(attr));
			}
			fu_security_attrs_append_internal(events, attr);
		}
	}

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

static void
fu_engine_load_plugins_builtins(FuEngine *self, FuProgress *progress)
{
	guint steps = 0;

	/* count possible steps */
	for (guint i = 0; fu_plugin_externals[i] != NULL; i++)
		steps++;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, steps);
	for (guint i = 0; fu_plugin_externals[i] != NULL; i++) {
		GType plugin_gtype = fu_plugin_externals[i]();
		g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(plugin_gtype, self->ctx);
		fu_progress_set_name(fu_progress_get_child(progress), fu_plugin_get_name(plugin));
		fu_engine_add_plugin(self, plugin);
		fu_progress_step_done(progress);
	}
}

static gboolean
fu_engine_load_plugins(FuEngine *self,
		       FuEngineLoadFlags flags,
		       FuProgress *progress,
		       GError **error)
{
	g_autofree gchar *plugin_path = NULL;
	g_autoptr(GPtrArray) filenames = NULL;
	g_autoptr(GError) error_local = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_NO_PROFILE);
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 13, "search");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 87, "load");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 5, "load-builtins");

	/* search */
	plugin_path = fu_path_from_kind(FU_PATH_KIND_LIBDIR_PKG);
	if (flags & FU_ENGINE_LOAD_FLAG_EXTERNAL_PLUGINS) {
		filenames = fu_path_get_files(plugin_path, &error_local);
		if (filenames == NULL)
			g_debug("no external plugins found: %s", error_local->message);
	}
	fu_progress_step_done(progress);

	/* load */
	if (filenames != NULL)
		fu_engine_load_plugins_filenames(self, filenames, fu_progress_get_child(progress));
	fu_progress_step_done(progress);

	/* load builtins */
	if (flags & FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS)
		fu_engine_load_plugins_builtins(self, fu_progress_get_child(progress));
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
		    fu_engine_is_test_plugin_disabled(self, plugin) ||
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
		fu_progress_step_done(progress);
	}

	/* show list */
	if (plugins_disabled->len > 0) {
		g_autofree gchar *str = NULL;
		g_ptr_array_add(plugins_disabled, NULL);
		str = g_strjoinv(", ", (gchar **)plugins_disabled->pdata);
		g_info("plugins disabled: %s", str);
	}
	if (plugins_disabled_rt->len > 0) {
		g_autofree gchar *str = NULL;
		g_ptr_array_add(plugins_disabled_rt, NULL);
		str = g_strjoinv(", ", (gchar **)plugins_disabled_rt->pdata);
		g_info("plugins runtime-disabled: %s", str);
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

static gboolean
fu_engine_apply_default_bios_settings_policy(FuEngine *self, GError **error)
{
	const gchar *tmp;
	g_autofree gchar *base = fu_path_from_kind(FU_PATH_KIND_SYSCONFDIR_PKG);
	g_autofree gchar *dirname = g_build_filename(base, "bios-settings.d", NULL);
	g_autoptr(FuBiosSettings) new_bios_settings = fu_bios_settings_new();
	g_autoptr(GHashTable) hashtable = NULL;
	g_autoptr(GDir) dir = NULL;

	if (!g_file_test(dirname, G_FILE_TEST_EXISTS))
		return TRUE;

	dir = g_dir_open(dirname, 0, error);
	if (dir == NULL)
		return FALSE;
	while ((tmp = g_dir_read_name(dir)) != NULL) {
		g_autofree gchar *data = NULL;
		g_autofree gchar *fn = NULL;
		if (!g_str_has_suffix(tmp, ".json"))
			continue;
		fn = g_build_filename(dirname, tmp, NULL);
		g_info("loading default BIOS settings policy from %s", fn);
		if (!g_file_get_contents(fn, &data, NULL, error))
			return FALSE;
		if (!fwupd_codec_from_json_string(FWUPD_CODEC(new_bios_settings), data, error))
			return FALSE;
	}
	hashtable = fu_bios_settings_to_hash_kv(new_bios_settings);
	return fu_engine_modify_bios_settings(self, hashtable, TRUE, error);
}

static void
fu_engine_check_firmware_attributes(FuEngine *self, FuDevice *device, gboolean added)
{
	const gchar *subsystem;

	if (!FU_IS_UDEV_DEVICE(device))
		return;
	if (self->host_emulation)
		return;
	subsystem = fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device));
	if (g_strcmp0(subsystem, "firmware-attributes") == 0) {
		g_autoptr(GError) error = NULL;
		if (added) {
			g_autoptr(FuBiosSettings) settings =
			    fu_context_get_bios_settings(self->ctx);
			g_autoptr(GPtrArray) items = fu_bios_settings_get_all(settings);

			if (items->len > 0) {
				g_debug("ignoring add event for already loaded settings");
				return;
			}
		}
		if (!fu_context_reload_bios_settings(self->ctx, &error)) {
			g_debug("%s", error->message);
			return;
		}
		if (!fu_engine_apply_default_bios_settings_policy(self, &error)) {
			if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO))
				g_debug("%s", error->message);
			else
				g_warning("failed to apply BIOS settings policy: %s",
					  error->message);
			return;
		}
	}
}

static void
fu_engine_backend_device_removed_cb(FuBackend *backend, FuDevice *device, FuEngine *self)
{
	g_autoptr(GPtrArray) devices = NULL;

	/* if this is for firmware attributes, reload that part of the daemon */
	fu_engine_check_firmware_attributes(self, device, FALSE);

	/* debug */
	g_debug("%s removed %s", fu_backend_get_name(backend), fu_device_get_backend_id(device));

	/* go through each device and remove any that match */
	devices = fu_device_list_get_active(self->device_list);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device_tmp = g_ptr_array_index(devices, i);
		if (g_strcmp0(fu_device_get_backend_id(device_tmp),
			      fu_device_get_backend_id(device)) == 0) {
			FuPlugin *plugin;
			if (fu_device_has_private_flag(device_tmp,
						       FU_DEVICE_PRIVATE_FLAG_NO_AUTO_REMOVE)) {
				g_info("not auto-removing backend device %s [%s] due to flags",
				       fu_device_get_name(device_tmp),
				       fu_device_get_id(device_tmp));
				continue;
			}
			plugin = fu_plugin_list_find_by_name(self->plugin_list,
							     fu_device_get_plugin(device_tmp),
							     NULL);
			if (plugin == NULL)
				continue;
			g_info("auto-removing backend device %s [%s]",
			       fu_device_get_name(device_tmp),
			       fu_device_get_id(device_tmp));
			fu_plugin_device_remove(plugin, device_tmp);
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
	if (!fu_plugin_runner_backend_device_added(plugin, device, progress, error)) {
#ifdef SUPPORTED_BUILD
		/* sanity check */
		if (*error == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
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
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED) ||
			    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
				g_debug("%s ignoring: %s", plugin_name, error_local->message);
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
	g_autofree gchar *str1 = NULL;
	g_autofree gchar *str2 = NULL;
	g_autoptr(GError) error_local = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_NO_PROFILE);
	fu_progress_set_name(progress, fu_device_get_backend_id(device));
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 50, "probe-baseclass");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 50, "query-possible-plugins");

	/* super useful for plugin development */
	str1 = fu_device_to_string(FU_DEVICE(device));
	g_debug("%s added %s", fu_device_get_backend_id(device), str1);

	/* add any extra quirks */
	fu_device_set_context(device, self->ctx);
	if (!fu_device_probe(device, &error_local)) {
		if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED) &&
		    !g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_TIMED_OUT)) {
			g_warning("failed to probe device %s: %s",
				  fu_device_get_backend_id(device),
				  error_local->message);
		} else {
			g_debug("failed to probe device %s : %s",
				fu_device_get_backend_id(device),
				error_local->message);
		}
		fu_progress_finished(progress);
		return;
	}
	fu_progress_step_done(progress);

	/* check if the device needs emulation-tag */
	fu_engine_ensure_device_emulation_tag(self, device);

	/* super useful for plugin development */
	str2 = fu_device_to_string(FU_DEVICE(device));
	g_debug("%s added %s", fu_device_get_backend_id(device), str2);

	/* if this is for firmware attributes, reload that part of the daemon */
	fu_engine_check_firmware_attributes(self, device, TRUE);

	/* can be specified using a quirk */
	fu_engine_backend_device_added_run_plugins(self, device, fu_progress_get_child(progress));
	fu_progress_step_done(progress);
}

static void
fu_engine_backend_device_added_cb(FuBackend *backend, FuDevice *device, FuEngine *self)
{
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GPtrArray) possible_plugins = NULL;

	fu_engine_backend_device_added(self, device, progress);

	/* there's no point keeping this in the cache */
	possible_plugins = fu_device_get_possible_plugins(device);
	if (possible_plugins->len == 0) {
		g_debug("removing %s from backend cache as no possible plugin",
			fu_device_get_backend_id(device));
		fu_backend_device_removed(backend, device);
	}
}

static void
fu_engine_backend_device_changed_cb(FuBackend *backend, FuDevice *device, FuEngine *self)
{
	GPtrArray *plugins = fu_plugin_list_get_all(self->plugin_list);
	g_autoptr(GPtrArray) devices = NULL;

	/* debug */
	g_debug("%s changed %s", fu_backend_get_name(backend), fu_device_get_physical_id(device));

	/* emit changed on any that match */
	devices = fu_device_list_get_active(self->device_list);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device_tmp = g_ptr_array_index(devices, i);
		if (!FU_IS_UDEV_DEVICE(device_tmp) || !FU_IS_UDEV_DEVICE(device))
			continue;
		if (g_strcmp0(fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device_tmp)),
			      fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device))) == 0) {
			fu_udev_device_emit_changed(FU_UDEV_DEVICE(device));
		}
	}

	/* update the device for emulated devices */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device_tmp = g_ptr_array_index(devices, i);
		if (!fu_device_has_flag(device_tmp, FWUPD_DEVICE_FLAG_EMULATED))
			continue;
		if (g_strcmp0(fu_device_get_backend_id(device_tmp),
			      fu_device_get_backend_id(device)) == 0) {
			g_debug("incorporating new device for %s", fu_device_get_id(device_tmp));
			fu_device_incorporate(device_tmp, device, FU_DEVICE_INCORPORATE_FLAG_ALL);
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
				  fu_device_get_id(device),
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
			g_info("no %s plugin for HwId %s: %s",
			       plugins[i],
			       hwid,
			       error_local->message);
			continue;
		}
		g_info("enabling %s due to HwId %s", plugins[i], hwid);
		fu_plugin_remove_flag(plugin, FWUPD_PLUGIN_FLAG_REQUIRE_HWID);
	}
}

static gboolean
fu_engine_update_history_device(FuEngine *self, FuDevice *dev_history, GError **error)
{
	FuPlugin *plugin;
	FuRelease *rel_history;
	g_autofree gchar *btime = NULL;
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(GHashTable) metadata_device = NULL;

	/* is in the device list */
	dev = fu_device_list_get_by_id(self->device_list, fu_device_get_id(dev_history), error);
	if (dev == NULL)
		return FALSE;

	/* does the installed version match what we tried to install
	 * before fwupd was restarted */
	rel_history = FU_RELEASE(fu_device_get_release_default(dev_history));
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
	if (g_strcmp0(fu_release_get_metadata_item(rel_history, "BootTime"), btime) == 0) {
		g_info("service restarted, but no reboot has taken place");

		/* if it needed reboot then, it also needs it now... */
		if (fu_device_get_update_state(dev_history) == FWUPD_UPDATE_STATE_NEEDS_REBOOT) {
			g_info("inheriting needs-reboot for %s", fu_device_get_name(dev));
			fu_device_set_update_state(dev, FWUPD_UPDATE_STATE_NEEDS_REBOOT);
		}
		return TRUE;
	}

	/* save any additional report metadata */
	metadata_device = fu_device_report_metadata_post(dev);
	if (metadata_device != NULL && g_hash_table_size(metadata_device) > 0) {
		fu_release_add_metadata(rel_history, metadata_device);
		if (!fu_history_modify_device_release(self->history,
						      dev_history,
						      rel_history,
						      error)) {
			g_prefix_error(error, "failed to set metadata: ");
			return FALSE;
		}
	}

	/* measure the "new" system state */
	plugin = fu_plugin_list_find_by_name(self->plugin_list, fu_device_get_plugin(dev), error);
	if (plugin == NULL)
		return FALSE;
	if (fu_plugin_has_flag(plugin, FWUPD_PLUGIN_FLAG_MEASURE_SYSTEM_INTEGRITY))
		fu_engine_update_release_integrity(self, rel_history, "SystemIntegrityNew");

	/* do any late-cleanup actions */
	if (!fu_plugin_runner_reboot_cleanup(plugin, dev, error)) {
		g_prefix_error(error, "failed to do post-reboot cleanup: ");
		return FALSE;
	}

	/* the system is running with the new firmware version */
	if (fu_version_compare(fu_device_get_version(dev),
			       fu_release_get_version(rel_history),
			       fu_device_get_version_format(dev)) == 0) {
		GPtrArray *checksums;
		g_info("installed version %s matching history %s",
		       fu_device_get_version(dev),
		       fu_release_get_version(rel_history));

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
		return fu_history_modify_device_release(self->history,
							dev_history,
							rel_history,
							error);
	}

	/* does the plugin know the update failure */
	if (!fu_plugin_runner_get_results(plugin, dev, error))
		return FALSE;

	/* the plugin either can't tell us the error, or doesn't know itself */
	if (fu_device_get_update_state(dev) != FWUPD_UPDATE_STATE_FAILED &&
	    fu_device_get_update_state(dev) != FWUPD_UPDATE_STATE_FAILED_TRANSIENT) {
		g_autoptr(GString) str = g_string_new("failed to run update on reboot: ");
		g_info("falling back to generic failure");
		fu_device_set_update_state(dev_history, FWUPD_UPDATE_STATE_FAILED);
		g_string_append_printf(str,
				       "expected %s and got %s",
				       fu_release_get_version(rel_history),
				       fu_device_get_version(dev));
		fu_device_set_update_error(dev_history, str->str);
	} else {
		fu_device_set_update_state(dev_history, fu_device_get_update_state(dev));
		fu_device_set_update_error(dev_history, fu_device_get_update_error(dev));
	}

	/* update the state in the database */
	return fu_history_modify_device_release(self->history, dev_history, rel_history, error);
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
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
				g_debug("failed to update history database: %s",
					error_local->message);
				continue;
			}
			g_warning("failed to update history database: %s", error_local->message);
		}
	}
	return TRUE;
}

static void
fu_engine_ensure_client_certificate(FuEngine *self)
{
	g_autoptr(GBytes) blob = g_bytes_new_static(NULL, 0);
	g_autoptr(GError) error_local = NULL;
	g_autoptr(JcatBlob) jcat_sig = NULL;
	g_autoptr(JcatEngine) jcat_engine = NULL;

	/* create keyring and sign dummy data to ensure certificate exists */
	jcat_engine =
	    jcat_context_get_engine(self->jcat_context, JCAT_BLOB_KIND_PKCS7, &error_local);
	if (jcat_engine == NULL) {
		g_message("failed to create keyring: %s", error_local->message);
		return;
	}
	jcat_sig = jcat_engine_self_sign(jcat_engine, blob, JCAT_SIGN_FLAG_NONE, &error_local);
	if (jcat_sig == NULL) {
		if (g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT)) {
			g_info("client certificate now exists: %s", error_local->message);
			return;
		}
		g_message("failed to sign using keyring: %s", error_local->message);
		return;
	}
	g_info("client certificate exists and working");
}

static void
fu_engine_context_set_battery_threshold(FuContext *ctx)
{
	guint64 minimum_battery;
	g_autofree gchar *battery_str = NULL;
	g_autofree gchar *vendor_guid = NULL;
	g_autofree gchar *vendor = NULL;

	vendor = fu_context_get_hwid_replace_value(ctx, FU_HWIDS_KEY_MANUFACTURER, NULL);
	vendor_guid = fwupd_guid_hash_string(vendor);
	if (vendor_guid != NULL) {
		battery_str = g_strdup(
		    fu_context_lookup_quirk_by_id(ctx, vendor_guid, FU_QUIRKS_BATTERY_THRESHOLD));
	}
	if (battery_str == NULL) {
		minimum_battery = MINIMUM_BATTERY_PERCENTAGE_FALLBACK;
	} else {
		g_autoptr(GError) error_local = NULL;
		if (!fu_strtoull(battery_str,
				 &minimum_battery,
				 0,
				 100,
				 FU_INTEGER_BASE_AUTO,
				 &error_local)) {
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
fu_engine_win32_registry_get_string(HKEY hkey,
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
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to get registry string %s [0x%lX]",
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
		g_autoptr(GPtrArray) possible_plugins = NULL;

		fu_engine_backend_device_added(self, device, fu_progress_get_child(progress));
		fu_progress_step_done(progress);

		/* there's no point keeping this in the cache */
		possible_plugins = fu_device_get_possible_plugins(device);
		if (possible_plugins->len == 0) {
			g_debug("removing %s from backend cache as no possible plugin",
				fu_device_get_backend_id(device));
			fu_backend_device_removed(backend, device);
		}
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
	GPtrArray *backends = fu_context_get_backends(self->ctx);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, backends->len);
	for (guint i = 0; i < backends->len; i++) {
		FuBackend *backend = g_ptr_array_index(backends, i);
		g_autoptr(GError) error_backend = NULL;

		if (!fu_backend_get_enabled(backend)) {
			fu_progress_step_done(progress);
			continue;
		}
		if (!fu_engine_backends_coldplug_backend(self,
							 backend,
							 fu_progress_get_child(progress),
							 &error_backend)) {
			if (g_error_matches(error_backend,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED)) {
				g_debug("ignoring coldplug failure %s: %s",
					fu_backend_get_name(backend),
					error_backend->message);
			} else {
				g_warning("failed to coldplug backend %s: %s",
					  fu_backend_get_name(backend),
					  error_backend->message);
			}
			fu_progress_finished(fu_progress_get_child(progress));
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
	FuPlugin *plugin_uefi;
	FuQuirksLoadFlags quirks_flags = FU_QUIRKS_LOAD_FLAG_NONE;
	GPtrArray *backends = fu_context_get_backends(self->ctx);
	GPtrArray *plugins = fu_plugin_list_get_all(self->plugin_list);
	const gchar *host_emulate = g_getenv("FWUPD_HOST_EMULATE");
	g_autoptr(GPtrArray) checksums_approved = NULL;
	g_autoptr(GPtrArray) checksums_blocked = NULL;
	g_autoptr(GError) error_quirks = NULL;
	g_autoptr(GError) error_json_devices = NULL;
	g_autoptr(GError) error_local = NULL;
#ifdef HAVE_PASSIM
	g_autoptr(GError) error_passim = NULL;
#endif
	g_autoptr(GString) str = g_string_new(NULL);

	g_return_val_if_fail(FU_IS_ENGINE(self), FALSE);
	g_return_val_if_fail(FU_IS_PROGRESS(progress), FALSE);
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
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "plugins-ready");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "update-history-db");

	/* sanity check libraries are in sync with daemon */
	if (g_strcmp0(fwupd_version_string(), VERSION) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "libfwupd version %s does not match daemon %s",
			    fwupd_version_string(),
			    VERSION);
		return FALSE;
	}

	/* cache machine ID so we can use it from a sandboxed app */
#ifdef _WIN32
	self->host_machine_id =
	    fu_engine_win32_registry_get_string(HKEY_LOCAL_MACHINE,
						"SOFTWARE\\Microsoft\\Cryptography",
						"MachineGuid",
						&error_local);
#else
	self->host_machine_id = fu_engine_build_machine_id("fwupd", &error_local);
#endif
	if (self->host_machine_id == NULL)
		g_info("failed to build machine-id: %s", error_local->message);

	/* ensure these exist before starting */
	if (!fu_engine_ensure_paths_exist(error))
		return FALSE;

	/* read config file */
	if (!fu_config_load(FU_CONFIG(self->config), error)) {
		g_prefix_error(error, "Failed to load config: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* set the hardcoded ESP */
	if (fu_engine_config_get_esp_location(self->config) != NULL) {
		fu_context_set_esp_location(self->ctx,
					    fu_engine_config_get_esp_location(self->config));
	}

	/* read remotes */
	if (flags & FU_ENGINE_LOAD_FLAG_REMOTES) {
		FuRemoteListLoadFlags remote_list_flags = FU_REMOTE_LIST_LOAD_FLAG_FIX_METADATA_URI;
		if (fu_engine_config_get_test_devices(self->config))
			remote_list_flags |= FU_REMOTE_LIST_LOAD_FLAG_TEST_REMOTE;
		if (flags & FU_ENGINE_LOAD_FLAG_READONLY)
			remote_list_flags |= FU_REMOTE_LIST_LOAD_FLAG_READONLY_FS;
		if (flags & FU_ENGINE_LOAD_FLAG_NO_CACHE)
			remote_list_flags |= FU_REMOTE_LIST_LOAD_FLAG_NO_CACHE;
		fu_remote_list_set_lvfs_metadata_format(self->remote_list, FU_LVFS_METADATA_FORMAT);
		if (!fu_remote_list_load(self->remote_list, remote_list_flags, error)) {
			g_prefix_error(error, "Failed to load remotes: ");
			return FALSE;
		}
	}
	fu_progress_step_done(progress);

	/* create client certificate */
	if (flags & FU_ENGINE_LOAD_FLAG_ENSURE_CLIENT_CERT)
		fu_engine_ensure_client_certificate(self);
	fu_progress_step_done(progress);

	/* get hardcoded approved and blocked firmware */
	checksums_approved = fu_engine_config_get_approved_firmware(self->config);
	for (guint i = 0; i < checksums_approved->len; i++) {
		const gchar *csum = g_ptr_array_index(checksums_approved, i);
		fu_engine_add_approved_firmware(self, csum);
	}
	checksums_blocked = fu_engine_config_get_blocked_firmware(self->config);
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
	if (!fu_engine_load_plugins(self, flags, fu_progress_get_child(progress), error)) {
		g_prefix_error(error, "failed to load plugins: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* migrate per-plugin settings into fwupd.conf */
	plugin_uefi = fu_plugin_list_find_by_name(self->plugin_list, "uefi_capsule", NULL);
	if (plugin_uefi != NULL) {
		const gchar *tmp = fu_plugin_get_config_value(plugin_uefi, "OverrideESPMountPoint");
		if (tmp != NULL &&
		    g_strcmp0(tmp, fu_engine_config_get_esp_location(self->config)) != 0) {
			g_info("migrating OverrideESPMountPoint=%s to EspLocation", tmp);
			if (!fu_config_set_value(FU_CONFIG(self->config),
						 "fwupd",
						 "EspLocation",
						 tmp,
						 error))
				return FALSE;
		}
	}

	/* set up idle exit */
	if ((flags & FU_ENGINE_LOAD_FLAG_NO_IDLE_SOURCES) == 0)
		fu_idle_set_timeout(self->idle, fu_engine_config_get_idle_timeout(self->config));

	/* on a read-only filesystem don't care about the cache GUID */
	if (flags & FU_ENGINE_LOAD_FLAG_READONLY)
		quirks_flags |= FU_QUIRKS_LOAD_FLAG_READONLY_FS;
	if (flags & FU_ENGINE_LOAD_FLAG_NO_CACHE)
		quirks_flags |= FU_QUIRKS_LOAD_FLAG_NO_CACHE;
	if (!fu_context_load_quirks(self->ctx, quirks_flags, &error_quirks))
		g_warning("Failed to load quirks: %s", error_quirks->message);
	fu_progress_step_done(progress);

	/* load SMBIOS and the hwids */
	if (flags & FU_ENGINE_LOAD_FLAG_HWINFO) {
		if (!fu_context_load_hwinfo(self->ctx,
					    fu_progress_get_child(progress),
					    FU_CONTEXT_HWID_FLAG_LOAD_ALL,
					    error))
			return FALSE;
	}
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
	fu_context_add_firmware_gtype(self->ctx, "cab", FU_TYPE_CAB_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "dfu", FU_TYPE_DFU_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "fdt", FU_TYPE_FDT_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "csv", FU_TYPE_CSV_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "fit", FU_TYPE_FIT_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "dfuse", FU_TYPE_DFUSE_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "ifwi-cpd", FU_TYPE_IFWI_CPD_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "ifwi-fpt", FU_TYPE_IFWI_FPT_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "oprom", FU_TYPE_OPROM_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "fmap", FU_TYPE_FMAP_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "ihex", FU_TYPE_IHEX_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "linear", FU_TYPE_LINEAR_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "srec", FU_TYPE_SREC_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "hid-descriptor", FU_TYPE_HID_DESCRIPTOR);
	fu_context_add_firmware_gtype(self->ctx, "archive", FU_TYPE_ARCHIVE_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "smbios", FU_TYPE_SMBIOS);
	fu_context_add_firmware_gtype(self->ctx, "acpi-table", FU_TYPE_ACPI_TABLE);
	fu_context_add_firmware_gtype(self->ctx, "sbatlevel", FU_TYPE_SBATLEVEL_SECTION);
	fu_context_add_firmware_gtype(self->ctx, "edid", FU_TYPE_EDID);
	fu_context_add_firmware_gtype(self->ctx, "efi-file", FU_TYPE_EFI_FILE);
	fu_context_add_firmware_gtype(self->ctx, "efi-load-option", FU_TYPE_EFI_LOAD_OPTION);
	fu_context_add_firmware_gtype(self->ctx,
				      "efi-device-path-list",
				      FU_TYPE_EFI_DEVICE_PATH_LIST);
	fu_context_add_firmware_gtype(self->ctx, "efi-filesystem", FU_TYPE_EFI_FILESYSTEM);
	fu_context_add_firmware_gtype(self->ctx, "efi-section", FU_TYPE_EFI_SECTION);
	fu_context_add_firmware_gtype(self->ctx, "efi-volume", FU_TYPE_EFI_VOLUME);
	fu_context_add_firmware_gtype(self->ctx, "ifd-bios", FU_TYPE_IFD_BIOS);
	fu_context_add_firmware_gtype(self->ctx, "ifd-firmware", FU_TYPE_IFD_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "cfu-offer", FU_TYPE_CFU_OFFER);
	fu_context_add_firmware_gtype(self->ctx, "cfu-payload", FU_TYPE_CFU_PAYLOAD);
	fu_context_add_firmware_gtype(self->ctx, "uswid", FU_TYPE_USWID_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "coswid", FU_TYPE_COSWID_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "pefile", FU_TYPE_PEFILE_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx, "elf", FU_TYPE_ELF_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx,
				      "intel-thunderbolt",
				      FU_TYPE_INTEL_THUNDERBOLT_FIRMWARE);
	fu_context_add_firmware_gtype(self->ctx,
				      "intel-thunderbolt-nvm",
				      FU_TYPE_INTEL_THUNDERBOLT_NVM);
	fu_context_add_firmware_gtype(self->ctx, "usb-device-fw-ds20", FU_TYPE_USB_DEVICE_FW_DS20);
	fu_context_add_firmware_gtype(self->ctx, "usb-device-ms-ds20", FU_TYPE_USB_DEVICE_MS_DS20);

	/* we are emulating a different host */
	if (host_emulate != NULL) {
		g_autofree gchar *fn = NULL;

		/* did the user specify an absolute path */
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
		FuBackendSetupFlags backend_flags = FU_BACKEND_SETUP_FLAG_NONE;
		if (flags & FU_ENGINE_LOAD_FLAG_DEVICE_HOTPLUG)
			backend_flags |= FU_BACKEND_SETUP_FLAG_USE_HOTPLUG;
		for (guint i = 0; i < backends->len; i++) {
			FuBackend *backend = g_ptr_array_index(backends, i);
			g_autoptr(GError) error_backend = NULL;
			if (!fu_backend_setup(backend,
					      backend_flags,
					      fu_progress_get_child(progress),
					      &error_backend)) {
				g_info("failed to setup backend %s: %s",
				       fu_backend_get_name(backend),
				       error_backend->message);
				continue;
			}
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
	if (fu_context_has_flag(self->ctx, FU_CONTEXT_FLAG_LOADED_HWINFO)) {
		GPtrArray *guids = fu_context_get_hwid_guids(self->ctx);
		for (guint i = 0; i < guids->len; i++) {
			const gchar *hwid = g_ptr_array_index(guids, i);
			fu_engine_load_quirks_for_hwid(self, hwid);
		}
	}
	fu_progress_step_done(progress);

	/* set up battery threshold */
	if (fu_context_has_flag(self->ctx, FU_CONTEXT_FLAG_LOADED_HWINFO))
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
		fu_engine_plugins_startup(self, fu_progress_get_child(progress));
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

	/* coldplug done, so plugin is ready */
	if (flags & FU_ENGINE_LOAD_FLAG_COLDPLUG) {
		fu_engine_plugins_ready(self, fu_progress_get_child(progress));
		fu_progress_step_done(progress);
	} else {
		fu_progress_step_done(progress);
	}

	/* dump plugin information to the console */
	for (guint i = 0; i < backends->len; i++) {
		FuBackend *backend = g_ptr_array_index(backends, i);
		fu_backend_add_string(backend, 0, str);
	}
	for (guint i = 0; i < plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index(plugins, i);
		if (fu_plugin_has_flag(plugin, FWUPD_PLUGIN_FLAG_DISABLED))
			continue;
		fu_plugin_add_string(plugin, 0, str);
	}
	g_info("%s", str->str);

	/* update the db for devices that were updated during the reboot */
	if (!fu_engine_update_history_database(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* update the devices JSON file */
	if (!fu_engine_update_devices_file(self, &error_json_devices))
		g_info("failed to update list of devices: %s", error_json_devices->message);

#ifdef HAVE_PASSIM
	/* connect to passimd */
	if (!passim_client_load(self->passim_client, &error_passim))
		g_debug("failed to load Passim: %s", error_passim->message);
	if (passim_client_get_version(self->passim_client) != NULL) {
		fu_engine_add_runtime_version(self,
					      "org.freedesktop.Passim",
					      passim_client_get_version(self->passim_client));
	}
#endif

	fu_engine_set_status(self, FWUPD_STATUS_IDLE);
	self->loaded = TRUE;

	/* let clients know engine finished starting up */
	fu_engine_emit_changed(self);

	/* success */
	return TRUE;
}

static void
fu_engine_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuEngine *self = FU_ENGINE(object);
	switch (prop_id) {
	case PROP_CONTEXT:
		g_value_set_object(value, self->ctx);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_engine_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuEngine *self = FU_ENGINE(object);
	switch (prop_id) {
	case PROP_CONTEXT:
		g_set_object(&self->ctx, g_value_get_object(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_engine_dispose(GObject *obj)
{
	FuEngine *self = FU_ENGINE(obj);

	if (self->plugin_list != NULL) {
		GPtrArray *plugins = fu_plugin_list_get_all(self->plugin_list);
		for (guint i = 0; i < plugins->len; i++) {
			FuPlugin *plugin = g_ptr_array_index(plugins, i);
			g_signal_handlers_disconnect_by_data(plugin, self);
		}
		fu_plugin_list_remove_all(self->plugin_list);
	}
	if (self->device_list != NULL)
		fu_device_list_remove_all(self->device_list);
	if (self->config != NULL)
		g_signal_handlers_disconnect_by_data(self->config, self);

	if (self->ctx != NULL) {
		GPtrArray *backends = fu_context_get_backends(self->ctx);
		for (guint i = 0; i < backends->len; i++) {
			FuBackend *backend = g_ptr_array_index(backends, i);
			g_signal_handlers_disconnect_by_data(backend, self);
		}
		g_ptr_array_set_size(backends, 0);
		g_signal_handlers_disconnect_by_data(self->ctx, self);
	}
	g_clear_object(&self->ctx);

	G_OBJECT_CLASS(fu_engine_parent_class)->dispose(obj);
}

static void
fu_engine_class_init(FuEngineClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = fu_engine_dispose;
	object_class->finalize = fu_engine_finalize;
	object_class->get_property = fu_engine_get_property;
	object_class->set_property = fu_engine_set_property;
	object_class->constructed = fu_engine_constructed;

	pspec = g_param_spec_object("context",
				    NULL,
				    NULL,
				    FU_TYPE_CONTEXT,
				    G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_CONTEXT, pspec);

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
fu_engine_context_power_changed(FuEngine *self)
{
	g_autoptr(GPtrArray) devices = fu_device_list_get_active(self->device_list);

	/* apply policy on any existing devices */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		fu_engine_ensure_device_power_inhibit(self, device);
		fu_engine_ensure_device_lid_inhibit(self, device);
		fu_engine_ensure_device_display_required_inhibit(self, device);
		fu_engine_ensure_device_system_inhibit(self, device);
	}
}

static void
fu_engine_context_power_changed_cb(FuContext *ctx, GParamSpec *pspec, FuEngine *self)
{
	if (fu_idle_has_inhibit(self->idle, FU_IDLE_INHIBIT_SIGNALS)) {
		g_debug("suppressing ::power-changed as transaction is in progress");
		return;
	}
	fu_engine_context_power_changed(self);
}

static void
fu_engine_idle_timeout_cb(FuIdle *idle, FuEngine *self)
{
	fu_engine_set_status(self, FWUPD_STATUS_SHUTDOWN);
}

static void
fu_engine_idle_inhibit_changed_cb(FuIdle *idle, GParamSpec *pspec, FuEngine *self)
{
	if (!fu_idle_has_inhibit(idle, FU_IDLE_INHIBIT_SIGNALS) &&
	    g_hash_table_size(self->device_changed_allowlist) > 0) {
		g_debug("clearing device-changed allowlist as transaction done");
		g_hash_table_remove_all(self->device_changed_allowlist);

		/* we might have suppressed this during the transaction, so ensure all the device
		 * inhibits are being set up correctly */
		fu_engine_context_power_changed(self);
	}
}

static void
fu_engine_constructed(GObject *obj)
{
	FuEngine *self = FU_ENGINE(obj);
#ifdef HAVE_UTSNAME_H
	struct utsname uname_tmp;
#endif
	g_autofree gchar *keyring_path = NULL;
	g_autofree gchar *pkidir_fw = NULL;
	g_autofree gchar *pkidir_md = NULL;
	g_autofree gchar *sysconfdir = NULL;

	/* for debugging */
	g_info("starting fwupd %s…", VERSION);
	g_signal_connect(FU_CONTEXT(self->ctx),
			 "security-changed",
			 G_CALLBACK(fu_engine_context_security_changed_cb),
			 self);
	g_signal_connect(FU_CONTEXT(self->ctx),
			 "notify::power-state",
			 G_CALLBACK(fu_engine_context_power_changed_cb),
			 self);
	g_signal_connect(FU_CONTEXT(self->ctx),
			 "notify::lid-state",
			 G_CALLBACK(fu_engine_context_power_changed_cb),
			 self);
	g_signal_connect(FU_CONTEXT(self->ctx),
			 "notify::display-state",
			 G_CALLBACK(fu_engine_context_power_changed_cb),
			 self);
	g_signal_connect(FU_CONTEXT(self->ctx),
			 "notify::battery-level",
			 G_CALLBACK(fu_engine_context_power_changed_cb),
			 self);
	g_signal_connect(FU_CONTEXT(self->ctx),
			 "notify::battery-threshold",
			 G_CALLBACK(fu_engine_context_power_changed_cb),
			 self);
	g_signal_connect(FU_CONTEXT(self->ctx),
			 "notify::flags",
			 G_CALLBACK(fu_engine_context_power_changed_cb),
			 self);

	g_signal_connect(FU_CONFIG(self->config),
			 "changed",
			 G_CALLBACK(fu_engine_config_changed_cb),
			 self);
	g_signal_connect(FU_REMOTE_LIST(self->remote_list),
			 "changed",
			 G_CALLBACK(fu_engine_remote_list_changed_cb),
			 self);
	g_signal_connect(FU_REMOTE_LIST(self->remote_list),
			 "added",
			 G_CALLBACK(fu_engine_remote_list_added_cb),
			 self);

	g_signal_connect(FU_IDLE(self->idle),
			 "inhibit-changed",
			 G_CALLBACK(fu_engine_idle_inhibit_changed_cb),
			 self);
	g_signal_connect(FU_IDLE(self->idle),
			 "timeout",
			 G_CALLBACK(fu_engine_idle_timeout_cb),
			 self);

	/* backends */
	{
		g_autoptr(FuBackend) backend = fu_usb_backend_new(self->ctx);
		fu_context_add_backend(self->ctx, backend);
	}
#ifdef HAVE_GUDEV
	{
		g_autoptr(FuBackend) backend = fu_udev_backend_new(self->ctx);
		fu_context_add_backend(self->ctx, backend);
	}
#endif
#ifdef HAVE_BLUEZ
	{
		g_autoptr(FuBackend) backend = fu_bluez_backend_new(self->ctx);
		fu_context_add_backend(self->ctx, backend);
	}
#endif

	self->history = fu_history_new(self->ctx);

	/* setup Jcat context */
	self->jcat_context = jcat_context_new();
	jcat_context_blob_kind_allow(self->jcat_context, JCAT_BLOB_KIND_SHA256);
	jcat_context_blob_kind_allow(self->jcat_context, JCAT_BLOB_KIND_SHA512);
	jcat_context_blob_kind_allow(self->jcat_context, JCAT_BLOB_KIND_PKCS7);
	jcat_context_blob_kind_allow(self->jcat_context, JCAT_BLOB_KIND_GPG);
	keyring_path = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR_PKG);
	jcat_context_set_keyring_path(self->jcat_context, keyring_path);
	sysconfdir = fu_path_from_kind(FU_PATH_KIND_SYSCONFDIR);
	pkidir_fw = g_build_filename(sysconfdir, "pki", "fwupd", NULL);
	jcat_context_add_public_keys(self->jcat_context, pkidir_fw);
	pkidir_md = g_build_filename(sysconfdir, "pki", "fwupd-metadata", NULL);
	jcat_context_add_public_keys(self->jcat_context, pkidir_md);

	/* add some runtime versions of things the daemon depends on */
	fu_engine_add_runtime_version(self, "org.freedesktop.fwupd", VERSION);
	fu_engine_add_runtime_version(self, "com.hughsie.libjcat", jcat_version_string());
	fu_engine_add_runtime_version(self, "com.hughsie.libxmlb", xb_version_string());

	/* optional kernel version */
#ifdef HAVE_UTSNAME_H
	memset(&uname_tmp, 0, sizeof(uname_tmp));
	if (uname(&uname_tmp) >= 0)
		fu_engine_add_runtime_version(self, "org.kernel", uname_tmp.release);
#endif

	fu_context_add_compile_version(self->ctx, "org.freedesktop.fwupd", VERSION);
#ifdef SOURCE_VERSION
	if (g_strcmp0(SOURCE_VERSION, VERSION) != 0)
		fu_context_add_compile_version(self->ctx,
					       "org.freedesktop.fwupd.source",
					       SOURCE_VERSION);
#endif
	fu_context_add_compile_version(self->ctx, "info.libusb", LIBUSB_VERSION);
#ifdef HAVE_PASSIM
	{
		g_autofree gchar *version = g_strdup_printf("%i.%i.%i",
							    PASSIM_MAJOR_VERSION,
							    PASSIM_MINOR_VERSION,
							    PASSIM_MICRO_VERSION);
		fu_context_add_compile_version(self->ctx, "org.freedesktop.Passim", version);
	}
#endif
	{
		g_autofree gchar *version = g_strdup_printf("%i.%i.%i",
							    JCAT_MAJOR_VERSION,
							    JCAT_MINOR_VERSION,
							    JCAT_MICRO_VERSION);
		fu_context_add_compile_version(self->ctx, "com.hughsie.libjcat", version);
	}
	{
		g_autofree gchar *version = g_strdup_printf("%i.%i.%i",
							    XMLB_MAJOR_VERSION,
							    XMLB_MINOR_VERSION,
							    XMLB_MICRO_VERSION);
		fu_context_add_compile_version(self->ctx, "com.hughsie.libxmlb", version);
	}

	/* add optional snap version */
	if (g_getenv("SNAP_REVISION") != NULL) {
		fu_context_add_compile_version(self->ctx,
					       "io.snapcraft.fwupd",
					       g_getenv("SNAP_REVISION"));
	}
}

static void
fu_engine_init(FuEngine *self)
{
	self->percentage = 0;
	self->config = fu_engine_config_new();
	self->remote_list = fu_remote_list_new();
	self->device_list = fu_device_list_new();
	self->idle = fu_idle_new();
	self->plugin_list = fu_plugin_list_new();
	self->plugin_filter = g_ptr_array_new_with_free_func(g_free);
	self->host_security_attrs = fu_security_attrs_new();
	self->local_monitors = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	self->acquiesce_loop = g_main_loop_new(NULL, FALSE);
	self->emulation_phases = g_hash_table_new_full(g_direct_hash,
						       g_direct_equal,
						       NULL,
						       (GDestroyNotify)g_bytes_unref);
	self->emulation_ids = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	self->device_changed_allowlist =
	    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
#ifdef HAVE_PASSIM
	self->passim_client = passim_client_new();
#endif

	/* register /org/freedesktop/fwupd globally */
	g_resources_register(fu_get_resource());
}

static void
fu_engine_finalize(GObject *obj)
{
	FuEngine *self = FU_ENGINE(obj);

	for (guint i = 0; i < self->local_monitors->len; i++) {
		GFileMonitor *monitor = g_ptr_array_index(self->local_monitors, i);
		g_file_monitor_cancel(monitor);
	}

	if (self->silo != NULL)
		g_object_unref(self->silo);
	if (self->query_component_by_guid != NULL)
		g_object_unref(self->query_component_by_guid);
	if (self->query_container_checksum1 != NULL)
		g_object_unref(self->query_container_checksum1);
	if (self->query_container_checksum2 != NULL)
		g_object_unref(self->query_container_checksum2);
	if (self->query_tag_by_guid_version != NULL)
		g_object_unref(self->query_tag_by_guid_version);
	if (self->coldplug_id != 0)
		g_source_remove(self->coldplug_id);
	if (self->approved_firmware != NULL)
		g_hash_table_unref(self->approved_firmware);
	if (self->blocked_firmware != NULL)
		g_hash_table_unref(self->blocked_firmware);
	if (self->acquiesce_id != 0)
		g_source_remove(self->acquiesce_id);
	if (self->update_motd_id != 0)
		g_source_remove(self->update_motd_id);
#ifdef HAVE_PASSIM
	if (self->passim_client != NULL)
		g_object_unref(self->passim_client);
#endif
	g_main_loop_unref(self->acquiesce_loop);

	g_free(self->host_machine_id);
	g_free(self->host_security_id);
	g_object_unref(self->host_security_attrs);
	g_object_unref(self->idle);
	g_object_unref(self->config);
	g_object_unref(self->remote_list);
	g_object_unref(self->history);
	g_object_unref(self->device_list);
	g_object_unref(self->jcat_context);
	g_ptr_array_unref(self->plugin_filter);
	g_ptr_array_unref(self->local_monitors);
	g_hash_table_unref(self->emulation_phases);
	g_hash_table_unref(self->emulation_ids);
	g_hash_table_unref(self->device_changed_allowlist);
	g_object_unref(self->plugin_list);

	G_OBJECT_CLASS(fu_engine_parent_class)->finalize(obj);
}

FuEngine *
fu_engine_new(FuContext *ctx)
{
	return FU_ENGINE(g_object_new(FU_TYPE_ENGINE, "context", ctx, NULL));
}
